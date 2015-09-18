/******************************************************************************
 * $Id$
 *
 * Project:  AmigoCloud Translator
 * Purpose:  Implements OGRAMIGOCLOUDTableLayer class.
 * Author:   Even Rouault, <even dot rouault at mines dash paris dot org>
 *
 ******************************************************************************
 * Copyright (c) 2015, Victor Chernetsky, <victor at amigocloud dot com>
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

#include "ogr_amigocloud.h"
#include "ogr_p.h"
#include "ogr_pgdump.h"
#include <sstream>

CPL_CVSID("$Id$");

/************************************************************************/
/*                    OGRAMIGOCLOUDEscapeIdentifier( )                     */
/************************************************************************/

CPLString OGRAMIGOCLOUDEscapeIdentifier(const char* pszStr)
{
    CPLString osStr;

    osStr += "\"";

    char ch;
    for(int i=0; (ch = pszStr[i]) != '\0'; i++)
    {
        if (ch == '"')
            osStr.append(1, ch);
        osStr.append(1, ch);
    }

    osStr += "\"";

    return osStr;
}

/************************************************************************/
/*                    OGRAMIGOCLOUDEscapeLiteral( )                        */
/************************************************************************/

CPLString OGRAMIGOCLOUDEscapeLiteral(const char* pszStr)
{
    CPLString osStr;

    char ch;
    for(int i=0; (ch = pszStr[i]) != '\0'; i++)
    {
        if (ch == '\'')
            osStr.append(1, ch);
        osStr.append(1, ch);
    }

    return osStr;
}

static std::string url_encode(const std::string &value) {
    std::stringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
        std::string::value_type c = (*i);

        // Keep alphanumeric and other accepted characters intact
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }

        // Any other characters are percent-encoded
        escaped << '%' << int((unsigned char) c);
    }

    return escaped.str();
}

static std::string json_encode(const std::string &value) {
    std::stringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
        std::string::value_type c = (*i);

        if ( c == '"') {
            escaped << "\\\"";
            continue;
        }

        escaped << c;
    }

    return escaped.str();
}

/************************************************************************/
/*                        OGRAMIGOCLOUDTableLayer()                        */
/************************************************************************/

OGRAMIGOCLOUDTableLayer::OGRAMIGOCLOUDTableLayer(OGRAMIGOCLOUDDataSource* poDS,
                                           const char* pszName) :
                                           OGRAMIGOCLOUDLayer(poDS)

{
    osDatasetId = CPLString(pszName);
    osTableName = CPLString("dataset_") + osDatasetId;
    SetDescription( osDatasetId );
    bLaunderColumnNames = TRUE;
    bInDeferedInsert = poDS->DoBatchInsert();
    nNextFID = -1;
    bDeferedCreation = FALSE;
    bAmigoCloudify = FALSE;

    nMaxChunkSize = atoi(CPLGetConfigOption("AMIGOCLOUD_MAX_CHUNK_SIZE", "15")) * 1024 * 1024;
}

/************************************************************************/
/*                    ~OGRAMIGOCLOUDTableLayer()                           */
/************************************************************************/

OGRAMIGOCLOUDTableLayer::~OGRAMIGOCLOUDTableLayer()

{
    if( bDeferedCreation ) RunDeferedCreationIfNecessary();
    FlushDeferedInsert();
}

/************************************************************************/
/*                          GetLayerDefnInternal()                      */
/************************************************************************/

OGRFeatureDefn * OGRAMIGOCLOUDTableLayer::GetLayerDefnInternal(CPL_UNUSED json_object* poObjIn)
{
//    if( poFeatureDefn != NULL )
//    {
//        return poFeatureDefn;
//    }

    if( poFeatureDefn == NULL )
    {
        osBaseSQL.Printf("SELECT * FROM %s", OGRAMIGOCLOUDEscapeIdentifier(osTableName).c_str());
        EstablishLayerDefn(osTableName, NULL);
        osBaseSQL = "";
    }

    if( osFIDColName.size() > 0 )
    {
        CPLString sql;
        sql.Printf("SELECT %s FROM %s", OGRAMIGOCLOUDEscapeIdentifier(osFIDColName).c_str(), OGRAMIGOCLOUDEscapeIdentifier(osTableName).c_str());
        json_object* poObj = poDS->RunSQL(sql);
        if( poObj != NULL && json_object_get_type(poObj) == json_type_object)
        {
            json_object* poRows = json_object_object_get(poObj, "data");

            if(poRows!=NULL && json_object_get_type(poRows) == json_type_array)
            {
                mFIDs.clear();
                for(GIntBig i = 0; i < json_object_array_length(poRows); i++)
                {
                    json_object *obj = json_object_array_get_idx(poRows, i);

                    json_object_iter it;
                    it.key = NULL;
                    it.val = NULL;
                    it.entry = NULL;
                    json_object_object_foreachC(obj, it)
                    {
                        const char *pszColName = it.key;
                        if(it.val != NULL)
                        {
                            if(EQUAL(pszColName, osFIDColName.c_str()))
                            {
                                std::string amigo_id = json_object_get_string(it.val);
                                OGRAmigoCloudFID aFID(amigo_id, iNext);
                                mFIDs[aFID.iFID] = aFID;
                            }
                        }
                    }
                }
            }
            json_object_put(poObj);
        }
    }

    if( osFIDColName.size() > 0 )
    {
        osBaseSQL = "SELECT ";
        osBaseSQL += OGRAMIGOCLOUDEscapeIdentifier(osFIDColName);
    }
    for(int i=0; i<poFeatureDefn->GetGeomFieldCount(); i++)
    {
        if( osBaseSQL.size() == 0 )
            osBaseSQL = "SELECT ";
        else
            osBaseSQL += ", ";
        osBaseSQL += OGRAMIGOCLOUDEscapeIdentifier(poFeatureDefn->GetGeomFieldDefn(i)->GetNameRef());
    }
    for(int i=0; i<poFeatureDefn->GetFieldCount(); i++)
    {
        if( osBaseSQL.size() == 0 )
            osBaseSQL = "SELECT ";
        else
            osBaseSQL += ", ";
        osBaseSQL += OGRAMIGOCLOUDEscapeIdentifier(poFeatureDefn->GetFieldDefn(i)->GetNameRef());
    }
    if( osBaseSQL.size() == 0 )
        osBaseSQL = "SELECT *";
    osBaseSQL += " FROM ";
    osBaseSQL += OGRAMIGOCLOUDEscapeIdentifier(osTableName);

    osSELECTWithoutWHERE = osBaseSQL;

    return poFeatureDefn;
}

