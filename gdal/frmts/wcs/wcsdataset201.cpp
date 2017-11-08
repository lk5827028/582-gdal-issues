/******************************************************************************
 *
 * Project:  WCS Client Driver
 * Purpose:  Implementation of Dataset class for WCS 2.0.
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 2006, Frank Warmerdam
 * Copyright (c) 2008-2013, Even Rouault <even dot rouault at mines-paris dot org>
 * Copyright (c) 2017, Ari Jolma
 * Copyright (c) 2017, Finnish Environment Institute
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "cpl_string.h"
#include "cpl_minixml.h"
#include "cpl_http.h"
#include "gmlutils.h"
#include "gdal_frmts.h"
#include "gdal_pam.h"
#include "ogr_spatialref.h"
#include "gmlcoverage.h"

#include <algorithm>
#include <dirent.h>

#include "wcsdataset.h"
#include "wcsutils.h"

/************************************************************************/
/*                         CoverageSubtype()                            */
/*                                                                      */
/************************************************************************/

static CPLString CoverageSubtype(CPLXMLNode *coverage)
{
    CPLString subtype = CPLGetXMLValue(coverage, "ServiceParameters.CoverageSubtype", "");
    size_t pos = subtype.find("Coverage");
    if (pos != std::string::npos) {
        subtype.erase(pos);
    }
    return subtype;
}


/************************************************************************/
/*                         GetGridNode()                                */
/*                                                                      */
/************************************************************************/

static CPLXMLNode *GetGridNode(CPLXMLNode *coverage, CPLString subtype)
{
    CPLXMLNode *grid = NULL;
    // Construct the name of the node that we look under domainSet.
    // For now we can handle RectifiedGrid and ReferenceableGridByVectors.
    // Note that if this is called at GetCoverage stage, the grid should not be NULL.
    CPLString path = "domainSet";
    if (subtype == "RectifiedGrid") {
        grid = CPLGetXMLNode(coverage, (path + "." + subtype).c_str());
    } else if (subtype == "ReferenceableGrid") {
        grid = CPLGetXMLNode(coverage, (path + "." + subtype + "ByVectors").c_str());
    }
    if (!grid) {
        CPLError(CE_Failure, CPLE_AppDefined, "Can't handle coverages of type '%s'.", subtype.c_str());
    }
    return grid;
}

/************************************************************************/
/*                         GetExtent()                                  */
/*                                                                      */
/************************************************************************/

std::vector<double> WCSDataset201::GetExtent(int nXOff, int nYOff,
                                             int nXSize, int nYSize,
                                             CPL_UNUSED int nBufXSize, CPL_UNUSED int nBufYSize)
{
    std::vector<double> extent;
    // WCS 2.0 extents are the outer edges of outer pixels.
    extent.push_back(adfGeoTransform[0] +
                     (nXOff) * adfGeoTransform[1]);
    extent.push_back(adfGeoTransform[3] +
                     (nYOff + nYSize) * adfGeoTransform[5]);
    extent.push_back(adfGeoTransform[0] +
                     (nXOff + nXSize) * adfGeoTransform[1]);
    extent.push_back(adfGeoTransform[3] +
                     (nYOff) * adfGeoTransform[5]);
    return extent;
}

/************************************************************************/
/*                        GetCoverageRequest()                          */
/*                                                                      */
/************************************************************************/

