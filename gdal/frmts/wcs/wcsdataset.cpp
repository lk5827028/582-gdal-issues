/******************************************************************************
 *
 * Project:  WCS Client Driver
 * Purpose:  Implementation of Dataset and RasterBand classes for WCS.
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 2006, Frank Warmerdam
 * Copyright (c) 2008-2013, Even Rouault <even dot rouault at mines-paris dot org>
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

#include "wcsdataset.h"
#include "wcsrasterband.h"
#include "wcsutils.h"

using namespace WCSUtils;

/************************************************************************/
/*                             WCSDataset()                             */
/************************************************************************/

WCSDataset::WCSDataset(int version, const char *cache_dir) :
    m_cache_dir(cache_dir),
    bServiceDirty(false),
    psService(NULL),
    papszSDSModifiers(NULL),
    m_Version(version),
    pszProjection(NULL),
    native_crs(true),
    axis_order_swap(false),
    pabySavedDataBuffer(NULL),
    papszHttpOptions(NULL),
    nMaxCols(-1),
    nMaxRows(-1)
{
    adfGeoTransform[0] = 0.0;
    adfGeoTransform[1] = 1.0;
    adfGeoTransform[2] = 0.0;
    adfGeoTransform[3] = 0.0;
    adfGeoTransform[4] = 0.0;
    adfGeoTransform[5] = 1.0;

    apszCoverageOfferingMD[0] = NULL;
    apszCoverageOfferingMD[1] = NULL;
}

/************************************************************************/
/*                            ~WCSDataset()                             */
/************************************************************************/

WCSDataset::~WCSDataset()

{
    // perhaps this should be moved into a FlushCache() method.
    if( bServiceDirty && !STARTS_WITH_CI(GetDescription(), "<WCS_GDAL>") )
    {
        CPLSerializeXMLTreeToFile( psService, GetDescription() );
        bServiceDirty = false;
    }

    CPLDestroyXMLNode( psService );

    CPLFree( pszProjection );
    pszProjection = NULL;

    CSLDestroy( papszHttpOptions );
    CSLDestroy( papszSDSModifiers );

    CPLFree( apszCoverageOfferingMD[0] );

    FlushMemoryResult();
}

/************************************************************************/
/*                           SetCRS()                                   */
/*                                                                      */
/*      Set the name and the WKT of the projection of this dataset.     */
/*      Based on the projection, sets the axis order flag.              */
/*      Also set the native flag.                                       */
/************************************************************************/

bool WCSDataset::SetCRS(const CPLString &crs, bool native)
{
    osCRS = crs;
    if (!CRSImpliesAxisOrderSwap(osCRS, axis_order_swap, &pszProjection)) {
        return false;
    }
    native_crs = native;
    return true;
}

/************************************************************************/
/*                           SetGeometry()                              */
/*                                                                      */
/*      Set GeoTransform and RasterSize from the coverage envelope,     */
/*      axis_order, grid size, and grid offsets.                        */
/************************************************************************/

void WCSDataset::SetGeometry(const std::vector<int> &size,
                             const std::vector<double> &origin,
                             const std::vector<std::vector<double> > &offsets)
{
    // note that this method is not used by wcsdataset100.cpp
    nRasterXSize = size[0];
    nRasterYSize = size[1];

    adfGeoTransform[0] = origin[0];
    adfGeoTransform[1] = offsets[0][0];
    adfGeoTransform[2] = offsets[0].size() == 1 ? 0.0 : offsets[0][1];
    adfGeoTransform[3] = origin[1];
    adfGeoTransform[4] = offsets[1].size() == 1 ? 0.0 : offsets[1][0];
    adfGeoTransform[5] = offsets[1].size() == 1 ? offsets[1][0] : offsets[1][1];

    if (!CPLGetXMLBoolean(psService, "OriginAtBoundary")) {
        adfGeoTransform[0] -= adfGeoTransform[1] * 0.5;
        adfGeoTransform[0] -= adfGeoTransform[2] * 0.5;
        adfGeoTransform[3] -= adfGeoTransform[4] * 0.5;
        adfGeoTransform[3] -= adfGeoTransform[5] * 0.5;
    }
}

/************************************************************************/
/*                           TestUseBlockIO()                           */
/*                                                                      */
/*      Check whether we should use blocked IO (true) or direct io      */
/*      (FALSE) for a given request configuration and environment.      */
/************************************************************************/

int WCSDataset::TestUseBlockIO( CPL_UNUSED int nXOff,
                                CPL_UNUSED int nYOff,
                                int nXSize,
                                int nYSize,
                                int nBufXSize,
                                int nBufYSize )
{
    int bUseBlockedIO = bForceCachedIO;

    if( nYSize == 1 || nXSize * ((double) nYSize) < 100.0 )
        bUseBlockedIO = TRUE;

    if( nBufYSize == 1 || nBufXSize * ((double) nBufYSize) < 100.0 )
        bUseBlockedIO = TRUE;

    if( bUseBlockedIO
        && CPLTestBool( CPLGetConfigOption( "GDAL_ONE_BIG_READ", "NO") ) )
        bUseBlockedIO = FALSE;

    return bUseBlockedIO;
}

/************************************************************************/
/*                             IRasterIO()                              */
/************************************************************************/

CPLErr WCSDataset::IRasterIO( GDALRWFlag eRWFlag,
                              int nXOff, int nYOff, int nXSize, int nYSize,
                              void * pData, int nBufXSize, int nBufYSize,
                              GDALDataType eBufType,
                              int nBandCount, int *panBandMap,
                              GSpacing nPixelSpace, GSpacing nLineSpace,
                              GSpacing nBandSpace,
                              GDALRasterIOExtraArg* psExtraArg)

{
    if( (nMaxCols > 0 && nMaxCols < nBufXSize)
        ||  (nMaxRows > 0 && nMaxRows < nBufYSize) )
        return CE_Failure;

/* -------------------------------------------------------------------- */
/*      We need various criteria to skip out to block based methods.    */
/* -------------------------------------------------------------------- */
    if( TestUseBlockIO( nXOff, nYOff, nXSize, nYSize, nBufXSize, nBufYSize ) )
        return GDALPamDataset::IRasterIO(
            eRWFlag, nXOff, nYOff, nXSize, nYSize,
            pData, nBufXSize, nBufYSize, eBufType,
            nBandCount, panBandMap, nPixelSpace, nLineSpace, nBandSpace, psExtraArg );
    else
        return DirectRasterIO(
            eRWFlag, nXOff, nYOff, nXSize, nYSize,
            pData, nBufXSize, nBufYSize, eBufType,
            nBandCount, panBandMap, nPixelSpace, nLineSpace, nBandSpace, psExtraArg );
}

/************************************************************************/
/*                           DirectRasterIO()                           */
/*                                                                      */
/*      Make exactly one request to the server for this data.           */
/************************************************************************/