/************************************************************************/
/*                        FetchNewFeatures()                            */
/************************************************************************/

json_object* OGRAMIGOCLOUDTableLayer::FetchNewFeatures(GIntBig iNext)
{
    if( osFIDColName.size() > 0 )
    {
        CPLString osSQL;

        if(osWHERE.size() > 0)
        {
            osSQL.Printf("%s WHERE %s ",
                         osSELECTWithoutWHERE.c_str(),
                         (osWHERE.size() > 0) ? CPLSPrintf("%s", osWHERE.c_str()) : "");
        } else
        {
            osSQL.Printf("%s", osSELECTWithoutWHERE.c_str());
        }
        printf("FetchNewFeatures(2) %lld\n", iNext);
        return poDS->RunSQL(osSQL);
    }
    else
        return OGRAMIGOCLOUDLayer::FetchNewFeatures(iNext);
}

/************************************************************************/
/*                           GetNextRawFeature()                        */
/************************************************************************/

OGRFeature  *OGRAMIGOCLOUDTableLayer::GetNextRawFeature()
{
    if( bDeferedCreation && RunDeferedCreationIfNecessary() != OGRERR_NONE )
        return NULL;
    FlushDeferedInsert();
    return OGRAMIGOCLOUDLayer::GetNextRawFeature();
}

/************************************************************************/
/*                         SetAttributeFilter()                         */
/************************************************************************/

OGRErr OGRAMIGOCLOUDTableLayer::SetAttributeFilter( const char *pszQuery )

{
    GetLayerDefn();

    if( pszQuery == NULL )
        osQuery = "";
    else
    {
        osQuery = "(";
        osQuery += pszQuery;
        osQuery += ")";
    }

    BuildWhere();

    ResetReading();

    return OGRERR_NONE;
}

/************************************************************************/
/*                          SetSpatialFilter()                          */
/************************************************************************/

void OGRAMIGOCLOUDTableLayer::SetSpatialFilter( int iGeomField, OGRGeometry * poGeomIn )

{
    if( iGeomField < 0 || iGeomField >= GetLayerDefn()->GetGeomFieldCount() ||
        GetLayerDefn()->GetGeomFieldDefn(iGeomField)->GetType() == wkbNone )
    {
        if( iGeomField != 0 )
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "Invalid geometry field index : %d", iGeomField);
        }
        return;
    }
    m_iGeomFieldFilter = iGeomField;

    if( InstallFilter( poGeomIn ) )
    {
        BuildWhere();

        ResetReading();
    }
}

/************************************************************************/
/*                         FlushDeferedInsert()                          */
/************************************************************************/

void OGRAMIGOCLOUDTableLayer::FlushDeferedInsert()

{
    if(vsDeferedInsertChangesets.size()==0)
        return;

    std::stringstream url;
    url << std::string(poDS->GetAPIURL()) << "/users/0/projects/" + std::string(poDS->GetProjetcId()) + "/datasets/"+ osDatasetId +"/submit_change";

    std::stringstream query;

    query << "{\"type\":\"DML\",\"entity\":\"" << osTableName << "\",";
    query << "\"parent\":null,\"action\":\"INSERT\",\"data\":[";

    int counter=0;
    for(int i=0; i < vsDeferedInsertChangesets.size(); i++)
    {
        if(counter>0)
            query << ",";
        query << vsDeferedInsertChangesets[i].c_str();
        counter++;
    }
    query << "]}";

    std::srand(std::time(0));
    int random_variable = std::rand();

    std::stringstream changeset;
    changeset << "{\"change\": \"" << json_encode(query.str()) << "\"}";

    json_object* poObj = poDS->RunPOST(url.str().c_str(), changeset.str().c_str());
    if( poObj != NULL )
        json_object_put(poObj);

    bInDeferedInsert = FALSE;

    vsDeferedInsertChangesets.clear();

    bInDeferedInsert = FALSE;
    nNextFID = -1;

//    DeleteFeature( 1 );
}

