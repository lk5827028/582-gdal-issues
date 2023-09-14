/**********************************************************************
 * $Id$
 *
 * Name:     mitab_priv.h
 * Project:  MapInfo TAB Read/Write library
 * Language: C++
 * Purpose:  Header file containing private definitions for the library.
 * Author:   Daniel Morissette, dmorissette@dmsolutions.ca
 *
 **********************************************************************
 * Copyright (c) 1999-2003, Daniel Morissette
 * Copyright (c) 2014, Even Rouault <even.rouault at spatialys.com>
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 **********************************************************************/

#ifndef MITAB_PRIV_H_INCLUDED_
#define MITAB_PRIV_H_INCLUDED_

#include "cpl_conv.h"
#include "cpl_string.h"
#include "ogr_feature.h"

#include <set>

class TABFile;
class TABFeature;
class TABMAPToolBlock;
class TABMAPIndexBlock;

/*---------------------------------------------------------------------
 * Access mode: Read or Write
 *--------------------------------------------------------------------*/
typedef enum
{
    TABRead,
    TABWrite,
    TABReadWrite
} TABAccess;

/*---------------------------------------------------------------------
 * Supported .MAP block types (the first byte at the beginning of a block)
 *--------------------------------------------------------------------*/
#define TAB_RAWBIN_BLOCK -1
#define TABMAP_HEADER_BLOCK 0
#define TABMAP_INDEX_BLOCK 1
#define TABMAP_OBJECT_BLOCK 2
#define TABMAP_COORD_BLOCK 3
#define TABMAP_GARB_BLOCK 4
#define TABMAP_TOOL_BLOCK 5
#define TABMAP_LAST_VALID_BLOCK_TYPE 5

/*---------------------------------------------------------------------
 * Drawing Tool types
 *--------------------------------------------------------------------*/
#define TABMAP_TOOL_PEN 1
#define TABMAP_TOOL_BRUSH 2
#define TABMAP_TOOL_FONT 3
#define TABMAP_TOOL_SYMBOL 4

/*---------------------------------------------------------------------
 * Limits related to .TAB version number.  If we pass any of those limits
 * then we have to use larger object types
 *--------------------------------------------------------------------*/
#define TAB_REGION_PLINE_300_MAX_VERTICES 32767

#define TAB_REGION_PLINE_450_MAX_SEGMENTS 32767
#define TAB_REGION_PLINE_450_MAX_VERTICES 1048575

#define TAB_MULTIPOINT_650_MAX_VERTICES 1048576

/* Use this macro to test whether the number of segments and vertices
 * in this object exceeds the V450/650 limits and requires a V800 object
 */
#define TAB_REGION_PLINE_REQUIRES_V800(numSegments, numVerticesTotal)          \
    ((numSegments) > TAB_REGION_PLINE_450_MAX_SEGMENTS ||                      \
     ((numSegments)*3 + numVerticesTotal) > TAB_REGION_PLINE_450_MAX_VERTICES)

/*---------------------------------------------------------------------
 * Codes for the known MapInfo Geometry types
 *--------------------------------------------------------------------*/
typedef enum
{
    TAB_GEOM_UNSET = -1,

    TAB_GEOM_NONE = 0,
    TAB_GEOM_SYMBOL_C = 0x01,
    TAB_GEOM_SYMBOL = 0x02,
    TAB_GEOM_LINE_C = 0x04,
    TAB_GEOM_LINE = 0x05,
    TAB_GEOM_PLINE_C = 0x07,
    TAB_GEOM_PLINE = 0x08,
    TAB_GEOM_ARC_C = 0x0a,
    TAB_GEOM_ARC = 0x0b,
    TAB_GEOM_REGION_C = 0x0d,
    TAB_GEOM_REGION = 0x0e,
    TAB_GEOM_TEXT_C = 0x10,
    TAB_GEOM_TEXT = 0x11,
    TAB_GEOM_RECT_C = 0x13,
    TAB_GEOM_RECT = 0x14,
    TAB_GEOM_ROUNDRECT_C = 0x16,
    TAB_GEOM_ROUNDRECT = 0x17,
    TAB_GEOM_ELLIPSE_C = 0x19,
    TAB_GEOM_ELLIPSE = 0x1a,
    TAB_GEOM_MULTIPLINE_C = 0x25,
    TAB_GEOM_MULTIPLINE = 0x26,
    TAB_GEOM_FONTSYMBOL_C = 0x28,
    TAB_GEOM_FONTSYMBOL = 0x29,
    TAB_GEOM_CUSTOMSYMBOL_C = 0x2b,
    TAB_GEOM_CUSTOMSYMBOL = 0x2c,
    /* Version 450 object types: */
    TAB_GEOM_V450_REGION_C = 0x2e,
    TAB_GEOM_V450_REGION = 0x2f,
    TAB_GEOM_V450_MULTIPLINE_C = 0x31,
    TAB_GEOM_V450_MULTIPLINE = 0x32,
    /* Version 650 object types: */
    TAB_GEOM_MULTIPOINT_C = 0x34,
    TAB_GEOM_MULTIPOINT = 0x35,
    TAB_GEOM_COLLECTION_C = 0x37,
    TAB_GEOM_COLLECTION = 0x38,
    /* Version 800 object types: */
    TAB_GEOM_UNKNOWN1_C = 0x3a,  // ???
    TAB_GEOM_UNKNOWN1 = 0x3b,    // ???
    TAB_GEOM_V800_REGION_C = 0x3d,
    TAB_GEOM_V800_REGION = 0x3e,
    TAB_GEOM_V800_MULTIPLINE_C = 0x40,
    TAB_GEOM_V800_MULTIPLINE = 0x41,
    TAB_GEOM_V800_MULTIPOINT_C = 0x43,
    TAB_GEOM_V800_MULTIPOINT = 0x44,
    TAB_GEOM_V800_COLLECTION_C = 0x46,
    TAB_GEOM_V800_COLLECTION = 0x47,
    TAB_GEOM_MAX_TYPE /* TODo: Does this need to be 0x80? */
} TABGeomType;

#define TAB_GEOM_GET_VERSION(nGeomType)                                        \
    (((nGeomType) < TAB_GEOM_V450_REGION_C)  ? 300                             \
     : ((nGeomType) < TAB_GEOM_MULTIPOINT_C) ? 450                             \
     : ((nGeomType) < TAB_GEOM_UNKNOWN1_C)   ? 650                             \
                                             : 800)

/*---------------------------------------------------------------------
 * struct TABMAPIndexEntry - Entries found in type 1 blocks of .MAP files
 *
 * We will use this struct to rebuild the geographic index in memory
 *--------------------------------------------------------------------*/
typedef struct TABMAPIndexEntry_t
{
    // These members refer to the info we find in the file
    int32_t XMin;
    int32_t YMin;
    int32_t XMax;
    int32_t YMax;
    int32_t nBlockPtr;
} TABMAPIndexEntry;

#define TAB_MIN_BLOCK_SIZE 512
#define TAB_MAX_BLOCK_SIZE (32768 - 512)

#define TAB_MAX_ENTRIES_INDEX_BLOCK ((TAB_MAX_BLOCK_SIZE - 4) / 20)

/*---------------------------------------------------------------------
 * TABVertex
 *--------------------------------------------------------------------*/
typedef struct TABVertex_t
{
    double x{};
    double y{};
} TABVertex;

/*---------------------------------------------------------------------
 * TABTableType - Attribute table format
 *--------------------------------------------------------------------*/
typedef enum
{
    TABTableNative,  // The default
    TABTableDBF,
    TABTableAccess
} TABTableType;

/*---------------------------------------------------------------------
 * TABFieldType - Native MapInfo attribute field types
 *--------------------------------------------------------------------*/
typedef enum
{
    TABFUnknown = 0,
    TABFChar,
    TABFInteger,
    TABFSmallInt,
    TABFDecimal,
    TABFFloat,
    TABFDate,
    TABFLogical,
    TABFTime,
    TABFDateTime,
    TABFLargeInt
} TABFieldType;

#define TABFIELDTYPE_2_STRING(type)                                            \
    (type == TABFChar       ? "Char"                                           \
     : type == TABFInteger  ? "Integer"                                        \
     : type == TABFSmallInt ? "SmallInt"                                       \
     : type == TABFLargeInt ? "LargeInt"                                       \
     : type == TABFDecimal  ? "Decimal"                                        \
     : type == TABFFloat    ? "Float"                                          \
     : type == TABFDate     ? "Date"                                           \
     : type == TABFLogical  ? "Logical"                                        \
     : type == TABFTime     ? "Time"                                           \
     : type == TABFDateTime ? "DateTime"                                       \
                            : "Unknown field type")

/*---------------------------------------------------------------------
 * TABDATFieldDef
 *--------------------------------------------------------------------*/
typedef struct TABDATFieldDef_t
{
    char szName[11];
    char cType;
    GByte byLength;
    GByte byDecimals;

    TABFieldType eTABType;
} TABDATFieldDef;

