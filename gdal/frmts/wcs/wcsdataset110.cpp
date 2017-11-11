/******************************************************************************
 *
 * Project:  WCS Client Driver
 * Purpose:  Implementation of Dataset class for WCS 1.1.
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
/*                         GetExtent()                                  */
/*                                                                      */
/************************************************************************/

std::vector<double> WCSDataset110::GetExtent(int nXOff, int nYOff,
                                             int nXSize, int nYSize,
                                             CPL_UNUSED int nBufXSize, CPL_UNUSED int nBufYSize)
{
    std::vector<double> extent;

    // outer edges of outer pixels.
    extent.push_back(adfGeoTransform[0] +
                     (nXOff) * adfGeoTransform[1]);
    extent.push_back(adfGeoTransform[3] +
                     (nYOff + nYSize) * adfGeoTransform[5]);
    extent.push_back(adfGeoTransform[0] +
                     (nXOff + nXSize) * adfGeoTransform[1]);
    extent.push_back(adfGeoTransform[3] +
                     (nYOff) * adfGeoTransform[5]);

    // WCS 1.1 extents are centers of outer pixels.
    extent[2] -= adfGeoTransform[1] * 0.5;
    extent[0] += adfGeoTransform[1] * 0.5;
    extent[1] -= adfGeoTransform[5] * 0.5;
    extent[3] += adfGeoTransform[5] * 0.5;

    // Carefully adjust bounds for pixel centered values at new
    // sampling density.

    double dfXStep = adfGeoTransform[1];
    double dfYStep = adfGeoTransform[5];

    if( nBufXSize != nXSize || nBufYSize != nYSize )
    {
        dfXStep = (nXSize/(double)nBufXSize) * adfGeoTransform[1];
        dfYStep = (nYSize/(double)nBufYSize) * adfGeoTransform[5];

        extent[0]  = nXOff * adfGeoTransform[1] + adfGeoTransform[0]
            + dfXStep * 0.5;
        extent[2]  = extent[0] + (nBufXSize - 1) * dfXStep;

        extent[3]  = nYOff * adfGeoTransform[5] + adfGeoTransform[3]
            + dfYStep * 0.5;
        extent[1]  = extent[3] + (nBufYSize - 1) * dfYStep;
    }

    extent.push_back(dfXStep);
    extent.push_back(dfYStep);

    return extent;
}

/************************************************************************/
/*                        GetCoverageRequest()                          */
/*                                                                      */
/************************************************************************/