/************************************************************************/
/*                            CreateField()                             */
/************************************************************************/

OGRErr OGRAMIGOCLOUDTableLayer::CreateField( OGRFieldDefn *poFieldIn,
                                          CPL_UNUSED int bApproxOK )
{
    GetLayerDefn();

    if (!poDS->IsReadWrite())
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Operation not available in read-only mode");
        return OGRERR_FAILURE;
    }

    OGRFieldDefn oField(poFieldIn);
    if( bLaunderColumnNames )
    {
        char* pszName = OGRPGCommonLaunderName(oField.GetNameRef());
        oField.SetName(pszName);
        CPLFree(pszName);
    }

/* -------------------------------------------------------------------- */
/*      Create the new field.                                           */
/* -------------------------------------------------------------------- */

    if( !bDeferedCreation )
    {
        CPLString osSQL;
        osSQL.Printf( "ALTER TABLE %s ADD COLUMN %s %s",
                    OGRAMIGOCLOUDEscapeIdentifier(osTableName).c_str(),
                    OGRAMIGOCLOUDEscapeIdentifier(oField.GetNameRef()).c_str(),
                    OGRPGCommonLayerGetType(oField, FALSE, TRUE).c_str() );
        if( !oField.IsNullable() )
            osSQL += " NOT NULL";
        if( oField.GetDefault() != NULL && !oField.IsDefaultDriverSpecific() )
        {
            osSQL += " DEFAULT ";
            osSQL += OGRPGCommonLayerGetPGDefault(&oField);
        }

        json_object* poObj = poDS->RunSQL(osSQL);
        if( poObj == NULL )
            return OGRERR_FAILURE;
        json_object_put(poObj);
    }

    poFeatureDefn->AddFieldDefn( &oField );

    return OGRERR_NONE;
}

/************************************************************************/
/*                           ICreateFeature()                            */
/************************************************************************/

OGRErr OGRAMIGOCLOUDTableLayer::ICreateFeature( OGRFeature *poFeature )

{
    int i;

    if( bDeferedCreation )
    {
        if( RunDeferedCreationIfNecessary() != OGRERR_NONE )
            return OGRERR_FAILURE;
    }

    GetLayerDefn();
    int bHasUserFieldMatchingFID = FALSE;
    if( osFIDColName.size() > 0 )
        bHasUserFieldMatchingFID = poFeatureDefn->GetFieldIndex(osFIDColName) >= 0;

    if (!poDS->IsReadWrite())
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Operation not available in read-only mode");
        return OGRERR_FAILURE;
    }


    std::stringstream record;

    record << "{\"new\":{";

    int counter=0;

    // Add geometry field
    for(i = 0; i < poFeatureDefn->GetGeomFieldCount(); i++)
    {
        if( poFeature->GetGeomFieldRef(i) == NULL )
            continue;

        record << "\"" << OGRAMIGOCLOUDEscapeLiteral(poFeatureDefn->GetGeomFieldDefn(i)->GetNameRef()) << "\":";

        OGRGeometry* poGeom = poFeature->GetGeomFieldRef(i);
        if( poGeom == NULL )
            continue;

        OGRAmigoCloudGeomFieldDefn* poGeomFieldDefn =
                (OGRAmigoCloudGeomFieldDefn *)poFeatureDefn->GetGeomFieldDefn(i);
        int nSRID = poGeomFieldDefn->nSRID;
        if( nSRID == 0 )
            nSRID = 4326;
        char* pszEWKB;
        if( wkbFlatten(poGeom->getGeometryType()) == wkbPolygon &&
            wkbFlatten(GetGeomType()) == wkbMultiPolygon )
        {
            OGRMultiPolygon* poNewGeom = new OGRMultiPolygon();
            poNewGeom->addGeometry(poGeom);
            pszEWKB = OGRGeometryToHexEWKB(poNewGeom, nSRID, FALSE);
            delete poNewGeom;
        }
        else
            pszEWKB = OGRGeometryToHexEWKB(poGeom, nSRID, FALSE);
        record << "\"" << pszEWKB << "\"";
        CPLFree(pszEWKB);

        counter++;
    }

    std::string amigo_id_value;

    // Add non-geometry field
    for(i = 0; i < poFeatureDefn->GetFieldCount(); i++)
    {
        std::string name = poFeatureDefn->GetFieldDefn(i)->GetNameRef();
        std::string value = poFeature->GetFieldAsString(i);

        if(name=="amigo_id")
        {
            amigo_id_value = value;
            continue;
        }

        if(counter > 0)
            record << ",";

        record << OGRAMIGOCLOUDEscapeIdentifier(name.c_str()) << ":";

        if(!value.empty())
        {
            OGRFieldType eType = poFeatureDefn->GetFieldDefn(i)->GetType();
            if( eType == OFTString || eType == OFTDateTime || eType == OFTDate || eType == OFTTime )
            {
                record << "\"" << OGRAMIGOCLOUDEscapeLiteral(value.c_str()) << "\"";
            } else
                record << OGRAMIGOCLOUDEscapeLiteral(value.c_str());
        }
        else
            record << "null";

        counter++;
    }

    record << "},";

    if(!amigo_id_value.empty())
    {
        record << "\"amigo_id\":\"" << amigo_id_value << "\"";
    } else
    {
        record << "\"amigo_id\":null";
    }

    record << "}";

    vsDeferedInsertChangesets.push_back(record.str());

    return OGRERR_NONE;
}