/*---------------------------------------------------------------------
 * TABMAPCoordSecHdr
 * struct used in the TABMAPCoordBlock to store info about the coordinates
 * for a section of a PLINE MULTIPLE or a REGION.
 *--------------------------------------------------------------------*/
typedef struct TABMAPCoordSecHdr_t
{
    int32_t numVertices;
    int32_t numHoles;
    int32_t nXMin;
    int32_t nYMin;
    int32_t nXMax;
    int32_t nYMax;

    int32_t nDataOffset;
    int nVertexOffset;
} TABMAPCoordSecHdr;

/*---------------------------------------------------------------------
 * TABProjInfo
 * struct used to store the projection parameters from the .MAP header
 *--------------------------------------------------------------------*/
typedef struct TABProjInfo_t
{
    GByte nProjId;  // See MapInfo Ref. Manual, App. F and G
    GByte nEllipsoidId;
    GByte nUnitsId;
    double adProjParams[7];  // params in same order as in .MIF COORDSYS

    int16_t nDatumId;     // Datum Id added in MapInfo 7.8+ (.map V500)
    double dDatumShiftX;  // Before that, we had to always lookup datum
    double dDatumShiftY;  // parameters to establish datum id
    double dDatumShiftZ;
    double adDatumParams[5];

    // Affine parameters only in .map version 500 and up
    GByte nAffineFlag;  // 0=No affine param, 1=Affine params
    GByte nAffineUnits;
    double dAffineParamA;  // Affine params
    double dAffineParamB;
    double dAffineParamC;
    double dAffineParamD;
    double dAffineParamE;
    double dAffineParamF;
} TABProjInfo;

/*---------------------------------------------------------------------
 * TABPenDef - Pen definition information
 *--------------------------------------------------------------------*/
typedef struct TABPenDef_t
{
    int32_t nRefCount;
    GByte nPixelWidth;
    GByte nLinePattern;
    int nPointWidth;
    int32_t rgbColor;
} TABPenDef;

/* MI Default = PEN(1,2,0) */
#define MITAB_PEN_DEFAULT                                                      \
    {                                                                          \
        0, 1, 2, 0, 0x000000                                                   \
    }

/*---------------------------------------------------------------------
 * TABBrushDef - Brush definition information
 *--------------------------------------------------------------------*/
typedef struct TABBrushDef_t
{
    int32_t nRefCount;
    GByte nFillPattern;
    GByte bTransparentFill;  // 1 = Transparent
    int32_t rgbFGColor;
    int32_t rgbBGColor;
} TABBrushDef;

/* MI Default = BRUSH(1,0,16777215) */
#define MITAB_BRUSH_DEFAULT                                                    \
    {                                                                          \
        0, 1, 0, 0, 0xffffff                                                   \
    }

/*---------------------------------------------------------------------
 * TABFontDef - Font Name information
 *--------------------------------------------------------------------*/
typedef struct TABFontDef_t
{
    int32_t nRefCount;
    char szFontName[33];
} TABFontDef;

/* MI Default = FONT("Arial",0,0,0) */
#define MITAB_FONT_DEFAULT                                                     \
    {                                                                          \
        0, "Arial"                                                             \
    }

/*---------------------------------------------------------------------
 * TABSymbolDef - Symbol definition information
 *--------------------------------------------------------------------*/
typedef struct TABSymbolDef_t
{
    int32_t nRefCount;
    int16_t nSymbolNo;
    int16_t nPointSize;
    GByte _nUnknownValue_;  // Style???
    int32_t rgbColor;
} TABSymbolDef;

/* MI Default = SYMBOL(35,0,12) */
#define MITAB_SYMBOL_DEFAULT                                                   \
    {                                                                          \
        0, 35, 12, 0, 0x000000                                                 \
    }

/*---------------------------------------------------------------------
 *                      class TABToolDefTable
 *
 * Class to handle the list of Drawing Tool Definitions for a dataset
 *
 * This class also contains methods to read tool defs from the file and
 * write them to the file.
 *--------------------------------------------------------------------*/

class TABToolDefTable
{
    CPL_DISALLOW_COPY_ASSIGN(TABToolDefTable)

  protected:
    TABPenDef **m_papsPen;
    int m_numPen;
    int m_numAllocatedPen;
    TABBrushDef **m_papsBrush;
    int m_numBrushes;
    int m_numAllocatedBrushes;
    TABFontDef **m_papsFont;
    int m_numFonts;
    int m_numAllocatedFonts;
    TABSymbolDef **m_papsSymbol;
    int m_numSymbols;
    int m_numAllocatedSymbols;

  public:
    TABToolDefTable();
    ~TABToolDefTable();

    int ReadAllToolDefs(TABMAPToolBlock *poToolBlock);
    int WriteAllToolDefs(TABMAPToolBlock *poToolBlock);

    TABPenDef *GetPenDefRef(int nIndex);
    int AddPenDefRef(TABPenDef *poPenDef);
    int GetNumPen();

    TABBrushDef *GetBrushDefRef(int nIndex);
    int AddBrushDefRef(TABBrushDef *poBrushDef);
    int GetNumBrushes();

    TABFontDef *GetFontDefRef(int nIndex);
    int AddFontDefRef(TABFontDef *poFontDef);
    int GetNumFonts();

    TABSymbolDef *GetSymbolDefRef(int nIndex);
    int AddSymbolDefRef(TABSymbolDef *poSymbolDef);
    int GetNumSymbols();

    int GetMinVersionNumber();
};

/*=====================================================================
          Classes to handle Object Headers inside TABMAPObjectBlocks
 =====================================================================*/

class TABMAPObjectBlock;
class TABMAPHeaderBlock;

class TABMAPObjHdr
{
  public:
    TABGeomType m_nType;
    int32_t m_nId;
    int32_t m_nMinX; /* Object MBR */
    int32_t m_nMinY;
    int32_t m_nMaxX;
    int32_t m_nMaxY;

    TABMAPObjHdr()
        : m_nType(TAB_GEOM_NONE), m_nId(0), m_nMinX(0), m_nMinY(0), m_nMaxX(0),
          m_nMaxY(0)
    {
    }
    virtual ~TABMAPObjHdr()
    {
    }

    static TABMAPObjHdr *NewObj(TABGeomType nNewObjType, int32_t nId = 0);
    static TABMAPObjHdr *ReadNextObj(TABMAPObjectBlock *poObjBlock,
                                     TABMAPHeaderBlock *poHeader);

    bool IsCompressedType();
    int WriteObjTypeAndId(TABMAPObjectBlock *);
    void SetMBR(int32_t nMinX, int32_t nMinY, int32_t nMaxX, int32_t mMaxY);

    virtual int WriteObj(TABMAPObjectBlock *)
    {
        return -1;
    }

    //  protected:
    virtual int ReadObj(TABMAPObjectBlock *)
    {
        return -1;
    }
};

class TABMAPObjHdrWithCoord : public TABMAPObjHdr
{
  public:
    int32_t m_nCoordBlockPtr = 0;
    int32_t m_nCoordDataSize = 0;

    /* Eventually this class may have methods to help maintaining refs to
     * coord. blocks when splitting object blocks.
     */
};

class TABMAPObjNone final : public TABMAPObjHdr
{
  public:
    TABMAPObjNone()
    {
    }
    virtual ~TABMAPObjNone()
    {
    }

    virtual int WriteObj(TABMAPObjectBlock *) override
    {
        return 0;
    }

    //  protected:
    virtual int ReadObj(TABMAPObjectBlock *) override
    {
        return 0;
    }
};

class TABMAPObjPoint : public TABMAPObjHdr
{
  public:
    int32_t m_nX;
    int32_t m_nY;
    GByte m_nSymbolId;

    TABMAPObjPoint() : m_nX(0), m_nY(0), m_nSymbolId(0)
    {
    }
    virtual ~TABMAPObjPoint()
    {
    }

    virtual int WriteObj(TABMAPObjectBlock *) override;

    //  protected:
    virtual int ReadObj(TABMAPObjectBlock *) override;
};

class TABMAPObjFontPoint : public TABMAPObjPoint
{
  public:
    GByte m_nPointSize;
    int16_t m_nFontStyle;
    GByte m_nR;
    GByte m_nG;
    GByte m_nB;
    int16_t m_nAngle; /* In tenths of degree */
    GByte m_nFontId;

    TABMAPObjFontPoint()
        : m_nPointSize(0), m_nFontStyle(0), m_nR(0), m_nG(0), m_nB(0),
          m_nAngle(0), m_nFontId(0)
    {
    }
    virtual ~TABMAPObjFontPoint()
    {
    }

    virtual int WriteObj(TABMAPObjectBlock *) override;

    //  protected:
    virtual int ReadObj(TABMAPObjectBlock *) override;
};

class TABMAPObjCustomPoint final : public TABMAPObjPoint
{
  public:
    GByte m_nUnknown_;
    GByte m_nCustomStyle;
    GByte m_nFontId;

    TABMAPObjCustomPoint() : m_nUnknown_(0), m_nCustomStyle(0), m_nFontId(0)
    {
    }
    virtual ~TABMAPObjCustomPoint()
    {
    }