CPLString WCSDataset110::GetCoverageRequest(bool scaled,
                                            CPL_UNUSED int nBufXSize, CPL_UNUSED int nBufYSize,
                                            std::vector<double> extent,
                                            CPLString osBandList)
{
    CPLString osRequest, osRangeSubset;

/* -------------------------------------------------------------------- */
/*      URL encode strings that could have questionable characters.     */
/* -------------------------------------------------------------------- */
    CPLString osCoverage = CPLGetXMLValue( psService, "CoverageName", "" );

    char *pszEncoded = CPLEscapeString( osCoverage, -1, CPLES_URL );
    osCoverage = pszEncoded;
    CPLFree( pszEncoded );

    CPLString osFormat = CPLGetXMLValue( psService, "PreferredFormat", "" );

    pszEncoded = CPLEscapeString( osFormat, -1, CPLES_URL );
    osFormat = pszEncoded;
    CPLFree( pszEncoded );


    osRangeSubset.Printf("&RangeSubset=%s",
                         CPLGetXMLValue(psService,"FieldName",""));

    if( CPLGetXMLValue( psService, "Resample", NULL ) )
    {
        osRangeSubset += ":";
        osRangeSubset += CPLGetXMLValue( psService, "Resample", "");
    }

    if( osBandList != "" )
    {
        osRangeSubset +=
            CPLString().Printf( "[%s[%s]]",
                                osBandIdentifier.c_str(),
                                osBandList.c_str() );
    }



    if (GML_IsSRSLatLongOrder(osCRS.c_str())) {
        double tmp = extent[0];
        extent[0] = extent[1];
        extent[1] = tmp;
        tmp = extent[2];
        extent[2] = extent[3];
        extent[3] = tmp;
    }
    CPLString request = CPLGetXMLValue( psService, "ServiceURL", "" );
    request = CPLURLAddKVP(request, "SERVICE", "WCS");
    request += CPLString().Printf(
        "&VERSION=%s&REQUEST=GetCoverage&IDENTIFIER=%s"
        "&FORMAT=%s&BOUNDINGBOX=%.15g,%.15g,%.15g,%.15g,%s%s%s",
        CPLGetXMLValue( psService, "Version", "" ),
        osCoverage.c_str(),
        osFormat.c_str(),
        extent[0], extent[1], extent[2], extent[3],
        osCRS.c_str(),
        osRangeSubset.c_str(),
        CPLGetXMLValue( psService, "GetCoverageExtra", "" ) );
    /*
    nogridcrs diff add
        0      0   1
        0      1   1
        1      0   0
        1      1   1
    */
    if( scaled || !EQUAL(CPLGetXMLValue( psService, "NoGridCRS", "" ), "TRUE") )
    {
        request += CPLString().Printf(
            "&GridBaseCRS=%s"
            "&GridCS=urn:ogc:def:cs:OGC:0.0:Grid2dSquareCS"
            "&GridType=urn:ogc:def:method:WCS:1.1:2dGridIn2dCrs"
            "&GridOrigin=%.15g,%.15g"
            "&GridOffsets=%.15g,0,0,%.15g",
            osCRS.c_str(),
            extent[0], extent[3],
            extent[4], extent[5] );
    }
    fprintf(stderr, "URL=%s\n", request.c_str());
    return request;
}

/************************************************************************/
/*                        DescribeCoverageRequest()                     */
/*                                                                      */
/************************************************************************/

CPLString WCSDataset110::DescribeCoverageRequest()
{
    CPLString request = CPLGetXMLValue( psService, "ServiceURL", "" );
    request = CPLURLAddKVP(request, "SERVICE", "WCS");
    request = CPLURLAddKVP(request, "REQUEST", "DescribeCoverage");
    request = CPLURLAddKVP(request, "VERSION", CPLGetXMLValue( psService, "Version", "1.1.0" ));
    request = CPLURLAddKVP(request, "IDENTIFIERS", CPLGetXMLValue( psService, "CoverageName", "" ));
    request = CPLURLAddKVP(request, "FORMAT", "text/xml");
    CPLString extra = CPLGetXMLValue(psService, "DescribeCoverageExtra", "");
    if (extra != "") {
        std::vector<CPLString> pairs = Split(extra, "&");
        for (unsigned int i = 0; i < pairs.size(); ++i) {
            std::vector<CPLString> pair = Split(pairs[i], "=");
            request = CPLURLAddKVP(request, pair[0], pair[1]);
        }
    }
    return request;
}

/************************************************************************/
/*                         CoverageOffering()                           */
/*                                                                      */
/************************************************************************/

CPLXMLNode *WCSDataset110::CoverageOffering(CPLXMLNode *psDC)
{
    return CPLGetXMLNode( psDC,"=CoverageDescriptions.CoverageDescription");
}


/************************************************************************/
/*                          ExtractGridInfo()                           */
/*                                                                      */
/*      Collect info about grid from describe coverage for WCS 1.1.     */
/*                                                                      */
/************************************************************************/

bool WCSDataset110::ExtractGridInfo()

{
    CPLXMLNode * psCO = CPLGetXMLNode( psService, "CoverageDescription" );

    if( psCO == NULL )
        return false;

/* -------------------------------------------------------------------- */
/*      We need to strip off name spaces so it is easier to             */
/*      searchfor plain gml names.                                      */
/* -------------------------------------------------------------------- */
    CPLStripXMLNamespace( psCO, NULL, TRUE );

/* -------------------------------------------------------------------- */
/*      Verify we have a SpatialDomain and GridCRS.                     */
/* -------------------------------------------------------------------- */
    CPLXMLNode *psSD =
        CPLGetXMLNode( psCO, "Domain.SpatialDomain" );
    CPLXMLNode *psGCRS =
        CPLGetXMLNode( psSD, "GridCRS" );

    if( psSD == NULL || psGCRS == NULL )
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "Unable to find GridCRS in CoverageDescription,\n"
                  "unable to process WCS Coverage." );
        return false;
    }

