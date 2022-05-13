/*
 *  keadataset.cpp
 *
 *  Created by Pete Bunting on 01/08/2012.
 *  Copyright 2012 LibKEA. All rights reserved.
 *
 *  This file is part of LibKEA.
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation
 *  files (the "Software"), to deal in the Software without restriction,
 *  including without limitation the rights to use, copy, modify,
 *  merge, publish, distribute, sublicense, and/or sell copies of the
 *  Software, and to permit persons to whom the Software is furnished
 *  to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 *  ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *  CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "keadataset.h"
#include "keaband.h"
#include "keacopy.h"
#include "../frmts/hdf5/hdf5vfl.h"

CPL_CVSID("$Id$")

/************************************************************************/
/*                     KEADatasetDriverUnload()                        */
/************************************************************************/

void KEADatasetDriverUnload(GDALDriver*)
{
    HDF5VFLUnloadFileDriver();
}

// Function for converting a libkea type into a GDAL type
GDALDataType KEA_to_GDAL_Type( kealib::KEADataType ekeaType )
{
    GDALDataType egdalType = GDT_Unknown;
    switch( ekeaType )
    {
        case kealib::kea_8int:
        case kealib::kea_8uint:
            egdalType = GDT_Byte;
            break;
        case kealib::kea_16int:
            egdalType = GDT_Int16;
            break;
        case kealib::kea_32int:
            egdalType = GDT_Int32;
            break;
        case kealib::kea_64int:
            egdalType = GDT_Int64;
            break;
        case kealib::kea_16uint:
            egdalType = GDT_UInt16;
            break;
        case kealib::kea_32uint:
            egdalType = GDT_UInt32;
            break;
        case kealib::kea_64uint:
            egdalType = GDT_UInt64;
            break;
        case kealib::kea_32float:
            egdalType = GDT_Float32;
            break;
        case kealib::kea_64float:
            egdalType = GDT_Float64;
            break;
        default:
            egdalType = GDT_Unknown;
            break;
    }
    return egdalType;
}

// function for converting a GDAL type to a kealib type
kealib::KEADataType GDAL_to_KEA_Type( GDALDataType egdalType )
{
    kealib::KEADataType ekeaType = kealib::kea_undefined;
    switch( egdalType )
    {
        case GDT_Byte:
            ekeaType = kealib::kea_8uint;
            break;
        case GDT_Int16:
            ekeaType = kealib::kea_16int;
            break;
        case GDT_Int32:
            ekeaType = kealib::kea_32int;
            break;
        case GDT_Int64:
            ekeaType = kealib::kea_64int;
            break;
        case GDT_UInt16:
            ekeaType = kealib::kea_16uint;
            break;
        case GDT_UInt32:
            ekeaType = kealib::kea_32uint;
            break;
        case GDT_UInt64:
            ekeaType = kealib::kea_64uint;
            break;
        case GDT_Float32:
            ekeaType = kealib::kea_32float;
            break;
        case GDT_Float64:
            ekeaType = kealib::kea_64float;
            break;
        default:
            ekeaType = kealib::kea_undefined;
            break;
    }
    return ekeaType;
}