CPLString WCSDataset201::GetCoverageRequest(bool scaled,
                                            int nBufXSize, int nBufYSize,
                                            std::vector<double> extent,
                                            CPL_UNUSED CPLString osBandList)
{
    CPLString request = CPLGetXMLValue(psService, "ServiceURL", ""), tmp;
    request += "SERVICE=WCS";
    request += "&REQUEST=GetCoverage";
    request += "&VERSION=" + String(CPLGetXMLValue(psService, "Version", ""));
    request += "&COVERAGEID=" + URLEncode(CPLGetXMLValue(psService, "CoverageName", ""));
    if (!native_crs) {
        CPLString crs = URLEncode(CPLGetXMLValue(psService, "CRS", ""));
        request += "&OUTPUTCRS=" + crs;
        request += "&SUBSETTINGCRS=" + crs;
    }
    request += "&FORMAT=" + URLEncode(CPLGetXMLValue(psService, "PreferredFormat", ""));
    // todo updatesequence

    std::vector<CPLString> domain = Split(CPLGetXMLValue(psService, "Domain", ""), ",");
    const char *x = domain[0].c_str();
    const char *y = domain[1].c_str();
    if (EQUAL(CPLGetXMLValue(psService, "SubsetAxisSwap", ""), "TRUE")) {
        const char *ctmp = x;
        x = y;
        y = ctmp;
    }
    tmp.Printf("&SUBSET=%s%%28%.15g,%.15g%%29", x, extent[0], extent[2]);
    request += tmp;
    tmp.Printf("&SUBSET=%s%%28%.15g,%.15g%%29", y, extent[1], extent[3]);
    request += tmp;

    // set subsets for axis other than x/y
    std::vector<CPLString> dimensions = Split(CPLGetXMLValue(psService, "Dimensions", ""), ";");
    for (unsigned int i = 0; i < dimensions.size(); ++i) {
        size_t pos = dimensions[i].find("(");
        CPLString dim = dimensions[i].substr(0, pos);
        if (IndexOf(dim, domain) != -1) {
            continue;
        }
        std::vector<CPLString> params = Split(FromParenthesis(dimensions[i]), ",");
        request += "&SUBSET=" + dim + "%28";
        for (unsigned int j = 0; j < params.size(); ++j) {
            // todo: %22 only for non-numbers
            request += "%22" + params[j] + "%22";
        }
        request += "%29";
    }

    if (scaled) {
        // scaling is expressed in grid axes
        std::vector<CPLString> grid_axes = Split(CPLGetXMLValue(psService, "GridAxes", ""), ",");
        tmp.Printf("&SCALESIZE=%s(%i),%s(%i)", grid_axes[0].c_str(), nBufXSize, grid_axes[1].c_str(), nBufYSize);
        request += tmp;
    }

    CPLString interpolation = CPLGetXMLValue(psService, "Interpolation", "");
    if (interpolation != "") {
        request += "&INTERPOLATION=" + interpolation;
    }

    CPLString range = CPLGetXMLValue(psService, "FieldName", "");
    if (range != "" && range != "*") {
        request += "&RANGESUBSET=" + range;
    }

    // todo other stuff, e.g., GeoTIFF encoding

    return request;

}

/************************************************************************/
/*                        DescribeCoverageRequest()                     */
/*                                                                      */
/************************************************************************/

CPLString WCSDataset201::DescribeCoverageRequest()
{
    CPLString request;
    request.Printf(
        "%sSERVICE=WCS&REQUEST=DescribeCoverage&VERSION=%s&COVERAGEID=%s%s&FORMAT=text/xml",
        CPLGetXMLValue( psService, "ServiceURL", "" ),
        CPLGetXMLValue( psService, "Version", "1.0.0" ),
        CPLGetXMLValue( psService, "CoverageName", "" ),
        CPLGetXMLValue( psService, "DescribeCoverageExtra", "" ) );
    return request;
}

/************************************************************************/
/*                             GridOffsets()                            */
/*                                                                      */
/************************************************************************/