/* -------------------------------------------------------------------- */
/*      Extract Geotransform from GridCRS.                              */
/* -------------------------------------------------------------------- */
    const char *pszGridType = CPLGetXMLValue( psGCRS, "GridType",
                                              "urn:ogc:def:method:WCS::2dSimpleGrid" );

    char **papszOriginTokens =
        CSLTokenizeStringComplex( CPLGetXMLValue( psGCRS, "GridOrigin", ""),
                                  " ", FALSE, FALSE );
    char **papszOffsetTokens =
        CSLTokenizeStringComplex( CPLGetXMLValue( psGCRS, "GridOffsets", ""),
                                  " ", FALSE, FALSE );

    if( strstr(pszGridType,":2dGridIn2dCrs")
        || strstr(pszGridType,":2dGridin2dCrs") )
    {
        if( CSLCount(papszOffsetTokens) == 4
            && CSLCount(papszOriginTokens) == 2 )
        {
            adfGeoTransform[0] = CPLAtof(papszOriginTokens[0]);
            adfGeoTransform[1] = CPLAtof(papszOffsetTokens[0]);
            adfGeoTransform[2] = CPLAtof(papszOffsetTokens[1]);
            adfGeoTransform[3] = CPLAtof(papszOriginTokens[1]);
            adfGeoTransform[4] = CPLAtof(papszOffsetTokens[2]);
            adfGeoTransform[5] = CPLAtof(papszOffsetTokens[3]);
        }
        else
        {
            CPLError( CE_Failure, CPLE_AppDefined,
                      "2dGridIn2dCrs does not have expected GridOrigin or\n"
                      "GridOffsets values - unable to process WCS coverage.");
            CSLDestroy( papszOffsetTokens );
            CSLDestroy( papszOriginTokens );
            return false;
        }
    }

    else if( strstr(pszGridType,":2dGridIn3dCrs") )
    {
        if( CSLCount(papszOffsetTokens) == 6
            && CSLCount(papszOriginTokens) == 3 )
        {
            adfGeoTransform[0] = CPLAtof(papszOriginTokens[0]);
            adfGeoTransform[1] = CPLAtof(papszOffsetTokens[0]);
            adfGeoTransform[2] = CPLAtof(papszOffsetTokens[1]);
            adfGeoTransform[3] = CPLAtof(papszOriginTokens[1]);
            adfGeoTransform[4] = CPLAtof(papszOffsetTokens[3]);
            adfGeoTransform[5] = CPLAtof(papszOffsetTokens[4]);
        }
        else
        {
            CPLError( CE_Failure, CPLE_AppDefined,
                      "2dGridIn3dCrs does not have expected GridOrigin or\n"
                      "GridOffsets values - unable to process WCS coverage.");
            CSLDestroy( papszOffsetTokens );
            CSLDestroy( papszOriginTokens );
            return false;
        }
    }

    else if( strstr(pszGridType,":2dSimpleGrid") )
    {
        if( CSLCount(papszOffsetTokens) == 2
            && CSLCount(papszOriginTokens) == 2 )
        {
            adfGeoTransform[0] = CPLAtof(papszOriginTokens[0]);
            adfGeoTransform[1] = CPLAtof(papszOffsetTokens[0]);
            adfGeoTransform[2] = 0.0;
            adfGeoTransform[3] = CPLAtof(papszOriginTokens[1]);
            adfGeoTransform[4] = 0.0;
            adfGeoTransform[5] = CPLAtof(papszOffsetTokens[1]);
        }
        else
        {
            CPLError( CE_Failure, CPLE_AppDefined,
                      "2dSimpleGrid does not have expected GridOrigin or\n"
                      "GridOffsets values - unable to process WCS coverage.");
            CSLDestroy( papszOffsetTokens );
            CSLDestroy( papszOriginTokens );
            return false;
        }
    }

    else
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "Unrecognized GridCRS.GridType value '%s',\n"
                  "unable to process WCS coverage.",
                  pszGridType );
        CSLDestroy( papszOffsetTokens );
        CSLDestroy( papszOriginTokens );
        return false;
    }

    CSLDestroy( papszOffsetTokens );
    CSLDestroy( papszOriginTokens );

    // GridOrigin is center of pixel ... offset half pixel to adjust.

    adfGeoTransform[0] -= (adfGeoTransform[1]+adfGeoTransform[2]) * 0.5;
    adfGeoTransform[3] -= (adfGeoTransform[4]+adfGeoTransform[5]) * 0.5;