// static function - pointer set in driver
GDALDataset *KEADataset::Open( GDALOpenInfo * poOpenInfo )
{
    if( Identify( poOpenInfo ) )
    {
        try
        {
            // try and open it in the appropriate mode
            H5::H5File *pH5File;
            if( poOpenInfo->eAccess == GA_ReadOnly )
            {
                // use the virtual driver so we can open files using
                // /vsicurl etc
                // do this same as libkea
                H5::FileAccPropList keaAccessPlist = H5::FileAccPropList(H5::FileAccPropList::DEFAULT);
                keaAccessPlist.setCache(kealib::KEA_MDC_NELMTS, kealib::KEA_RDCC_NELMTS, 
                            kealib::KEA_RDCC_NBYTES, kealib::KEA_RDCC_W0);
                keaAccessPlist.setSieveBufSize(kealib::KEA_SIEVE_BUF);
                hsize_t blockSize = kealib::KEA_META_BLOCKSIZE;
                keaAccessPlist.setMetaBlockSize(blockSize);
                // but set the driver
                keaAccessPlist.setDriver(HDF5VFLGetFileDriver(), nullptr);
                
                const H5std_string keaImgFilePath(poOpenInfo->pszFilename);
                pH5File = new H5::H5File(keaImgFilePath, H5F_ACC_RDONLY, H5::FileCreatPropList::DEFAULT, keaAccessPlist);
            }
            else
            {
                // Must be a local file
                pH5File = kealib::KEAImageIO::openKeaH5RW( poOpenInfo->pszFilename );
            }
            // create the KEADataset object
            KEADataset *pDataset = new KEADataset( pH5File, poOpenInfo->eAccess );

            // set the description as the name
            pDataset->SetDescription( poOpenInfo->pszFilename );

            return pDataset;
        }
        catch (const kealib::KEAIOException &e)
        {
            // was a problem - can't be a valid file
            CPLError( CE_Failure, CPLE_OpenFailed,
                  "Attempt to open file `%s' failed. Error: %s\n",
                  poOpenInfo->pszFilename, e.what() );
            return nullptr;
        }
    }
    else
    {
        // not a KEA file
        return nullptr;
    }
}

// static function- pointer set in driver
// this function is called in preference to Open
//
int KEADataset::Identify( GDALOpenInfo * poOpenInfo )
{

/* -------------------------------------------------------------------- */
/*      Is it an HDF5 file?                                             */
/* -------------------------------------------------------------------- */
    static const char achSignature[] = "\211HDF\r\n\032\n";

    if( poOpenInfo->pabyHeader == nullptr ||
        memcmp(poOpenInfo->pabyHeader,achSignature,8) != 0 )
    {
        return 0;
    }

    // avoid using kealib::KEAImageIO::isKEAImage as this is likely
    // to be too slow over curl etc (and doesn't take a HDF5 file handle
    // anyway).
    // Just test the extension
    CPLString osExt(CPLGetExtension(poOpenInfo->pszFilename));
    if( EQUAL(osExt, "KEA") )
        return 1;
    else
        return 0;
}