/************************************************************************/
/*                            ISetFeature()                              */
/************************************************************************/

OGRErr OGRAMIGOCLOUDTableLayer::ISetFeature( OGRFeature *poFeature )

{
    int i;

    if( bDeferedCreation && RunDeferedCreationIfNecessary() != OGRERR_NONE )
        return OGRERR_FAILURE;
    FlushDeferedInsert();

    GetLayerDefn();

    if (!poDS->IsReadWrite())
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Operation not available in read-only mode");
        return OGRERR_FAILURE;
    }

    if (poFeature->GetFID() == OGRNullFID)
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "FID required on features given to SetFeature()." );
        return OGRERR_FAILURE;
    }
    
    CPLString osSQL;
    osSQL.Printf("UPDATE %s SET ", OGRAMIGOCLOUDEscapeIdentifier(osTableName).c_str());
    int bMustComma = FALSE;
    for(i = 0; i < poFeatureDefn->GetFieldCount(); i++)
    {
        if( bMustComma )
            osSQL += ", ";
        else
            bMustComma = TRUE;

        osSQL += OGRAMIGOCLOUDEscapeIdentifier(poFeatureDefn->GetFieldDefn(i)->GetNameRef());
        osSQL += " = ";

        if( !poFeature->IsFieldSet(i) )
        {
            osSQL += "NULL";
        }
        else
        {
            OGRFieldType eType = poFeatureDefn->GetFieldDefn(i)->GetType();
            if( eType == OFTString || eType == OFTDateTime || eType == OFTDate || eType == OFTTime )
            {
                osSQL += "'";
                osSQL += OGRAMIGOCLOUDEscapeLiteral(poFeature->GetFieldAsString(i));
                osSQL += "'";
            }
            else if( (eType == OFTInteger || eType == OFTInteger64) &&
                poFeatureDefn->GetFieldDefn(i)->GetSubType() == OFSTBoolean )
            {
                osSQL += poFeature->GetFieldAsInteger(i) ? "'t'" : "'f'";
            }
            else
                osSQL += poFeature->GetFieldAsString(i);
        }
    }

    for(i = 0; i < poFeatureDefn->GetGeomFieldCount(); i++)
    {
        if( bMustComma )
            osSQL += ", ";
        else
            bMustComma = TRUE;

        osSQL += OGRAMIGOCLOUDEscapeIdentifier(poFeatureDefn->GetGeomFieldDefn(i)->GetNameRef());
        osSQL += " = ";

        OGRGeometry* poGeom = poFeature->GetGeomFieldRef(i);
        if( poGeom == NULL )
        {
            osSQL += "NULL";
        }
        else
        {
            OGRAmigoCloudGeomFieldDefn* poGeomFieldDefn =
                (OGRAmigoCloudGeomFieldDefn *)poFeatureDefn->GetGeomFieldDefn(i);
            int nSRID = poGeomFieldDefn->nSRID;
            if( nSRID == 0 )
                nSRID = 4326;
            char* pszEWKB = OGRGeometryToHexEWKB(poGeom, nSRID, FALSE);
            osSQL += "'";
            osSQL += pszEWKB;
            osSQL += "'";
            CPLFree(pszEWKB);
        }
    }

    osSQL += CPLSPrintf(" WHERE %s = " CPL_FRMT_GIB,
                    OGRAMIGOCLOUDEscapeIdentifier(osFIDColName).c_str(),
                    poFeature->GetFID());

    OGRErr eRet = OGRERR_FAILURE;

    printf("ISetFeature() %lld\n", poFeature->GetFID());

    json_object* poObj = poDS->RunSQL(osSQL);
    if( poObj != NULL )
    {
        json_object* poTotalRows = json_object_object_get(poObj, "total_rows");
        if( poTotalRows != NULL && json_object_get_type(poTotalRows) == json_type_int )
        {
            int nTotalRows = json_object_get_int(poTotalRows);
            if( nTotalRows > 0 )
            {
                eRet = OGRERR_NONE;
            }
            else
                eRet = OGRERR_NON_EXISTING_FEATURE;
        }
        json_object_put(poObj);
    }

    return eRet;
}

/************************************************************************/
/*                          DeleteFeature()                             */
/************************************************************************/

OGRErr OGRAMIGOCLOUDTableLayer::DeleteFeature( GIntBig nFID )

