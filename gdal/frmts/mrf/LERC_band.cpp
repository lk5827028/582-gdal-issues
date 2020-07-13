/*
Copyright 2013-2020 Esri
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
A local copy of the license and additional notices are located with the
source distribution at:
http://github.com/Esri/lerc/

LERC band implementation
LERC page compression and decompression functions

Contributors:  Lucian Plesea
*/

#include "marfa.h"
#include <algorithm>
#include "libLERC/CntZImage.h"
#include <Lerc2.h>

USING_NAMESPACE_LERC
NAMESPACE_MRF_START

// Read an unaligned 4 byte little endian int from location p, advances pointer
static void READ_GINT32(int& X, const char*& p)
{
    memcpy(&X, p, sizeof(GInt32));
    p+= sizeof(GInt32);
}

static void READ_FLOAT(float& X, const char*& p)
{
    memcpy(&X, p, sizeof(float));
    p+= sizeof(float);
}

//
// Check that a buffer contains a supported Lerc1 blob, the type supported by MRF
// Can't really check everything without decoding, this just checks the main structure
// returns actual size if it is Lerc1 with size < sz
// returns 0 if format doesn't match
// returns -1 if Lerc1 but size can't be determined
//
// returns -<actual size> if actual size > sz

static int checkV1(const char *s, size_t sz)
{
    GInt32 nBytesMask, nBytesData;

    // Header is 34 bytes
    // band header is 16, first mask band then data band
    if (sz < static_cast<size_t>(CntZImage::computeNumBytesNeededToWriteVoidImage()))
        return 0;
    // First ten bytes are ASCII signature
    if (!STARTS_WITH(s, "CntZImage "))
        return 0;
    s += 10;

    // Version 11
    int i;
    READ_GINT32(i, s);
    if (i != 11) return 0;

    // Type 8 is CntZ
    READ_GINT32(i, s);
    if (i != 8) return 0;

    // Height
    READ_GINT32(i, s); // Arbitrary number in CntZImage::read()
    if (i > 20000 || i <= 0) return 0;

    // Width
    READ_GINT32(i, s);
    if (i > 20000 || i <= 0) return 0;

    // Skip the max val stored as double
    s += sizeof(double);

    // First header should be the mask, which mean 0 blocks
    // Height
    READ_GINT32(i, s);
    if (i != 0) return 0;

    // WIDTH
    READ_GINT32(i, s);
    if (i != 0) return 0;

    READ_GINT32(nBytesMask, s);
    if (nBytesMask < 0) return 0;

    // mask max value, 0 or 1 as float
    float val;
    READ_FLOAT(val, s);
    if (val != 0.0f && val != 1.0f) return 0;

    // If data header can't be read the actual size is unknown
    if (nBytesMask > INT_MAX - 66 ||
        static_cast<size_t>(66 + nBytesMask) >= sz)
    {
        return -1;
    }

    s += nBytesMask;

    // Data Band header
    READ_GINT32(i, s); // number of full height blocks, never single pixel blocks
    if (i <= 0 || i > 10000)
        return 0;

    READ_GINT32(i, s); // number of full width blocks, never single pixel blocks
    if (i <= 0 || i > 10000)
        return 0;

    READ_GINT32(nBytesData, s);
    if (nBytesData < 0) return 0;

    // Actual LERC blob size
    if( 66 + nBytesMask > INT_MAX - nBytesData )
        return -1;
    int size = static_cast<int>(66 + nBytesMask + nBytesData);
    return (static_cast<size_t>(size) > sz) ? -size : size;
}

static GDALDataType GetL2DataType(Lerc2::DataType L2type) {
    GDALDataType dt;
    switch (L2type) {
    case Lerc2::DT_Byte:  dt = GDT_Byte; break;
    case Lerc2::DT_Short: dt = GDT_Int16; break;
    case Lerc2::DT_UShort: dt = GDT_UInt16; break;
    case Lerc2::DT_Int: dt = GDT_Int32; break;
    case Lerc2::DT_UInt: dt = GDT_UInt32; break;
    case Lerc2::DT_Float: dt = GDT_Float32; break;
    case Lerc2::DT_Double: dt = GDT_Float64; break;
    default: dt = GDT_Unknown;
    }
    return dt;
}