    virtual int WriteObj(TABMAPObjectBlock *) override;

    //  protected:
    virtual int ReadObj(TABMAPObjectBlock *) override;
};

class TABMAPObjLine final : public TABMAPObjHdr
{
  public:
    int32_t m_nX1;
    int32_t m_nY1;
    int32_t m_nX2;
    int32_t m_nY2;
    GByte m_nPenId;

    TABMAPObjLine() : m_nX1(0), m_nY1(0), m_nX2(0), m_nY2(0), m_nPenId(0)
    {
    }
    virtual ~TABMAPObjLine()
    {
    }

    virtual int WriteObj(TABMAPObjectBlock *) override;

    //  protected:
    virtual int ReadObj(TABMAPObjectBlock *) override;
};

class TABMAPObjPLine final : public TABMAPObjHdrWithCoord
{
  public:
    int32_t m_numLineSections; /* MULTIPLINE/REGION only. Not in PLINE */
    int32_t m_nLabelX;         /* Centroid/label location */
    int32_t m_nLabelY;
    int32_t m_nComprOrgX; /* Present only in compressed coord. case */
    int32_t m_nComprOrgY;
    GByte m_nPenId;
    GByte m_nBrushId;
    bool m_bSmooth; /* TRUE if (m_nCoordDataSize & 0x80000000) */

    TABMAPObjPLine()
        : m_numLineSections(0), m_nLabelX(0), m_nLabelY(0), m_nComprOrgX(0),
          m_nComprOrgY(0), m_nPenId(0), m_nBrushId(0), m_bSmooth(0)
    {
    }
    virtual ~TABMAPObjPLine()
    {
    }

    virtual int WriteObj(TABMAPObjectBlock *) override;

    //  protected:
    virtual int ReadObj(TABMAPObjectBlock *) override;
};

class TABMAPObjRectEllipse final : public TABMAPObjHdr
{
  public:
    int32_t m_nCornerWidth; /* For rounded rect only */
    int32_t m_nCornerHeight;
    GByte m_nPenId;
    GByte m_nBrushId;

    TABMAPObjRectEllipse()
        : m_nCornerWidth(0), m_nCornerHeight(0), m_nPenId(0), m_nBrushId(0)
    {
    }
    virtual ~TABMAPObjRectEllipse()
    {
    }

    virtual int WriteObj(TABMAPObjectBlock *) override;

    //  protected:
    virtual int ReadObj(TABMAPObjectBlock *) override;
};

class TABMAPObjArc final : public TABMAPObjHdr
{
  public:
    int32_t m_nStartAngle;
    int32_t m_nEndAngle;
    int32_t m_nArcEllipseMinX; /* MBR of the arc defining ellipse */
    int32_t m_nArcEllipseMinY; /* Only present in arcs            */
    int32_t m_nArcEllipseMaxX;
    int32_t m_nArcEllipseMaxY;
    GByte m_nPenId;

    TABMAPObjArc()
        : m_nStartAngle(0), m_nEndAngle(0), m_nArcEllipseMinX(0),
          m_nArcEllipseMinY(0), m_nArcEllipseMaxX(0), m_nArcEllipseMaxY(0),
          m_nPenId(0)
    {
    }
    virtual ~TABMAPObjArc()
    {
    }

    virtual int WriteObj(TABMAPObjectBlock *) override;

    //  protected:
    virtual int ReadObj(TABMAPObjectBlock *) override;
};

class TABMAPObjText final : public TABMAPObjHdrWithCoord
{
  public:
    /* String and its len stored in the nCoordPtr and nCoordSize */

    int16_t m_nTextAlignment;
    int32_t m_nAngle;
    int16_t m_nFontStyle;

    GByte m_nFGColorR;
    GByte m_nFGColorG;
    GByte m_nFGColorB;
    GByte m_nBGColorR;
    GByte m_nBGColorG;
    GByte m_nBGColorB;

    int32_t m_nLineEndX;
    int32_t m_nLineEndY;

    int32_t m_nHeight;
    GByte m_nFontId;

    GByte m_nPenId;

    TABMAPObjText()
        : m_nTextAlignment(0), m_nAngle(0), m_nFontStyle(0), m_nFGColorR(0),
          m_nFGColorG(0), m_nFGColorB(0), m_nBGColorR(0), m_nBGColorG(0),
          m_nBGColorB(0), m_nLineEndX(0), m_nLineEndY(0), m_nHeight(0),
          m_nFontId(0), m_nPenId(0)
    {
    }
    virtual ~TABMAPObjText()
    {
    }

    virtual int WriteObj(TABMAPObjectBlock *) override;

    //  protected:
    virtual int ReadObj(TABMAPObjectBlock *) override;
};

class TABMAPObjMultiPoint final : public TABMAPObjHdrWithCoord
{
  public:
    int32_t m_nNumPoints;
    int32_t m_nComprOrgX; /* Present only in compressed coord. case */
    int32_t m_nComprOrgY;
    GByte m_nSymbolId;
    int32_t m_nLabelX; /* Not sure if it is a label point, but */
    int32_t m_nLabelY; /* it is similar to what we find in PLINE */

    TABMAPObjMultiPoint()
        : m_nNumPoints(0), m_nComprOrgX(0), m_nComprOrgY(0), m_nSymbolId(0),
          m_nLabelX(0), m_nLabelY(0)
    {
    }
    virtual ~TABMAPObjMultiPoint()
    {
    }

    virtual int WriteObj(TABMAPObjectBlock *) override;

    //  protected:
    virtual int ReadObj(TABMAPObjectBlock *) override;
};

class TABMAPObjCollection final : public TABMAPObjHdrWithCoord
{
  public:
    int32_t m_nRegionDataSize;
    int32_t m_nPolylineDataSize;
    int32_t m_nMPointDataSize;
    int32_t m_nComprOrgX; /* Present only in compressed coord. case */
    int32_t m_nComprOrgY;
    int32_t m_nNumMultiPoints;
    int32_t m_nNumRegSections;
    int32_t m_nNumPLineSections;

    GByte m_nMultiPointSymbolId;
    GByte m_nRegionPenId;
    GByte m_nRegionBrushId;
    GByte m_nPolylinePenId;

    TABMAPObjCollection()
        : m_nRegionDataSize(0), m_nPolylineDataSize(0), m_nMPointDataSize(0),
          m_nComprOrgX(0), m_nComprOrgY(0), m_nNumMultiPoints(0),
          m_nNumRegSections(0), m_nNumPLineSections(0),
          m_nMultiPointSymbolId(0), m_nRegionPenId(0), m_nRegionBrushId(0),
          m_nPolylinePenId(0)
    {
    }
    virtual ~TABMAPObjCollection()
    {
    }

    virtual int WriteObj(TABMAPObjectBlock *) override;

    //  protected:
    virtual int ReadObj(TABMAPObjectBlock *) override;

  private:
    // private copy ctor and assignment operator to prevent shallow copying
    TABMAPObjCollection &operator=(const TABMAPObjCollection &rhs);
    TABMAPObjCollection(const TABMAPObjCollection &rhs);
};

/*=====================================================================
          Classes to handle .MAP files low-level blocks
 =====================================================================*/

typedef struct TABBlockRef_t
{
    int32_t nBlockPtr;
    struct TABBlockRef_t *psPrev;
    struct TABBlockRef_t *psNext;
} TABBlockRef;

/*---------------------------------------------------------------------
 *                      class TABBinBlockManager
 *
 * This class is used to keep track of allocated blocks and is used
 * by various classes that need to allocate a new block in a .MAP file.
 *--------------------------------------------------------------------*/
class TABBinBlockManager
{
    CPL_DISALLOW_COPY_ASSIGN(TABBinBlockManager)

  protected:
    int m_nBlockSize;
    int32_t m_nLastAllocatedBlock;
    TABBlockRef *m_psGarbageBlocksFirst;
    TABBlockRef *m_psGarbageBlocksLast;
    char m_szName[32]; /* for debug purposes */

  public:
    TABBinBlockManager();
    ~TABBinBlockManager();

    void SetBlockSize(int nBlockSize);
    int GetBlockSize() const
    {
        return m_nBlockSize;
    }

    int32_t AllocNewBlock(const char *pszReason = "");
    void Reset();
    void SetLastPtr(int nBlockPtr)
    {
        m_nLastAllocatedBlock = nBlockPtr;
    }

    void PushGarbageBlockAsFirst(int32_t nBlockPtr);
    void PushGarbageBlockAsLast(int32_t nBlockPtr);
    int32_t GetFirstGarbageBlock();
    int32_t PopGarbageBlock();

    void SetName(const char *pszName);
};

/*---------------------------------------------------------------------
 *                      class TABRawBinBlock
 *
 * This is the base class used for all other data block types... it
 * contains all the base functions to handle binary data.
 *--------------------------------------------------------------------*/

class TABRawBinBlock
{
    CPL_DISALLOW_COPY_ASSIGN(TABRawBinBlock)

  protected:
    VSILFILE *m_fp;      /* Associated file handle               */
    TABAccess m_eAccess; /* Read/Write access mode               */