{
    OGRErr eRet = OGRERR_FAILURE;

    printf("DeleteFeature: %lld, mFIDs size=%d\n" , nFID, mFIDs.size());

    if( bDeferedCreation && RunDeferedCreationIfNecessary() != OGRERR_NONE )
        return OGRERR_FAILURE;
    FlushDeferedInsert();

    GetLayerDefn();

    if (!poDS->IsReadWrite())
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Operation not available in read-only mode");
        return OGRERR_FAILURE;
    }
    
    if( osFIDColName.size() == 0 )
        return OGRERR_FAILURE;

    std::map<GIntBig, OGRAmigoCloudFID>::iterator it = mFIDs.find(nFID);
    if(it!=mFIDs.end())
    {
        OGRAmigoCloudFID &aFID = it->second;

        CPLString osSQL;
        osSQL.Printf("DELETE FROM %s WHERE %s = '%s'" ,
                     OGRAMIGOCLOUDEscapeIdentifier(osTableName).c_str(),
                     OGRAMIGOCLOUDEscapeIdentifier(osFIDColName).c_str(),
                     aFID.amigo_id.c_str());

        std::stringstream changeset;
        changeset << "{\"query\": \"" << json_encode(osSQL) << "\"}";
        std::stringstream url;
        url << std::string(poDS->GetAPIURL()) << "/users/0/projects/" + std::string(poDS->GetProjetcId()) + "/sql";
        json_object *poObj = poDS->RunPOST(url.str().c_str(), changeset.str().c_str());
//        json_object *poObj = poDS->RunSQL(osSQL);
        if(poObj != NULL)
        {
//            json_object *poTotalRows = json_object_object_get(poObj, "total_rows");
//            if(poTotalRows != NULL && json_object_get_type(poTotalRows) == json_type_int)
//            {
//                int nTotalRows = json_object_get_int(poTotalRows);
//                if(nTotalRows > 0)
//                {
//                    eRet = OGRERR_NONE;
//                }
//                else
//                    eRet = OGRERR_NON_EXISTING_FEATURE;
//            }
            json_object_put(poObj);
            eRet = OGRERR_NONE;
        }
    } else
    {
        printf("DeleteFeature: nFID:%lld not found\n" , nFID, mFIDs.size());
    }
    return eRet;
}

/************************************************************************/
/*                             GetSRS_SQL()                             */
/************************************************************************/

CPLString OGRAMIGOCLOUDTableLayer::GetSRS_SQL(const char* pszGeomCol)
{
    CPLString osSQL;

    osSQL.Printf("SELECT srid, srtext FROM spatial_ref_sys WHERE srid IN "
                "(SELECT Find_SRID('%s', '%s', '%s'))",
                OGRAMIGOCLOUDEscapeLiteral(poDS->GetCurrentSchema()).c_str(),
                OGRAMIGOCLOUDEscapeLiteral(osTableName).c_str(),
                OGRAMIGOCLOUDEscapeLiteral(pszGeomCol).c_str());

    return osSQL;
}

/************************************************************************/
/*                             BuildWhere()                             */
/*                                                                      */
/*      Build the WHERE statement appropriate to the current set of     */
/*      criteria (spatial and attribute queries).                       */
/************************************************************************/

void OGRAMIGOCLOUDTableLayer::BuildWhere()

{
    osWHERE = "";

    if( m_poFilterGeom != NULL &&
        m_iGeomFieldFilter >= 0 &&
        m_iGeomFieldFilter < poFeatureDefn->GetGeomFieldCount() )
    {
        OGREnvelope  sEnvelope;

        m_poFilterGeom->getEnvelope( &sEnvelope );

        CPLString osGeomColumn(poFeatureDefn->GetGeomFieldDefn(m_iGeomFieldFilter)->GetNameRef());

        char szBox3D_1[128];
        char szBox3D_2[128];
        char* pszComma;

        CPLsnprintf(szBox3D_1, sizeof(szBox3D_1), "%.18g %.18g", sEnvelope.MinX, sEnvelope.MinY);
        while((pszComma = strchr(szBox3D_1, ',')) != NULL)
            *pszComma = '.';
        CPLsnprintf(szBox3D_2, sizeof(szBox3D_2), "%.18g %.18g", sEnvelope.MaxX, sEnvelope.MaxY);
        while((pszComma = strchr(szBox3D_2, ',')) != NULL)
            *pszComma = '.';
        osWHERE.Printf("(%s && 'BOX3D(%s, %s)'::box3d)",
                       OGRAMIGOCLOUDEscapeIdentifier(osGeomColumn).c_str(),
                       szBox3D_1, szBox3D_2 );
    }

    if( strlen(osQuery) > 0 )
    {
        if( osWHERE.size() > 0 )
            osWHERE += " AND ";
        osWHERE += osQuery;
    }

    if( osFIDColName.size() == 0 )
    {
        osBaseSQL = osSELECTWithoutWHERE;
        if( osWHERE.size() > 0 )
        {
            osBaseSQL += " WHERE ";
            osBaseSQL += osWHERE;
        }
    }
}

/************************************************************************/
/*                              GetFeature()                            */
/************************************************************************/