// Load a buffer of type T into zImg
template <typename T> static void CntZImgFill(CntZImage &zImg, T *src, const ILImage &img)
{
    int w = img.pagesize.x;
    int h = img.pagesize.y;
    zImg.resize(w, h);
    const float ndv = static_cast<float>(img.hasNoData ? img.NoDataValue : 0);
    for (int i = 0; i < h; i++)
        for (int j = 0; j < w; j++) {
            CntZ pixel;
            pixel.z = static_cast<float>(*src++);
            pixel.cnt = static_cast<float>(CPLIsEqual(pixel.z, ndv) ? 0 : 1);
            zImg.setPixel(i, j, pixel);
        }
    return;
}

// Unload zImg into a type T buffer
template <typename T> static bool CntZImgUFill(CntZImage &zImg, T *dst, const ILImage &img)
{
    const T ndv = static_cast<T>(img.hasNoData ? img.NoDataValue : 0);
    for (int i = 0; i < zImg.getHeight(); i++)
        for (int j = 0; j < zImg.getWidth(); j++)
            *dst++ = (zImg(i, j).cnt == 0) ? ndv : static_cast<T>(zImg(i, j).z);
    return true;
}

//  LERC 1 compression
static CPLErr CompressLERC(buf_mgr &dst, buf_mgr &src, const ILImage &img, double precision)
{
    CntZImage zImg;
    // Fill data into zImg
#define FILL(T) CntZImgFill(zImg, reinterpret_cast<T *>(src.buffer), img)
    switch (img.dt) {
    case GDT_Byte:      FILL(GByte);    break;
    case GDT_UInt16:    FILL(GUInt16);  break;
    case GDT_Int16:     FILL(GInt16);   break;
    case GDT_Int32:     FILL(GInt32);   break;
    case GDT_UInt32:    FILL(GUInt32);  break;
    case GDT_Float32:   FILL(float);    break;
    case GDT_Float64:   FILL(double);   break;
    default: break;
    }
#undef FILL

    Byte *ptr = (Byte *)dst.buffer;

    // if it can't compress in output buffer it will crash
    if (!zImg.write(&ptr, precision)) {
        CPLError(CE_Failure,CPLE_AppDefined,"MRF: Error during LERC compression");
        return CE_Failure;
    }

    // write changes the value of the pointer, we can find the size by testing how far it moved
    // Add a couple of bytes, to avoid buffer overflow on reading
    dst.size = ptr - (Byte *)dst.buffer + PADDING_BYTES;
    CPLDebug("MRF_LERC","LERC Compressed to %d\n", (int)dst.size);
    return CE_None;
}

static CPLErr DecompressLERC(buf_mgr &dst, buf_mgr &src, const ILImage &img)
{
    CntZImage zImg;

    // we need to add the padding bytes so that out-of-buffer-access checksum
    // don't false-positively trigger.
    size_t nRemainingBytes = src.size + PADDING_BYTES;
    Byte *ptr = (Byte *)src.buffer;

    // Check that input passes snicker test
    int actual = checkV1(src.buffer, src.size);
    if (actual == 0) {
        CPLError(CE_Failure, CPLE_AppDefined, "MRF: Not a supported LERC format");
        return CE_Failure;
    }
    if (actual < 0) { // Negative return means buffer is too short
            CPLError(CE_Failure, CPLE_AppDefined, "MRF: Lerc object too large");
            return CE_Failure;
    }

    if (!zImg.read(&ptr, nRemainingBytes, 1e12))
    {
        CPLError(CE_Failure,CPLE_AppDefined,"MRF: Error during LERC decompression");
        return CE_Failure;
    }

// Unpack from zImg to dst buffer, calling the right type
    bool success = false;
#define UFILL(T) success = CntZImgUFill(zImg, reinterpret_cast<T *>(dst.buffer), img)
    switch (img.dt) {
    case GDT_Byte:      UFILL(GByte);   break;
    case GDT_UInt16:    UFILL(GUInt16); break;
    case GDT_Int16:     UFILL(GInt16);  break;
    case GDT_Int32:     UFILL(GInt32);  break;
    case GDT_UInt32:    UFILL(GUInt32); break;
    case GDT_Float32:   UFILL(float);   break;
    case GDT_Float64:   UFILL(double);  break;
    default: break;
    }
#undef UFILL
    if (!success) {
        CPLError(CE_Failure, CPLE_AppDefined, "MRF: Error during LERC compression");
        return CE_Failure;
    }
    return CE_None;
}