    int m_nBlockType;

    GByte *m_pabyBuf;      /* Buffer to contain the block's data    */
    int m_nBlockSize;      /* Size of current block (and buffer)    */
    int m_nSizeUsed;       /* Number of bytes used in buffer        */
    bool m_bHardBlockSize; /* TRUE=Blocks MUST always be nSize bytes  */
                           /* FALSE=last block may be less than nSize */
    int m_nFileOffset;     /* Location of current block in the file */
    int m_nCurPos;         /* Next byte to read from m_pabyBuf[]    */
    int m_nFirstBlockPtr;  /* Size of file header when different from */
                           /* block size (used by GotoByteInFile())   */
    int m_nFileSize;

    int m_bModified; /* Used only to detect changes        */

  public:
    explicit TABRawBinBlock(TABAccess eAccessMode = TABRead,
                            bool bHardBlockSize = TRUE);
    virtual ~TABRawBinBlock();

    virtual int ReadFromFile(VSILFILE *fpSrc, int nOffset, int nSize);
    virtual int CommitToFile();
    int CommitAsDeleted(int32_t nNextBlockPtr);

    virtual int InitBlockFromData(GByte *pabyBuf, int nBlockSize, int nSizeUsed,
                                  bool bMakeCopy = TRUE,
                                  VSILFILE *fpSrc = nullptr, int nOffset = 0);
    virtual int InitNewBlock(VSILFILE *fpSrc, int nBlockSize,
                             int nFileOffset = 0);

    int GetBlockType();
    virtual int GetBlockClass()
    {
        return TAB_RAWBIN_BLOCK;
    }

    int32_t GetStartAddress()
    {
        return m_nFileOffset;
    }
#ifdef DEBUG
    virtual void Dump(FILE *fpOut = nullptr);
#endif
    static void DumpBytes(int32_t nValue, int nOffset = 0,
                          FILE *fpOut = nullptr);

    int GotoByteRel(int nOffset);
    int GotoByteInBlock(int nOffset);
    int GotoByteInFile(int nOffset, bool bForceReadFromFile = FALSE,
                       bool bOffsetIsEndOfData = FALSE);
    void SetFirstBlockPtr(int nOffset);

    int GetNumUnusedBytes();
    int GetFirstUnusedByteOffset();
    int GetCurAddress();

    virtual int ReadBytes(int numBytes, GByte *pabyDstBuf);
    GByte ReadByte();
    // cppcheck-suppress functionStatic
    int16_t ReadInt16();
    // cppcheck-suppress functionStatic
    int32_t ReadInt32();
    // cppcheck-suppress functionStatic
    int64_t ReadInt64();
    // cppcheck-suppress functionStatic
    float ReadFloat();
    // cppcheck-suppress functionStatic
    double ReadDouble();

    virtual int WriteBytes(int nBytesToWrite, const GByte *pBuf);
    int WriteByte(GByte byValue);
    // cppcheck-suppress functionStatic
    int WriteInt16(int16_t n16Value);
    // cppcheck-suppress functionStatic
    int WriteInt32(int32_t n32Value);
    // cppcheck-suppress functionStatic
    int WriteInt64(int64_t n64Value);
    // cppcheck-suppress functionStatic
    int WriteFloat(float fValue);
    // cppcheck-suppress functionStatic
    int WriteDouble(double dValue);
    int WriteZeros(int nBytesToWrite);
    int WritePaddedString(int nFieldSize, const char *pszString);

    void SetModifiedFlag(bool bModified)
    {
        m_bModified = bModified;
    }

    // This semi-private method gives a direct access to the internal
    // buffer... to be used with extreme care!!!!!!!!!
    GByte *GetCurDataPtr()
    {
        return (m_pabyBuf + m_nCurPos);
    }
};

/*---------------------------------------------------------------------
 *                      class TABMAPHeaderBlock
 *
 * Class to handle Read/Write operation on .MAP Header Blocks
 *--------------------------------------------------------------------*/

class TABMAPHeaderBlock final : public TABRawBinBlock
{
    void InitMembersWithDefaultValues();
    void UpdatePrecision();

  protected:
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
    TABProjInfo m_sProj{};
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

  public:
    explicit TABMAPHeaderBlock(TABAccess eAccessMode = TABRead);
    virtual ~TABMAPHeaderBlock();

    virtual int CommitToFile() override;

    virtual int InitBlockFromData(GByte *pabyBuf, int nBlockSize, int nSizeUsed,
                                  bool bMakeCopy = TRUE,
                                  VSILFILE *fpSrc = nullptr,
                                  int nOffset = 0) override;
    virtual int InitNewBlock(VSILFILE *fpSrc, int nBlockSize,
                             int nFileOffset = 0) override;

    virtual int GetBlockClass() override
    {
        return TABMAP_HEADER_BLOCK;
    }

    int Int2Coordsys(int32_t nX, int32_t nY, double &dX, double &dY);
    int Coordsys2Int(double dX, double dY, int32_t &nX, int32_t &nY,
                     bool bIgnoreOverflow = FALSE);
    int ComprInt2Coordsys(int32_t nCenterX, int32_t nCenterY, int nDeltaX,
                          int nDeltaY, double &dX, double &dY);
    int Int2CoordsysDist(int32_t nX, int32_t nY, double &dX, double &dY);
    int Coordsys2IntDist(double dX, double dY, int32_t &nX, int32_t &nY);
    int SetCoordsysBounds(double dXMin, double dYMin, double dXMax,
                          double dYMax);

    int GetMapObjectSize(int nObjType);
    bool MapObjectUsesCoordBlock(int nObjType);

    int GetProjInfo(TABProjInfo *psProjInfo);
    int SetProjInfo(TABProjInfo *psProjInfo);

#ifdef DEBUG
    virtual void Dump(FILE *fpOut = nullptr) override;
#endif

    // Instead of having over 30 get/set methods, we'll make all data
    // members public and we will initialize them in the overloaded
    // LoadFromFile().  For this reason, this class should be used with care.

    int16_t m_nMAPVersionNumber{};
    int16_t m_nRegularBlockSize{};

    double m_dCoordsys2DistUnits{};
    int32_t m_nXMin{};
    int32_t m_nYMin{};
    int32_t m_nXMax{};
    int32_t m_nYMax{};
    bool m_bIntBoundsOverflow{};  // Set to TRUE if coordinates
                                  // outside of bounds were written

    int32_t m_nFirstIndexBlock{};
    int32_t m_nFirstGarbageBlock{};
    int32_t m_nFirstToolBlock{};
    int32_t m_numPointObjects{};
    int32_t m_numLineObjects{};
    int32_t m_numRegionObjects{};
    int32_t m_numTextObjects{};
    int32_t m_nMaxCoordBufSize{};

    GByte m_nDistUnitsCode{};  // See Appendix F
    GByte m_nMaxSpIndexDepth{};
    GByte m_nCoordPrecision{};  // Num. decimal places on coord.
    GByte m_nCoordOriginQuadrant{};
    GByte m_nReflectXAxisCoord{};
    GByte m_nMaxObjLenArrayId{};  // See gabyObjLenArray[]
    GByte m_numPenDefs{};
    GByte m_numBrushDefs{};
    GByte m_numSymbolDefs{};
    GByte m_numFontDefs{};
    int16_t m_numMapToolBlocks{};

    double m_XScale{};
    double m_YScale{};
    double m_XDispl{};
    double m_YDispl{};
    double m_XPrecision{};  // maximum achievable precision along X axis
                            // depending on bounds extent
    double m_YPrecision{};  // maximum achievable precision along Y axis
                            // depending on bounds extent
};

/*---------------------------------------------------------------------
 *                      class TABMAPIndexBlock
 *
 * Class to handle Read/Write operation on .MAP Index Blocks (Type 01)
 *--------------------------------------------------------------------*/

class TABMAPIndexBlock final : public TABRawBinBlock
{
    CPL_DISALLOW_COPY_ASSIGN(TABMAPIndexBlock)

  protected:
    int m_numEntries;
    TABMAPIndexEntry m_asEntries[TAB_MAX_ENTRIES_INDEX_BLOCK];

    int ReadNextEntry(TABMAPIndexEntry *psEntry);
    int WriteNextEntry(TABMAPIndexEntry *psEntry);

    // Use these to keep track of current block's MBR
    int32_t m_nMinX;
    int32_t m_nMinY;
    int32_t m_nMaxX;
    int32_t m_nMaxY;

    TABBinBlockManager *m_poBlockManagerRef;

    // Info about child currently loaded
    TABMAPIndexBlock *m_poCurChild;
    int m_nCurChildIndex;
    // Also need to know about its parent
    TABMAPIndexBlock *m_poParentRef;

    int ReadAllEntries();

    int GetMaxEntries() const
    {
        return ((m_nBlockSize - 4) / 20);
    }

  public:
    explicit TABMAPIndexBlock(TABAccess eAccessMode = TABRead);
    virtual ~TABMAPIndexBlock();