CPLErr
WCSDataset::DirectRasterIO( CPL_UNUSED GDALRWFlag eRWFlag,
                            int nXOff,
                            int nYOff,
                            int nXSize,
                            int nYSize,
                            void * pData,
                            int nBufXSize,
                            int nBufYSize,
                            GDALDataType eBufType,
                            int nBandCount,
                            int *panBandMap,
                            GSpacing nPixelSpace, GSpacing nLineSpace,
                            GSpacing nBandSpace,
                            CPL_UNUSED GDALRasterIOExtraArg* psExtraArg)
{
    CPLDebug( "WCS", "DirectRasterIO(%d,%d,%d,%d) -> (%d,%d) (%d bands)\n",
              nXOff, nYOff, nXSize, nYSize,
              nBufXSize, nBufYSize, nBandCount );

/* -------------------------------------------------------------------- */
/*      Get the coverage.                                               */
/* -------------------------------------------------------------------- */
    CPLHTTPResult *psResult = NULL;
    CPLErr eErr =
        GetCoverage( nXOff, nYOff, nXSize, nYSize, nBufXSize, nBufYSize,
                     nBandCount, panBandMap, &psResult );

    if( eErr != CE_None )
        return eErr;

/* -------------------------------------------------------------------- */
/*      Try and open result as a dataset.                               */
/* -------------------------------------------------------------------- */
    GDALDataset *poTileDS = GDALOpenResult( psResult );

    GDALDriver *dr = (GDALDriver*)GDALGetDriverByName("GTiff");
    CPLString filename = CPLGetXMLValue(psService, "filename", "result");
    filename = "/tmp/" + filename + ".tiff";
    dr->CreateCopy(filename, poTileDS, TRUE, NULL, NULL, NULL);

    if( poTileDS == NULL )
        return CE_Failure;

/* -------------------------------------------------------------------- */
/*      Verify configuration.                                           */
/* -------------------------------------------------------------------- */
    if( poTileDS->GetRasterXSize() != nBufXSize
        || poTileDS->GetRasterYSize() != nBufYSize )
    {
        CPLDebug( "WCS", "Got size=%dx%d instead of %dx%d.",
                  poTileDS->GetRasterXSize(), poTileDS->GetRasterYSize(),
                  nBufXSize, nBufYSize );

        CPLError( CE_Failure, CPLE_AppDefined,
                  "Returned tile does not match expected configuration.\n"
                  "Got %dx%d instead of %dx%d.",
                  poTileDS->GetRasterXSize(), poTileDS->GetRasterYSize(),
                  nBufXSize, nBufYSize );
        delete poTileDS;
        return CE_Failure;
    }

    if( ( !osBandIdentifier.empty()
          && osBandIdentifier != "none"
          && poTileDS->GetRasterCount() != nBandCount)
        ||
        ( osBandIdentifier.empty()
          && poTileDS->GetRasterCount() != GetRasterCount() ) )
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "Returned tile does not match expected band count." );
        delete poTileDS;
        return CE_Failure;
    }

/* -------------------------------------------------------------------- */
/*      Pull requested bands from the downloaded dataset.               */
/* -------------------------------------------------------------------- */
    eErr = CE_None;

    for( int iBand = 0;
         iBand < nBandCount && eErr == CE_None;
         iBand++ )
    {
        GDALRasterBand *poTileBand = NULL;

        if( !osBandIdentifier.empty() )
            poTileBand = poTileDS->GetRasterBand( iBand + 1 );
        else
            poTileBand = poTileDS->GetRasterBand( panBandMap[iBand] );

        eErr = poTileBand->RasterIO( GF_Read,
                                     0, 0, nBufXSize, nBufYSize,
                                     ((GByte *) pData) +
                                     iBand * nBandSpace, nBufXSize, nBufYSize,
                                     eBufType, nPixelSpace, nLineSpace, NULL );
    }

/* -------------------------------------------------------------------- */
/*      Cleanup                                                         */
/* -------------------------------------------------------------------- */
    delete poTileDS;

    FlushMemoryResult();

    return eErr;
}

/************************************************************************/
/*                            GetCoverage()                             */
/*                                                                      */
/*      Issue the appropriate version of request for a given window,    */
/*      buffer size and band list.                                      */
/************************************************************************/

CPLErr WCSDataset::GetCoverage( int nXOff, int nYOff, int nXSize, int nYSize,
                                int nBufXSize, int nBufYSize,
                                int nBandCount, int *panBandList,
                                CPLHTTPResult **ppsResult )

{
/* -------------------------------------------------------------------- */
/*      Figure out the georeferenced extents.                           */
/* -------------------------------------------------------------------- */
    std::vector<double> extent = GetExtent(nXOff, nYOff, nXSize, nYSize,
                                           nBufXSize, nBufYSize);

/* -------------------------------------------------------------------- */
/*      Build band list if we have the band identifier.                 */
/* -------------------------------------------------------------------- */
    CPLString osBandList;

    if( !osBandIdentifier.empty() && nBandCount > 0 && panBandList != NULL )
    {
        int iBand;

        for( iBand = 0; iBand < nBandCount; iBand++ )
        {
            if( iBand > 0 )
                osBandList += ",";
            osBandList += CPLString().Printf( "%d", panBandList[iBand] );
        }
    }

/* -------------------------------------------------------------------- */
/*      Construct a KVP GetCoverage request.                            */
/* -------------------------------------------------------------------- */
    bool scaled = nBufXSize != nXSize || nBufYSize != nYSize;
    CPLString osRequest = GetCoverageRequest(scaled, nBufXSize, nBufYSize,
                                             extent, osBandList);
    fprintf(stderr, "URL=%s\n", osRequest.c_str());

/* -------------------------------------------------------------------- */
/*      Fetch the result.                                               */
/* -------------------------------------------------------------------- */
    CPLErrorReset();
    *ppsResult = CPLHTTPFetch( osRequest, papszHttpOptions );

    if( ProcessError( *ppsResult ) )
        return CE_Failure;
    else
        return CE_None;
}

/************************************************************************/
/*                          DescribeCoverage()                          */
/*                                                                      */
/*      Fetch the DescribeCoverage result and attach it to the          */
/*      service description.                                            */
/************************************************************************/

int WCSDataset::DescribeCoverage()