// Populate a bitmask based on comparison with the image no data value
// Returns the number of NoData values found
template <typename T> static int MaskFill(BitMask &bitMask, T *src, const ILImage &img)
{
    int w = img.pagesize.x;
    int h = img.pagesize.y;
    int count = 0;

    bitMask.SetSize(w, h);
    bitMask.SetAllValid();

    // No data value
    T ndv = static_cast<T>(img.NoDataValue);
    if (!img.hasNoData) ndv = 0; // It really doesn't get called when img doesn't have NoDataValue

    for (int i = 0; i < h; i++)
        for (int j = 0; j < w; j++)
            if (ndv == *src++) {
                bitMask.SetInvalid(i, j);
                count++;
            }

    return count;
}

static CPLErr CompressLERC2(buf_mgr &dst, buf_mgr &src, const ILImage &img, double precision)
{
    int w = img.pagesize.x;
    int h = img.pagesize.y;
    // So we build a bitmask to pass a pointer to bytes, which gets converted to a bitmask?
    BitMask bitMask;
    int ndv_count = 0;
    if (img.hasNoData) { // Only build a bitmask if no data value is defined
        switch (img.dt) {

#define MASK(T) ndv_count = MaskFill(bitMask, reinterpret_cast<T *>(src.buffer), img)

        case GDT_Byte:          MASK(GByte);    break;
        case GDT_UInt16:        MASK(GUInt16);  break;
        case GDT_Int16:         MASK(GInt16);   break;
        case GDT_Int32:         MASK(GInt32);   break;
        case GDT_UInt32:        MASK(GUInt32);  break;
        case GDT_Float32:       MASK(float);    break;
        case GDT_Float64:       MASK(double);   break;
        default:                CPLAssert(false); break;

#undef MASK
        }
    }
    // Set bitmask if it has some ndvs
    Lerc2 lerc2(1, w, h, (ndv_count == 0) ? nullptr : bitMask.Bits());
    // Default to LERC2 V2
    lerc2.SetEncoderToOldVersion(2);
    bool success = false;
    Byte *ptr = (Byte *)dst.buffer;

    long sz = 0;
    switch (img.dt) {

#define ENCODE(T) if (true) { \
    sz = lerc2.ComputeNumBytesNeededToWrite(reinterpret_cast<T *>(src.buffer), precision, ndv_count != 0);\
    success = lerc2.Encode(reinterpret_cast<T *>(src.buffer), &ptr);\
    }

    case GDT_Byte:      ENCODE(GByte);      break;
    case GDT_UInt16:    ENCODE(GUInt16);    break;
    case GDT_Int16:     ENCODE(GInt16);     break;
    case GDT_Int32:     ENCODE(GInt32);     break;
    case GDT_UInt32:    ENCODE(GUInt32);    break;
    case GDT_Float32:   ENCODE(float);      break;
    case GDT_Float64:   ENCODE(double);     break;
    default:            CPLAssert(false); break;

#undef ENCODE
    }

    // write changes the value of the pointer, we can find the size by testing how far it moved
    dst.size = (char *)ptr - dst.buffer;
    if (!success || sz != static_cast<long>(dst.size)) {
        CPLError(CE_Failure, CPLE_AppDefined, "MRF: Error during LERC2 compression");
        return CE_Failure;
    }
    CPLDebug("MRF_LERC", "LERC2 Compressed to %d\n", (int)sz);
    return CE_None;
}