/* -------------------------------------------------------------------- */
/*      Establish our coordinate system.                                */
/* -------------------------------------------------------------------- */
    CPLString crs = ParseCRS(psGCRS);

    if (crs.empty()) {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "Unable to find GridCRS.GridBaseCRS" );
        return false;
    }

    // todo: our new CRSImpliesAxisOrderSwap should be very forgiving so that this does not end here
    if (!SetCRS(crs, true)) {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "Unable to interpret GridBaseCRS '%s'.",
                  crs.c_str() );
        return false;
    }

/* -------------------------------------------------------------------- */
/*      Search for an ImageCRS for raster size.                         */
/* -------------------------------------------------------------------- */
    CPLXMLNode *psNode;

    nRasterXSize = -1;
    nRasterYSize = -1;
    for( psNode = psSD->psChild;
         psNode != NULL && nRasterXSize == -1;
         psNode = psNode->psNext )
    {
        if( psNode->eType != CXT_Element
            || !EQUAL(psNode->pszValue,"BoundingBox") )
            continue;

        CPLString osBBCRS = ParseCRS(psNode);
        if (strstr(osBBCRS,":imageCRS")) {
            std::vector<CPLString> bbox = ParseBoundingBox(psNode);
            if (bbox.size() >= 2) {
                std::vector<int> low = Ilist(Split(bbox[0], " "), 0, 2);
                std::vector<int> high = Ilist(Split(bbox[1], " "), 0, 2);
                if (low[0] == 0 && low[1] == 0) {
                    nRasterXSize = high[0];
                    nRasterYSize = high[1];
                }
            }
        }
    }

/* -------------------------------------------------------------------- */
/*      Otherwise we search for a bounding box in our coordinate        */
/*      system and derive the size from that.                           */
/* -------------------------------------------------------------------- */
    for( psNode = psSD->psChild;
         psNode != NULL && nRasterXSize == -1;
         psNode = psNode->psNext )
    {
        if( psNode->eType != CXT_Element
            || !EQUAL(psNode->pszValue,"BoundingBox") )
            continue;

        CPLString osBBCRS = ParseCRS(psNode);
        if (osBBCRS == osCRS) {
            std::vector<CPLString> bbox = ParseBoundingBox(psNode);
            if (bbox.size() >= 2
                && adfGeoTransform[2] == 0.0
                && adfGeoTransform[4] == 0.0)
            {
                std::vector<double> low = Flist(Split(bbox[0], " ", axis_order_swap), 0, 2);
                std::vector<double> high = Flist(Split(bbox[1], " ", axis_order_swap), 0, 2);
                nRasterXSize =
                    (int) ((high[0] - low[0]) / adfGeoTransform[1] + 1.01);
                nRasterYSize =
                    (int) ((high[1] - low[1]) / fabs(adfGeoTransform[5]) + 1.01);
            }
        }
    }