{
    CPLString osRequest;

/* -------------------------------------------------------------------- */
/*      Fetch coverage description for this coverage.                   */
/* -------------------------------------------------------------------- */

    CPLXMLNode *psDC = NULL;

    // if it is in cache, get it from there
    CPLString dc_filename = this->GetDescription(); // the WCS_GDAL file (<basename>.xml)
    dc_filename.erase(dc_filename.length()-4, 4);
    dc_filename += ".DC.xml";
    if (FileIsReadable(dc_filename)) {
        psDC = CPLParseXMLFile(dc_filename);
    }

    if (!psDC) {
        osRequest = DescribeCoverageRequest();
        CPLErrorReset();
        CPLHTTPResult *psResult = CPLHTTPFetch( osRequest, papszHttpOptions );
        if( ProcessError( psResult ) ) {
            return FALSE;
        }

/* -------------------------------------------------------------------- */
/*      Parse result.                                                   */
/* -------------------------------------------------------------------- */
        psDC = CPLParseXMLString( (const char *) psResult->pabyData );
        CPLHTTPDestroyResult( psResult );

        if( psDC == NULL ) {
            return FALSE;
        }

        // if we have cache, put it there
        if (dc_filename != "") {
            CPLSerializeXMLTreeToFile(psDC, dc_filename);
        }
    }

    CPLStripXMLNamespace( psDC, NULL, TRUE );

/* -------------------------------------------------------------------- */
/*      Did we get a CoverageOffering?                                  */
/* -------------------------------------------------------------------- */
    CPLXMLNode *psCO = CoverageOffering(psDC);

    if( !psCO )
    {
        CPLDestroyXMLNode( psDC );

        CPLError( CE_Failure, CPLE_AppDefined,
                  "Failed to fetch a <CoverageOffering> back %s.",
                  osRequest.c_str() );
        return FALSE;
    }

/* -------------------------------------------------------------------- */
/*      Duplicate the coverage offering, and insert into                */
/* -------------------------------------------------------------------- */
    CPLXMLNode *psNext = psCO->psNext;
    psCO->psNext = NULL;

    CPLAddXMLChild( psService, CPLCloneXMLTree( psCO ) );
    bServiceDirty = true;

    psCO->psNext = psNext;

    CPLDestroyXMLNode( psDC );
    return TRUE;
}

/************************************************************************/
/*                            ProcessError()                            */
/*                                                                      */
/*      Process an HTTP error, reporting it via CPL, and destroying     */
/*      the HTTP result object.  Returns TRUE if there was an error,    */
/*      or FALSE if the result seems ok.                                */
/************************************************************************/

int WCSDataset::ProcessError( CPLHTTPResult *psResult )

{
/* -------------------------------------------------------------------- */
/*      There isn't much we can do in this case.  Hopefully an error    */
/*      was already issued by CPLHTTPFetch()                            */
/* -------------------------------------------------------------------- */
    if( psResult == NULL || psResult->nDataLen == 0 )
    {
        CPLHTTPDestroyResult( psResult );
        return TRUE;
    }

/* -------------------------------------------------------------------- */
/*      If we got an html document, we presume it is an error           */
/*      message and report it verbatim up to a certain size limit.      */
/* -------------------------------------------------------------------- */

    if( psResult->pszContentType != NULL
        && strstr(psResult->pszContentType, "html") != NULL )
    {
        CPLString osErrorMsg = (char *) psResult->pabyData;

        if( osErrorMsg.size() > 2048 )
            osErrorMsg.resize( 2048 );

        CPLError( CE_Failure, CPLE_AppDefined,
                  "Malformed Result:\n%s",
                  osErrorMsg.c_str() );
        CPLHTTPDestroyResult( psResult );
        return TRUE;
    }

/* -------------------------------------------------------------------- */
/*      Does this look like a service exception?  We would like to      */
/*      check based on the Content-type, but this seems quite           */
/*      undependable, even from MapServer!                              */
/* -------------------------------------------------------------------- */
    if( strstr((const char *)psResult->pabyData, "ServiceException")
        || strstr((const char *)psResult->pabyData, "ExceptionReport") )
    {
        CPLXMLNode *psTree = CPLParseXMLString( (const char *)
                                                psResult->pabyData );

        CPLStripXMLNamespace( psTree, NULL, TRUE );

        const char *pszMsg = CPLGetXMLValue(psTree, this->ExceptionNodeName(), NULL);

        if( pszMsg )
            CPLError( CE_Failure, CPLE_AppDefined,
                      "%s", pszMsg );
        else
            CPLError( CE_Failure, CPLE_AppDefined,
                      "Corrupt Service Exception:\n%s",
                      (const char *) psResult->pabyData );

        CPLDestroyXMLNode( psTree );
        CPLHTTPDestroyResult( psResult );
        return TRUE;
    }

/* -------------------------------------------------------------------- */
/*      Hopefully the error already issued by CPLHTTPFetch() is         */
/*      sufficient.                                                     */
/* -------------------------------------------------------------------- */
    if( CPLGetLastErrorNo() != 0 )
        return TRUE;

    return false;
}

/************************************************************************/
/*                       EstablishRasterDetails()                       */
/*                                                                      */
/*      Do a "test" coverage query to work out the number of bands,     */
/*      and pixel data type of the remote coverage.                     */
/************************************************************************/

int WCSDataset::EstablishRasterDetails()

{
    CPLXMLNode * psCO = CPLGetXMLNode( psService, "CoverageOffering" );

    const char* pszCols = CPLGetXMLValue( psCO, "dimensionLimit.columns", NULL );
    const char* pszRows = CPLGetXMLValue( psCO, "dimensionLimit.rows", NULL );
    if( pszCols && pszRows )
    {
        nMaxCols = atoi(pszCols);
        nMaxRows = atoi(pszRows);
        SetMetadataItem("MAXNCOLS", pszCols, "IMAGE_STRUCTURE" );
        SetMetadataItem("MAXNROWS", pszRows, "IMAGE_STRUCTURE" );
    }

/* -------------------------------------------------------------------- */
/*      Do we already have bandcount and pixel type settings?           */
/* -------------------------------------------------------------------- */
    if( CPLGetXMLValue( psService, "BandCount", NULL ) != NULL
        && CPLGetXMLValue( psService, "BandType", NULL ) != NULL )
        return TRUE;

/* -------------------------------------------------------------------- */
/*      Fetch a small block of raster data.                             */
/* -------------------------------------------------------------------- */
    CPLHTTPResult *psResult = NULL;
    CPLErr eErr;

    eErr = GetCoverage( 0, 0, 2, 2, 2, 2, 0, NULL, &psResult );
    if( eErr != CE_None )
        return false;

/* -------------------------------------------------------------------- */
/*      Try and open result as a dataset.                               */
/* -------------------------------------------------------------------- */
    GDALDataset *poDS = GDALOpenResult( psResult );

    if( poDS == NULL )
        return false;

    GDALDriver *dr = (GDALDriver*)GDALGetDriverByName("GTiff");
    CPLString filename = "/tmp/result0.tiff";
    dr->CreateCopy(filename, poDS, TRUE, NULL, NULL, NULL);

    const char* pszPrj = poDS->GetProjectionRef();
    if( pszPrj && strlen(pszPrj) > 0 )
    {
        if( pszProjection )
            CPLFree( pszProjection );

        pszProjection = CPLStrdup( pszPrj );
    }

/* -------------------------------------------------------------------- */
/*      Record details.                                                 */
/* -------------------------------------------------------------------- */
    if( poDS->GetRasterCount() < 1 )
    {
        delete poDS;
        return false;
    }

    if( CPLGetXMLValue(psService,"BandCount",NULL) == NULL )
        CPLCreateXMLElementAndValue(
            psService, "BandCount",
            CPLString().Printf("%d",poDS->GetRasterCount()));

    CPLCreateXMLElementAndValue(
        psService, "BandType",
        GDALGetDataTypeName(poDS->GetRasterBand(1)->GetRasterDataType()) );

    bServiceDirty = true;

/* -------------------------------------------------------------------- */
/*      Cleanup                                                         */
/* -------------------------------------------------------------------- */
    delete poDS;

    FlushMemoryResult();

    return TRUE;
}