bool WCSDataset201::GridOffsets(CPLXMLNode *grid,
                                CPLString subtype,
                                std::vector<double> &origin,
                                std::vector<std::vector<double>> &offset,
                                std::vector<CPLString> axes,
                                char ***metadata)
{
    // todo: use domain_index

    // origin position, center of cell
    CPLXMLNode *point = CPLGetXMLNode(grid, "origin.Point.pos");
    origin = Flist(Split(CPLGetXMLValue(point, NULL, ""), " ", axis_order_swap), 0, 2);

    // offsets = coefficients of affine transformation from cell coords to
    // CRS coords, (1,2) and (4,5)

    if (subtype == "RectifiedGrid") {

        // for rectified grid the geo transform is from origin and offsetVectors
        int i = 0;
        for (CPLXMLNode *node = grid->psChild; node != NULL; node = node->psNext) {
            if (node->eType != CXT_Element || !EQUAL(node->pszValue, "offsetVector")) {
                continue;
            }
            offset.push_back(Flist(Split(CPLGetXMLValue(node, NULL, ""), " ", axis_order_swap), 0, 2));
            if (i == 1) {
                break;
            }
            i++;
        }
        // if axis_order_swap
        // the offset order should be swapped
        // Rasdaman does it
        // MapServer and GeoServer not
        if (offset.size() > 1 && axis_order_swap) {
            CPLString no_offset_swap = CPLGetXMLValue(psService, "NoOffsetSwap", "");
            if (no_offset_swap == "") {
                std::vector<double> tmp = offset[0];
                offset[0] = offset[1];
                offset[1] = tmp;
            }
        }

    } else { // if (coverage_type == "ReferenceableGrid"(ByVector)) {

        // for vector referenceable grid the geo transform is from
        // offsetVector, coefficients, gridAxesSpanned, sequenceRule
        // in generalGridAxis.GeneralGridAxis
        for (CPLXMLNode *node = grid->psChild; node != NULL; node = node->psNext) {
            CPLXMLNode *axis = CPLGetXMLNode(node, "GeneralGridAxis");
            if (!axis) {
                continue;
            }
            CPLString spanned = CPLGetXMLValue(axis, "gridAxesSpanned", "");
            int index = IndexOf(spanned, axes);
            if (index == -1) {
                CPLError(CE_Failure, CPLE_AppDefined, "This is not a rectilinear grid(?).");
                return false;
            }
            CPLString coeffs = CPLGetXMLValue(axis, "coefficients", "");
            if (coeffs != "") {
                *metadata = CSLSetNameValue(*metadata, CPLString().Printf("DIMENSION_%i_COEFFS", index), coeffs);
                /*
                CPLError(CE_Failure, CPLE_AppDefined,
                         "This is not a uniform grid, coefficients: '%s'.", coeffs.c_str());
                return false;
                */
            }
            CPLString order = CPLGetXMLValue(axis, "sequenceRule.axisOrder", "");
            CPLString rule = CPLGetXMLValue(axis, "sequenceRule", "");
            if (!(order == "+1" && rule == "Linear")) {
                CPLError(CE_Failure, CPLE_AppDefined, "The grid is not linear and increasing from origo.");
                return false;
            }
            CPLXMLNode *offset_node = CPLGetXMLNode(axis, "offsetVector");
            if (offset_node) {
                offset.push_back(Flist(Split(CPLGetXMLValue(offset_node, NULL, ""), " ", axis_order_swap), 0, 2));
            } else {
                CPLError(CE_Failure, CPLE_AppDefined, "Missing offset vector in grid axis.");
                return false;
            }
        }
        // todo: make sure offset order is the same as the axes order but see above

    }
    if (origin.size() < 2 || offset.size() < 2) {
        CPLError(CE_Failure, CPLE_AppDefined, "Could not parse origin or offset vectors from grid.");
        return false;
    }
    return true;
}

/************************************************************************/
/*                             GetSubdataset()                          */
/*                                                                      */
/************************************************************************/

CPLString WCSDataset201::GetSubdataset(CPLString coverage)
{
    char **metadata = GDALPamDataset::GetMetadata("SUBDATASETS");
    CPLString subdataset;
    if (metadata != NULL) {
        for (int i = 0; metadata[i] != NULL; ++i) {
            char *key;
            CPLString url = CPLParseNameValue(metadata[i], &key);
            if (strstr(key, "SUBDATASET_") && strstr(key, "_NAME")) {
                if (coverage == CPLURLGetValue(url, "coverageId")) {
                    subdataset = key;
                    subdataset.erase(subdataset.find("_NAME"), 5);
                    CPLFree(key);
                    break;
                }
            }
            CPLFree(key);
        }
    }
    return subdataset;
}