// static function
H5::H5File *KEADataset::CreateLL( const char * pszFilename,
                                  int nXSize, int nYSize, int nBands,
                                  GDALDataType eType,
                                  char ** papszParamList  )
{
    GDALDriverH hDriver = GDALGetDriverByName( "KEA" );
    if( ( hDriver == nullptr ) || !GDALValidateCreationOptions( hDriver, papszParamList ) )
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "Attempt to create file `%s' failed. Invalid creation option(s)\n", pszFilename);
        return nullptr;
    }
    // process any creation options in papszParamList
    // default value
    unsigned int nimageblockSize = kealib::KEA_IMAGE_CHUNK_SIZE;
    // see if they have provided a different value
    const char *pszValue = CSLFetchNameValue( papszParamList, "IMAGEBLOCKSIZE" );
    if( pszValue != nullptr )
        nimageblockSize = (unsigned int) atol( pszValue );

    unsigned int nattblockSize = kealib::KEA_ATT_CHUNK_SIZE;
    pszValue = CSLFetchNameValue( papszParamList, "ATTBLOCKSIZE" );
    if( pszValue != nullptr )
        nattblockSize = (unsigned int) atol( pszValue );

    unsigned int nmdcElmts = kealib::KEA_MDC_NELMTS;
    pszValue = CSLFetchNameValue( papszParamList, "MDC_NELMTS" );
    if( pszValue != nullptr )
        nmdcElmts = (unsigned int) atol( pszValue );

    hsize_t nrdccNElmts = kealib::KEA_RDCC_NELMTS;
    pszValue = CSLFetchNameValue( papszParamList, "RDCC_NELMTS" );
    if( pszValue != nullptr )
        nrdccNElmts = (unsigned int) atol( pszValue );

    hsize_t nrdccNBytes = kealib::KEA_RDCC_NBYTES;
    pszValue = CSLFetchNameValue( papszParamList, "RDCC_NBYTES" );
    if( pszValue != nullptr )
        nrdccNBytes = (unsigned int) atol( pszValue );

    double nrdccW0 = kealib::KEA_RDCC_W0;
    pszValue = CSLFetchNameValue( papszParamList, "RDCC_W0" );
    if( pszValue != nullptr )
        nrdccW0 = CPLAtof( pszValue );

    hsize_t nsieveBuf = kealib::KEA_SIEVE_BUF;
    pszValue = CSLFetchNameValue( papszParamList, "SIEVE_BUF" );
    if( pszValue != nullptr )
        nsieveBuf = (unsigned int) atol( pszValue );

    hsize_t nmetaBlockSize = kealib::KEA_META_BLOCKSIZE;
    pszValue = CSLFetchNameValue( papszParamList, "META_BLOCKSIZE" );
    if( pszValue != nullptr )
        nmetaBlockSize = (unsigned int) atol( pszValue );

    unsigned int ndeflate = kealib::KEA_DEFLATE;
    pszValue = CSLFetchNameValue( papszParamList, "DEFLATE" );
    if( pszValue != nullptr )
        ndeflate = (unsigned int) atol( pszValue );

    kealib::KEADataType keaDataType = GDAL_to_KEA_Type( eType );
    if( nBands > 0 && keaDataType == kealib::kea_undefined )
    {
        CPLError( CE_Failure, CPLE_NotSupported,
                  "Data type %s not supported in KEA",
                  GDALGetDataTypeName(eType) );
        return nullptr;
    }

    try
    {
        // now create it
        H5::H5File *keaImgH5File = kealib::KEAImageIO::createKEAImage( pszFilename,
                                                    keaDataType,
                                                    nXSize, nYSize, nBands,
                                                    nullptr, nullptr, nimageblockSize,
                                                    nattblockSize, nmdcElmts, nrdccNElmts,
                                                    nrdccNBytes, nrdccW0, nsieveBuf,
                                                    nmetaBlockSize, ndeflate );
        return keaImgH5File;
    }
    catch (kealib::KEAIOException &e)
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "Attempt to create file `%s' failed. Error: %s\n",
                  pszFilename, e.what() );
        return nullptr;
    }
}

// static function- pointer set in driver
GDALDataset *KEADataset::Create( const char * pszFilename,
                                  int nXSize, int nYSize, int nBands,
                                  GDALDataType eType,
                                  char ** papszParamList  )
{
    H5::H5File *keaImgH5File = CreateLL( pszFilename, nXSize, nYSize, nBands,
                                         eType, papszParamList  );
    if( keaImgH5File == nullptr )
        return nullptr;

    bool bThematic =
        CPLTestBool(CSLFetchNameValueDef( papszParamList, "THEMATIC", "FALSE" ));

    try
    {
        // create our dataset object
        KEADataset *pDataset = new KEADataset( keaImgH5File, GA_Update );

        pDataset->SetDescription( pszFilename );

        // set all to thematic if asked
        if( bThematic )
        {
            for( int nCount = 0; nCount < nBands; nCount++ )
            {
                GDALRasterBand *pBand = pDataset->GetRasterBand(nCount+1);
                pBand->SetMetadataItem("LAYER_TYPE", "thematic");
            }
        }

        return pDataset;
    }
    catch (kealib::KEAIOException &e)
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "Attempt to create file `%s' failed. Error: %s\n",
                  pszFilename, e.what() );
        return nullptr;
    }
}