    virtual int InitBlockFromData(GByte *pabyBuf, int nBlockSize, int nSizeUsed,
                                  bool bMakeCopy = TRUE,
                                  VSILFILE *fpSrc = nullptr,
                                  int nOffset = 0) override;
    virtual int InitNewBlock(VSILFILE *fpSrc, int nBlockSize,
                             int nFileOffset = 0) override;
    virtual int CommitToFile() override;

    virtual int GetBlockClass() override
    {
        return TABMAP_INDEX_BLOCK;
    }

    void UnsetCurChild();

    int GetNumFreeEntries();
    int GetNumEntries()
    {
        return m_numEntries;
    }
    TABMAPIndexEntry *GetEntry(int iIndex);
    int AddEntry(int32_t XMin, int32_t YMin, int32_t XMax, int32_t YMax,
                 int32_t nBlockPtr, bool bAddInThisNodeOnly = FALSE);
    int GetCurMaxDepth();
    void GetMBR(int32_t &nXMin, int32_t &nYMin, int32_t &nXMax, int32_t &nYMax);
    void SetMBR(int32_t nXMin, int32_t nYMin, int32_t nXMax, int32_t nYMax);

    int32_t GetNodeBlockPtr()
    {
        return GetStartAddress();
    }

    void SetMAPBlockManagerRef(TABBinBlockManager *poBlockMgr);
    void SetParentRef(TABMAPIndexBlock *poParent);
    void SetCurChildRef(TABMAPIndexBlock *poChild, int nChildIndex);

    int GetCurChildIndex()
    {
        return m_nCurChildIndex;
    }
    TABMAPIndexBlock *GetCurChild()
    {
        return m_poCurChild;
    }
    TABMAPIndexBlock *GetParentRef()
    {
        return m_poParentRef;
    }

    int SplitNode(int32_t nNewEntryXMin, int32_t nNewEntryYMin,
                  int32_t nNewEntryXMax, int32_t nNewEntryYMax);
    int SplitRootNode(int32_t nNewEntryXMin, int32_t nNewEntryYMin,
                      int32_t nNewEntryXMax, int32_t nNewEntryYMax);
    void UpdateCurChildMBR(int32_t nXMin, int32_t nYMin, int32_t nXMax,
                           int32_t nYMax, int32_t nBlockPtr);
    void RecomputeMBR();
    int InsertEntry(int32_t XMin, int32_t YMin, int32_t XMax, int32_t YMax,
                    int32_t nBlockPtr);
    int ChooseSubEntryForInsert(int32_t nXMin, int32_t nYMin, int32_t nXMax,
                                int32_t nYMax);
    int32_t ChooseLeafForInsert(int32_t nXMin, int32_t nYMin, int32_t nXMax,
                                int32_t nYMax);
    int UpdateLeafEntry(int32_t nBlockPtr, int32_t nXMin, int32_t nYMin,
                        int32_t nXMax, int32_t nYMax);
    int GetCurLeafEntryMBR(int32_t nBlockPtr, int32_t &nXMin, int32_t &nYMin,
                           int32_t &nXMax, int32_t &nYMax);

    // Static utility functions for node splitting, also used by
    // the TABMAPObjectBlock class.
    static double ComputeAreaDiff(int32_t nNodeXMin, int32_t nNodeYMin,
                                  int32_t nNodeXMax, int32_t nNodeYMax,
                                  int32_t nEntryXMin, int32_t nEntryYMin,
                                  int32_t nEntryXMax, int32_t nEntryYMax);
    static int PickSeedsForSplit(TABMAPIndexEntry *pasEntries, int numEntries,
                                 int nSrcCurChildIndex, int32_t nNewEntryXMin,
                                 int32_t nNewEntryYMin, int32_t nNewEntryXMax,
                                 int32_t nNewEntryYMax, int &nSeed1,
                                 int &nSeed2);
#ifdef DEBUG
    virtual void Dump(FILE *fpOut = nullptr) override;
#endif
};

/*---------------------------------------------------------------------
 *                      class TABMAPObjectBlock
 *
 * Class to handle Read/Write operation on .MAP Object data Blocks (Type 02)
 *--------------------------------------------------------------------*/

class TABMAPObjectBlock final : public TABRawBinBlock
{
    CPL_DISALLOW_COPY_ASSIGN(TABMAPObjectBlock)

  protected:
    int m_numDataBytes; /* Excluding first 4 bytes header */
    int32_t m_nFirstCoordBlock;
    int32_t m_nLastCoordBlock;
    int32_t m_nCenterX;
    int32_t m_nCenterY;

    // In order to compute block center, we need to keep track of MBR
    int32_t m_nMinX;
    int32_t m_nMinY;
    int32_t m_nMaxX;
    int32_t m_nMaxY;

    // Keep track of current object either in read or read/write mode
    int m_nCurObjectOffset;  // -1 if there is no current object.
    int m_nCurObjectId;      // -1 if there is no current object.
    TABGeomType
        m_nCurObjectType;  // TAB_GEOM_UNSET if there is no current object.

    int m_bLockCenter;

  public:
    explicit TABMAPObjectBlock(TABAccess eAccessMode = TABRead);
    virtual ~TABMAPObjectBlock();

    virtual int CommitToFile() override;
    virtual int InitBlockFromData(GByte *pabyBuf, int nBlockSize, int nSizeUsed,
                                  bool bMakeCopy = TRUE,
                                  VSILFILE *fpSrc = nullptr,
                                  int nOffset = 0) override;
    virtual int InitNewBlock(VSILFILE *fpSrc, int nBlockSize,
                             int nFileOffset = 0) override;

    virtual int GetBlockClass() override
    {
        return TABMAP_OBJECT_BLOCK;
    }

    virtual int ReadIntCoord(bool bCompressed, int32_t &nX, int32_t &nY);
    int WriteIntCoord(int32_t nX, int32_t nY, bool bCompressed);
    int WriteIntMBRCoord(int32_t nXMin, int32_t nYMin, int32_t nXMax,
                         int32_t nYMax, bool bCompressed);
    int UpdateMBR(int32_t nX, int32_t nY);

    int PrepareNewObject(TABMAPObjHdr *poObjHdr);
    int CommitNewObject(TABMAPObjHdr *poObjHdr);

    void AddCoordBlockRef(int32_t nCoordBlockAddress);
    int32_t GetFirstCoordBlockAddress()
    {
        return m_nFirstCoordBlock;
    }
    int32_t GetLastCoordBlockAddress()
    {
        return m_nLastCoordBlock;
    }

    void GetMBR(int32_t &nXMin, int32_t &nYMin, int32_t &nXMax, int32_t &nYMax);
    void SetMBR(int32_t nXMin, int32_t nYMin, int32_t nXMax, int32_t nYMax);

    void Rewind();
    void ClearObjects();
    void LockCenter();
    void SetCenterFromOtherBlock(TABMAPObjectBlock *poOtherObjBlock);
    int AdvanceToNextObject(TABMAPHeaderBlock *);
    int GetCurObjectOffset()
    {
        return m_nCurObjectOffset;
    }
    int GetCurObjectId()
    {
        return m_nCurObjectId;
    }
    TABGeomType GetCurObjectType()
    {
        return m_nCurObjectType;
    }

#ifdef DEBUG
    virtual void Dump(FILE *fpOut = nullptr) override
    {
        Dump(fpOut, FALSE);
    }
    void Dump(FILE *fpOut, bool bDetails);
#endif
};

/*---------------------------------------------------------------------
 *                      class TABMAPCoordBlock
 *
 * Class to handle Read/Write operation on .MAP Coordinate Blocks (Type 03)
 *--------------------------------------------------------------------*/

class TABMAPCoordBlock final : public TABRawBinBlock
{
    CPL_DISALLOW_COPY_ASSIGN(TABMAPCoordBlock)

  protected:
    int m_numDataBytes; /* Excluding first 8 bytes header */
    int32_t m_nNextCoordBlock;
    int m_numBlocksInChain;

    int32_t m_nComprOrgX;
    int32_t m_nComprOrgY;

    // In order to compute block center, we need to keep track of MBR
    int32_t m_nMinX;
    int32_t m_nMinY;
    int32_t m_nMaxX;
    int32_t m_nMaxY;

    TABBinBlockManager *m_poBlockManagerRef;

    int m_nTotalDataSize;    // Num bytes in whole chain of blocks
    int m_nFeatureDataSize;  // Num bytes for current feature coords

    int32_t m_nFeatureXMin;  // Used to keep track of current
    int32_t m_nFeatureYMin;  // feature MBR.
    int32_t m_nFeatureXMax;
    int32_t m_nFeatureYMax;

  public:
    explicit TABMAPCoordBlock(TABAccess eAccessMode = TABRead);
    virtual ~TABMAPCoordBlock();

    virtual int InitBlockFromData(GByte *pabyBuf, int nBlockSize, int nSizeUsed,
                                  bool bMakeCopy = TRUE,
                                  VSILFILE *fpSrc = nullptr,
                                  int nOffset = 0) override;
    virtual int InitNewBlock(VSILFILE *fpSrc, int nBlockSize,
                             int nFileOffset = 0) override;
    virtual int CommitToFile() override;