OGRFeature* OGRAMIGOCLOUDTableLayer::GetFeature( GIntBig nFeatureId )
{

    if( bDeferedCreation && RunDeferedCreationIfNecessary() != OGRERR_NONE )
        return NULL;
    FlushDeferedInsert();

    GetLayerDefn();
    
    if( osFIDColName.size() == 0 )
        return OGRAMIGOCLOUDLayer::GetFeature(nFeatureId);

    CPLString osSQL = osSELECTWithoutWHERE;
    osSQL += " WHERE ";
    osSQL += OGRAMIGOCLOUDEscapeIdentifier(osFIDColName).c_str();
    osSQL += " = ";
    osSQL += CPLSPrintf(CPL_FRMT_GIB, nFeatureId);

    printf("GetFeature() %lld\n", nFeatureId);
    json_object* poObj = poDS->RunSQL(osSQL);
    json_object* poRowObj = OGRAMIGOCLOUDGetSingleRow(poObj);
    if( poRowObj == NULL )
    {
        if( poObj != NULL )
            json_object_put(poObj);
        return OGRAMIGOCLOUDLayer::GetFeature(nFeatureId);
    }

    OGRFeature* poFeature = BuildFeature(poRowObj);
    json_object_put(poObj);

    return poFeature;
}

/************************************************************************/
/*                          GetFeatureCount()                           */
/************************************************************************/

GIntBig OGRAMIGOCLOUDTableLayer::GetFeatureCount(int bForce)
{

    if( bDeferedCreation && RunDeferedCreationIfNecessary() != OGRERR_NONE )
        return 0;
    FlushDeferedInsert();

    GetLayerDefn();

    CPLString osSQL(CPLSPrintf("SELECT COUNT(*) FROM %s",
                               OGRAMIGOCLOUDEscapeIdentifier(osTableName).c_str()));
    if( osWHERE.size() > 0 )
    {
        osSQL += " WHERE ";
        osSQL += osWHERE;
    }

    json_object* poObj = poDS->RunSQL(osSQL);
    json_object* poRowObj = OGRAMIGOCLOUDGetSingleRow(poObj);
    if( poRowObj == NULL )
    {
        if( poObj != NULL )
            json_object_put(poObj);
        return OGRAMIGOCLOUDLayer::GetFeatureCount(bForce);
    }

    json_object* poCount = json_object_object_get(poRowObj, "count");
    if( poCount == NULL || json_object_get_type(poCount) != json_type_int )
    {
        json_object_put(poObj);
        return OGRAMIGOCLOUDLayer::GetFeatureCount(bForce);
    }

    GIntBig nRet = (GIntBig)json_object_get_int64(poCount);

    json_object_put(poObj);

    return nRet;
}

/************************************************************************/
/*                             GetExtent()                              */
/*                                                                      */
/*      For PostGIS use internal Extend(geometry) function              */
/*      in other cases we use standard OGRLayer::GetExtent()            */
/************************************************************************/

OGRErr OGRAMIGOCLOUDTableLayer::GetExtent( int iGeomField, OGREnvelope *psExtent, int bForce )
{
    CPLString   osSQL;

    if( bDeferedCreation && RunDeferedCreationIfNecessary() != OGRERR_NONE )
        return OGRERR_FAILURE;
    FlushDeferedInsert();

    if( iGeomField < 0 || iGeomField >= GetLayerDefn()->GetGeomFieldCount() ||
        GetLayerDefn()->GetGeomFieldDefn(iGeomField)->GetType() == wkbNone )
    {
        if( iGeomField != 0 )
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "Invalid geometry field index : %d", iGeomField);
        }
        return OGRERR_FAILURE;
    }

    OGRGeomFieldDefn* poGeomFieldDefn =
        poFeatureDefn->GetGeomFieldDefn(iGeomField);

    /* Do not take the spatial filter into account */
    osSQL.Printf( "SELECT ST_Extent(%s) FROM %s",
                  OGRAMIGOCLOUDEscapeIdentifier(poGeomFieldDefn->GetNameRef()).c_str(),
                  OGRAMIGOCLOUDEscapeIdentifier(osTableName).c_str());

    json_object* poObj = poDS->RunSQL(osSQL);
    json_object* poRowObj = OGRAMIGOCLOUDGetSingleRow(poObj);
    if( poRowObj != NULL )
    {
        json_object* poExtent = json_object_object_get(poRowObj, "st_extent");
        if( poExtent != NULL && json_object_get_type(poExtent) == json_type_string )
        {
            const char* pszBox = json_object_get_string(poExtent);
            const char * ptr, *ptrEndParenthesis;
            char szVals[64*6+6];

            ptr = strchr(pszBox, '(');
            if (ptr)
                ptr ++;
            if (ptr == NULL ||
                (ptrEndParenthesis = strchr(ptr, ')')) == NULL ||
                ptrEndParenthesis - ptr > (int)(sizeof(szVals) - 1))
            {
                CPLError( CE_Failure, CPLE_IllegalArg,
                            "Bad extent representation: '%s'", pszBox);

                json_object_put(poObj);
                return OGRERR_FAILURE;
            }

            strncpy(szVals,ptr,ptrEndParenthesis - ptr);
            szVals[ptrEndParenthesis - ptr] = '\0';

            char ** papszTokens = CSLTokenizeString2(szVals," ,",CSLT_HONOURSTRINGS);
            int nTokenCnt = 4;

            if ( CSLCount(papszTokens) != nTokenCnt )
            {
                CPLError( CE_Failure, CPLE_IllegalArg,
                            "Bad extent representation: '%s'", pszBox);
                CSLDestroy(papszTokens);

                json_object_put(poObj);
                return OGRERR_FAILURE;
            }

            // Take X,Y coords
            // For PostGis ver >= 1.0.0 -> Tokens: X1 Y1 X2 Y2 (nTokenCnt = 4)
            // For PostGIS ver < 1.0.0 -> Tokens: X1 Y1 Z1 X2 Y2 Z2 (nTokenCnt = 6)
            // =>   X2 index calculated as nTokenCnt/2
            //      Y2 index caluclated as nTokenCnt/2+1
            
            psExtent->MinX = CPLAtof( papszTokens[0] );
            psExtent->MinY = CPLAtof( papszTokens[1] );
            psExtent->MaxX = CPLAtof( papszTokens[nTokenCnt/2] );
            psExtent->MaxY = CPLAtof( papszTokens[nTokenCnt/2+1] );

            CSLDestroy(papszTokens);

            json_object_put(poObj);
            return OGRERR_NONE;
        }
    }

    if( poObj != NULL )
        json_object_put(poObj);

    if( iGeomField == 0 )
        return OGRLayer::GetExtent( psExtent, bForce );
    else
        return OGRLayer::GetExtent( iGeomField, psExtent, bForce );
}