// Populate a bitmask based on comparison with the image no data value
template <typename T> static void UnMask(BitMask &bitMask, T *arr, const ILImage &img)
{
    int w = img.pagesize.x;
    int h = img.pagesize.y;
    if (w * h == bitMask.CountValidBits())
        return;
    T *ptr = arr;
    T ndv = T(img.NoDataValue);
    if (!img.hasNoData) ndv = 0; // It doesn't get called when img doesn't have NoDataValue
    for (int i = 0; i < h; i++)
        for (int j = 0; j < w; j++, ptr++)
            if (!bitMask.IsValid(i, j))
                *ptr = ndv;
    return;
}

CPLErr LERC_Band::Decompress(buf_mgr &dst, buf_mgr &src)
{
    const Byte *ptr = reinterpret_cast<Byte *>(src.buffer);
    Lerc2::HeaderInfo hdInfo;
    Lerc2 lerc2;

    // If not Lerc2 switch to Lerc
    if (!lerc2.GetHeaderInfo(ptr, src.size, hdInfo))
        return DecompressLERC(dst, src, img);

    // It is Lerc2 test that it looks reasonable
    if (static_cast<size_t>(hdInfo.blobSize) > src.size) {
        CPLError(CE_Failure, CPLE_AppDefined, "MRF: Lerc2 object too large");
        return CE_Failure;
    }

    if (img.pagesize.x != hdInfo.nCols
        || img.pagesize.y != hdInfo.nRows
        || img.dt != GetL2DataType(hdInfo.dt)
        || hdInfo.nDim != 1
        || dst.size < static_cast<size_t>(hdInfo.nCols * hdInfo.nRows * GDALGetDataTypeSizeBytes(img.dt))) {
        CPLError(CE_Failure, CPLE_AppDefined, "MRF: Lerc2 format error");
        return CE_Failure;
    }

    bool success = false;
    // we need to add the padding bytes so that out-of-buffer-access checksum
    // don't false-positively trigger.
    size_t nRemainingBytes = src.size + PADDING_BYTES;
    BitMask bitMask(img.pagesize.x, img.pagesize.y);
    switch (img.dt) {
#define DECODE(T) success = lerc2.Decode(&ptr, nRemainingBytes, reinterpret_cast<T *>(dst.buffer), bitMask.Bits())
    case GDT_Byte:      DECODE(GByte);      break;
    case GDT_UInt16:    DECODE(GUInt16);    break;
    case GDT_Int16:     DECODE(GInt16);     break;
    case GDT_Int32:     DECODE(GInt32);     break;
    case GDT_UInt32:    DECODE(GUInt32);    break;
    case GDT_Float32:   DECODE(float);      break;
    case GDT_Float64:   DECODE(double);     break;
    default:            CPLAssert(false);   break;
#undef DECODE
    }
    if (!success) {
        CPLError(CE_Failure, CPLE_AppDefined, "MRF: Error during LERC2 decompression");
        return CE_Failure;
    }
    if (!img.hasNoData)
        return CE_None;

    // Fill in no data values
    switch (img.dt) {
#define UNMASK(T) UnMask(bitMask, reinterpret_cast<T *>(dst.buffer), img)
    case GDT_Byte:      UNMASK(GByte);      break;
    case GDT_UInt16:    UNMASK(GUInt16);    break;
    case GDT_Int16:     UNMASK(GInt16);     break;
    case GDT_Int32:     UNMASK(GInt32);     break;
    case GDT_UInt32:    UNMASK(GUInt32);    break;
    case GDT_Float32:   UNMASK(float);      break;
    case GDT_Float64:   UNMASK(double);     break;
    default:            CPLAssert(false);   break;
#undef DECODE
    }
    return CE_None;
}

CPLErr LERC_Band::Compress(buf_mgr &dst, buf_mgr &src)
{
    if (version == 2)
        return CompressLERC2(dst, src, img, precision);
    else
        return CompressLERC(dst, src, img, precision);
}