/************************************************************************/
/*                         FlushMemoryResult()                          */
/*                                                                      */
/*      This actually either cleans up the in memory /vsimem/           */
/*      temporary file, or the on disk temporary file.                  */
/************************************************************************/
void WCSDataset::FlushMemoryResult()

{
    if( !osResultFilename.empty() )
    {
        VSIUnlink( osResultFilename );
        osResultFilename = "";
    }

    if( pabySavedDataBuffer )
    {
        CPLFree( pabySavedDataBuffer );
        pabySavedDataBuffer = NULL;
    }
}

/************************************************************************/
/*                           GDALOpenResult()                           */
/*                                                                      */
/*      Open a CPLHTTPResult as a GDALDataset (if possible).  First     */
/*      attempt is to open handle it "in memory".  Eventually we        */
/*      will add support for handling it on file if necessary.          */
/*                                                                      */
/*      This method will free CPLHTTPResult, the caller should not      */
/*      access it after the call.                                       */
/************************************************************************/

GDALDataset *WCSDataset::GDALOpenResult( CPLHTTPResult *psResult )

{
    FlushMemoryResult();

    CPLDebug( "WCS", "GDALOpenResult() on content-type: %s",
              psResult->pszContentType );

/* -------------------------------------------------------------------- */
/*      If this is multipart/related content type, we should search     */
/*      for the second part.                                            */
/* -------------------------------------------------------------------- */
    GByte *pabyData = psResult->pabyData;
    int    nDataLen = psResult->nDataLen;

    if( psResult->pszContentType
        && strstr(psResult->pszContentType,"multipart")
        && CPLHTTPParseMultipartMime(psResult) )
    {
        if( psResult->nMimePartCount > 1 )
        {
            pabyData = psResult->pasMimePart[1].pabyData;
            nDataLen = psResult->pasMimePart[1].nDataLen;

            if (CSLFindString(psResult->pasMimePart[1].papszHeaders,
                              "Content-Transfer-Encoding: base64") != -1)
            {
                nDataLen = CPLBase64DecodeInPlace(pabyData);
            }
        }
    }

/* -------------------------------------------------------------------- */
/*      Create a memory file from the result.                           */
/* -------------------------------------------------------------------- */
    // Eventually we should be looking at mime info and stuff to figure
    // out an optimal filename, but for now we just use a fixed one.
    osResultFilename.Printf( "/vsimem/wcs/%p/wcsresult.dat",
                             this );

    VSILFILE *fp = VSIFileFromMemBuffer( osResultFilename, pabyData, nDataLen,
                                     FALSE );

    if( fp == NULL )
    {
        CPLHTTPDestroyResult(psResult);
        return NULL;
    }

    VSIFCloseL( fp );

/* -------------------------------------------------------------------- */
/*      Try opening this result as a gdaldataset.                       */
/* -------------------------------------------------------------------- */
    GDALDataset *poDS = (GDALDataset *)
        GDALOpen( osResultFilename, GA_ReadOnly );

/* -------------------------------------------------------------------- */
/*      If opening it in memory didn't work, perhaps we need to         */
/*      write to a temp file on disk?                                   */
/* -------------------------------------------------------------------- */
    if( poDS == NULL )
    {
        CPLString osTempFilename;
        VSILFILE *fpTemp;

        osTempFilename.Printf( "/tmp/%p_wcs.dat", this );

        fpTemp = VSIFOpenL( osTempFilename, "wb" );
        if( fpTemp == NULL )
        {
            CPLError( CE_Failure, CPLE_OpenFailed,
                      "Failed to create temporary file:%s",
                      osTempFilename.c_str() );
        }
        else
        {
            if( VSIFWriteL( pabyData, nDataLen, 1, fpTemp )
                != 1 )
            {
                CPLError( CE_Failure, CPLE_OpenFailed,
                          "Failed to write temporary file:%s",
                          osTempFilename.c_str() );
                VSIFCloseL( fpTemp );
                VSIUnlink( osTempFilename );
            }
            else
            {
                VSIFCloseL( fpTemp );
                VSIUnlink( osResultFilename );
                osResultFilename = osTempFilename;

                poDS =  (GDALDataset *)
                    GDALOpen( osResultFilename, GA_ReadOnly );
            }
        }
    }

/* -------------------------------------------------------------------- */
/*      Steal the memory buffer from HTTP result.                       */
/* -------------------------------------------------------------------- */
    pabySavedDataBuffer = psResult->pabyData;

    psResult->pabyData = NULL;

    if( poDS == NULL )
        FlushMemoryResult();

    CPLHTTPDestroyResult(psResult);

    return poDS;
}

/************************************************************************/
/*                             Identify()                               */
/************************************************************************/

int WCSDataset::Identify( GDALOpenInfo * poOpenInfo )

{
/* -------------------------------------------------------------------- */
/*      Filename is WCS:URL                                             */
/*                                                                      */
/* -------------------------------------------------------------------- */
    if( poOpenInfo->nHeaderBytes == 0
        && STARTS_WITH_CI((const char *) poOpenInfo->pszFilename, "WCS:") )
        return TRUE;

/* -------------------------------------------------------------------- */
/*      Is this a WCS_GDAL service description file or "in url"         */
/*      equivalent?                                                     */
/* -------------------------------------------------------------------- */
    if( poOpenInfo->nHeaderBytes == 0
        && STARTS_WITH_CI((const char *) poOpenInfo->pszFilename, "<WCS_GDAL>") )
        return TRUE;

    else if( poOpenInfo->nHeaderBytes >= 10
             && STARTS_WITH_CI((const char *) poOpenInfo->pabyHeader, "<WCS_GDAL>") )
        return TRUE;

/* -------------------------------------------------------------------- */
/*      Is this apparently a WCS subdataset reference?                  */
/* -------------------------------------------------------------------- */
    else if( STARTS_WITH_CI((const char *) poOpenInfo->pszFilename, "WCS_SDS:")
             && poOpenInfo->nHeaderBytes == 0 )
        return TRUE;

    else
        return FALSE;
}

/************************************************************************/
/*                            WCSParseVersion()                         */
/************************************************************************/

static int WCSParseVersion( const char *version )
{
    if( EQUAL(version, "2.0.1") )
        return 201;
    if( EQUAL(version, "1.1.2") )
        return 112;
    if( EQUAL(version, "1.1.1") )
        return 111;
    if( EQUAL(version, "1.1.0") )
        return 110;
    if( EQUAL(version, "1.0.0") )
        return 100;
    return 0;
}

/************************************************************************/
/*                             Version()                                */
/************************************************************************/