GDALDataset *KEADataset::CreateCopy( const char * pszFilename, GDALDataset *pSrcDs,
                                CPL_UNUSED int bStrict, char **  papszParamList,
                                GDALProgressFunc pfnProgress, void *pProgressData )
{
    // get the data out of the input dataset
    int nXSize = pSrcDs->GetRasterXSize();
    int nYSize = pSrcDs->GetRasterYSize();
    int nBands = pSrcDs->GetRasterCount();

    GDALDataType eType = (nBands == 0) ? GDT_Unknown : pSrcDs->GetRasterBand(1)->GetRasterDataType();
    H5::H5File *keaImgH5File = CreateLL( pszFilename, nXSize, nYSize, nBands,
                                         eType, papszParamList  );
    if( keaImgH5File == nullptr )
        return nullptr;

    bool bThematic =
        CPLTestBool(CSLFetchNameValueDef( papszParamList, "THEMATIC", "FALSE" ));

    try
    {
        // create the imageio
        kealib::KEAImageIO *pImageIO = new kealib::KEAImageIO();

        // open the file
        pImageIO->openKEAImageHeader( keaImgH5File );

        // copy file
        if( !KEACopyFile( pSrcDs, pImageIO, pfnProgress, pProgressData) )
        {
            delete pImageIO;
            return nullptr;
        }

        // close it
        try
        {
            pImageIO->close();
        }
        catch (const kealib::KEAIOException &)
        {
        }
        delete pImageIO;

        // now open it again - because the constructor loads all the info
        // in we need to copy the data first....
        keaImgH5File = kealib::KEAImageIO::openKeaH5RW( pszFilename );

        // and wrap it in a dataset
        KEADataset *pDataset = new KEADataset( keaImgH5File, GA_Update );
        pDataset->SetDescription( pszFilename );

        // set all to thematic if asked - overrides whatever set by CopyFile
        if( bThematic )
        {
            for( int nCount = 0; nCount < nBands; nCount++ )
            {
                GDALRasterBand *pBand = pDataset->GetRasterBand(nCount+1);
                pBand->SetMetadataItem("LAYER_TYPE", "thematic");
            }
        }

        for( int nCount = 0; nCount < nBands; nCount++ )
        {
            pDataset->GetRasterBand(nCount+1)->SetColorInterpretation(
                pSrcDs->GetRasterBand(nCount+1)->GetColorInterpretation());
        }

        // KEA has no concept of per-dataset mask band for now.
        for( int nCount = 0; nCount < nBands; nCount++ )
        {
            if( pSrcDs->GetRasterBand(nCount+1)->GetMaskFlags() == 0 ) // Per-band mask
            {
                pDataset->GetRasterBand(nCount+1)->CreateMaskBand(0);
                if( GDALRasterBandCopyWholeRaster(
                    (GDALRasterBandH)pSrcDs->GetRasterBand(nCount+1)->GetMaskBand(),
                    (GDALRasterBandH)pDataset->GetRasterBand(nCount+1)->GetMaskBand(),
                    nullptr, nullptr, nullptr) != CE_None )
                {
                    delete pDataset;
                    return nullptr;
                }
            }
        }

        return pDataset;
    }
    catch (kealib::KEAException &e)
    {
        CPLError( CE_Failure, CPLE_OpenFailed,
                  "Attempt to create file `%s' failed. Error: %s\n",
                  pszFilename, e.what() );
        return nullptr;
    }
}

