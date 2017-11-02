#include "cpl_http.h"
#include "gdal_pam.h"

#include "wcsdataset.h"
#include "wcsrasterband.h"

/************************************************************************/
/*                           WCSRasterBand()                            */
/************************************************************************/

WCSRasterBand::WCSRasterBand( WCSDataset *poDSIn, int nBandIn,
                              int iOverviewIn ) :
    iOverview(iOverviewIn),
    nResFactor(1 << (iOverviewIn+1)), // iOverview == -1 is base layer
    poODS(poDSIn),
    nOverviewCount(0),
    papoOverviews(NULL)
{
    poDS = poDSIn;
    nBand = nBandIn;

    eDataType = GDALGetDataTypeByName(
        CPLGetXMLValue( poDSIn->psService, "BandType", "Byte" ) );

/* -------------------------------------------------------------------- */
/*      Establish resolution reduction for this overview level.         */
/* -------------------------------------------------------------------- */

/* -------------------------------------------------------------------- */
/*      Establish block size.                                           */
/* -------------------------------------------------------------------- */
    nRasterXSize = poDS->GetRasterXSize() / nResFactor;
    nRasterYSize = poDS->GetRasterYSize() / nResFactor;

    nBlockXSize = atoi(CPLGetXMLValue( poDSIn->psService, "BlockXSize", "0" ) );
    nBlockYSize = atoi(CPLGetXMLValue( poDSIn->psService, "BlockYSize", "0" ) );

    if( nBlockXSize < 1 )
    {
        if( nRasterXSize > 1800 )
            nBlockXSize = 1024;
        else
            nBlockXSize = nRasterXSize;
    }

    if( nBlockYSize < 1 )
    {
        if( nRasterYSize > 900 )
            nBlockYSize = 512;
        else
            nBlockYSize = nRasterYSize;
    }

/* -------------------------------------------------------------------- */
/*      If this is the base layer, create the overview layers.          */
/* -------------------------------------------------------------------- */
    if( iOverview == -1 )
    {
        nOverviewCount = atoi(CPLGetXMLValue(poODS->psService,"OverviewCount",
                                             "-1"));
        if( nOverviewCount < 0 )
        {
            for( nOverviewCount = 0;
                 (std::max(nRasterXSize, nRasterYSize) /
                  (1 << nOverviewCount)) > 900;
                 nOverviewCount++ ) {}
        }
        else if( nOverviewCount > 30 )
        {
            /* There's no reason to have more than 30 overviews, because */
            /* 2^(30+1) overflows a int32 */
            nOverviewCount = 30;
        }

        papoOverviews = (WCSRasterBand **)
            CPLCalloc( nOverviewCount, sizeof(void*) );

        for( int i = 0; i < nOverviewCount; i++ )
            papoOverviews[i] = new WCSRasterBand( poODS, nBand, i );
    }
}

/************************************************************************/
/*                           ~WCSRasterBand()                           */
/************************************************************************/

WCSRasterBand::~WCSRasterBand()

{
    FlushCache();

    if( nOverviewCount > 0 )
    {
        for( int i = 0; i < nOverviewCount; i++ )
            delete papoOverviews[i];

        CPLFree( papoOverviews );
    }
}

/************************************************************************/
/*                             IReadBlock()                             */
/************************************************************************/

CPLErr WCSRasterBand::IReadBlock( int nBlockXOff, int nBlockYOff,
                                  void * pImage )

{
    CPLErr eErr;
    CPLHTTPResult *psResult = NULL;

    eErr = poODS->GetCoverage( nBlockXOff * nBlockXSize * nResFactor,
                               nBlockYOff * nBlockYSize * nResFactor,
                               nBlockXSize * nResFactor,
                               nBlockYSize * nResFactor,
                               nBlockXSize, nBlockYSize,
                               1, &nBand, &psResult );
    if( eErr != CE_None )
        return eErr;

/* -------------------------------------------------------------------- */
/*      Try and open result as a dataset.                               */
/* -------------------------------------------------------------------- */
    GDALDataset *poTileDS = poODS->GDALOpenResult( psResult );

    if( poTileDS == NULL )
        return CE_Failure;

/* -------------------------------------------------------------------- */
/*      Verify configuration.                                           */
/* -------------------------------------------------------------------- */
    if( poTileDS->GetRasterXSize() != nBlockXSize
        || poTileDS->GetRasterYSize() != nBlockYSize )
    {
        CPLDebug( "WCS", "Got size=%dx%d instead of %dx%d.",
                  poTileDS->GetRasterXSize(), poTileDS->GetRasterYSize(),
                  nBlockXSize, nBlockYSize );

        CPLError( CE_Failure, CPLE_AppDefined,
                  "Returned tile does not match expected configuration.\n"
                  "Got %dx%d instead of %dx%d.",
                  poTileDS->GetRasterXSize(), poTileDS->GetRasterYSize(),
                  nBlockXSize, nBlockYSize );
        delete poTileDS;
        return CE_Failure;
    }

    if( (strlen(poODS->osBandIdentifier) && poTileDS->GetRasterCount() != 1)
        || (!strlen(poODS->osBandIdentifier)
            && poTileDS->GetRasterCount() != poODS->GetRasterCount()) )
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "Returned tile does not match expected band configuration.");
        delete poTileDS;
        return CE_Failure;
    }