const char *WCSDataset::Version() const
{
    if( this->m_Version == 201 )
        return "2.0.1";
    if( this->m_Version == 112 )
        return "1.1.2";
    if( this->m_Version == 111 )
        return "1.1.1";
    if( this->m_Version == 110 )
        return "1.1.0";
    if( this->m_Version == 100 )
        return "1.0.0";
    return "";
}

/************************************************************************/
/*                      CreateFromCapabilities()                        */
/************************************************************************/

WCSDataset *WCSDataset::CreateFromCapabilities(GDALOpenInfo * poOpenInfo, CPLString cache, CPLString path, CPLString url)
{
    // request Capabilities, later code will write PAM to cache
    url = CPLURLAddKVP(url, "SERVICE", "WCS");
    url = CPLURLAddKVP(url, "REQUEST", "GetCapabilities");

    CPLString extra = CSLFetchNameValueDef(poOpenInfo->papszOpenOptions, "GetCapabilitiesExtra", "");
    if (extra != "") {
        std::vector<CPLString> pairs = Split(extra, "&");
        for (unsigned int i = 0; i < pairs.size(); ++i) {
            std::vector<CPLString> pair = Split(pairs[i], "=");
            url = CPLURLAddKVP(url, pair[0], pair[1]);
        }
    }

    char **options = NULL;
    const char *keys[] = {
        "TIMEOUT",
        "USERPWD",
        "HTTPAUTH"
    };
    for (unsigned int i = 0; i < CPL_ARRAYSIZE(keys); i++) {
        CPLString value = CSLFetchNameValueDef(poOpenInfo->papszOpenOptions, keys[i], "");
        if (value != "") {
            options = CSLSetNameValue(options, keys[i], value);
        }
    }
    CPLHTTPResult *psResult = CPLHTTPFetch(url.c_str(), options);
    if (psResult == NULL || psResult->nDataLen == 0) {
        CPLHTTPDestroyResult(psResult);
        return NULL;
    }
    CPLXMLTreeCloser doc(CPLParseXMLString((const char*)psResult->pabyData));
    CPLHTTPDestroyResult(psResult);
    if (doc.get() == NULL) {
        return NULL;
    }
    CPLXMLNode *capabilities = doc.get();
    // to avoid hardcoding the name of the Capabilities element
    // we skip the Declaration and assume the next is the body
    while (capabilities != NULL
           && (capabilities->eType != CXT_Element
               || capabilities->pszValue[0] == '?') )
    {
        capabilities = capabilities->psNext;
    }
    // get version, this version will overwrite the user's request
    int version_from_server = WCSParseVersion(CPLGetXMLValue(capabilities, "version", ""));
    if (version_from_server == 0) {
        // broken server, assume 1.0.0
        version_from_server = 100;
    }

    CPLSerializeXMLTreeToFile(capabilities, (path + ".xml").c_str());

    WCSDataset *poDS;
    if (version_from_server == 201) {
        poDS = new WCSDataset201(cache);
    } else if (version_from_server/10 == 11) {
        poDS = new WCSDataset110(version_from_server, cache);
    } else {
        poDS = new WCSDataset100(cache);
    }
    if (poDS->ParseCapabilities(capabilities, url) != CE_None) {
        poDS->ProcessError(psResult);
        delete poDS;
        return NULL;
    }
    poDS->SetDescription(path);
    poDS->TrySaveXML();
    return poDS;
}

/************************************************************************/
/*                        CreateFromMetadata()                          */
/************************************************************************/

WCSDataset *WCSDataset::CreateFromMetadata(const CPLString &cache, CPLString path)
{
    WCSDataset *poDS;
    // try to read the PAM XML from path + metadata extension
    if (FileIsReadable(path + ".aux.xml")) {
        CPLXMLTreeCloser doc(CPLParseXMLFile((path + ".aux.xml").c_str()));
        CPLXMLNode *metadata = doc.get();
        if (metadata == NULL) {
            return NULL;
        }
        int version_from_metadata =
            WCSParseVersion(
                CPLGetXMLValue(
                    SearchChildWithValue(
                        SearchChildWithValue(metadata, "domain", ""),
                        "key", "WCS_GLOBAL#version"),
                    NULL, ""));
        if (version_from_metadata == 201) {
            poDS = new WCSDataset201(cache);
        } else if (version_from_metadata/10 == 11) {
            poDS = new WCSDataset110(version_from_metadata, cache);
        } else if (version_from_metadata/10 == 10) {
            poDS = new WCSDataset100(cache);
        } else {
            CPLError(CE_Failure, CPLE_AppDefined, "The metadata does not contain version. RECREATE_META?");
            return NULL;
        }
        poDS->SetDescription(path);
        poDS->TryLoadXML(); // todo: avoid reload
    } else {
        // obviously there was an error
        // processing the Capabilities file
        // so we show it to the user
        GByte *pabyOut = NULL;
        path += ".xml";
        if( !VSIIngestFile( NULL, path, &pabyOut, NULL, -1 ) )
            return NULL;
        CPLString error = reinterpret_cast<char *>(pabyOut);
        if( error.size() > 2048 ) {
            error.resize( 2048 );
        }
        CPLError( CE_Failure, CPLE_AppDefined,
                  "Error:\n%s",
                  error.c_str() );
        CPLFree(pabyOut);
        return NULL;
    }
    return poDS;
}

/************************************************************************/
/*                        CreateServiceMetadata()                       */
/************************************************************************/

// master filename is the capabilities basename
// filename is the subset/coverage basename
static void CreateServiceMetadata(const CPLString &coverage,
                                  CPLString master_filename,
                                  CPLString filename)
{
    master_filename += ".aux.xml";
    filename += ".aux.xml";

    CPLXMLTreeCloser doc(CPLParseXMLFile(master_filename));
    CPLXMLNode *metadata = doc.get();
    // remove other subdatasets than the current
    int subdataset = 0;
    CPLXMLNode *domain = SearchChildWithValue(metadata, "domain", "SUBDATASETS");
    if (domain == NULL) {
        return;
    }
    for (CPLXMLNode *node = domain->psChild; node != NULL; node = node->psNext) {
        if (node->eType != CXT_Element) {
            continue;
        }
        CPLString key = CPLGetXMLValue(node, "key", "");
        if (!STARTS_WITH(key, "SUBDATASET_")) {
            continue;
        }
        CPLString value = CPLGetXMLValue(node, NULL, "");
        if (value.find(coverage) != std::string::npos) {
            key.erase(0, 11); // SUBDATASET_
            key.erase(key.find("_"), std::string::npos);
            subdataset = atoi(key);
            break;
        }
    }
    if (subdataset > 0) {
        CPLXMLNode *next = NULL;
        for (CPLXMLNode *node = domain->psChild; node != NULL; node = next) {
            next = node->psNext;
            if (node->eType != CXT_Element) {
                continue;
            }
            CPLString key = CPLGetXMLValue(node, "key", "");
            if (key.find(CPLString().Printf("SUBDATASET_%i_", subdataset)) == std::string::npos) {
                CPLRemoveXMLChild(domain, node);
                CPLDestroyXMLNode(node);
            }
        }
    }
    CPLSerializeXMLTreeToFile(metadata, filename);
}