// constructor
KEADataset::KEADataset( H5::H5File *keaImgH5File, GDALAccess eAccessIn )
{
    this->m_hMutex = CPLCreateMutex();
    CPLReleaseMutex( this->m_hMutex );
    try
    {
        // Create the image IO and initialize the refcount.
        m_pImageIO = new kealib::KEAImageIO();
        m_pRefcount = new LockedRefCount();
        
        // NULL until we read them in.
        m_papszMetadataList = nullptr;
        m_pGCPs = nullptr;
        m_pszGCPProjection = nullptr;

        // open the file
        m_pImageIO->openKEAImageHeader( keaImgH5File );
        kealib::KEAImageSpatialInfo *pSpatialInfo = m_pImageIO->getSpatialInfo();

        // get the dimensions
        this->nBands = m_pImageIO->getNumOfImageBands();
        this->nRasterXSize = static_cast<int>(pSpatialInfo->xSize);
        this->nRasterYSize = static_cast<int>(pSpatialInfo->ySize);
        this->eAccess = eAccessIn;

        // create all the bands
        for( int nCount = 0; nCount < nBands; nCount++ )
        {
            // Note: GDAL uses indices starting at 1 and so does kealib.
            // Create band object.
            KEARasterBand *pBand = new KEARasterBand(
                this, nCount + 1, eAccess, m_pImageIO, m_pRefcount );
            // read in overviews
            pBand->readExistingOverviews();
            // set the band into this dataset
            this->SetBand( nCount + 1, pBand );
        }

        // read in the metadata
        this->UpdateMetadataList();
    }
    catch (const kealib::KEAIOException &e)
    {
        // ignore?
        CPLError( CE_Warning, CPLE_AppDefined,
                "Caught exception in KEADataset constructor %s", e.what() );
    }
}

KEADataset::~KEADataset()
{
    {
        CPLMutexHolderD( &m_hMutex );
        // destroy the metadata
        CSLDestroy(m_papszMetadataList);
        this->DestroyGCPs();
        CPLFree( m_pszGCPProjection );
    }
    if( m_pRefcount->DecRef() )
    {
        try
        {
            m_pImageIO->close();
        }
        catch (const kealib::KEAIOException &)
        {
        }
        delete m_pImageIO;
        delete m_pRefcount;
    }
    
    CPLDestroyMutex( m_hMutex );
    m_hMutex = nullptr;
}

// read in the metadata into our CSLStringList
void KEADataset::UpdateMetadataList()
{
    CPLMutexHolderD( &m_hMutex );
    std::vector< std::pair<std::string, std::string> > odata;
    // get all the metadata
    odata = this->m_pImageIO->getImageMetaData();
    for(std::vector< std::pair<std::string, std::string> >::iterator iterMetaData = odata.begin(); iterMetaData != odata.end(); ++iterMetaData)
    {
        m_papszMetadataList = CSLSetNameValue(m_papszMetadataList, iterMetaData->first.c_str(), iterMetaData->second.c_str());
    }
}

// read in the geotransform
CPLErr KEADataset::GetGeoTransform( double * padfTransform )
{
    try
    {
        kealib::KEAImageSpatialInfo *pSpatialInfo = m_pImageIO->getSpatialInfo();
        // GDAL uses an array format
        padfTransform[0] = pSpatialInfo->tlX;
        padfTransform[1] = pSpatialInfo->xRes;
        padfTransform[2] = pSpatialInfo->xRot;
        padfTransform[3] = pSpatialInfo->tlY;
        padfTransform[4] = pSpatialInfo->yRot;
        padfTransform[5] = pSpatialInfo->yRes;

        return CE_None;
    }
    catch (const kealib::KEAIOException &e)
    {
        CPLError( CE_Warning, CPLE_AppDefined,
                "Unable to read geotransform: %s", e.what() );
        return CE_Failure;
    }
}

// read in the projection ref
const char *KEADataset::_GetProjectionRef()
{
    try
    {
        kealib::KEAImageSpatialInfo *pSpatialInfo = m_pImageIO->getSpatialInfo();
        // should be safe since pSpatialInfo should be around a while...
        return pSpatialInfo->wktString.c_str();
    }
    catch (const kealib::KEAIOException &)
    {
        return nullptr;
    }
}

// set the geotransform
CPLErr KEADataset::SetGeoTransform (double *padfTransform )
{
    try
    {
        // get the spatial info and update it
        kealib::KEAImageSpatialInfo *pSpatialInfo = m_pImageIO->getSpatialInfo();
        // convert back from GDAL's array format
        pSpatialInfo->tlX = padfTransform[0];
        pSpatialInfo->xRes = padfTransform[1];
        pSpatialInfo->xRot = padfTransform[2];
        pSpatialInfo->tlY = padfTransform[3];
        pSpatialInfo->yRot = padfTransform[4];
        pSpatialInfo->yRes = padfTransform[5];

        m_pImageIO->setSpatialInfo( pSpatialInfo );
        return CE_None;
    }
    catch (const kealib::KEAIOException &e)
    {
        CPLError( CE_Warning, CPLE_AppDefined,
                "Unable to write geotransform: %s", e.what() );
        return CE_Failure;
    }
}