/************************************************************************/
/*                           TestCapability()                           */
/************************************************************************/

int OGRAMIGOCLOUDTableLayer::TestCapability( const char * pszCap )

{
    if( EQUAL(pszCap, OLCFastFeatureCount) )
        return TRUE;
    if( EQUAL(pszCap, OLCFastGetExtent) )
        return TRUE;
    if( EQUAL(pszCap, OLCRandomRead) )
    {
        GetLayerDefn();
        return osFIDColName.size() != 0;
    }

    if( EQUAL(pszCap,OLCSequentialWrite)
     || EQUAL(pszCap,OLCRandomWrite)
     || EQUAL(pszCap,OLCDeleteFeature)
     || EQUAL(pszCap,OLCCreateField) )
    {
        return poDS->IsReadWrite();
    }

    return OGRAMIGOCLOUDLayer::TestCapability(pszCap);
}

/************************************************************************/
/*                        SetDeferedCreation()                          */
/************************************************************************/

void OGRAMIGOCLOUDTableLayer::SetDeferedCreation(OGRwkbGeometryType eGType,
                                     OGRSpatialReference *poSRS,
                                     int bGeomNullable,
                                     int bAmigoCloudify)
{
    bDeferedCreation = TRUE;
    nNextFID = 1;
    CPLAssert(poFeatureDefn == NULL);
    this->bAmigoCloudify = bAmigoCloudify;
    poFeatureDefn = new OGRFeatureDefn(osTableName);
    poFeatureDefn->Reference();
    poFeatureDefn->SetGeomType(wkbNone);
    if( eGType == wkbPolygon )
        eGType = wkbMultiPolygon;
    else if( eGType == wkbPolygon25D )
        eGType = wkbMultiPolygon25D;
    if( eGType != wkbNone )
    {
        OGRAmigoCloudGeomFieldDefn *poFieldDefn =
            new OGRAmigoCloudGeomFieldDefn("wkb_geometry", eGType);
        poFieldDefn->SetNullable(bGeomNullable);
        poFeatureDefn->AddGeomFieldDefn(poFieldDefn, FALSE);
        if( poSRS != NULL )
        {
            poFieldDefn->nSRID = poDS->FetchSRSId( poSRS );
            poFeatureDefn->GetGeomFieldDefn(
                poFeatureDefn->GetGeomFieldCount() - 1)->SetSpatialRef(poSRS);
        }
    }

    osBaseSQL.Printf("SELECT * FROM %s",
                     OGRAMIGOCLOUDEscapeIdentifier(osTableName).c_str());
}