/* -------------------------------------------------------------------- */
/*      Do we have a coordinate system override?                        */
/* -------------------------------------------------------------------- */
    const char *pszProjOverride = CPLGetXMLValue( psService, "SRS", NULL );

    if( pszProjOverride )
    {
        OGRSpatialReference oSRS;

        if( oSRS.SetFromUserInput( pszProjOverride ) != OGRERR_NONE )
        {
            CPLError( CE_Failure, CPLE_AppDefined,
                      "<SRS> element contents not parsable:\n%s",
                      pszProjOverride );
            return false;
        }

        CPLFree( pszProjection );
        oSRS.exportToWkt( &pszProjection );
    }

/* -------------------------------------------------------------------- */
/*      Pick a format type if we don't already have one selected.       */
/*                                                                      */
/*      We will prefer anything that sounds like TIFF, otherwise        */
/*      falling back to the first supported format.  Should we          */
/*      consider preferring the nativeFormat if available?              */
/* -------------------------------------------------------------------- */
    if( CPLGetXMLValue( psService, "PreferredFormat", NULL ) == NULL )
    {
        CPLString osPreferredFormat;

        for( psNode = psCO->psChild; psNode != NULL; psNode = psNode->psNext )
        {
            if( psNode->eType == CXT_Element
                && EQUAL(psNode->pszValue,"SupportedFormat")
                && psNode->psChild
                && psNode->psChild->eType == CXT_Text )
            {
                if( osPreferredFormat.empty() )
                    osPreferredFormat = psNode->psChild->pszValue;

                if( strstr(psNode->psChild->pszValue,"tiff") != NULL
                    || strstr(psNode->psChild->pszValue,"TIFF") != NULL
                    || strstr(psNode->psChild->pszValue,"Tiff") != NULL )
                {
                    osPreferredFormat = psNode->psChild->pszValue;
                    break;
                }
            }
        }

        if( !osPreferredFormat.empty() )
        {
            bServiceDirty = TRUE;
            CPLCreateXMLElementAndValue( psService, "PreferredFormat",
                                         osPreferredFormat );
        }
    }

/* -------------------------------------------------------------------- */
/*      Try to identify a nodata value.  For now we only support the    */
/*      singleValue mechanism.                                          */
/* -------------------------------------------------------------------- */
    if( CPLGetXMLValue( psService, "NoDataValue", NULL ) == NULL )
    {
        const char *pszSV =
            CPLGetXMLValue( psCO, "Range.Field.NullValue", NULL );

        if( pszSV != NULL && (CPLAtof(pszSV) != 0.0 || *pszSV == DIGIT_ZERO) )
        {
            bServiceDirty = TRUE;
            CPLCreateXMLElementAndValue( psService, "NoDataValue",
                                         pszSV );
        }
    }

/* -------------------------------------------------------------------- */
/*      Grab the field name, if possible.                               */
/* -------------------------------------------------------------------- */
    if( CPLGetXMLValue( psService, "FieldName", NULL ) == NULL )
    {
        CPLString osFieldName =
            CPLGetXMLValue( psCO, "Range.Field.Identifier", "" );

        if( !osFieldName.empty() )
        {
            bServiceDirty = TRUE;
            CPLCreateXMLElementAndValue( psService, "FieldName",
                                         osFieldName );
        }
        else
        {
            CPLError( CE_Failure, CPLE_AppDefined,
                      "Unable to find required Identifier name %s for Range Field.",
                      osCRS.c_str() );
            return false;
        }
    }