// set the projection
CPLErr KEADataset::_SetProjection( const char *pszWKT )
{
    try
    {
        // get the spatial info and update it
        kealib::KEAImageSpatialInfo *pSpatialInfo = m_pImageIO->getSpatialInfo();

        pSpatialInfo->wktString = pszWKT;

        m_pImageIO->setSpatialInfo( pSpatialInfo );
        return CE_None;
    }
    catch (const kealib::KEAIOException &e)
    {
        CPLError( CE_Warning, CPLE_AppDefined,
                "Unable to write projection: %s", e.what() );
        return CE_Failure;
    }
}

// Thought this might be handy to pass back to the application
void * KEADataset::GetInternalHandle(const char *)
{
    return m_pImageIO;
}

// this is called by GDALDataset::BuildOverviews. we implement this function to support
// building of overviews
CPLErr KEADataset::IBuildOverviews(const char *pszResampling, int nOverviews, int *panOverviewList,
                                    int nListBands, int *panBandList, GDALProgressFunc pfnProgress,
                                    void *pProgressData)
{
    // go through the list of bands that have been passed in
    int nCurrentBand, nOK = 1;
    for( int nBandCount = 0; (nBandCount < nListBands) && nOK; nBandCount++ )
    {
        // get the band number
        nCurrentBand = panBandList[nBandCount];
        // get the band
        KEARasterBand *pBand = (KEARasterBand*)this->GetRasterBand(nCurrentBand);
        // create the overview object
        pBand->CreateOverviews( nOverviews, panOverviewList );

        // get GDAL to do the hard work. It will calculate the overviews and write them
        // back into the objects
        if( GDALRegenerateOverviews( (GDALRasterBandH)pBand, nOverviews, (GDALRasterBandH*)pBand->GetOverviewList(),
                                    pszResampling, pfnProgress, pProgressData ) != CE_None )
        {
            nOK = 0;
        }
    }
    if( !nOK )
    {
        return CE_Failure;
    }
    else
    {
        return CE_None;
    }
}

// set a single metadata item
CPLErr KEADataset::SetMetadataItem(const char *pszName, const char *pszValue, const char *pszDomain)
{
    CPLMutexHolderD( &m_hMutex );
    // only deal with 'default' domain - no geolocation etc
    if( ( pszDomain != nullptr ) && ( *pszDomain != '\0' ) )
        return CE_Failure;

    try
    {
        this->m_pImageIO->setImageMetaData(pszName, pszValue );
        // CSLSetNameValue will update if already there
        m_papszMetadataList = CSLSetNameValue( m_papszMetadataList, pszName, pszValue );
        return CE_None;
    }
    catch (const kealib::KEAIOException &e)
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                "Unable to write metadata: %s", e.what() );
        return CE_Failure;
    }
}

// get a single metadata item
const char *KEADataset::GetMetadataItem (const char *pszName, const char *pszDomain)
{
    CPLMutexHolderD( &m_hMutex );
    // only deal with 'default' domain - no geolocation etc
    if( ( pszDomain != nullptr ) && ( *pszDomain != '\0' ) )
        return nullptr;
    // string returned from CSLFetchNameValue should be persistent
    return CSLFetchNameValue(m_papszMetadataList, pszName);
}

// get the whole metadata as CSLStringList - note may be thread safety issues
char **KEADataset::GetMetadata(const char *pszDomain)
{
    // only deal with 'default' domain - no geolocation etc
    if( ( pszDomain != nullptr ) && ( *pszDomain != '\0' ) )
        return nullptr;
    // this is what we store it as anyway
    return m_papszMetadataList;
}