/************************************************************************/
/*                          CreateService()                             */
/************************************************************************/

static CPLXMLNode *CreateService(const CPLString &base_url,
                                 const CPLString &version,
                                 const CPLString &coverage)
{
    // construct WCS_GDAL XML into psService
    CPLString xml = "<WCS_GDAL>";
    xml += "<ServiceURL>" + base_url + "</ServiceURL>";
    xml += "<Version>" + version + "</Version>";
    xml += "<CoverageName>" + coverage + "</CoverageName>";
    xml += "</WCS_GDAL>";
    CPLXMLNode *psService = CPLParseXMLString(xml);
    return psService;
}

/************************************************************************/
/*                          UpdateService()                             */
/************************************************************************/

static bool UpdateService(CPLXMLNode *service, GDALOpenInfo * poOpenInfo)
{
    bool updated = false;
    // descriptions in frmt_wcs.html
    const char *keys[] = {
        "PreferredFormat",
        "Interpolation",
        "Range",
        "BandIdentifier",
        "BandCount",
        "BandType",
        "NoDataValue",
        "BlockXSize",
        "BlockYSize",
        "Timeout",
        "UserPwd",
        "HttpAuth",
        "OverviewCount",
        "GetCoverageExtra",
        "DescribeCoverageExtra",
        "Domain",
        "Dimensions",
        "DimensionToBand",
        "DefaultTime",
        "OriginAtBoundary",
        "OuterExtents",
        "BufSizeAdjust",
        "OffsetsPositive",
        "NrOffsets",
        "GridCRSOptional",
        "NoGridAxisSwap",
        "GridAxisLabelSwap",
        "SubsetAxisSwap",
        "UseScaleFactor",
        "CRS",
        "filename"
    };
    for (unsigned int i = 0; i < CPL_ARRAYSIZE(keys); i++) {
        const char *value;
        if (CSLFindString(poOpenInfo->papszOpenOptions, keys[i]) != -1) {
            value = "TRUE";
        } else {
            value = CSLFetchNameValue(poOpenInfo->papszOpenOptions, keys[i]);
            if (value == NULL) {
                continue;
            }
        }
        updated = CPLUpdateXML(service, keys[i], value) || updated;
    }
    return updated;
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *WCSDataset::Open( GDALOpenInfo * poOpenInfo )

{
    CPLString cache = CSLFetchNameValueDef(poOpenInfo->papszOpenOptions, "CACHE_DIR", "");
    if (!SetupCache(cache,
                    CPLFetchBool(poOpenInfo->papszOpenOptions, "CLEAR_CACHE", false)))
    {
        return NULL;
    }
    CPLXMLNode *service = NULL;
    char **papszModifiers = NULL;
    bool dry_run = false; // do not make a GetCoverage call to get data type etc

/* -------------------------------------------------------------------- */
/*      If filename is WCS:URL                                          */
/*      We will set service and request the URL,                        */
/*      but version / acceptVersions is left for the user.              */
/*      The server *should* return the latest supported version         */
/*      but that is not dependable.                                     */
/*      If there is no coverage id/name, get capabilities.              */
/*      Otherwise, proceed to describe coverage / get coverage.         */
/* -------------------------------------------------------------------- */
    if( poOpenInfo->nHeaderBytes == 0
        && STARTS_WITH_CI((const char *) poOpenInfo->pszFilename, "WCS:") )
    {
        CPLString url = (const char *)(poOpenInfo->pszFilename + 4);
        CPLString version = CPLURLGetValue(url, "version");
        url = URLRemoveKey(url, "version");

        // the default version, the aim is to have version explicitly in cache values
        if (WCSParseVersion(version) == 0) {
            version = "2.0.1";
        }
        
        CPLString coverage = CPLURLGetValue(url, "coverageid"); // 2.0
        if (coverage == "") {
            coverage = CPLURLGetValue(url, "identifiers"); // 1.1
            if (coverage == "") {
                coverage = CPLURLGetValue(url, "coverage"); // 1.0
                url = URLRemoveKey(url, "coverage");
            } else {
                url = URLRemoveKey(url, "identifiers");
            }
        } else {
            url = URLRemoveKey(url, "coverageid");
        }
        if (strchr(url, '?') == NULL) {
            url += "?";
        }
        CPLString base_url = url;
        url = base_url;
        if (version != "") {
            url = CPLURLAddKVP(url, "version", version);
        }
        if (coverage != "") {
            url = CPLURLAddKVP(url, "coverage", coverage);
        }

        if (CPLFetchBool(poOpenInfo->papszOpenOptions, "REFRESH_CACHE", false)) {
            DeleteEntryFromCache(cache, "", url);
        }

        // cache is a hash of basename => URL
        // below 'cached' means the URL is in cache
        // for basename there may be either
        // Capabilities (.xml) and PAM metadata file (.aux.xml)
        // or DescribeCoverage (.DC.xml) and WCS_GDAL (.xml)
        CPLString filename;
        bool cached;
        if (!FromCache(cache, filename, url, cached)) { // error
            return NULL;
        }
        cached = cached && FileIsReadable(filename + ".xml");

        bool recreate_meta = CPLFetchBool(poOpenInfo->papszOpenOptions, "RECREATE_META", false);

        // the general policy for service documents in cache is
        // that they are not user editable
        // users should use options and possibly RECREATE options

        if (coverage == "") {
            // Open a dataset with subdataset(s)
            // Information is in PAM file (<basename>.aux.xml)
            // which is made from the Capabilities file (<basename>.xml)

            if (cached && !recreate_meta) {
                return WCSDataset::CreateFromMetadata(cache, filename);
            }
            return WCSDataset::CreateFromCapabilities(poOpenInfo, cache, filename, url);
        } else {
            // Open a subdataset
            // Metadata is in (<basename>.xml.aux.xml)
            // Information is in WCS_GDAL file (<basename>.xml)
            // which is made from options and URL
            // and a Coverage description file (<basename>.DC.xml)

            filename += ".xml";
            CPLFree(poOpenInfo->pszFilename);
            poOpenInfo->pszFilename = CPLStrdup(filename);

            CPLString pam_url = URLRemoveKey(url, "coverage");
            CPLString pam_filename;
            bool pam_in_cache;
            if (!FromCache(cache, pam_filename, pam_url, pam_in_cache)) {
                return NULL;
            }

            // even if we have coverage we need global PAM metadata
            // if we don't have it we need to create it
            if (recreate_meta || !FileIsReadable((filename + ".aux.xml").c_str())) {
                if (!pam_in_cache || !FileIsReadable((pam_filename + ".aux.xml").c_str())) {
                    // if we don't have it, fetch it first
                    WCSDataset *pam = CreateFromCapabilities(poOpenInfo, cache, pam_filename, pam_url);
                    if (!pam) {
                        return NULL;
                    }
                    // the version may have changed
                    version = pam->Version();
                    url = URLRemoveKey(url, "version");
                    url = CPLURLAddKVP(url, "version", version);
                    url = URLRemoveKey(url, "coverage");
                    url = CPLURLAddKVP(url, "coverage", coverage);
                    if (!FromCache(cache, filename, url, cached)) { // error
                        return NULL;
                    }
                    filename += ".xml";
                    cached = cached && FileIsReadable(filename);
                    delete pam;
                }
                CreateServiceMetadata(coverage, pam_filename, filename);
            }

            bool recreate_service = CPLFetchBool(poOpenInfo->papszOpenOptions, "RECREATE_SERVICE", false);

            if (cached && !recreate_service) {
                service = CPLParseXMLFile(filename);
            } else {
                service = CreateService(base_url, version, coverage);
            }
            bool updated = UpdateService(service, poOpenInfo);
            if (updated || !(cached && !recreate_service)) {
                CPLSerializeXMLTreeToFile(service, filename);
            }
            if (updated) {
                CreateServiceMetadata(coverage, pam_filename, filename);
            }

            dry_run = CPLFetchBool(poOpenInfo->papszOpenOptions, "SKIP_GETCOVERAGE", false);
        }
    }
/* -------------------------------------------------------------------- */
/*      Is this a WCS_GDAL service description file or "in url"         */
/*      equivalent?                                                     */
/* -------------------------------------------------------------------- */
    else if( poOpenInfo->nHeaderBytes == 0
             && STARTS_WITH_CI((const char *) poOpenInfo->pszFilename, "<WCS_GDAL>") )
    {
        service = CPLParseXMLString( poOpenInfo->pszFilename );
    }
    else if( poOpenInfo->nHeaderBytes >= 10
             && STARTS_WITH_CI((const char *) poOpenInfo->pabyHeader, "<WCS_GDAL>") )
    {
        service = CPLParseXMLFile( poOpenInfo->pszFilename );
    }
/* -------------------------------------------------------------------- */
/*      Is this apparently a subdataset?                                */
/* -------------------------------------------------------------------- */
    else if( STARTS_WITH_CI((const char *) poOpenInfo->pszFilename, "WCS_SDS:")
             && poOpenInfo->nHeaderBytes == 0 )
    {
        int iLast;

        papszModifiers = CSLTokenizeString2( poOpenInfo->pszFilename+8, ",",
                                             CSLT_HONOURSTRINGS );

        iLast = CSLCount(papszModifiers)-1;
        if( iLast >= 0 )
        {
            service = CPLParseXMLFile( papszModifiers[iLast] );
            CPLFree( papszModifiers[iLast] );
            papszModifiers[iLast] = NULL;
        }
    }

/* -------------------------------------------------------------------- */
/*      Success so far?                                                 */
/* -------------------------------------------------------------------- */
    if( service == NULL )
    {
        CSLDestroy( papszModifiers );
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Confirm the requested access is supported.                      */
/* -------------------------------------------------------------------- */
    if( poOpenInfo->eAccess == GA_Update )
    {
        CSLDestroy( papszModifiers );
        CPLDestroyXMLNode( service );
        CPLError( CE_Failure, CPLE_NotSupported,
                  "The WCS driver does not support update access to existing"
                  " datasets.\n" );
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Check for required minimum fields.                              */
/* -------------------------------------------------------------------- */
    if( !CPLGetXMLValue( service, "ServiceURL", NULL )
        || !CPLGetXMLValue( service, "CoverageName", NULL ) )
    {
        CSLDestroy( papszModifiers );
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "Missing one or both of ServiceURL and CoverageName elements.\n"
                  "See WCS driver documentation for details on service description file format." );

        CPLDestroyXMLNode( service );
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      What version are we working with?                               */
/* -------------------------------------------------------------------- */
    const char *pszVersion = CPLGetXMLValue( service, "Version", "1.0.0" );

    int nVersion = WCSParseVersion(pszVersion);

    if( nVersion == 0 )
    {
        
        CSLDestroy( papszModifiers );
        CPLDestroyXMLNode( service );
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Create a corresponding GDALDataset.                             */
/* -------------------------------------------------------------------- */
    // note: at this point version can still be from user
    WCSDataset *poDS;
    if (nVersion == 201) {
        poDS = new WCSDataset201(cache);
    } else if (nVersion/10 == 11) {
        poDS = new WCSDataset110(nVersion, cache);
    } else {
        poDS = new WCSDataset100(cache);
    }

    poDS->psService = service;
    poDS->SetDescription( poOpenInfo->pszFilename );
    poDS->papszSDSModifiers = papszModifiers;
    poDS->TryLoadXML(); // we need the PAM metadata already in ExtractGridInfo

/* -------------------------------------------------------------------- */
/*      Capture HTTP parameters.                                        */
/* -------------------------------------------------------------------- */
    const char  *pszParm;

    poDS->papszHttpOptions =
        CSLSetNameValue(poDS->papszHttpOptions,
                        "TIMEOUT",
                        CPLGetXMLValue( service, "Timeout", "30" ) );

    pszParm = CPLGetXMLValue( service, "HTTPAUTH", NULL );
    if( pszParm )
        poDS->papszHttpOptions =
            CSLSetNameValue( poDS->papszHttpOptions,
                             "HTTPAUTH", pszParm );

    pszParm = CPLGetXMLValue( service, "USERPWD", NULL );
    if( pszParm )
        poDS->papszHttpOptions =
            CSLSetNameValue( poDS->papszHttpOptions,
                             "USERPWD", pszParm );

/* -------------------------------------------------------------------- */
/*      If we don't have the DescribeCoverage result for this           */
/*      coverage, fetch it now.                                         */
/* -------------------------------------------------------------------- */
    if( CPLGetXMLNode( service, "CoverageOffering" ) == NULL
        && CPLGetXMLNode( service, "CoverageDescription" ) == NULL )
    {
        if( !poDS->DescribeCoverage() )
        {
            delete poDS;
            return NULL;
        }
    }

/* -------------------------------------------------------------------- */
/*      Extract coordinate system, grid size, and geotransform from     */
/*      the coverage description and/or service description             */
/*      information.                                                    */
/* -------------------------------------------------------------------- */
    if( !poDS->ExtractGridInfo() ) {
        delete poDS;
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Leave now or there may be a GetCoverage call.                   */
/*                                                                      */
/* -------------------------------------------------------------------- */
    int nBandCount = -1;
    CPLString sBandCount = CPLGetXMLValue(service, "BandCount", "");
    if (sBandCount != "") {
        nBandCount = atoi(sBandCount);
    }
    if (dry_run || nBandCount == 0)
    {
        return poDS;
    }

/* -------------------------------------------------------------------- */
/*      Extract band count and type from a sample.                      */
/* -------------------------------------------------------------------- */
    if( !poDS->EstablishRasterDetails() ) // todo: do this only if missing info
    {
        delete poDS;
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      It is ok to not have bands. The user just needs to supply       */
/*      more information.                                               */
/* -------------------------------------------------------------------- */
    nBandCount = atoi(CPLGetXMLValue(service, "BandCount", "0"));
    if (nBandCount == 0)
    {
        return poDS;
    }

/* -------------------------------------------------------------------- */
/*      Create band information objects.                                */
/* -------------------------------------------------------------------- */
    int iBand;

    if (!GDALCheckBandCount(nBandCount, FALSE))
    {
        delete poDS;
        return NULL;
    }

    for( iBand = 0; iBand < nBandCount; iBand++ )
        poDS->SetBand( iBand+1, new WCSRasterBand( poDS, iBand+1, -1 ) );

/* -------------------------------------------------------------------- */
/*      Set time metadata on the dataset if we are selecting a          */
/*      temporal slice.                                                 */
/* -------------------------------------------------------------------- */
    CPLString osTime = CSLFetchNameValueDef( poDS->papszSDSModifiers, "time",
                                             poDS->osDefaultTime );

    if( osTime != "" )
        poDS->GDALMajorObject::SetMetadataItem( "TIME_POSITION",
                                                osTime.c_str() );

/* -------------------------------------------------------------------- */
/*      Do we have a band identifier to select only a subset of bands?  */
/* -------------------------------------------------------------------- */
    poDS->osBandIdentifier = CPLGetXMLValue(service,"BandIdentifier","");

/* -------------------------------------------------------------------- */
/*      Do we have time based subdatasets?  If so, record them in       */
/*      metadata.  Note we don't do subdatasets if this is a            */
/*      subdataset or if this is an all-in-memory service.              */
/* -------------------------------------------------------------------- */
    if( !STARTS_WITH_CI(poOpenInfo->pszFilename, "WCS_SDS:")
        && !STARTS_WITH_CI(poOpenInfo->pszFilename, "<WCS_GDAL>")
        && !poDS->aosTimePositions.empty() )
    {
        char **papszSubdatasets = NULL;
        int iTime;

        for( iTime = 0; iTime < (int)poDS->aosTimePositions.size(); iTime++ )
        {
            CPLString osName;
            CPLString osValue;

            osName.Printf( "SUBDATASET_%d_NAME", iTime+1 );
            osValue.Printf( "WCS_SDS:time=\"%s\",%s",
                            poDS->aosTimePositions[iTime].c_str(),
                            poOpenInfo->pszFilename );
            papszSubdatasets = CSLSetNameValue( papszSubdatasets,
                                                osName, osValue );

            CPLString osCoverage =
                CPLGetXMLValue( poDS->psService, "CoverageName", "" );

            osName.Printf( "SUBDATASET_%d_DESC", iTime+1 );
            osValue.Printf( "Coverage %s at time %s",
                            osCoverage.c_str(),
                            poDS->aosTimePositions[iTime].c_str() );
            papszSubdatasets = CSLSetNameValue( papszSubdatasets,
                                                osName, osValue );
        }

        poDS->GDALMajorObject::SetMetadata( papszSubdatasets,
                                            "SUBDATASETS" );

        CSLDestroy( papszSubdatasets );
    }

/* -------------------------------------------------------------------- */
/*      Initialize any PAM information.                                 */
/* -------------------------------------------------------------------- */
    poDS->TryLoadXML();
    return poDS;
}

/************************************************************************/
/*                          GetGeoTransform()                           */
/************************************************************************/

CPLErr WCSDataset::GetGeoTransform( double * padfTransform )

{
    memcpy( padfTransform, adfGeoTransform, sizeof(double)*6 );
    return CE_None;
}

/************************************************************************/
/*                          GetProjectionRef()                          */
/************************************************************************/

const char *WCSDataset::GetProjectionRef()

{
    const char* pszPrj = GDALPamDataset::GetProjectionRef();
    if( pszPrj && strlen(pszPrj) > 0 )
        return pszPrj;

    if ( pszProjection && strlen(pszProjection) > 0 )
        return pszProjection;

    return "";
}

/************************************************************************/
/*                            GetFileList()                             */
/************************************************************************/

char **WCSDataset::GetFileList()

{
    char **papszFileList = GDALPamDataset::GetFileList();

/* -------------------------------------------------------------------- */
/*      ESRI also wishes to include service urls in the file list       */
/*      though this is not currently part of the general definition     */
/*      of GetFileList() for GDAL.                                      */
/* -------------------------------------------------------------------- */
#ifdef ESRI_BUILD
    CPLString file;
    file.Printf( "%s%s",
                 CPLGetXMLValue( psService, "ServiceURL", "" ),
                 CPLGetXMLValue( psService, "CoverageName", "" ) );
    papszFileList = CSLAddString( papszFileList, file.c_str() );
#endif /* def ESRI_BUILD */

    return papszFileList;
}

/************************************************************************/
/*                      GetMetadataDomainList()                         */
/************************************************************************/

char **WCSDataset::GetMetadataDomainList()
{
    return BuildMetadataDomainList(GDALPamDataset::GetMetadataDomainList(),
                                   TRUE,
                                   "xml:CoverageOffering", NULL);
}

/************************************************************************/
/*                            GetMetadata()                             */
/************************************************************************/

char **WCSDataset::GetMetadata( const char *pszDomain )

{
    if( pszDomain == NULL
        || !EQUAL(pszDomain,"xml:CoverageOffering") )
        return GDALPamDataset::GetMetadata( pszDomain );

    CPLXMLNode *psNode = CPLGetXMLNode( psService, "CoverageOffering" );

    if( psNode == NULL )
        psNode = CPLGetXMLNode( psService, "CoverageDescription" );

    if( psNode == NULL )
        return NULL;

    if( apszCoverageOfferingMD[0] == NULL )
    {
        CPLXMLNode *psNext = psNode->psNext;
        psNode->psNext = NULL;

        apszCoverageOfferingMD[0] = CPLSerializeXMLTree( psNode );

        psNode->psNext = psNext;
    }

    return apszCoverageOfferingMD;
}

/************************************************************************/
/*                          GDALRegister_WCS()                          */
/************************************************************************/

void GDALRegister_WCS()

{
    if( GDALGetDriverByName( "WCS" ) != NULL )
        return;

    GDALDriver *poDriver = new GDALDriver();

    poDriver->SetDescription( "WCS" );
    poDriver->SetMetadataItem( GDAL_DCAP_RASTER, "YES" );
    poDriver->SetMetadataItem( GDAL_DMD_LONGNAME, "OGC Web Coverage Service" );
    poDriver->SetMetadataItem( GDAL_DMD_HELPTOPIC, "frmt_wcs.html" );
    poDriver->SetMetadataItem( GDAL_DCAP_VIRTUALIO, "YES" );
    poDriver->SetMetadataItem( GDAL_DMD_SUBDATASETS, "YES" );

    poDriver->pfnOpen = WCSDataset::Open;
    poDriver->pfnIdentify = WCSDataset::Identify;

    GetGDALDriverManager()->RegisterDriver( poDriver );
}