/* -------------------------------------------------------------------- */
/*      Process all bands of memory result, copying into pBuffer, or    */
/*      pushing into cache for other bands.                             */
/* -------------------------------------------------------------------- */
    int iBand;
    eErr = CE_None;

    for( iBand = 0;
         iBand < poTileDS->GetRasterCount() && eErr == CE_None;
         iBand++ )
    {
        GDALRasterBand *poTileBand = poTileDS->GetRasterBand( iBand+1 );

        if( iBand+1 == GetBand() || strlen(poODS->osBandIdentifier) )
        {
            eErr = poTileBand->RasterIO( GF_Read,
                                         0, 0, nBlockXSize, nBlockYSize,
                                         pImage, nBlockXSize, nBlockYSize,
                                         eDataType, 0, 0, NULL );
        }
        else
        {
            GDALRasterBand *poTargBand = poODS->GetRasterBand( iBand+1 );

            if( iOverview != -1 )
                poTargBand = poTargBand->GetOverview( iOverview );

            GDALRasterBlock *poBlock = poTargBand->GetLockedBlockRef(
                nBlockXOff, nBlockYOff, TRUE );

            if( poBlock != NULL )
            {
                eErr = poTileBand->RasterIO( GF_Read,
                                            0, 0, nBlockXSize, nBlockYSize,
                                            poBlock->GetDataRef(),
                                            nBlockXSize, nBlockYSize,
                                            eDataType, 0, 0, NULL );
                poBlock->DropLock();
            }
            else
                eErr = CE_Failure;
        }
    }

/* -------------------------------------------------------------------- */
/*      Cleanup                                                         */
/* -------------------------------------------------------------------- */
    delete poTileDS;

    poODS->FlushMemoryResult();

    return eErr;
}

/************************************************************************/
/*                             IRasterIO()                              */
/************************************************************************/

CPLErr WCSRasterBand::IRasterIO( GDALRWFlag eRWFlag,
                                 int nXOff, int nYOff, int nXSize, int nYSize,
                                 void * pData, int nBufXSize, int nBufYSize,
                                 GDALDataType eBufType,
                                 GSpacing nPixelSpace, GSpacing nLineSpace,
                                 GDALRasterIOExtraArg* psExtraArg)

{
    if( (poODS->nMaxCols > 0 && poODS->nMaxCols < nBufXSize)
        ||  (poODS->nMaxRows > 0 && poODS->nMaxRows < nBufYSize) )
        return CE_Failure;

    if( poODS->TestUseBlockIO( nXOff, nYOff, nXSize, nYSize,
                               nBufXSize,nBufYSize ) )
        return GDALPamRasterBand::IRasterIO(
            eRWFlag, nXOff, nYOff, nXSize, nYSize,
            pData, nBufXSize, nBufYSize, eBufType,
            nPixelSpace, nLineSpace, psExtraArg );
    else
        return poODS->DirectRasterIO(
            eRWFlag,
            nXOff * nResFactor, nYOff * nResFactor,
            nXSize * nResFactor, nYSize * nResFactor,
            pData, nBufXSize, nBufYSize, eBufType,
            1, &nBand, nPixelSpace, nLineSpace, 0, psExtraArg );
}

/************************************************************************/
/*                           GetNoDataValue()                           */
/************************************************************************/

double WCSRasterBand::GetNoDataValue( int *pbSuccess )

{
    const char *pszSV = CPLGetXMLValue( poODS->psService, "NoDataValue", NULL);

    if( pszSV == NULL )
        return GDALPamRasterBand::GetNoDataValue( pbSuccess );
    else
    {
        if( pbSuccess )
            *pbSuccess = TRUE;
        return CPLAtof(pszSV);
    }
}

/************************************************************************/
/*                          GetOverviewCount()                          */
/************************************************************************/

int WCSRasterBand::GetOverviewCount()

{
    return nOverviewCount;
}

/************************************************************************/
/*                            GetOverview()                             */
/************************************************************************/

GDALRasterBand *WCSRasterBand::GetOverview( int iOverviewIn )

{
    if( iOverviewIn < 0 || iOverviewIn >= nOverviewCount )
        return NULL;
    else
        return papoOverviews[iOverviewIn];
}