    virtual int GetBlockClass() override
    {
        return TABMAP_COORD_BLOCK;
    }

    void SetMAPBlockManagerRef(TABBinBlockManager *poBlockManager);
    virtual int ReadBytes(int numBytes, GByte *pabyDstBuf) override;
    virtual int WriteBytes(int nBytesToWrite, const GByte *pBuf) override;
    void SetComprCoordOrigin(int32_t nX, int32_t nY);
    int ReadIntCoord(bool bCompressed, int32_t &nX, int32_t &nY);
    int ReadIntCoords(bool bCompressed, int numCoords, int32_t *panXY);
    int ReadCoordSecHdrs(bool bCompressed, int nVersion, int numSections,
                         TABMAPCoordSecHdr *pasHdrs, int32_t &numVerticesTotal);
    int WriteCoordSecHdrs(int nVersion, int numSections,
                          TABMAPCoordSecHdr *pasHdrs, bool bCompressed);

    void SetNextCoordBlock(int32_t nNextCoordBlockAddress);
    int32_t GetNextCoordBlock()
    {
        return m_nNextCoordBlock;
    }

    int WriteIntCoord(int32_t nX, int32_t nY, bool bCompressed);

    int GetNumBlocksInChain()
    {
        return m_numBlocksInChain;
    }

    void ResetTotalDataSize()
    {
        m_nTotalDataSize = 0;
    }
    int GetTotalDataSize()
    {
        return m_nTotalDataSize;
    }

    void SeekEnd();
    void StartNewFeature();
    int GetFeatureDataSize()
    {
        return m_nFeatureDataSize;
    }
    //__TODO__ Can we flush GetFeatureMBR() and all MBR tracking in this
    // class???
    void GetFeatureMBR(int32_t &nXMin, int32_t &nYMin, int32_t &nXMax,
                       int32_t &nYMax);

#ifdef DEBUG
    virtual void Dump(FILE *fpOut = nullptr) override;
#endif
};

/*---------------------------------------------------------------------
 *                      class TABMAPToolBlock
 *
 * Class to handle Read/Write operation on .MAP Drawing Tool Blocks (Type 05)
 *
 * In addition to handling the I/O, this class also maintains the list
 * of Tool definitions in memory.
 *--------------------------------------------------------------------*/

class TABMAPToolBlock final : public TABRawBinBlock
{
    CPL_DISALLOW_COPY_ASSIGN(TABMAPToolBlock)

  protected:
    int m_numDataBytes; /* Excluding first 8 bytes header */
    int32_t m_nNextToolBlock;
    int m_numBlocksInChain;

    TABBinBlockManager *m_poBlockManagerRef;

  public:
    explicit TABMAPToolBlock(TABAccess eAccessMode = TABRead);
    virtual ~TABMAPToolBlock();

    virtual int InitBlockFromData(GByte *pabyBuf, int nBlockSize, int nSizeUsed,
                                  bool bMakeCopy = TRUE,
                                  VSILFILE *fpSrc = nullptr,
                                  int nOffset = 0) override;
    virtual int InitNewBlock(VSILFILE *fpSrc, int nBlockSize,
                             int nFileOffset = 0) override;
    virtual int CommitToFile() override;

    virtual int GetBlockClass() override
    {
        return TABMAP_TOOL_BLOCK;
    }

    void SetMAPBlockManagerRef(TABBinBlockManager *poBlockManager);
    virtual int ReadBytes(int numBytes, GByte *pabyDstBuf) override;
    virtual int WriteBytes(int nBytesToWrite, const GByte *pBuf) override;

    void SetNextToolBlock(int32_t nNextCoordBlockAddress);

    bool EndOfChain();
    int GetNumBlocksInChain()
    {
        return m_numBlocksInChain;
    }

    int CheckAvailableSpace(int nToolType);

#ifdef DEBUG
    virtual void Dump(FILE *fpOut = nullptr) override;
#endif
};

/*=====================================================================
       Classes to deal with .MAP files at the MapInfo object level
 =====================================================================*/

/*---------------------------------------------------------------------
 *                      class TABIDFile
 *
 * Class to handle Read/Write operation on .ID files... the .ID file
 * contains an index to the objects in the .MAP file by object id.
 *--------------------------------------------------------------------*/

class TABIDFile
{
    CPL_DISALLOW_COPY_ASSIGN(TABIDFile)

  private:
    char *m_pszFname;
    VSILFILE *m_fp;
    TABAccess m_eAccessMode;

    TABRawBinBlock *m_poIDBlock;
    int m_nBlockSize;
    int32_t m_nMaxId;

  public:
    TABIDFile();
    ~TABIDFile();

    int Open(const char *pszFname, const char *pszAccess);
    int Open(const char *pszFname, TABAccess eAccess);
    int Close();

    int SyncToDisk();

    int32_t GetObjPtr(int32_t nObjId);
    int SetObjPtr(int32_t nObjId, int32_t nObjPtr);
    int32_t GetMaxObjId();

#ifdef DEBUG
    void Dump(FILE *fpOut = nullptr);
#endif
};

/*---------------------------------------------------------------------
 *                      class TABMAPFile
 *
 * Class to handle Read/Write operation on .MAP files... this class hides
 * all the dealings with blocks, indexes, etc.
 * Use this class to deal with MapInfo objects directly.
 *--------------------------------------------------------------------*/

class TABMAPFile
{
    CPL_DISALLOW_COPY_ASSIGN(TABMAPFile)

  private:
    int m_nMinTABVersion;
    char *m_pszFname;
    VSILFILE *m_fp;
    TABAccess m_eAccessMode;

    TABBinBlockManager m_oBlockManager{};

    TABMAPHeaderBlock *m_poHeader;

    // Members used to access objects using the spatial index
    TABMAPIndexBlock *m_poSpIndex;

    // Defaults to FALSE, i.e. optimized spatial index
    bool m_bQuickSpatialIndexMode;

    // Member used to access objects using the object ids (.ID file)
    TABIDFile *m_poIdIndex;

    // Current object data block.
    TABMAPObjectBlock *m_poCurObjBlock;
    int m_nCurObjPtr;
    TABGeomType m_nCurObjType;
    int m_nCurObjId;
    TABMAPCoordBlock *m_poCurCoordBlock;

    // Drawing Tool Def. table (takes care of all drawing tools in memory)
    TABToolDefTable *m_poToolDefTable;

    // Coordinates filter... default is MBR of the whole file
    TABVertex m_sMinFilter{};
    TABVertex m_sMaxFilter{};
    int32_t m_XMinFilter;
    int32_t m_YMinFilter;
    int32_t m_XMaxFilter;
    int32_t m_YMaxFilter;

    int m_bUpdated;
    int m_bLastOpWasRead;
    int m_bLastOpWasWrite;

    int CommitObjAndCoordBlocks(bool bDeleteObjects = FALSE);
    int LoadObjAndCoordBlocks(int32_t nBlockPtr);
    TABMAPObjectBlock *SplitObjBlock(TABMAPObjHdr *poObjHdrToAdd,
                                     int nSizeOfObjToAdd);
    int MoveObjToBlock(TABMAPObjHdr *poObjHdr,
                       TABMAPCoordBlock *poSrcCoordBlock,
                       TABMAPObjectBlock *poDstObjBlock,
                       TABMAPCoordBlock **ppoDstCoordBlock);
    int PrepareCoordBlock(int nObjType, TABMAPObjectBlock *poObjBlock,
                          TABMAPCoordBlock **ppoCoordBlock);

    int InitDrawingTools();
    int CommitDrawingTools();

    int CommitSpatialIndex();

    // Stuff related to traversing spatial index.
    TABMAPIndexBlock *m_poSpIndexLeaf;

    // Strings encoding
    CPLString m_osEncoding;

    int LoadNextMatchingObjectBlock(int bFirstObject);
    TABRawBinBlock *PushBlock(int nFileOffset);

    int ReOpenReadWrite();

  public:
    explicit TABMAPFile(const char *pszEncoding);
    ~TABMAPFile();

    int Open(const char *pszFname, const char *pszAccess,
             bool bNoErrorMsg = FALSE, int nBlockSizeForCreate = 512);
    int Open(const char *pszFname, TABAccess eAccess, bool bNoErrorMsg = FALSE,
             int nBlockSizeForCreate = 512);
    int Close();

    uint32_t GetFileSize();

    int SyncToDisk();

    int SetQuickSpatialIndexMode(bool bQuickSpatialIndexMode = TRUE);

    int Int2Coordsys(int32_t nX, int32_t nY, double &dX, double &dY);
    int Coordsys2Int(double dX, double dY, int32_t &nX, int32_t &nY,
                     bool bIgnoreOverflow = FALSE);
    int Int2CoordsysDist(int32_t nX, int32_t nY, double &dX, double &dY);
    int Coordsys2IntDist(double dX, double dY, int32_t &nX, int32_t &nY);
    void SetCoordFilter(TABVertex sMin, TABVertex sMax);
    // cppcheck-suppress functionStatic
    void GetCoordFilter(TABVertex &sMin, TABVertex &sMax) const;
    void ResetCoordFilter();
    int SetCoordsysBounds(double dXMin, double dYMin, double dXMax,
                          double dYMax);