/* -------------------------------------------------------------------- */
/*      Do we have a "Band" axis?  If so try to grab the bandcount      */
/*      and data type from it.                                          */
/* -------------------------------------------------------------------- */
    CPLXMLNode * psAxis = CPLGetXMLNode(
        psService, "CoverageDescription.Range.Field.Axis" );

    if( (EQUAL(CPLGetXMLValue(psAxis,"Identifier",""),"Band")
         || EQUAL(CPLGetXMLValue(psAxis,"Identifier",""),"Bands"))
        && CPLGetXMLNode(psAxis,"AvailableKeys") != NULL )
    {
        osBandIdentifier = CPLGetXMLValue(psAxis,"Identifier","");

        // verify keys are ascending starting at 1
        CPLXMLNode *psValues = CPLGetXMLNode(psAxis,"AvailableKeys");
        CPLXMLNode *psSV;
        int iBand;

        for( psSV = psValues->psChild, iBand = 1;
             psSV != NULL;
             psSV = psSV->psNext, iBand++ )
        {
            if( psSV->eType != CXT_Element
                || !EQUAL(psSV->pszValue,"Key")
                || psSV->psChild == NULL
                || psSV->psChild->eType != CXT_Text
                || atoi(psSV->psChild->pszValue) != iBand )
            {
                osBandIdentifier = "";
                break;
            }
        }

        if( !osBandIdentifier.empty() )
        {
            bServiceDirty = TRUE;
            if( CPLGetXMLValue(psService,"BandIdentifier",NULL) == NULL )
                CPLCreateXMLElementAndValue( psService, "BandIdentifier",
                                             osBandIdentifier );

            if( CPLGetXMLValue(psService,"BandCount",NULL) == NULL )
                CPLCreateXMLElementAndValue( psService, "BandCount",
                                             CPLString().Printf("%d",iBand-1));
        }

        // Is this an ESRI server returning a GDAL recognised data type?
        CPLString osDataType = CPLGetXMLValue( psAxis, "DataType", "" );
        if( GDALGetDataTypeByName(osDataType) != GDT_Unknown
            && CPLGetXMLValue(psService,"BandType",NULL) == NULL )
        {
            bServiceDirty = TRUE;
            CPLCreateXMLElementAndValue( psService, "BandType", osDataType );
        }
    }

    return true;
}

/************************************************************************/
/*                      ParseCapabilities()                             */
/************************************************************************/