CPLXMLNode *LERC_Band::GetMRFConfig(GDALOpenInfo *poOpenInfo)
{
    if (poOpenInfo->eAccess != GA_ReadOnly
        || poOpenInfo->pszFilename == nullptr
        || poOpenInfo->pabyHeader == nullptr
        || strlen(poOpenInfo->pszFilename) < 2)
        // Header of Lerc2 takes 58 bytes, an empty area 62.  Lerc 1 empty file is 67.
        // || poOpenInfo->nHeaderBytes < static_cast<int>(Lerc2::ComputeNumBytesHeader()))
        return nullptr;

    // Check the header too
    char *psz = reinterpret_cast<char *>(poOpenInfo->pabyHeader);
    CPLString sHeader;
    sHeader.assign(psz, psz + poOpenInfo->nHeaderBytes);
    if (!IsLerc(sHeader))
        return nullptr;

    GDALDataType dt = GDT_Unknown; // Use this as a validity flag

    // Use this structure to fetch width and height
    ILSize size(-1, -1, 1, 1, 1);

    // Try lerc2
    {
        Lerc2 l2;
        Lerc2::HeaderInfo hinfo;
        hinfo.RawInit();
        if (l2.GetHeaderInfo(reinterpret_cast<Byte *>(psz), poOpenInfo->nHeaderBytes, hinfo)) {
            size.x = hinfo.nCols;
            size.y = hinfo.nRows;
            // Set the datatype, which marks it as valid
            dt = GetL2DataType(hinfo.dt);
        }
    }

    if (size.x <= 0 && sHeader.size() >= CntZImage::computeNumBytesNeededToWriteVoidImage()) {
        CntZImage zImg;
        size_t nRemainingBytes = poOpenInfo->nHeaderBytes;
        Byte *pb = reinterpret_cast<Byte *>(psz);
        // Read only the header, changes pb
        if (zImg.read(&pb, nRemainingBytes, 1e12, true))
        {
            size.x = zImg.getWidth();
            size.y = zImg.getHeight();

            // Read as byte by default, otherwise LERC can be read as anything
            // Get the desired type
            const char *pszDataType = CSLFetchNameValue(poOpenInfo->papszOpenOptions, "DATATYPE");
            dt = pszDataType ? GDALGetDataTypeByName(pszDataType) : GDT_Byte;
        }
    }

    if (size.x <=0 || size.y <=0 || dt == GDT_Unknown)
        return nullptr;

    // Build and return the MRF configuration for a single tile reader
    CPLXMLNode *config = CPLCreateXMLNode(nullptr, CXT_Element, "MRF_META");
    CPLXMLNode *raster = CPLCreateXMLNode(config, CXT_Element, "Raster");
    XMLSetAttributeVal(raster, "Size", size, "%.0f");
    XMLSetAttributeVal(raster, "PageSize", size, "%.0f");
    CPLCreateXMLElementAndValue(raster, "Compression", CompName(IL_LERC));
    CPLCreateXMLElementAndValue(raster, "DataType", GDALGetDataTypeName(dt));
    CPLCreateXMLElementAndValue(raster, "DataFile", poOpenInfo->pszFilename);
    // Set a magic index file name to prevent the driver from attempting to open itd
    CPLCreateXMLElementAndValue(raster, "IndexFile", "(null)");

    return config;
}

LERC_Band::LERC_Band(GDALMRFDataset *pDS, const ILImage &image,
                      int b, int level ) :
    GDALMRFRasterBand(pDS, image, b, level)
{
    // Pick 1/1000 for floats and 0.5 losless for integers.
    if (eDataType == GDT_Float32 || eDataType == GDT_Float64 )
        precision = strtod(GetOptionValue( "LERC_PREC" , ".001" ),nullptr);
    else
        precision =
            std::max(0.5, strtod(GetOptionValue("LERC_PREC", ".5"), nullptr));

    // Encode in V2 by default.
    version = GetOptlist().FetchBoolean("V1", FALSE) ? 1 : 2;

    if( image.pageSizeBytes > INT_MAX / 2 )
    {
        CPLError(CE_Failure, CPLE_AppDefined, "Integer overflow");
        return;
    }
    // Enlarge the page buffer in this case, LERC may expand data.
    pDS->SetPBufferSize( 2 * image.pageSizeBytes);
}

LERC_Band::~LERC_Band() {}

NAMESPACE_MRF_END