    int32_t GetMaxObjId();
    int MoveToObjId(int nObjId);
    void UpdateMapHeaderInfo(TABGeomType nObjType);
    int PrepareNewObj(TABMAPObjHdr *poObjHdr);
    int PrepareNewObjViaSpatialIndex(TABMAPObjHdr *poObjHdr);
    int PrepareNewObjViaObjBlock(TABMAPObjHdr *poObjHdr);
    int CommitNewObj(TABMAPObjHdr *poObjHdr);

    void ResetReading();
    int GetNextFeatureId(int nPrevId);

    int MarkAsDeleted();

    TABGeomType GetCurObjType();
    int GetCurObjId();
    TABMAPObjectBlock *GetCurObjBlock();
    TABMAPCoordBlock *GetCurCoordBlock();
    TABMAPCoordBlock *GetCoordBlock(int nFileOffset);
    TABMAPHeaderBlock *GetHeaderBlock();
    TABIDFile *GetIDFileRef();
    TABRawBinBlock *GetIndexObjectBlock(int nFileOffset);

    int ReadPenDef(int nPenIndex, TABPenDef *psDef);
    int ReadBrushDef(int nBrushIndex, TABBrushDef *psDef);
    int ReadFontDef(int nFontIndex, TABFontDef *psDef);
    int ReadSymbolDef(int nSymbolIndex, TABSymbolDef *psDef);
    int WritePenDef(TABPenDef *psDef);
    int WriteBrushDef(TABBrushDef *psDef);
    int WriteFontDef(TABFontDef *psDef);
    int WriteSymbolDef(TABSymbolDef *psDef);

    int GetMinTABFileVersion();

    const CPLString &GetEncoding() const;
    void SetEncoding(const CPLString &);

    static bool IsValidObjType(int nObjType);

#ifdef DEBUG
    void Dump(FILE *fpOut = nullptr);
    void DumpSpatialIndexToMIF(TABMAPIndexBlock *poNode, FILE *fpMIF,
                               FILE *fpMID, int nParentId = -1,
                               int nIndexInNode = -1, int nCurDepth = 0,
                               int nMaxDepth = -1);
#endif
};

/*---------------------------------------------------------------------
 *                      class TABINDNode
 *
 * An index node in a .IND file.
 *
 * This class takes care of reading child nodes as necessary when looking
 * for a given key value in the index tree.
 *--------------------------------------------------------------------*/

class TABINDNode
{
    CPL_DISALLOW_COPY_ASSIGN(TABINDNode)

  private:
    VSILFILE *m_fp;
    TABAccess m_eAccessMode;
    TABINDNode *m_poCurChildNode;
    TABINDNode *m_poParentNodeRef;

    TABBinBlockManager *m_poBlockManagerRef;

    int m_nSubTreeDepth;
    int m_nKeyLength;
    TABFieldType m_eFieldType;
    bool m_bUnique;

    int32_t m_nCurDataBlockPtr;
    int m_nCurIndexEntry;
    TABRawBinBlock *m_poDataBlock;
    int m_numEntriesInNode;
    int32_t m_nPrevNodePtr;
    int32_t m_nNextNodePtr;

    int GotoNodePtr(int32_t nNewNodePtr);
    int32_t ReadIndexEntry(int nEntryNo, GByte *pKeyValue);
    int IndexKeyCmp(const GByte *pKeyValue, int nEntryNo);

    int InsertEntry(GByte *pKeyValue, int32_t nRecordNo,
                    bool bInsertAfterCurChild = FALSE,
                    bool bMakeNewEntryCurChild = FALSE);
    int SetNodeBufferDirectly(int numEntries, GByte *pBuf,
                              int nCurIndexEntry = 0,
                              TABINDNode *poCurChild = nullptr);
    int32_t FindFirst(const GByte *pKeyValue,
                      std::set<int> &oSetVisitedNodePtr);

  public:
    explicit TABINDNode(TABAccess eAccessMode = TABRead);
    ~TABINDNode();

    int InitNode(VSILFILE *fp, int nBlockPtr, int nKeyLength, int nSubTreeDepth,
                 bool bUnique, TABBinBlockManager *poBlockMgr = nullptr,
                 TABINDNode *poParentNode = nullptr, int nPrevNodePtr = 0,
                 int nNextNodePtr = 0);

    int SetFieldType(TABFieldType eType);
    TABFieldType GetFieldType()
    {
        return m_eFieldType;
    }

    void SetUnique(bool bUnique)
    {
        m_bUnique = bUnique;
    }
    bool IsUnique()
    {
        return m_bUnique;
    }

    int GetKeyLength()
    {
        return m_nKeyLength;
    }
    int GetSubTreeDepth()
    {
        return m_nSubTreeDepth;
    }
    int32_t GetNodeBlockPtr()
    {
        return m_nCurDataBlockPtr;
    }
    int GetNumEntries()
    {
        return m_numEntriesInNode;
    }
    int GetMaxNumEntries()
    {
        return (512 - 12) / (m_nKeyLength + 4);
    }

    int32_t FindFirst(const GByte *pKeyValue);
    int32_t FindNext(GByte *pKeyValue);

    int CommitToFile();

    int AddEntry(GByte *pKeyValue, int32_t nRecordNo,
                 bool bAddInThisNodeOnly = FALSE,
                 bool bInsertAfterCurChild = FALSE,
                 bool bMakeNewEntryCurChild = FALSE);
    int SplitNode();
    int SplitRootNode();
    GByte *GetNodeKey();
    int UpdateCurChildEntry(GByte *pKeyValue, int32_t nRecordNo);
    int UpdateSplitChild(GByte *pKeyValue1, int32_t nRecordNo1,
                         GByte *pKeyValue2, int32_t nRecordNo2,
                         int nNewCurChildNo /* 1 or 2 */);

    int SetNodeBlockPtr(int32_t nThisNodePtr);
    int SetPrevNodePtr(int32_t nPrevNodePtr);
    int SetNextNodePtr(int32_t nNextNodePtr);

#ifdef DEBUG
    void Dump(FILE *fpOut = nullptr);
#endif
};

/*---------------------------------------------------------------------
 *                      class TABINDFile
 *
 * Class to handle table field index (.IND) files... we use this
 * class as the main entry point to open and search the table field indexes.
 * Note that .IND files are supported for read access only.
 *--------------------------------------------------------------------*/

class TABINDFile
{
    CPL_DISALLOW_COPY_ASSIGN(TABINDFile)

  private:
    char *m_pszFname;
    VSILFILE *m_fp;
    TABAccess m_eAccessMode;

    TABBinBlockManager m_oBlockManager{};

    int m_numIndexes;
    TABINDNode **m_papoIndexRootNodes;
    GByte **m_papbyKeyBuffers;

    int ValidateIndexNo(int nIndexNumber);
    int ReadHeader();
    int WriteHeader();

  public:
    TABINDFile();
    ~TABINDFile();

    int Open(const char *pszFname, const char *pszAccess,
             bool bTestOpenNoError = FALSE);
    int Close();

    int GetNumIndexes()
    {
        return m_numIndexes;
    }
    int SetIndexFieldType(int nIndexNumber, TABFieldType eType);
    int SetIndexUnique(int nIndexNumber, bool bUnique = TRUE);
    GByte *BuildKey(int nIndexNumber, int32_t nValue);
    GByte *BuildKey(int nIndexNumber, int64_t nValue);
    GByte *BuildKey(int nIndexNumber, const char *pszStr);
    GByte *BuildKey(int nIndexNumber, double dValue);
    int32_t FindFirst(int nIndexNumber, GByte *pKeyValue);
    int32_t FindNext(int nIndexNumber, GByte *pKeyValue);

    int CreateIndex(TABFieldType eType, int nFieldSize);
    int AddEntry(int nIndexNumber, GByte *pKeyValue, int32_t nRecordNo);

#ifdef DEBUG
    void Dump(FILE *fpOut = nullptr);
#endif
};

/*---------------------------------------------------------------------
 *                      class TABDATFile
 *
 * Class to handle Read/Write operation on .DAT files... the .DAT file
 * contains the table of attribute field values.
 *--------------------------------------------------------------------*/

class TABDATFile
{
    CPL_DISALLOW_COPY_ASSIGN(TABDATFile)

  private:
    char *m_pszFname;
    VSILFILE *m_fp;
    TABAccess m_eAccessMode;
    TABTableType m_eTableType;

    TABRawBinBlock *m_poHeaderBlock;
    int m_numFields;
    TABDATFieldDef *m_pasFieldDef;

    TABRawBinBlock *m_poRecordBlock;
    int m_nBlockSize;
    int m_nRecordSize;
    int m_nCurRecordId;
    bool m_bCurRecordDeletedFlag;