CPLErr WCSDataset110::ParseCapabilities( CPLXMLNode * Capabilities, CPLString url )
{
    CPLStripXMLNamespace(Capabilities, NULL, TRUE);

    // make sure this is a capabilities document
    if( strcmp(Capabilities->pszValue, "Capabilities") != 0 )
    {
        return CE_Failure;
    }

    char **metadata = NULL;
    CPLString path = "WCS_GLOBAL#";

    CPLString key = path + "version";
    metadata = CSLSetNameValue(metadata, key, Version());

    for( CPLXMLNode *node = Capabilities->psChild; node != NULL; node = node->psNext)
    {
        const char *attr = node->pszValue;
        if( node->eType == CXT_Attribute && EQUAL(attr, "updateSequence") )
        {
            key = path + "updateSequence";
            CPLString value = CPLGetXMLValue(node, NULL, "");
            metadata = CSLSetNameValue(metadata, key, value);
        }
    }

    // identification metadata
    CPLString path2 = path;
    std::vector<CPLString> keys2 = {
        "Title",
        "Abstract",
        "Fees",
        "AccessConstraints"
    };
    CPLXMLNode *service = AddSimpleMetaData(&metadata, Capabilities, path2, "ServiceIdentification", keys2);
    CPLString kw = GetKeywords(service, "Keywords", "Keyword");
    if (kw != "") {
        CPLString name = path + "Keywords";
        metadata = CSLSetNameValue(metadata, name, kw);
    }
    CPLString profiles = GetKeywords(service, "", "Profile");
    if (profiles != "") {
        CPLString name = path + "Profiles";
        metadata = CSLSetNameValue(metadata, name, profiles);
    }

    // provider metadata
    path2 = path;
    keys2 = {
        "ProviderName"
    };
    CPLXMLNode *provider = AddSimpleMetaData(&metadata, Capabilities, path2, "ServiceProvider", keys2);
    if (provider) {
        if( CPLXMLNode *site = CPLGetXMLNode(provider, "ProviderSite") )
        {
            CPLString path3 = path2 + "ProviderSite";
            CPLString value = CPLGetXMLValue(CPLGetXMLNode(site, "href"), NULL, "");
            metadata = CSLSetNameValue( metadata, path3, value );
        }
        CPLString path3 = path2;
        std::vector<CPLString> keys3 = {
            "IndividualName",
            "PositionName",
            "Role"
        };
        CPLXMLNode *contact = AddSimpleMetaData(&metadata, provider, path3, "ServiceContact", keys3);
        if (contact) {
            CPLString path4 = path3;
            std::vector<CPLString> keys4 = {
                "HoursOfService",
                "ContactInstructions"
            };
            CPLXMLNode *info = AddSimpleMetaData(&metadata, contact, path4, "ContactInfo", keys4);
            if (info) {
                CPLString path5 = path4;
                std::vector<CPLString> keys5 = {
                    "DeliveryPoint",
                    "City",
                    "AdministrativeArea",
                    "PostalCode",
                    "Country",
                    "ElectronicMailAddress"
                };
                CPLString path6 = path4;
                std::vector<CPLString> keys6 = {
                    "Voice",
                    "Facsimile"
                };
                AddSimpleMetaData(&metadata, info, path5, "Address", keys5);
                AddSimpleMetaData(&metadata, info, path6, "Phone", keys6);
            }
        }
    }

    // operations metadata
    CPLString DescribeCoverageURL = "";
    if( CPLXMLNode *service2 = CPLGetXMLNode(Capabilities, "OperationsMetadata") )
    {
        for( CPLXMLNode *operation = service2->psChild; operation != NULL; operation = operation->psNext)
        {
            if( operation->eType != CXT_Element
                || !EQUAL(operation->pszValue, "Operation") )
            {
                continue;
            }
            if( EQUAL(CPLGetXMLValue(CPLGetXMLNode(operation, "name"), NULL, ""), "DescribeCoverage") )
            {
                DescribeCoverageURL = CPLGetXMLValue(
                    CPLGetXMLNode(CPLSearchXMLNode(operation, "Get"),
                                  "href"), NULL, "");
            }
        }
    }
    // todo: if DescribeCoverageURL looks wrong (i.e. has localhost) should we change it?
    if (DescribeCoverageURL.find("localhost") != std::string::npos) {
        DescribeCoverageURL = URLRemoveKey(url, "request");
    }

    // service metadata (2.0)
    CPLString ext = "ServiceMetadata";
    CPLString formats = GetKeywords(Capabilities, ext, "formatSupported");
    if (formats != "") {
        CPLString name = path + "formatSupported";
        metadata = CSLSetNameValue(metadata, name, formats);
    }
    // wcs:Extension seems to be rather varying
    // Extension.crsSupported from GeoServer is a huge list, todo: shorten it to something like EPSG(x,y-z)
    // Extension.interpolationSupported
    ext += ".Extension";
    CPLString interpolation = GetKeywords(Capabilities, ext, "interpolationSupported");
    if (interpolation == "") {
        interpolation = GetKeywords(Capabilities, ext + ".InterpolationMetadata", "InterpolationSupported");
    }
    if (interpolation != "") {
        CPLString name = path + "InterpolationSupported";
        metadata = CSLSetNameValue(metadata, name, interpolation);
    }
    CPLString crs = GetKeywords(Capabilities, ext, "crsSupported");
    if (crs == "") {
        crs = GetKeywords(Capabilities, ext + ".CrsMetadata", "crsSupported");
    }
    if (crs != "") {
        CPLString name = path + "crsSupported";

        // crs, replace "http://www.opengis.net/def/crs/EPSG/0/" with EPSG:

        metadata = CSLSetNameValue(metadata, name, crs);
    }

    this->SetMetadata( metadata, "" );
    CSLDestroy( metadata );
    metadata = NULL;

    // contents metadata
    if( CPLXMLNode *contents = CPLGetXMLNode(Capabilities, "Contents") )
    {
        int index = 1;
        for( CPLXMLNode *summary = contents->psChild; summary != NULL; summary = summary->psNext)
        {
            if( summary->eType != CXT_Element
                || !EQUAL(summary->pszValue, "CoverageSummary") )
            {
                continue;
            }
            CPLString path3;
            path3.Printf( "SUBDATASET_%d_", index);

            CPLXMLNode *node;

            CPLString keywords = GetKeywords(summary, "Keywords", "Keyword");
            if (keywords != "") {
                CPLString name = path3 + "KEYWORDS";
                metadata = CSLSetNameValue(metadata, name, keywords);
            }
            crs = GetKeywords(summary, "", "SupportedCRS");
            if (crs != "") {
                CPLString name = path3 + "SUPPORTED_CRS";
                metadata = CSLSetNameValue(metadata, name, crs);
            }

            if ((node = CPLGetXMLNode(summary, "CoverageId"))
                || (node = CPLGetXMLNode(summary, "Identifier")))
            {
                CPLString name = path3 + "NAME";
                CPLString value = DescribeCoverageURL;
                value = CPLURLAddKVP(value, "service", "WCS");
                value = CPLURLAddKVP(value, "version", this->Version());
                if (EQUAL(node->pszValue, "CoverageId")) {
                    value = CPLURLAddKVP(value, "coverageId", CPLGetXMLValue(node, NULL, ""));
                } else {
                    value = CPLURLAddKVP(value, "identifiers", CPLGetXMLValue(node, NULL, ""));
                }
                value = "WCS:" + value;
                // GDAL Data Model:
                // The value of the _NAME is a string that can be passed to GDALOpen() to access the file.
                metadata = CSLSetNameValue(metadata, name, value);
            }

            if ((node = CPLGetXMLNode(summary, "WGS84BoundingBox"))) {
                CPLString name = path3 + "WGS84BBOX";
                // WGS84BoundingBox is always lon,lat
                std::vector<CPLString> bbox = ParseBoundingBox(node);
                if (bbox.size() >= 2) {
                    std::vector<double> low = Flist(Split(bbox[0], " "), 0, 2);
                    std::vector<double> high = Flist(Split(bbox[1], " "), 0, 2);
                    CPLString tmp;
                    tmp.Printf("%f,%f,%f,%f", low[0], low[1], high[0], high[1]);
                    metadata = CSLSetNameValue(metadata, name, tmp);
                }
            }

            if ((node = CPLGetXMLNode(summary, "BoundingBox"))) {
                CPLString CRS = ParseCRS(node);
                std::vector<CPLString> bbox = ParseBoundingBox(node);
                if (bbox.size() >= 2) {
                    // todo: CRSImpliesAxisOrderSwap may fail, skip that for now
                    bool local_axis_order_swap;
                    CRSImpliesAxisOrderSwap(CRS, local_axis_order_swap, NULL);
                    std::vector<CPLString> b = Split(bbox[0], " ", local_axis_order_swap);
                    std::vector<double> low;
                    if (b.size() >= 2) {
                        low = Flist(b, 0, 2);
                    }
                    std::vector<double> high;
                    b = Split(bbox[1], " ", local_axis_order_swap);
                    if (b.size() >= 2) {
                        high = Flist(b, 0, 2);
                    }
                    if (low.size() >= 2 && high.size() >= 2) {
                        CPLString tmp;
                        tmp.Printf("CRS=%s minX=%f minY=%f maxX=%f maxY=%f",
                                   CRS.c_str(), low[0], low[1], high[0], high[1]);
                        CPLString name = path3 + "BBOX";
                        metadata = CSLSetNameValue(metadata, name, tmp);
                    }
                }
            }

            if ((node = CPLGetXMLNode(summary, "CoverageSubtype"))) {
                CPLString name = path3 + "TYPE";
                metadata = CSLSetNameValue(metadata, name, CPLGetXMLValue(node, NULL, ""));
            }

            index++;
        }
    }
    this->SetMetadata( metadata, "SUBDATASETS" );
    CSLDestroy( metadata );
    return CE_None;
}

/************************************************************************/
/*                          ExceptionNodeName()                         */
/*                                                                      */
/************************************************************************/

const char *WCSDataset110::ExceptionNodeName()
{
    return "=ExceptionReport.Exception.ExceptionText";
}