CPLString OGRAMIGOCLOUDTableLayer::GetAmigoCloudType(OGRFieldDefn& oField,
                                  int bPreservePrecision,
                                  int bApproxOK)
{
    char                szFieldType[256];

/* -------------------------------------------------------------------- */
/*      AmigoCloud supported types.                                   */
/* -------------------------------------------------------------------- */
    if( oField.GetType() == OFTInteger ||  oField.GetType() == OFTInteger64 )
    {
            strcpy( szFieldType, "integer" );
    }
    else if( oField.GetType() == OFTReal )
    {
       strcpy( szFieldType, "float" );
    }
    else if( oField.GetType() == OFTString )
    {
        strcpy( szFieldType, "string");
    }
    else if( oField.GetType() == OFTDate )
    {
        strcpy( szFieldType, "date" );
    }
    else if( oField.GetType() == OFTTime )
    {
        strcpy( szFieldType, "time" );
    }
    else if( oField.GetType() == OFTDateTime )
    {
        strcpy( szFieldType, "timestamp with time zone" );
    }
    else if( oField.GetType() == OFTBinary )
    {
        strcpy( szFieldType, "bytea" );
    }
    else if( bApproxOK )
    {
        CPLError( CE_Warning, CPLE_NotSupported,
                  "Can't create field %s with type %s on PostgreSQL layers.  Creating as VARCHAR.",
                  oField.GetNameRef(),
                  OGRFieldDefn::GetFieldTypeName(oField.GetType()) );
        strcpy( szFieldType, "VARCHAR" );
    }
    else
    {
        CPLError( CE_Failure, CPLE_NotSupported,
                  "Can't create field %s with type %s on PostgreSQL layers.",
                  oField.GetNameRef(),
                  OGRFieldDefn::GetFieldTypeName(oField.GetType()) );
        strcpy( szFieldType, "");
    }

    return szFieldType;
}

bool OGRAMIGOCLOUDTableLayer::IsDatasetExists()
{
    std::stringstream url;

    url << std::string(poDS->GetAPIURL()) << "/users/0/projects/" + std::string(poDS->GetProjetcId()) + "/datasets/"+ osDatasetId;

    json_object* result = poDS->RunGET(url.str().c_str());
    if( result == NULL )
        return false;

    if( result != NULL )
    {
        int type = json_object_get_type(result);
        if(type == json_type_object)
        {
            json_object *poId = json_object_object_get(result, "id");
            if(poId != NULL)
            {
                json_object_put(result);
                return true;
            }
        }
        json_object_put(result);
    }

    // Sleep 1 sec
    usleep(1000);

    return false;
}

/************************************************************************/
/*                      RunDeferedCreationIfNecessary()                 */
/************************************************************************/

OGRErr OGRAMIGOCLOUDTableLayer::RunDeferedCreationIfNecessary()
{
    if( !bDeferedCreation )
        return OGRERR_NONE;
    bDeferedCreation = FALSE;

    std::stringstream json;

    json << "{ \"name\":\"" << osDatasetId << "\",";

    json << "\"schema\": \"[";

    int counter=0;

    OGRwkbGeometryType eGType = GetGeomType();
    if( eGType != wkbNone )
    {
        CPLString osGeomType = OGRToOGCGeomType(eGType);
        if( wkbHasZ(eGType) )
            osGeomType += "Z";

        OGRAmigoCloudGeomFieldDefn *poFieldDefn =
                (OGRAmigoCloudGeomFieldDefn *)poFeatureDefn->GetGeomFieldDefn(0);

        json << "{\\\"name\\\":\\\"" << poFieldDefn->GetNameRef() << "\\\",";
        json << "\\\"type\\\":\\\"geometry\\\",";
        json << "\\\"geometry_type\\\":\\\"" << osGeomType << "\\\",";

        if( !poFieldDefn->IsNullable() )
            json << "\\\"nullable\\\":false,";
        else
            json << "\\\"nullable\\\":true,";

        json << "\\\"visible\\\": true}";

        counter++;
    }

    for( int i = 0; i < poFeatureDefn->GetFieldCount(); i++ )
    {
        OGRFieldDefn* poFieldDefn = poFeatureDefn->GetFieldDefn(i);
        if( strcmp(poFieldDefn->GetNameRef(), osFIDColName) != 0 )
        {
            if(counter>0)
                json << ",";

            json << "{\\\"name\\\":\\\"" << poFieldDefn->GetNameRef() << "\\\",";
            json << "\\\"type\\\":\\\"" << GetAmigoCloudType(*poFieldDefn, FALSE, TRUE) << "\\\",";
            if( !poFieldDefn->IsNullable() )
                json << "\\\"nullable\\\":false,";
            else
                json << "\\\"nullable\\\":true,";

            if( poFieldDefn->GetDefault() != NULL && !poFieldDefn->IsDefaultDriverSpecific() )
            {
                json << "\\\"default\\\":\\\"" << poFieldDefn->GetDefault() << "\\\",";
            }
            json << "\\\"visible\\\": true}";
            counter++;
        }
    }

    json << " ] \" }";

    std::stringstream url;
    url << std::string(poDS->GetAPIURL()) << "/users/0/projects/" + std::string(poDS->GetProjetcId()) + "/datasets/create";

    json_object* result = poDS->RunPOST(url.str().c_str(), json.str().c_str());
    if( result != NULL )
    {
        if(json_object_get_type(result) == json_type_object)
        {
            json_object *poId = json_object_object_get(result, "id");
            if(poId!=NULL)
            {
                osTableName = CPLString("dataset_") + json_object_to_json_string(poId);
                osDatasetId = json_object_to_json_string(poId);
                int retry=10;
                while(!IsDatasetExists() && retry >= 0)
                {
                    retry--;
                }
                json_object_put(result);
                return OGRERR_NONE;
            }
        }
    }
    return OGRERR_FAILURE;
}