/************************************************************************/
/*                             SetFormat()                              */
/*                                                                      */
/************************************************************************/

bool WCSDataset201::SetFormat(CPLXMLNode *coverage)
{
    // set the PreferredFormat value in service, unless is set
    // by the user, either through direct edit or options
    CPLString format = CPLGetXMLValue(psService, "PreferredFormat", "");

    // todo: check the value against list of supported formats
    if (format != "") {
        return true;
    }

/*      We will prefer anything that sounds like TIFF, otherwise        */
/*      falling back to the first supported format.  Should we          */
/*      consider preferring the nativeFormat if available?              */

    char **metadata = GDALPamDataset::GetMetadata(NULL);
    const char *value = CSLFetchNameValue(metadata, "WCS_GLOBAL#formatSupported");
    if (value == NULL) {
        format = CPLGetXMLValue(coverage, "ServiceParameters.nativeFormat", "");
    } else {
        std::vector<CPLString> format_list = Split(value, ",");
        for (unsigned j = 0; j < format_list.size(); ++j) {
            if (format_list[j].ifind("tiff") != std::string::npos) {
                format = format_list[j];
                break;
            }
        }
        if (format == "" && format_list.size() > 0) {
            format = format_list[0];
        }
    }
    if (format != "") {
        CPLSetXMLValue(psService, "PreferredFormat", format);
        return true;
    } else {
        return false;
    }
}

/************************************************************************/
/*                         ParseGridFunction()                          */
/*                                                                      */
/************************************************************************/

bool WCSDataset201::ParseGridFunction(std::vector<CPLString> &axisOrder)
{
    CPLXMLNode *function = CPLGetXMLNode(psService, "coverageFunction.GridFunction");
    if (function) {
        CPLString path = "sequenceRule";
        CPLString sequenceRule = CPLGetXMLValue(function, path, "");
        path += ".axisOrder";
        axisOrder = Split(CPLGetXMLValue(function, path, ""), " ");
        path = "startPoint";
        std::vector<CPLString> startPoint = Split(CPLGetXMLValue(function, path, ""), " ");
        // for now require simple
        if (sequenceRule != "Linear") {
            CPLError(CE_Failure, CPLE_AppDefined, "Can't handle '%s' coverages.", sequenceRule.c_str());
            return false;
        }
    }
    return true;
}

/************************************************************************/
/*                             ParseRange()                             */
/*                                                                      */
/************************************************************************/