// set the whole metadata as a CSLStringList
CPLErr KEADataset::SetMetadata(char **papszMetadata, const char *pszDomain)
{
    CPLMutexHolderD( &m_hMutex );
    // only deal with 'default' domain - no geolocation etc
    if( ( pszDomain != nullptr ) && ( *pszDomain != '\0' ) )
        return CE_Failure;

    int nIndex = 0;
    try
    {
        // go through each item
        while( papszMetadata[nIndex] != nullptr )
        {
            // get the value/name
            char *pszName = nullptr;
            const char *pszValue =
                CPLParseNameValue( papszMetadata[nIndex], &pszName );
            if( pszValue == nullptr )
                pszValue = "";
            if( pszName != nullptr )
            {
                // set it with imageio
                this->m_pImageIO->setImageMetaData(pszName, pszValue );
                CPLFree(pszName);
            }
            nIndex++;
        }
    }
    catch (const kealib::KEAIOException &e)
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                "Unable to write metadata: %s", e.what() );
        return CE_Failure;
    }

    // destroy our one and replace it
    CSLDestroy(m_papszMetadataList);
    m_papszMetadataList = CSLDuplicate(papszMetadata);
    return CE_None;
}

CPLErr KEADataset::AddBand(GDALDataType eType, char **papszOptions)
{
    // process any creation options in papszOptions
    unsigned int nimageBlockSize = kealib::KEA_IMAGE_CHUNK_SIZE;
    unsigned int nattBlockSize = kealib::KEA_ATT_CHUNK_SIZE;
    unsigned int ndeflate = kealib::KEA_DEFLATE;
    if (papszOptions != nullptr) {
        const char *pszValue = CSLFetchNameValue(papszOptions,"IMAGEBLOCKSIZE");
        if ( pszValue != nullptr ) {
            nimageBlockSize = atoi(pszValue);
        }

        pszValue = CSLFetchNameValue(papszOptions, "ATTBLOCKSIZE");
        if (pszValue != nullptr) {
            nattBlockSize = atoi(pszValue);
        }

        pszValue = CSLFetchNameValue(papszOptions, "DEFLATE");
        if (pszValue != nullptr) {
            ndeflate = atoi(pszValue);
        }
    }

    kealib::KEADataType keaDataType = GDAL_to_KEA_Type( eType );
    if( keaDataType == kealib::kea_undefined )
    {
        CPLError( CE_Failure, CPLE_NotSupported,
                  "Data type %s not supported in KEA",
                  GDALGetDataTypeName(eType) );
        return CE_Failure;
    }

    try {
        m_pImageIO->addImageBand(keaDataType, "", nimageBlockSize,
                nattBlockSize, ndeflate);
    } catch (const kealib::KEAIOException &e) {
        CPLError( CE_Failure, CPLE_AppDefined,
                "Unable to create band: %s", e.what() );
        return CE_Failure;
    }

    // create a new band and add it to the dataset
    // note GDAL uses indices starting at 1 and so does kealib
    KEARasterBand *pBand = new KEARasterBand(this, this->nBands+1, this->eAccess,
            m_pImageIO, m_pRefcount);
    this->SetBand(this->nBands+1, pBand);

    return CE_None;
}

int KEADataset::GetGCPCount()
{
    try
    {
        return m_pImageIO->getGCPCount();
    }
    catch (const kealib::KEAIOException &)
    {
        return 0;
    }
}

const char* KEADataset::_GetGCPProjection()
{
    CPLMutexHolderD( &m_hMutex );
    if( m_pszGCPProjection == nullptr )
    {
        try
        {
            std::string sProj = m_pImageIO->getGCPProjection();
            m_pszGCPProjection = CPLStrdup( sProj.c_str() );
        }
        catch (const kealib::KEAIOException &)
        {
            return nullptr;
        }
    }
    return m_pszGCPProjection;
}