    int32_t m_numRecords;
    int32_t m_nFirstRecordPtr;
    bool m_bWriteHeaderInitialized;
    bool m_bWriteEOF;

    int m_bUpdated;
    CPLString m_osEncoding;

    int InitWriteHeader();
    int WriteHeader();

    // We know that character strings are limited to 254 chars in MapInfo
    // Using a buffer pr. class instance to avoid threading issues with the
    // library
    char m_szBuffer[256];

  public:
    explicit TABDATFile(const char *pszEncoding);
    ~TABDATFile();

    int Open(const char *pszFname, const char *pszAccess,
             TABTableType eTableType = TABTableNative);
    int Open(const char *pszFname, TABAccess eAccess,
             TABTableType eTableType = TABTableNative);
    int Close();

    int GetNumFields();
    TABFieldType GetFieldType(int nFieldId);
    int GetFieldWidth(int nFieldId);
    int GetFieldPrecision(int nFieldId);
    int ValidateFieldInfoFromTAB(int iField, const char *pszName,
                                 TABFieldType eType, int nWidth,
                                 int nPrecision);

    int AddField(const char *pszName, TABFieldType eType, int nWidth,
                 int nPrecision = 0);

    int DeleteField(int iField);
    int ReorderFields(int *panMap);
    int AlterFieldDefn(int iField, OGRFieldDefn *poNewFieldDefn, int nFlags);

    int SyncToDisk();

    int32_t GetNumRecords();
    TABRawBinBlock *GetRecordBlock(int nRecordId);
    bool IsCurrentRecordDeleted()
    {
        return m_bCurRecordDeletedFlag;
    }
    int CommitRecordToFile();

    int MarkAsDeleted();
    int MarkRecordAsExisting();

    const char *ReadCharField(int nWidth);
    int32_t ReadIntegerField(int nWidth);
    int16_t ReadSmallIntField(int nWidth);
    int64_t ReadLargeIntField(int nWidth);
    double ReadFloatField(int nWidth);
    double ReadDecimalField(int nWidth);
    const char *ReadLogicalField(int nWidth);
    const char *ReadDateField(int nWidth);
    int ReadDateField(int nWidth, int *nYear, int *nMonth, int *nDay);
    const char *ReadTimeField(int nWidth);
    int ReadTimeField(int nWidth, int *nHour, int *nMinute, int *nSecond,
                      int *nMS);
    const char *ReadDateTimeField(int nWidth);
    int ReadDateTimeField(int nWidth, int *nYear, int *nMonth, int *nDay,
                          int *nHour, int *nMinute, int *nSecond, int *nMS);

    int WriteCharField(const char *pszValue, int nWidth, TABINDFile *poINDFile,
                       int nIndexNo);
    int WriteIntegerField(int32_t nValue, TABINDFile *poINDFile, int nIndexNo);
    int WriteSmallIntField(int16_t nValue, TABINDFile *poINDFile, int nIndexNo);
    int WriteLargeIntField(int64_t nValue, TABINDFile *poINDFile, int nIndexNo);
    int WriteFloatField(double dValue, TABINDFile *poINDFile, int nIndexNo);
    int WriteDecimalField(double dValue, int nWidth, int nPrecision,
                          TABINDFile *poINDFile, int nIndexNo);
    int WriteLogicalField(const char *pszValue, TABINDFile *poINDFile,
                          int nIndexNo);
    int WriteDateField(const char *pszValue, TABINDFile *poINDFile,
                       int nIndexNo);
    int WriteDateField(int nYear, int nMonth, int nDay, TABINDFile *poINDFile,
                       int nIndexNo);
    int WriteTimeField(const char *pszValue, TABINDFile *poINDFile,
                       int nIndexNo);
    int WriteTimeField(int nHour, int nMinute, int nSecond, int nMS,
                       TABINDFile *poINDFile, int nIndexNo);
    int WriteDateTimeField(const char *pszValue, TABINDFile *poINDFile,
                           int nIndexNo);
    int WriteDateTimeField(int nYear, int nMonth, int nDay, int nHour,
                           int nMinute, int nSecond, int nMS,
                           TABINDFile *poINDFile, int nIndexNo);

    const CPLString &GetEncoding() const;
    void SetEncoding(const CPLString &);

#ifdef DEBUG
    void Dump(FILE *fpOut = nullptr);
#endif
};

/*---------------------------------------------------------------------
 *                      class TABRelation
 *
 * Class that maintains a relation between 2 tables through a field
 * in each table (the SQL "where table1.field1=table2.field2" found in
 * TABView datasets).
 *
 * An instance of this class is used to read data records from the
 * combined tables as if they were a single one.
 *--------------------------------------------------------------------*/

class TABRelation
{
    CPL_DISALLOW_COPY_ASSIGN(TABRelation)

  private:
    /* Information about the main table.
     */
    TABFile *m_poMainTable;
    char *m_pszMainFieldName;
    int m_nMainFieldNo;

    /* Information about the related table.
     * NOTE: The related field MUST be indexed.
     */
    TABFile *m_poRelTable;
    char *m_pszRelFieldName;
    int m_nRelFieldNo;

    TABINDFile *m_poRelINDFileRef;
    int m_nRelFieldIndexNo;

    int m_nUniqueRecordNo;

    /* Main and Rel table field map:
     * For each field in the source tables, -1 means that the field is not
     * selected, and a value >=0 is the index of this field in the combined
     * FeatureDefn
     */
    int *m_panMainTableFieldMap;
    int *m_panRelTableFieldMap;

    OGRFeatureDefn *m_poDefn;

    void ResetAllMembers();
    GByte *BuildFieldKey(TABFeature *poFeature, int nFieldNo,
                         TABFieldType eType, int nIndexNo);

  public:
    TABRelation();
    ~TABRelation();

    int Init(const char *pszViewName, TABFile *poMainTable, TABFile *poRelTable,
             const char *pszMainFieldName, const char *pszRelFieldName,
             char **papszSelectedFields);
    int CreateRelFields();

    OGRFeatureDefn *GetFeatureDefn()
    {
        return m_poDefn;
    }
    TABFieldType GetNativeFieldType(int nFieldId);
    TABFeature *GetFeature(int nFeatureId);

    int WriteFeature(TABFeature *poFeature, int nFeatureId = -1);

    int SetFeatureDefn(OGRFeatureDefn *poFeatureDefn,
                       TABFieldType *paeMapInfoNativeFieldTypes = nullptr);
    int AddFieldNative(const char *pszName, TABFieldType eMapInfoType,
                       int nWidth = 0, int nPrecision = 0,
                       bool bIndexed = FALSE, bool bUnique = FALSE,
                       int bApproxOK = TRUE);

    int SetFieldIndexed(int nFieldId);
    bool IsFieldIndexed(int nFieldId);
    bool IsFieldUnique(int nFieldId);

    const char *GetMainFieldName()
    {
        return m_pszMainFieldName;
    }
    const char *GetRelFieldName()
    {
        return m_pszRelFieldName;
    }
};

/*---------------------------------------------------------------------
 *                      class MIDDATAFile
 *
 * Class to handle a file pointer with a copy of the latest read line.
 *
 *--------------------------------------------------------------------*/

class MIDDATAFile
{
    CPL_DISALLOW_COPY_ASSIGN(MIDDATAFile)

  public:
    explicit MIDDATAFile(const char *pszEncoding);
    ~MIDDATAFile();

    int Open(const char *pszFname, const char *pszAccess);
    int Close();

    const char *GetLine();
    const char *GetLastLine();
    char **GetTokenizedNextLine();
    int Rewind();
    void SaveLine(const char *pszLine);
    const char *GetSavedLine();
    void WriteLine(const char *, ...) CPL_PRINT_FUNC_FORMAT(2, 3);
    static bool IsValidFeature(const char *pszString);

    //  Translation information
    void SetTranslation(double, double, double, double);
    double GetXTrans(double);
    double GetYTrans(double);
    double GetXMultiplier()
    {
        return m_dfXMultiplier;
    }
    const char *GetDelimiter()
    {
        return m_pszDelimiter;
    }
    void SetDelimiter(const char *pszDelimiter)
    {
        m_pszDelimiter = pszDelimiter;
    }

    void SetEof(bool bEof);
    bool GetEof();

    const CPLString &GetEncoding() const;
    void SetEncoding(const CPLString &);

  private:
    VSILFILE *m_fp;
    const char *m_pszDelimiter;

    std::string m_osLastRead{};
    std::string m_osSavedLine{};

    char *m_pszFname;
    TABAccess m_eAccessMode;
    double m_dfXMultiplier;
    double m_dfYMultiplier;
    double m_dfXDisplacement;
    double m_dfYDisplacement;
    bool m_bEof;
    CPLString m_osEncoding;
};

/*=====================================================================
                        Function prototypes
 =====================================================================*/

TABRawBinBlock *TABCreateMAPBlockFromFile(VSILFILE *fpSrc, int nOffset,
                                          int nSize, bool bHardBlockSize = TRUE,
                                          TABAccess eAccessMode = TABRead);

#endif /* MITAB_PRIV_H_INCLUDED_ */