int WCSDataset201::ParseRange(CPLXMLNode *coverage, char ***metadata)
{
    int fields = 0;
    // The range
    // Default is to include all (types permitting?)
    // Can also be controlled with Range parameter

    // assuming here that the attributes are defined by swe:DataRecord
    CPLString path = "rangeType";
    CPLXMLNode *record = CPLGetXMLNode(coverage, (path + ".DataRecord").c_str());
    if (!record) {
        CPLError(CE_Failure, CPLE_AppDefined, "Attributes are not defined in a DataRecord, giving up.");
        return 0;
    }

    // todo: mapserver does not like field names, it wants indexes
    // so we should be able to give those

    // if Range is set remove those not in it
    std::vector<CPLString> range = Split(CPLGetXMLValue(psService, "FieldName", ""), ",");
    // todo: add check for range subsetting profile existence in server metadata here
    unsigned int range_index = 0; // index for reading from range
    bool in_band_range = false;

    unsigned int field_index = 1;
    CPLString field_name;
    std::vector<CPLString> nodata_array;

    for (CPLXMLNode *field = record->psChild; field != NULL; field = field->psNext) {
        if (field->eType != CXT_Element || !EQUAL(field->pszValue, "field")) {
            continue;
        }
        CPLString fname = CPLGetXMLValue(field, "name", "");
        bool include = true;

        if (range.size() > 0) {
            include = false;
            if (range_index < range.size()) {
                CPLString current_range = range[range_index];
                CPLString fname_test;

                if (atoi(current_range) != 0) {
                    fname_test = CPLString().Printf("%i", field_index);
                } else {
                    fname_test = fname;
                }

                if (current_range == "*") {
                    include = true;
                } else if (current_range == fname_test) {
                    include = true;
                    range_index += 1;
                } else if (current_range.find(fname_test + ":") != std::string::npos) {
                    include = true;
                    in_band_range = true;
                } else if (current_range.find(":" + fname_test) != std::string::npos) {
                    include = true;
                    in_band_range = false;
                    range_index += 1;
                } else if (in_band_range) {
                    include = true;
                }
            }
        }

        if (include) {
            CPLString key;
            key.Printf("FIELD_%i_", field_index);
            *metadata = CSLSetNameValue(*metadata, (key + "NAME").c_str(), fname);

            CPLString nodata = CPLGetXMLValue(field, "Quantity.nilValues.NilValue", "");
            if (nodata != "") {
                *metadata = CSLSetNameValue(*metadata, (key + "NODATA").c_str(), nodata);
            }

            CPLString descr = CPLGetXMLValue(field, "Quantity.description", "");
            if (descr != "") {
                *metadata = CSLSetNameValue(*metadata, (key + "DESCR").c_str(), descr);
            }

            path = "Quantity.constraint.AllowedValues.interval";
            CPLString interval = CPLGetXMLValue(field, path, "");
            if (interval != "") {
                *metadata = CSLSetNameValue(*metadata, (key + "INTERVAL").c_str(), interval);
            }

            if (field_name == "") {
                field_name = fname;
            }
            nodata_array.push_back(nodata);
            fields += 1;
        }

        field_index += 1;
    }

    if (fields == 0) {
        CPLError(CE_Failure, CPLE_AppDefined, "No data fields found (bad Range?).");
    } else {
        // todo: default to the first one?
        CPLSetXMLValue(psService, "NoDataValue", Join(nodata_array, ","));
    }

    return fields;
}

/************************************************************************/
/*                          ExtractGridInfo()                           */
/*                                                                      */
/*      Collect info about grid from describe coverage for WCS 2.0.     */
/*                                                                      */
/************************************************************************/