const GDAL_GCP* KEADataset::GetGCPs()
{
    CPLMutexHolderD( &m_hMutex );
    if( m_pGCPs == nullptr )
    {
        // convert to GDAL data structures
        try
        {
            unsigned int nCount = m_pImageIO->getGCPCount();
            std::vector<kealib::KEAImageGCP*> *pKEAGCPs = m_pImageIO->getGCPs();

            m_pGCPs = (GDAL_GCP*)CPLCalloc(nCount, sizeof(GDAL_GCP));
            for( unsigned int nIndex = 0; nIndex < nCount; nIndex++)
            {
                GDAL_GCP *pGCP = &m_pGCPs[nIndex];
                kealib::KEAImageGCP *pKEAGCP = pKEAGCPs->at(nIndex);
                pGCP->pszId = CPLStrdup( pKEAGCP->pszId.c_str() );
                pGCP->pszInfo = CPLStrdup( pKEAGCP->pszInfo.c_str() );
                pGCP->dfGCPPixel = pKEAGCP->dfGCPPixel;
                pGCP->dfGCPLine = pKEAGCP->dfGCPLine;
                pGCP->dfGCPX = pKEAGCP->dfGCPX;
                pGCP->dfGCPY = pKEAGCP->dfGCPY;
                pGCP->dfGCPZ = pKEAGCP->dfGCPZ;

                delete pKEAGCP;
            }

            delete pKEAGCPs;
        }
        catch (const kealib::KEAIOException &)
        {
            return nullptr;
        }
    }
    return m_pGCPs;
}

CPLErr KEADataset::_SetGCPs(int nGCPCount, const GDAL_GCP *pasGCPList, const char *pszGCPProjection)
{
    CPLMutexHolderD( &m_hMutex );
    this->DestroyGCPs();
    CPLFree( m_pszGCPProjection );
    m_pszGCPProjection = nullptr;
    CPLErr result = CE_None;

    std::vector<kealib::KEAImageGCP*> *pKEAGCPs = new std::vector<kealib::KEAImageGCP*>(nGCPCount);
    for( int nIndex = 0; nIndex < nGCPCount; nIndex++ )
    {
        const GDAL_GCP *pGCP = &pasGCPList[nIndex];
        kealib::KEAImageGCP *pKEA = new kealib::KEAImageGCP;
        pKEA->pszId = pGCP->pszId;
        pKEA->pszInfo = pGCP->pszInfo;
        pKEA->dfGCPPixel = pGCP->dfGCPPixel;
        pKEA->dfGCPLine = pGCP->dfGCPLine;
        pKEA->dfGCPX = pGCP->dfGCPX;
        pKEA->dfGCPY = pGCP->dfGCPY;
        pKEA->dfGCPZ = pGCP->dfGCPZ;
        pKEAGCPs->at(nIndex) = pKEA;
    }
    try
    {
        m_pImageIO->setGCPs(pKEAGCPs, pszGCPProjection);
    }
    catch (const kealib::KEAIOException &e)
    {
        CPLError( CE_Warning, CPLE_AppDefined,
                "Unable to write GCPs: %s", e.what() );
        result = CE_Failure;
    }

    for( std::vector<kealib::KEAImageGCP*>::iterator itr = pKEAGCPs->begin(); itr != pKEAGCPs->end(); ++itr)
    {
        kealib::KEAImageGCP *pKEA = (*itr);
        delete pKEA;
    }
    delete pKEAGCPs;

    return result;
}

void KEADataset::DestroyGCPs()
{
    CPLMutexHolderD( &m_hMutex );
    if( m_pGCPs != nullptr )
    {
        // we assume this is always the same as the internal list...
        int nCount = this->GetGCPCount();
        for( int nIndex = 0; nIndex < nCount; nIndex++ )
        {
            GDAL_GCP *pGCP = &m_pGCPs[nIndex];
            CPLFree( pGCP->pszId );
            CPLFree( pGCP->pszInfo );
        }
        CPLFree( m_pGCPs );
        m_pGCPs = nullptr;
    }
}