bool WCSDataset201::ExtractGridInfo()
{
    // this is for checking what's in service and for filling in empty slots in it
    // if the service file can be considered ready for use, this could be skipped

    CPLXMLNode *coverage = CPLGetXMLNode(psService, "CoverageDescription");

    if (coverage == NULL) {
        CPLError(CE_Failure, CPLE_AppDefined, "CoverageDescription missing from service. RECREATE_SERVICE?");
        return false;
    }

    CPLString subtype = CoverageSubtype(coverage);

    // GridFunction (is optional)
    // We support only linear grid functions.
    // axisOrder affects the geo transform, if it swaps i and j
    std::vector<CPLString> axisOrder;
    if (!ParseGridFunction(axisOrder)) {
        return false;
    }

    // get CRS from boundedBy.Envelope and set the native flag to true
    // below we may set the CRS again but that won't be native
    // also axis order swap is set
    CPLString path = "boundedBy.Envelope";
    CPLXMLNode *envelope = CPLGetXMLNode(coverage, path);
    std::vector<CPLString> bbox = ParseBoundingBox(envelope);
    if (!SetCRS(ParseCRS(envelope), true)) {
        return false;
    }

    // has the user set the domain?
    std::vector<CPLString> domain = Split(CPLGetXMLValue(psService, "Domain", ""), ",");

    // names of axes
    std::vector<CPLString> axes = Split(
        CPLGetXMLValue(coverage, (path + ".axisLabels").c_str(), ""), " ", axis_order_swap);
    std::vector<CPLString> uoms = Split(
        CPLGetXMLValue(coverage, (path + ".uomLabels").c_str(), ""), " ", axis_order_swap);

    if (axes.size() < 2 || bbox.size() < 2) {
        CPLError(CE_Failure, CPLE_AppDefined, "Less than 2 dimensions in coverage envelope or no axisLabels.");
        return false;
    }

    std::vector<int> domain_indexes = IndexOf(domain, axes);
    if (Contains(domain_indexes, -1)) {
        CPLError(CE_Failure, CPLE_AppDefined, "Axis in given domain does not exist in coverage.");
        return false;
    }
    if (domain_indexes.size() == 0) { // default is the first two
        domain_indexes.push_back(0);
        domain_indexes.push_back(1);
    }
    if (domain.size() == 0) {
        domain.push_back(axes[0]);
        domain.push_back(axes[1]);
        CPLSetXMLValue(psService, "Domain", Join(domain, ","));
    }

    char **metadata = CSLDuplicate(GetMetadata("SUBDATASETS")); // coverage metadata to be added/updated

    metadata = CSLSetNameValue(metadata, "DOMAIN", Join(domain, ","));

    std::vector<CPLString> slow = Split(bbox[0], " ", axis_order_swap);
    std::vector<CPLString> shigh = Split(bbox[1], " ", axis_order_swap);
    std::vector<double> low = Flist(slow, 0, 2);
    std::vector<double> high = Flist(shigh, 0, 2);
    std::vector<double> env;
    env.insert(env.end(), low.begin(), low.begin() + 2);
    env.insert(env.end(), high.begin(), high.begin() + 2);
    // todo: EnvelopeWithTimePeriod

    for (unsigned int i = 0; i < axes.size(); ++i) {
        CPLString key;
        key.Printf("DIMENSION_%i_", i);
        metadata = CSLSetNameValue(metadata, (key + "AXIS").c_str(), axes[i]);
        if (i < uoms.size()) {
            metadata = CSLSetNameValue(metadata, (key + "UOM").c_str(), uoms[i]);
        }
        if (i < 2) {
            metadata = CSLSetNameValue(metadata, (key + "INTERVAL").c_str(),
                                       CPLString().Printf("%.15g,%.15g", low[i], high[i]));
        } else {
            metadata = CSLSetNameValue(metadata, (key + "INTERVAL").c_str(),
                                       CPLString().Printf("%s,%s", slow[i].c_str(), shigh[i].c_str()));
        }
    }

    // domainSet
    // requirement 23: the srsName here _shall_ be the same as in boundedBy
    // => we ignore it
    // the CRS of this dataset is from boundedBy (unless it is overridden)
    // this is the size of this dataset
    // this gives the geotransform of this dataset (unless there is CRS override)

    CPLXMLNode *grid = GetGridNode(coverage, subtype);
    if (!grid) {
        return false;
    }

    path = "limits.GridEnvelope";
    CPLString no_grid_envelope_swap = CPLGetXMLValue(psService, "NoGridEnvelopeSwap", "");
    bool local_swap = axis_order_swap && no_grid_envelope_swap == "";
    std::vector<std::vector<int>> size = ParseGridEnvelope(CPLGetXMLNode(grid, path), local_swap);
    std::vector<int> grid_size;

    grid_size.push_back(size[1][domain_indexes[0]] - size[0][domain_indexes[0]] + 1);
    grid_size.push_back(size[1][domain_indexes[1]] - size[0][domain_indexes[1]] + 1);

    path = "axisLabels";
    std::vector<CPLString> grid_axes = Split(CPLGetXMLValue(grid, path, ""), " ", axis_order_swap);
    CPLSetXMLValue(psService, "GridAxes", Join(grid_axes, ","));

    std::vector<double> origin;
    std::vector<std::vector<double>> offsets;
    if (!GridOffsets(grid, subtype, origin, offsets, axes, &metadata)) {
        return false;
    }

    SetGeometry(grid_size, origin, offsets, axisOrder);

    // has the user set the dimension to band?
    CPLString dimension_to_band = CPLGetXMLValue(psService, "DimensionToBand", "");
    int dimension_to_band_index = IndexOf(dimension_to_band, axes); // returns -1 if dimension_to_band is ""
    if (IndexOf(dimension_to_band_index, domain_indexes) != -1) {
        CPLError(CE_Failure, CPLE_AppDefined, "'Dimension to band' can't be x nor y dimension.");
        return false;
    }
    if (dimension_to_band != "" && dimension_to_band_index == -1) {
        CPLError(CE_Failure, CPLE_AppDefined, "Given 'dimension to band' does not exist in coverage.");
        return false;
    }

    // has the user set slicing or trimming?
    std::vector<CPLString> dimensions = Split(CPLGetXMLValue(psService, "Dimensions", ""), ";");
    // it is ok to have trimming or even slicing for x/y, it just affects our bounding box
    std::vector<std::vector<double>> domain_trim;
    std::vector<CPLString> dimension_to_band_trim;

    // are all dimensions that are not x/y domain and dimension to band sliced?
    // if not, bands can't be defined, see below
    bool dimensions_are_ok = true;
    for (unsigned int i = 0; i < axes.size(); ++i) {
        std::vector<CPLString> params;
        for (unsigned int j = 0; j < dimensions.size(); ++j) {
            if (dimensions[j].find(axes[i] + "(") != std::string::npos) {
                params = Split(FromParenthesis(dimensions[j]), ",");
                break;
            }
        }
        int domain_index = IndexOf(axes[i], domain);
        if (domain_index != -1) {
            std::vector<double> trim = Flist(params, 0, 2);
            domain_trim.push_back(trim);
            continue;
        }
        if (axes[i] == dimension_to_band) {
            dimension_to_band_trim = params;
            continue;
        }
        // size == 1 => sliced
        if (params.size() != 1) {
            dimensions_are_ok = false;
        }
    }
    if (domain_trim.size() > 0) {
        // todo: BoundGeometry(domain_trim);
    }

    // check for CRS override
    CPLString crs = CPLGetXMLValue(psService, "CRS", "");
    if (crs != "" && crs != osCRS) {
        if (!SetCRS(crs, false)) {
            return false;
        }
        // todo: support CRS override, it requires warping the grid to the new CRS
        CPLError(CE_Failure, CPLE_AppDefined, "CRS override not yet supported.");
        return false;
        SetGeometry(grid_size, origin, offsets, axisOrder);
    }

    // todo: ElevationDomain, DimensionDomain

    // rangeType

    // get the field metadata
    // get the count of fields
    // if Range is set in service that may limit the fields
    int fields = ParseRange(coverage, &metadata);
    if (fields == 0) {
        //return false;
    }

    // add PAM metadata to domain SUBDATASET_i
    CPLString subdataset = GetSubdataset(CPLGetXMLValue(psService, "CoverageName", ""));
    //this->SetMetadata(metadata, subdataset);

    this->SetMetadata(metadata, "SUBDATASETS");
    CSLDestroy(metadata);
    TrySaveXML();

    // determine the band count
    int bands = 0;
    if (dimensions_are_ok) {
        // must not have a loose dimension
        if (dimension_to_band != "") {
            if (fields == 1) {
                bands = size[1][dimension_to_band_index] - size[0][dimension_to_band_index] + 1;
            }
        } else {
            bands = fields;
        }
    }
    CPLSetXMLValue(psService, "BandCount", CPLString().Printf("%d",bands));

    // set the PreferredFormat value in service, unless it is set
    // by the user, either through direct edit or options
    if (!SetFormat(coverage)) {
        // all attempts to find a format have failed...
        CPLError(CE_Failure, CPLE_AppDefined, "All attempts to find a format have failed, giving up.");
        return false;
    }

    // todo: set the Interpolation, check it against InterpolationSupported

    bServiceDirty = TRUE;
    return true;
}
