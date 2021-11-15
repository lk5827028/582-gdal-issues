﻿/******************************************************************************
 *
 * Project:  SAP HANA Spatial Driver
 * Purpose:  OGRHanaTableLayer class implementation
 * Author:   Maxim Rylov
 *
 ******************************************************************************
 * Copyright (c) 2020, SAP SE
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

#include "ogr_hana.h"
#include "ogrhanafeaturereader.h"
#include "ogrhanautils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <regex>

#include "odbc/Exception.h"
#include "odbc/ResultSet.h"
#include "odbc/PreparedStatement.h"
#include "odbc/Types.h"

CPL_CVSID("$Id$")

using namespace hana_utils;

namespace {

constexpr const char* UNSUPPORTED_OP_READ_ONLY =
    "%s : unsupported operation on a read-only datasource.";

bool IsArrayField(OGRFieldType fieldType)
{
    return (
        fieldType == OFTIntegerList || fieldType == OFTInteger64List
        || fieldType == OFTRealList || fieldType == OFTStringList);
}

const char* GetColumnDefaultValue(const OGRFieldDefn& field)
{
    const char* defaultValue = field.GetDefault();
    if (field.GetType() == OFTInteger && field.GetSubType() == OFSTBoolean)
        return (EQUAL(defaultValue, "1") || EQUAL(defaultValue, "'t'"))
                   ? "TRUE"
                   : "FALSE";
    return defaultValue;
}

CPLString GetParameterValue(short type, const CPLString& typeName, bool isArray)
{
    if (isArray)
    {
        CPLString arrayType = "STRING";
        switch (type)
        {
        case odbc::SQLDataTypes::TinyInt:
            arrayType = "TINYINT";
            break;
        case odbc::SQLDataTypes::SmallInt:
            arrayType = "SMALLINT";
            break;
        case odbc::SQLDataTypes::Integer:
            arrayType = "INT";
            break;
        case odbc::SQLDataTypes::BigInt:
            arrayType = "BIGINT";
            break;
        case odbc::SQLDataTypes::Float:
        case odbc::SQLDataTypes::Real:
            arrayType = "REAL";
            break;
        case odbc::SQLDataTypes::Double:
            arrayType = "DOUBLE";
            break;
        case odbc::SQLDataTypes::WVarChar:
            arrayType = "STRING";
            break;
        }
        return "ARRAY(SELECT * FROM OGR_PARSE_" + arrayType + "_ARRAY(?, '"
               + ARRAY_VALUES_DELIMITER + "'))";
    }
    else if (typeName.compare("NCLOB") == 0)
        return "TO_NCLOB(?)";
    else if (typeName.compare("CLOB") == 0)
        return "TO_CLOB(?)";
    else if (typeName.compare("BLOB") == 0)
        return "TO_BLOB(?)";
    else
        return "?";
}

std::vector<int> ParseIntValues(const char* str)
{
    std::vector<int> values;
    std::stringstream stream(str);
    while (stream.good())
    {
        std::string value;
        getline(stream, value, ',');
        values.push_back(std::atoi(value.c_str()));
    }
    return values;
}

FieldTypeInfo ParseFieldTypeInfo(const CPLString& typeDef)
{
    auto incorrectFormatErr = [&]() {
        CPLError(
            CE_Failure, CPLE_NotSupported,
            "Column type '%s' has incorrect format.", typeDef.c_str());
    };

    CPLString typeName;
    std::vector<int> typeSize;

    if (std::strstr(typeDef, "(") == nullptr)
    {
        typeName = typeDef;
    }
    else
    {
        const auto regex = std::regex(R"((\w+)+\((\d+(,\d+)*)\)$)");
        std::smatch match;
        std::regex_search(typeDef, match, regex);

        if (match.size() != 0)
        {
            typeName.assign(match[1]);
            typeSize = ParseIntValues(match[2].str().c_str());
        }

        if (typeSize.empty() || typeSize.size() > 2)
        {
            incorrectFormatErr();
            return {"", UNKNOWN_DATA_TYPE, 0, 0};
        }
    }

    if (EQUAL(typeName.c_str(), "BOOLEAN"))
        return {typeName, odbc::SQLDataTypes::Boolean, 0, 0};
    else if (EQUAL(typeName.c_str(), "TINYINT"))
        return {typeName, odbc::SQLDataTypes::TinyInt, 0, 0};
    else if (EQUAL(typeName.c_str(), "SMALLINT"))
        return {typeName, odbc::SQLDataTypes::SmallInt, 0, 0};
    else if (EQUAL(typeName.c_str(), "INTEGER"))
        return {typeName, odbc::SQLDataTypes::Integer, 0, 0};
    else if (EQUAL(typeName.c_str(), "DECIMAL"))
    {
        switch (typeSize.size())
        {
        case 0:
            return {typeName, odbc::SQLDataTypes::Decimal, 0, 0};
        case 1:
            return {typeName, odbc::SQLDataTypes::Decimal, typeSize[0], 0};
        case 2:
            return {typeName, odbc::SQLDataTypes::Decimal, typeSize[0],
                    typeSize[1]};
        }
    }
    else if (EQUAL(typeName.c_str(), "FLOAT"))
    {
        switch (typeSize.size())
        {
        case 0:
            return {typeName, odbc::SQLDataTypes::Float, 10, 0};
        case 1:
            return {typeName, odbc::SQLDataTypes::Float, typeSize[0], 0};
        case 2:
            incorrectFormatErr();
            return {"", UNKNOWN_DATA_TYPE, 0, 0};
        }
    }
    else if (EQUAL(typeName.c_str(), "REAL"))
        return {typeName, odbc::SQLDataTypes::Real, 0, 0};
    else if (EQUAL(typeName.c_str(), "DOUBLE"))
        return {typeName, odbc::SQLDataTypes::Double, 0, 0};
    else if (EQUAL(typeName.c_str(), "VARCHAR"))
    {
        switch (typeSize.size())
        {
        case 0:
            return {typeName, odbc::SQLDataTypes::VarChar, 1, 0};
        case 1:
            return {typeName, odbc::SQLDataTypes::VarChar, typeSize[0], 0};
        case 2:
            incorrectFormatErr();
            return {"", UNKNOWN_DATA_TYPE, 0, 0};
        }
    }
    else if (EQUAL(typeName.c_str(), "NVARCHAR"))
    {
        switch (typeSize.size())
        {
        case 0:
            return {typeName, odbc::SQLDataTypes::WVarChar, 1, 0};
        case 1:
            return {typeName, odbc::SQLDataTypes::WVarChar, typeSize[0], 0};
        case 2:
            incorrectFormatErr();
            return {"", UNKNOWN_DATA_TYPE, 0, 0};
        }
    }
    else if (EQUAL(typeName.c_str(), "NCLOB"))
        return {typeName, odbc::SQLDataTypes::WLongVarChar, 0, 0};
    else if (EQUAL(typeName.c_str(), "DATE"))
        return {typeName, odbc::SQLDataTypes::Date, 0, 0};
    else if (EQUAL(typeName.c_str(), "TIME"))
        return {typeName, odbc::SQLDataTypes::Time, 0, 0};
    else if (EQUAL(typeName.c_str(), "TIMESTAMP"))
        return {typeName, odbc::SQLDataTypes::Timestamp, 0, 0};
    else if (EQUAL(typeName.c_str(), "VARBINARY"))
    {
        switch (typeSize.size())
        {
        case 0:
            return {typeName, odbc::SQLDataTypes::VarBinary, 1, 0};
        case 1:
            return {typeName, odbc::SQLDataTypes::VarBinary, typeSize[0], 0};
        case 2:
            incorrectFormatErr();
            return {"", UNKNOWN_DATA_TYPE, 0, 0};
        }
    }
    else if (EQUAL(typeName.c_str(), "BLOB"))
        return {typeName, odbc::SQLDataTypes::LongVarBinary, 0, 0};

    CPLError(
        CE_Failure, CPLE_NotSupported, "Unknown column type '%s'.",
        typeName.c_str());
    return {typeName, UNKNOWN_DATA_TYPE, 0, 0};
}

void SetFieldDefn(OGRFieldDefn& field, const FieldTypeInfo& typeInfo)
{
    auto isArray = [&typeInfo]() {
        return std::strstr(typeInfo.name, "ARRAY") != nullptr;
    };

    switch (typeInfo.type)
    {
    case odbc::SQLDataTypes::Bit:
    case odbc::SQLDataTypes::Boolean:
        field.SetType(OFTInteger);
        field.SetSubType(OFSTBoolean);
        break;
    case odbc::SQLDataTypes::TinyInt:
    case odbc::SQLDataTypes::SmallInt:
        field.SetType(isArray() ? OFTIntegerList : OFTInteger);
        field.SetSubType(OFSTInt16);
        break;
    case odbc::SQLDataTypes::Integer:
        field.SetType(isArray() ? OFTIntegerList : OFTInteger);
        break;
    case odbc::SQLDataTypes::BigInt:
        field.SetType(isArray() ? OFTInteger64List : OFTInteger64);
        break;
    case odbc::SQLDataTypes::Double:
    case odbc::SQLDataTypes::Real:
    case odbc::SQLDataTypes::Float:
        field.SetType(isArray() ? OFTRealList : OFTReal);
        if (typeInfo.type != odbc::SQLDataTypes::Double)
            field.SetSubType(OFSTFloat32);
        break;
    case odbc::SQLDataTypes::Decimal:
    case odbc::SQLDataTypes::Numeric:
        field.SetType(isArray() ? OFTRealList : OFTReal);
        break;
    case odbc::SQLDataTypes::Char:
    case odbc::SQLDataTypes::VarChar:
    case odbc::SQLDataTypes::LongVarChar:
        field.SetType(isArray() ? OFTStringList : OFTString);
        break;
    case odbc::SQLDataTypes::WChar:
    case odbc::SQLDataTypes::WVarChar:
    case odbc::SQLDataTypes::WLongVarChar:
        field.SetType(isArray() ? OFTStringList : OFTString);
        break;
    case odbc::SQLDataTypes::Date:
    case odbc::SQLDataTypes::TypeDate:
        field.SetType(OFTDate);
        break;
    case odbc::SQLDataTypes::Time:
    case odbc::SQLDataTypes::TypeTime:
        field.SetType(OFTTime);
        break;
    case odbc::SQLDataTypes::Timestamp:
    case odbc::SQLDataTypes::TypeTimestamp:
        field.SetType(OFTDateTime);
        break;
    case odbc::SQLDataTypes::Binary:
    case odbc::SQLDataTypes::VarBinary:
    case odbc::SQLDataTypes::LongVarBinary:
        field.SetType(OFTBinary);
        break;
    default:
        break;
    }

    field.SetWidth(typeInfo.width);
    field.SetPrecision(typeInfo.precision);
}

} // namespace

/************************************************************************/
/*                         OGRHanaTableLayer()                          */
/************************************************************************/

OGRHanaTableLayer::OGRHanaTableLayer(OGRHanaDataSource* datasource, int update)
    : OGRHanaLayer(datasource)
    , updateMode_(update)
    , batchSize_(4 * 1024)
    , defaultStringSize_(256)
    , launderColumnNames_(true)
    , preservePrecision_(true)
    , parseFunctionsChecked_(false)
{
}

/************************************************************************/
/*                        ~OGRHanaTableLayer()                          */
/************************************************************************/

OGRHanaTableLayer::~OGRHanaTableLayer()
{
    FlushPendingFeatures();
}

/************************************************************************/
/*                        ReadTableDefinition()                         */
/************************************************************************/

OGRErr OGRHanaTableLayer::ReadTableDefinition()
{
    OGRErr err = ReadFeatureDefinition(
        schemaName_, tableName_, rawQuery_, tableName_.c_str());
    if (err != OGRERR_NONE)
        return err;

    if (fidFieldIndex_ != OGRNullFID)
        CPLDebug(
            "HANA", "table %s has FID column %s.", tableName_.c_str(),
            attrColumns_[static_cast<size_t>(fidFieldIndex_)].name.c_str());
    else
        CPLDebug(
            "HANA", "table %s has no FID column, FIDs will not be reliable!",
            tableName_.c_str());

    return OGRERR_NONE;
}

/* -------------------------------------------------------------------- */
/*                                 ExecuteUpdate()                      */
/* -------------------------------------------------------------------- */

std::pair<OGRErr, std::size_t> OGRHanaTableLayer::ExecuteUpdate(
    odbc::PreparedStatement& statement, bool withBatch,  const char* functionName)
{
    std::size_t ret = 0;

    try
    {
        if (withBatch)
        {
            if (statement.getBatchDataSize() >= batchSize_)
                statement.executeBatch();
            ret = 1;
        }
        else
        {
            ret = statement.executeUpdate();
        }

        if (!dataSource_->IsTransactionStarted())
            dataSource_->Commit();
    }
    catch (odbc::Exception& ex)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined, "Failed to execute %s: %s",
            functionName, ex.what());
        return {OGRERR_FAILURE, 0};
    }

    return {OGRERR_NONE, ret};
}

/* -------------------------------------------------------------------- */
/*                   CreateDeleteFeatureStatement()                     */
/* -------------------------------------------------------------------- */

odbc::PreparedStatementRef OGRHanaTableLayer::CreateDeleteFeatureStatement()
{
    CPLString sql = StringFormat(
        "DELETE FROM %s WHERE %s = ?",
        GetFullTableNameQuoted(schemaName_, tableName_).c_str(),
        QuotedIdentifier(GetFIDColumn()).c_str());
    return dataSource_->PrepareStatement(sql.c_str());
}

/* -------------------------------------------------------------------- */
/*                   CreateInsertFeatureStatement()                     */
/* -------------------------------------------------------------------- */

odbc::PreparedStatementRef OGRHanaTableLayer::CreateInsertFeatureStatement(bool withFID)
{
    std::vector<CPLString> columns;
    std::vector<CPLString> values;
    bool hasArray = false;
    for (const AttributeColumnDescription& clmDesc : attrColumns_)
    {
        if (clmDesc.isFeatureID && !withFID)
        {
            if (clmDesc.isAutoIncrement)
                continue;
        }

        columns.push_back(QuotedIdentifier(clmDesc.name));
        values.push_back(
            GetParameterValue(clmDesc.type, clmDesc.typeName, clmDesc.isArray));
        if (clmDesc.isArray)
            hasArray = true;
    }

    for (const GeometryColumnDescription& geomClmDesc : geomColumns_)
    {
        columns.push_back(QuotedIdentifier(geomClmDesc.name));
        values.push_back(
            "ST_GeomFromWKB(? , " + std::to_string(geomClmDesc.srid) + ")");
    }

    if (hasArray && !parseFunctionsChecked_)
    {
        // Create helper functions if needed.
        if (!dataSource_->ParseArrayFunctionsExist(schemaName_.c_str()))
            dataSource_->CreateParseArrayFunctions(schemaName_.c_str());
        parseFunctionsChecked_ = true;
    }

    const CPLString sql = StringFormat(
        "INSERT INTO %s (%s) VALUES(%s)",
        GetFullTableNameQuoted(schemaName_, tableName_).c_str(),
        JoinStrings(columns, ", ").c_str(), JoinStrings(values, ", ").c_str());

    return dataSource_->PrepareStatement(sql.c_str());
}

/* -------------------------------------------------------------------- */
/*                     CreateUpdateFeatureStatement()                   */
/* -------------------------------------------------------------------- */

odbc::PreparedStatementRef OGRHanaTableLayer::CreateUpdateFeatureStatement()
{
    std::vector<CPLString> values;
    values.reserve(attrColumns_.size());
    bool hasArray = false;

    for (const AttributeColumnDescription& clmDesc : attrColumns_)
    {
        if (clmDesc.isFeatureID)
        {
            if (clmDesc.isAutoIncrement)
                continue;
        }
        values.push_back(
            QuotedIdentifier(clmDesc.name) + " = "
            + GetParameterValue(
                clmDesc.type, clmDesc.typeName, clmDesc.isArray));
        if (clmDesc.isArray)
            hasArray = true;
    }

    for (const GeometryColumnDescription& geomClmDesc : geomColumns_)
    {
        values.push_back(
            QuotedIdentifier(geomClmDesc.name) + " = " + "ST_GeomFromWKB(?, "
            + std::to_string(geomClmDesc.srid) + ")");
    }

    if (hasArray && !parseFunctionsChecked_)
    {
        // Create helper functions if needed.
        if (!dataSource_->ParseArrayFunctionsExist(schemaName_.c_str()))
            dataSource_->CreateParseArrayFunctions(schemaName_.c_str());
        parseFunctionsChecked_ = true;
    }

    const CPLString sql = StringFormat(
        "UPDATE %s SET %s WHERE %s = ?",
        GetFullTableNameQuoted(schemaName_, tableName_).c_str(),
        JoinStrings(values, ", ").c_str(),
        QuotedIdentifier(GetFIDColumn()).c_str());

    return dataSource_->PrepareStatement(sql.c_str());
}

/* -------------------------------------------------------------------- */
/*                     ResetPreparedStatements()                        */
/* -------------------------------------------------------------------- */

void OGRHanaTableLayer::ResetPreparedStatements()
{
    if (!currentIdentityValueStmt_.isNull())
        currentIdentityValueStmt_ = nullptr;
    if (!insertFeatureStmtWithFID_.isNull())
        insertFeatureStmtWithFID_ = nullptr;
    if (!insertFeatureStmtWithoutFID_.isNull())
        insertFeatureStmtWithoutFID_ = nullptr;
    if (!deleteFeatureStmt_.isNull())
        deleteFeatureStmt_ = nullptr;
    if (!updateFeatureStmt_.isNull())
        updateFeatureStmt_ = nullptr;
}

/************************************************************************/
/*                        SetStatementParameters()                      */
/************************************************************************/

OGRErr OGRHanaTableLayer::SetStatementParameters(
    odbc::PreparedStatement& statement,
    OGRFeature* feature,
    bool newFeature,
    bool withFID,
    const char* functionName)
{
    OGRHanaFeatureReader featReader(*feature);

    unsigned short paramIndex = 0;
    int fieldIndex = -1;
    for (const AttributeColumnDescription& clmDesc : attrColumns_)
    {
        if (clmDesc.isFeatureID)
        {
            if (!withFID && clmDesc.isAutoIncrement)
                continue;

            ++paramIndex;

            switch (clmDesc.type)
            {
            case odbc::SQLDataTypes::Integer:
                if (feature->GetFID() == OGRNullFID)
                    statement.setInt(paramIndex, odbc::Int());
                else
                {
                    if (!CanCastIntBigTo<std ::int32_t>(feature->GetFID()))
                    {
                        CPLError(
                            CE_Failure, CPLE_AppDefined,
                            "%s: Feature id with value %s cannot "
                            "be stored in a column of type INTEGER",
                            functionName,
                            std::to_string(feature->GetFID()).c_str());
                        return OGRERR_FAILURE;
                    }

                    statement.setInt(
                        paramIndex,
                        odbc::Int(static_cast<std::int32_t>(feature->GetFID())));
                }
                break;
            case odbc::SQLDataTypes::BigInt:
                if (feature->GetFID() == OGRNullFID)
                    statement.setLong(paramIndex, odbc::Long());
                else
                {
                    if (!CanCastIntBigTo<std::int64_t>(feature->GetFID()))
                    {
                        CPLError(
                            CE_Failure, CPLE_AppDefined,
                            "%s: Feature id with value %s cannot "
                            "be stored in a column of type BIGINT",
                            functionName,
                            std::to_string(feature->GetFID()).c_str());
                        return OGRERR_FAILURE;
                    }
                    statement.setLong(
                        paramIndex,
                        odbc::Long(static_cast<std::int64_t>(feature->GetFID())));
                }
                break;
            default:
                CPLError(
                    CE_Failure, CPLE_AppDefined,
                    "%s: Unexpected type ('%s') in the field "
                    "'%s'",
                    functionName, std::to_string(clmDesc.type).c_str(),
                    clmDesc.name.c_str());
                return OGRERR_FAILURE;
            }
            continue;
        }
        else
            ++paramIndex;

        ++fieldIndex;

        switch (clmDesc.type)
        {
        case odbc::SQLDataTypes::Bit:
        case odbc::SQLDataTypes::Boolean:
            statement.setBoolean(
                paramIndex, featReader.GetFieldAsBoolean(fieldIndex));
            break;
        case odbc::SQLDataTypes::TinyInt:
            if (clmDesc.isArray)
                statement.setString(
                    paramIndex, featReader.GetFieldAsIntArray(fieldIndex));
            else
                statement.setByte(
                    paramIndex, featReader.GetFieldAsByte(fieldIndex));
            break;
        case odbc::SQLDataTypes::SmallInt:
            if (clmDesc.isArray)
                statement.setString(
                    paramIndex, featReader.GetFieldAsIntArray(fieldIndex));
            else
                statement.setShort(
                    paramIndex, featReader.GetFieldAsShort(fieldIndex));
            break;
        case odbc::SQLDataTypes::Integer:
            if (clmDesc.isArray)
                statement.setString(
                    paramIndex, featReader.GetFieldAsIntArray(fieldIndex));
            else
                statement.setInt(
                    paramIndex, featReader.GetFieldAsInt(fieldIndex));
            break;
        case odbc::SQLDataTypes::BigInt:
            if (clmDesc.isArray)
                statement.setString(
                    paramIndex, featReader.GetFieldAsBigIntArray(fieldIndex));
            else
                statement.setLong(
                    paramIndex, featReader.GetFieldAsLong(fieldIndex));
            break;
        case odbc::SQLDataTypes::Float:
        case odbc::SQLDataTypes::Real:
            if (clmDesc.isArray)
                statement.setString(
                    paramIndex, featReader.GetFieldAsRealArray(fieldIndex));
            else
                statement.setFloat(
                    paramIndex, featReader.GetFieldAsFloat(fieldIndex));
            break;
        case odbc::SQLDataTypes::Double:
            if (clmDesc.isArray)
                statement.setString(
                    paramIndex, featReader.GetFieldAsDoubleArray(fieldIndex));
            else
                statement.setDouble(
                    paramIndex, featReader.GetFieldAsDouble(fieldIndex));
            break;
        case odbc::SQLDataTypes::Decimal:
        case odbc::SQLDataTypes::Numeric:
            if ((!feature->IsFieldSet(fieldIndex)
                 || feature->IsFieldNull(fieldIndex))
                && feature->GetFieldDefnRef(fieldIndex)->GetDefault()
                       == nullptr)
                statement.setDecimal(paramIndex, odbc::Decimal());
            else
                statement.setDouble(
                    paramIndex, featReader.GetFieldAsDouble(fieldIndex));
            break;
        case odbc::SQLDataTypes::Char:
        case odbc::SQLDataTypes::VarChar:
        case odbc::SQLDataTypes::LongVarChar:
            if (clmDesc.isArray)
                statement.setString(
                    paramIndex, featReader.GetFieldAsStringArray(fieldIndex));
            else
                statement.setString(
                    paramIndex,
                    featReader.GetFieldAsString(fieldIndex, clmDesc.length));
            break;
        case odbc::SQLDataTypes::WChar:
        case odbc::SQLDataTypes::WVarChar:
        case odbc::SQLDataTypes::WLongVarChar:
            if (clmDesc.isArray)
                statement.setString(
                    paramIndex, featReader.GetFieldAsStringArray(fieldIndex));
            else
                statement.setString(
                    paramIndex,
                    featReader.GetFieldAsNString(fieldIndex, clmDesc.length));
            break;
        case odbc::SQLDataTypes::Binary:
        case odbc::SQLDataTypes::VarBinary:
        case odbc::SQLDataTypes::LongVarBinary: {
            Binary bin = featReader.GetFieldAsBinary(fieldIndex);
            statement.setBytes(paramIndex, bin.data, bin.size);
        }
        break;
        case odbc::SQLDataTypes::DateTime:
        case odbc::SQLDataTypes::TypeDate:
            statement.setDate(
                paramIndex, featReader.GetFieldAsDate(fieldIndex));
            break;
        case odbc::SQLDataTypes::Time:
        case odbc::SQLDataTypes::TypeTime:
            statement.setTime(
                paramIndex, featReader.GetFieldAsTime(fieldIndex));
            break;
        case odbc::SQLDataTypes::Timestamp:
        case odbc::SQLDataTypes::TypeTimestamp:
            statement.setTimestamp(
                paramIndex, featReader.GetFieldAsTimestamp(fieldIndex));
            break;
        }
    }

    for (std::size_t i = 0; i < geomColumns_.size(); ++i)
    {
        ++paramIndex;
        Binary wkb{nullptr, 0};
        OGRErr err = GetGeometryWkb(feature, static_cast<int>(i), wkb);
        if (OGRERR_NONE != err)
            return err;
        statement.setBytes(paramIndex, wkb.data, wkb.size);
    }

    if (!newFeature)
    {
        ++paramIndex;
        if (!CanCastIntBigTo<std::int64_t>(feature->GetFID()))
        {
            CPLError(
                CE_Failure, CPLE_AppDefined,
                "%s: Feature id with value %s cannot "
                "be stored in a column of type INTEGER",
                functionName, std::to_string(feature->GetFID()).c_str());
            return OGRERR_FAILURE;
        }

        statement.setLong(
            paramIndex,
            odbc::Long(static_cast<std::int64_t>(feature->GetFID())));
    }

    return OGRERR_NONE;
}

/* -------------------------------------------------------------------- */
/*                            DropTable()                               */
/* -------------------------------------------------------------------- */

OGRErr OGRHanaTableLayer::DropTable()
{
    CPLString sql =
        "DROP TABLE " + GetFullTableNameQuoted(schemaName_, tableName_);
    try
    {
        dataSource_->ExecuteSQL(sql.c_str());
        CPLDebug("HANA", "Dropped table %s.", GetName());
    }
    catch (const odbc::Exception& ex)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined, "Unable to delete layer '%s': %s",
            tableName_.c_str(), ex.what());
        return OGRERR_FAILURE;
    }

    return OGRERR_NONE;
}

/* -------------------------------------------------------------------- */
/*                        FlushPendingFeatures()                        */
/* -------------------------------------------------------------------- */

void OGRHanaTableLayer::FlushPendingFeatures()
{
    if (HasPendingFeatures())
        dataSource_->Commit();
}

/* -------------------------------------------------------------------- */
/*                        HasPendingFeatures()                          */
/* -------------------------------------------------------------------- */

bool OGRHanaTableLayer::HasPendingFeatures() const
{
    return (!deleteFeatureStmt_.isNull()
            && deleteFeatureStmt_->getBatchDataSize() > 0)
            || (!insertFeatureStmtWithFID_.isNull()
                && insertFeatureStmtWithFID_->getBatchDataSize() > 0)
           || (!insertFeatureStmtWithoutFID_.isNull()
               && insertFeatureStmtWithoutFID_->getBatchDataSize() > 0)
           || (!updateFeatureStmt_.isNull()
               && updateFeatureStmt_->getBatchDataSize() > 0);
}

/* -------------------------------------------------------------------- */
/*                            GetFieldTypeInfo()                        */
/* -------------------------------------------------------------------- */

FieldTypeInfo OGRHanaTableLayer::GetFieldTypeInfo(OGRFieldDefn& field) const
{
    for (const auto& clmType : customColumnDefs_)
    {
        if (EQUAL(clmType.name.c_str(), field.GetNameRef()))
            return ParseFieldTypeInfo(clmType.typeDef);
    }

    switch (field.GetType())
    {
    case OFTInteger:
        if (preservePrecision_ && field.GetWidth() > 10)
        {
            return {StringFormat("DECIMAL(%d)", field.GetWidth()),
                    odbc::SQLDataTypes::Decimal, field.GetWidth(), 0};
        }
        else
        {
            if (field.GetSubType() == OFSTBoolean)
                return {"BOOLEAN", odbc::SQLDataTypes::Boolean,
                        field.GetWidth(), 0};
            else if (field.GetSubType() == OFSTInt16)
                return {"SMALLINT", odbc::SQLDataTypes::SmallInt,
                        field.GetWidth(), 0};
            else
                return {"INTEGER", odbc::SQLDataTypes::Integer,
                        field.GetWidth(), 0};
        }
        break;
    case OFTInteger64:
        if (preservePrecision_ && field.GetWidth() > 20)
        {
            return {StringFormat("DECIMAL(%d)", field.GetWidth()),
                    odbc::SQLDataTypes::Decimal, field.GetWidth(), 0};
        }
        else
            return {"BIGINT", odbc::SQLDataTypes::BigInt, field.GetWidth(), 0};
        break;
    case OFTReal:
        if (preservePrecision_ && field.GetWidth() != 0)
        {
            return {
                StringFormat(
                    "DECIMAL(%d,%d)", field.GetWidth(), field.GetPrecision()),
                odbc::SQLDataTypes::Decimal, field.GetWidth(),
                field.GetPrecision()};
        }
        else
        {
            if (field.GetSubType() == OFSTFloat32)
                return {"REAL", odbc::SQLDataTypes::Real, field.GetWidth(),
                        field.GetPrecision()};
            else
                return {"DOUBLE", odbc::SQLDataTypes::Double, field.GetWidth(),
                        field.GetPrecision()};
        }
    case OFTString:
        if (field.GetWidth() == 0 || !preservePrecision_)
        {
            int width = static_cast<int>(defaultStringSize_);
            CPLString fieldTypeName =
                (width == 0) ? "NVARCHAR" : StringFormat("NVARCHAR(%d)", width);
            return {fieldTypeName, odbc::SQLDataTypes::WLongVarChar, width, 0};
        }
        else
        {
            if (field.GetWidth() <= 5000)
                return {StringFormat("NVARCHAR(%d)", field.GetWidth()),
                        odbc::SQLDataTypes::WLongVarChar, field.GetWidth(), 0};
            else
                return {"NCLOB", odbc::SQLDataTypes::WLongVarChar,
                        field.GetWidth(), 0};
        }
    case OFTBinary:
        if (field.GetWidth() <= 5000)
        {
            CPLString fieldTypeName =
                (field.GetWidth() == 0)
                    ? "VARBINARY"
                    : StringFormat("VARBINARY(%d)", field.GetWidth());
            return {fieldTypeName, odbc::SQLDataTypes::VarBinary,
                    field.GetWidth(), 0};
        }
        else
            return {"BLOB", odbc::SQLDataTypes::LongVarBinary, field.GetWidth(),
                    0};
    case OFTDate:
        return {"DATE", odbc::SQLDataTypes::TypeDate, field.GetWidth(), 0};
    case OFTTime:
        return {"TIME", odbc::SQLDataTypes::TypeTime, field.GetWidth(), 0};
    case OFTDateTime:
        return {"TIMESTAMP", odbc::SQLDataTypes::TypeTimestamp,
                field.GetWidth(), 0};
    case OFTIntegerList:
        if (field.GetSubType() == OGRFieldSubType::OFSTInt16)
            return {"SMALLINT ARRAY", odbc::SQLDataTypes::SmallInt,
                    field.GetWidth(), 0};
        else
            return {"INTEGER ARRAY", odbc::SQLDataTypes::Integer,
                    field.GetWidth(), 0};
    case OFTInteger64List:
        return {"BIGINT ARRAY", odbc::SQLDataTypes::BigInt, field.GetWidth(),
                0};
    case OFTRealList:
        if (field.GetSubType() == OGRFieldSubType::OFSTFloat32)
            return {"REAL ARRAY", odbc::SQLDataTypes::Real, field.GetWidth(),
                    0};
        else
            return {"DOUBLE ARRAY", odbc::SQLDataTypes::Double,
                    field.GetWidth(), 0};
        break;
    case OFTStringList:
        return {"NVARCHAR(512) ARRAY", odbc::SQLDataTypes::WVarChar, 512, 0};
    default:
        break;
    }

    return {"", UNKNOWN_DATA_TYPE, 0, 0};
}

/* -------------------------------------------------------------------- */
/*                           GetGeometryWkb()                           */
/* -------------------------------------------------------------------- */
OGRErr OGRHanaTableLayer::GetGeometryWkb(
    OGRFeature* feature, int fieldIndex, Binary& binary)
{
    OGRGeometry* geom = feature->GetGeomFieldRef(fieldIndex);
    if (geom == nullptr || !IsGeometryTypeSupported(geom->getIsoGeometryType()))
        return OGRERR_NONE;

    // Rings must be closed, otherwise HANA throws an exception
    geom->closeRings();
    std::size_t size = static_cast<std::size_t>(geom->WkbSize());
    EnsureBufferCapacity(size);
    unsigned char* data = reinterpret_cast<unsigned char*>(dataBuffer_.data());
    OGRErr err = geom->exportToWkb(
        OGRwkbByteOrder::wkbNDR, data, OGRwkbVariant::wkbVariantIso);
    if (OGRERR_NONE == err)
    {
        binary.data = data;
        binary.size = size;
    }
    return err;
}

/* -------------------------------------------------------------------- */
/*                            Initialize()                               */
/* -------------------------------------------------------------------- */

OGRErr OGRHanaTableLayer::Initialize(
    const char* schemaName, const char* tableName)
{
    schemaName_ = schemaName;
    tableName_ = tableName;
    rawQuery_ =
        "SELECT * FROM " + GetFullTableNameQuoted(schemaName, tableName);

    OGRErr err = ReadTableDefinition();
    if (err != OGRERR_NONE)
        return err;

    SetDescription(featureDefn_->GetName());

    ResetReading();
    return OGRERR_NONE;
}

/************************************************************************/
/*                              ResetReading()                          */
/************************************************************************/

void OGRHanaTableLayer::ResetReading()
{
    FlushPendingFeatures();

    OGRHanaLayer::ResetReading();
}

/************************************************************************/
/*                            TestCapability()                          */
/************************************************************************/

int OGRHanaTableLayer::TestCapability(const char* capabilities)
{
    if (EQUAL(capabilities, OLCRandomRead))
        return fidFieldIndex_ != OGRNullFID;
    if (EQUAL(capabilities, OLCFastFeatureCount))
        return TRUE;
    if (EQUAL(capabilities, OLCFastSpatialFilter))
        return !geomColumns_.empty();
    if (EQUAL(capabilities, OLCFastGetExtent))
        return !geomColumns_.empty();
    if (EQUAL(capabilities, OLCCreateField))
        return updateMode_;
    if (EQUAL(capabilities, OLCCreateGeomField)
        || EQUAL(capabilities, ODsCCreateGeomFieldAfterCreateLayer))
        return updateMode_;
    if (EQUAL(capabilities, OLCDeleteField))
        return updateMode_;
    if (EQUAL(capabilities, OLCDeleteFeature))
        return updateMode_ && fidFieldIndex_ != OGRNullFID;
    if (EQUAL(capabilities, OLCAlterFieldDefn))
        return updateMode_;
    if (EQUAL(capabilities, OLCRandomWrite))
        return updateMode_;
    if (EQUAL(capabilities, OLCMeasuredGeometries))
        return TRUE;
    if (EQUAL(capabilities, OLCSequentialWrite))
        return updateMode_;
    if (EQUAL(capabilities, OLCTransactions))
        return updateMode_;

    return FALSE;
}

/************************************************************************/
/*                          ICreateFeature()                            */
/************************************************************************/

OGRErr OGRHanaTableLayer::ICreateFeature(OGRFeature* feature)
{
    if (!updateMode_)
    {
        CPLError(
            CE_Failure, CPLE_NotSupported, UNSUPPORTED_OP_READ_ONLY,
            "CreateFeature");
        return OGRERR_FAILURE;
    }

    if( nullptr == feature )
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "NULL pointer to OGRFeature passed to CreateFeature()." );
        return OGRERR_FAILURE;
    }

    GIntBig nFID = feature->GetFID();
    bool withFID = nFID != OGRNullFID;
    bool withBatch = withFID && dataSource_->IsTransactionStarted();

    try
    {
        odbc::PreparedStatementRef& stmt = withFID ? insertFeatureStmtWithFID_ : insertFeatureStmtWithoutFID_;

        if (stmt.isNull())
        {
            stmt = CreateInsertFeatureStatement(withFID);
            if (stmt.isNull())
                return OGRERR_FAILURE;
        }

        OGRErr err = SetStatementParameters(*stmt, feature, true, withFID, "CreateFeature");

        if (OGRERR_NONE != err)
            return err;

        if (withBatch)
            stmt->addBatch();

        auto ret = ExecuteUpdate(*stmt, withBatch, "CreateFeature");

        err = ret.first;
        if (OGRERR_NONE != err)
            return err;

        if (!withFID)
        {
            const CPLString sql = StringFormat(
                "SELECT CURRENT_IDENTITY_VALUE() \"current identity value\" FROM %s",
                GetFullTableNameQuoted(schemaName_, tableName_).c_str());

            if (currentIdentityValueStmt_.isNull())
                currentIdentityValueStmt_ = dataSource_->PrepareStatement(sql.c_str());

            odbc::ResultSetRef rsIdentity = currentIdentityValueStmt_->executeQuery();
            if ( rsIdentity->next() )
            {
              odbc::Long id = rsIdentity->getLong( 1 );
              if ( !id.isNull() )
                feature->SetFID(static_cast<GIntBig>( *id ) );
            }
            rsIdentity->close();
        }

        return err;
    }
    catch (const std::exception& ex)
    {
        CPLError( CE_Failure, CPLE_NotSupported,
                  "Unable to create feature: %s", ex.what());
        return OGRERR_FAILURE;
    }
    return OGRERR_NONE;
}

/************************************************************************/
/*                           DeleteFeature()                            */
/************************************************************************/

OGRErr OGRHanaTableLayer::DeleteFeature(GIntBig nFID)
{
    if (!updateMode_)
    {
        CPLError(
            CE_Failure, CPLE_NotSupported, UNSUPPORTED_OP_READ_ONLY,
            "DeleteFeature");
        return OGRERR_FAILURE;
    }

    if (nFID == OGRNullFID)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined,
            "DeleteFeature(" CPL_FRMT_GIB
            ") failed.  Unable to delete features "
            "in tables without\n a recognised FID column.",
            nFID);
        return OGRERR_FAILURE;
    }

    if (OGRNullFID == fidFieldIndex_)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined,
            "DeleteFeature(" CPL_FRMT_GIB
            ") failed.  Unable to delete features "
            "in tables without\n a recognised FID column.",
            nFID);
        return OGRERR_FAILURE;
    }

    if (deleteFeatureStmt_.isNull())
    {
        deleteFeatureStmt_ = CreateDeleteFeatureStatement();
        if (deleteFeatureStmt_.isNull())
            return OGRERR_FAILURE;
    }

    deleteFeatureStmt_->setLong(1, odbc::Long(static_cast<std::int64_t>(nFID)));
    bool withBatch = dataSource_->IsTransactionStarted();
    if (withBatch)
       deleteFeatureStmt_->addBatch();

    auto ret = ExecuteUpdate(*deleteFeatureStmt_, withBatch, "DeleteFeature");
    return (OGRERR_NONE == ret.first && ret.second != 1)
               ? OGRERR_NON_EXISTING_FEATURE
               : ret.first;
}

/************************************************************************/
/*                             ISetFeature()                            */
/************************************************************************/

OGRErr OGRHanaTableLayer::ISetFeature(OGRFeature* feature)
{
    if (!updateMode_)
    {
        CPLError(
            CE_Failure, CPLE_NotSupported, UNSUPPORTED_OP_READ_ONLY,
            "SetFeature");
        return OGRERR_FAILURE;
    }

    if( nullptr == feature )
    {
        CPLError( CE_Failure, CPLE_AppDefined,
                  "NULL pointer to OGRFeature passed to SetFeature()." );
        return OGRERR_FAILURE;
    }

    if (feature->GetFID() == OGRNullFID)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined,
            "FID required on features given to SetFeature().");
        return OGRERR_FAILURE;
    }

    if (nullptr == feature)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined,
            "NULL pointer to OGRFeature passed to SetFeature().");
        return OGRERR_FAILURE;
    }

    if (OGRNullFID == fidFieldIndex_)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined,
            "Unable to update features in tables without\n"
            "a recognised FID column.");
        return OGRERR_FAILURE;
    }

    if (updateFeatureStmt_.isNull())
    {
        updateFeatureStmt_ = CreateUpdateFeatureStatement();
        if (updateFeatureStmt_.isNull())
            return OGRERR_FAILURE;
    }

    OGRErr err = SetStatementParameters(
        *updateFeatureStmt_, feature, false, false, "SetFeature");

    if (OGRERR_NONE != err)
        return err;

    bool withBatch = dataSource_->IsTransactionStarted();
    if (withBatch)
        updateFeatureStmt_->addBatch();

    auto ret = ExecuteUpdate(*updateFeatureStmt_, withBatch, "SetFeature");
    return (OGRERR_NONE == ret.first && ret.second != 1)
               ? OGRERR_NON_EXISTING_FEATURE
               : ret.first;
}

/************************************************************************/
/*                            CreateField()                             */
/************************************************************************/

OGRErr OGRHanaTableLayer::CreateField(OGRFieldDefn* srsField, int approxOK)
{
    if (!updateMode_)
    {
        CPLError(
            CE_Failure, CPLE_NotSupported, UNSUPPORTED_OP_READ_ONLY,
            "CreateField");
        return OGRERR_FAILURE;
    }

    OGRFieldDefn dstField(srsField);

    if (launderColumnNames_)
        dstField.SetName(LaunderName(dstField.GetNameRef()).c_str());

    if (fidFieldIndex_ != OGRNullFID
        && EQUAL(dstField.GetNameRef(), GetFIDColumn())
        && dstField.GetType() != OFTInteger
        && dstField.GetType() != OFTInteger64)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined, "Wrong field type for %s",
            dstField.GetNameRef());
        return OGRERR_FAILURE;
    }

    FieldTypeInfo fieldTypeInfo = GetFieldTypeInfo(dstField);

    if (fieldTypeInfo.type == UNKNOWN_DATA_TYPE)
    {
        if (fieldTypeInfo.name.empty())
            return OGRERR_FAILURE;

        if (approxOK)
        {
            dstField.SetDefault(nullptr);
            CPLError(
                CE_Warning, CPLE_NotSupported,
                "Unable to create field %s with type %s on HANA layers. "
                "Creating as VARCHAR.",
                dstField.GetNameRef(),
                OGRFieldDefn::GetFieldTypeName(dstField.GetType()));
            fieldTypeInfo.name =
                "VARCHAR(" + std::to_string(defaultStringSize_) + ")";
            fieldTypeInfo.width = static_cast<int>(defaultStringSize_);
        }
        else
        {
            CPLError(
                CE_Failure, CPLE_NotSupported,
                "Unable to create field %s with type %s on HANA layers.",
                dstField.GetNameRef(),
                OGRFieldDefn::GetFieldTypeName(dstField.GetType()));

            return OGRERR_FAILURE;
        }
    }

    CPLString clmClause =
        QuotedIdentifier(dstField.GetNameRef()) + " " + fieldTypeInfo.name;
    if (!dstField.IsNullable())
        clmClause += " NOT NULL";
    if (dstField.GetDefault() != nullptr && !dstField.IsDefaultDriverSpecific())
    {
        if (IsArrayField(dstField.GetType())
            || fieldTypeInfo.type == odbc::SQLDataTypes::LongVarBinary)
        {
            CPLError(
                CE_Failure, CPLE_NotSupported,
                "Default value cannot be created on column of data type %s: "
                "%s.",
                fieldTypeInfo.name.c_str(), dstField.GetNameRef());

            return OGRERR_FAILURE;
        }

        clmClause +=
            StringFormat(" DEFAULT %s", GetColumnDefaultValue(dstField));
    }

    const CPLString sql = StringFormat(
        "ALTER TABLE %s ADD(%s)",
        GetFullTableNameQuoted(schemaName_, tableName_).c_str(),
        clmClause.c_str());

    try
    {
        dataSource_->ExecuteSQL(sql.c_str());
    }
    catch (const odbc::Exception& ex)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined,
            "Failed to execute create attribute field %s: %s",
            dstField.GetNameRef(), ex.what());
        return OGRERR_FAILURE;
    }

    // fieldTypeInfo might contain a different defintion due to custom column types
    SetFieldDefn(dstField, fieldTypeInfo);

    AttributeColumnDescription clmDesc;
    clmDesc.name = dstField.GetNameRef();
    clmDesc.type = fieldTypeInfo.type;
    clmDesc.typeName = fieldTypeInfo.name;
    clmDesc.isArray = IsArrayField(dstField.GetType());
    clmDesc.length = fieldTypeInfo.width;
    clmDesc.isNullable = dstField.IsNullable();
    clmDesc.isAutoIncrement = false; // TODO
    clmDesc.scale = static_cast<unsigned short>(fieldTypeInfo.precision);
    clmDesc.precision = static_cast<unsigned short>(fieldTypeInfo.width);
    if (dstField.GetDefault() != nullptr)
        clmDesc.defaultValue = dstField.GetDefault();

    featureDefn_->AddFieldDefn(&dstField);
    attrColumns_.push_back(clmDesc);

    rebuildQueryStatement_ = true;
    ResetPreparedStatements();
    ResetReading();

    return OGRERR_NONE;
}

/************************************************************************/
/*                          CreateGeomField()                           */
/************************************************************************/

OGRErr OGRHanaTableLayer::CreateGeomField(OGRGeomFieldDefn* geomField, int)
{
    if (!updateMode_)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined, UNSUPPORTED_OP_READ_ONLY,
            "CreateGeomField");
        return OGRERR_FAILURE;
    }

    if (EQUALN(geomField->GetNameRef(), "OGR_GEOMETRY", strlen("OGR_GEOMETRY")))
        return OGRERR_NONE;

    CPLString clmName = (launderColumnNames_)
                            ? LaunderName(geomField->GetNameRef())
                            : CPLString(geomField->GetNameRef());
    int srid = dataSource_->GetSrsId(geomField->GetSpatialRef());
    CPLString sql = StringFormat(
        "ALTER TABLE %s ADD(%s ST_GEOMETRY(%d))",
        GetFullTableNameQuoted(schemaName_, tableName_).c_str(),
        QuotedIdentifier(clmName).c_str(), srid);

    if (!IsGeometryTypeSupported(geomField->GetType()))
    {
        CPLError(
            CE_Warning, CPLE_NotSupported,
            "Geometry field '%s' in layer '%s' has unsupported type %s",
            clmName.c_str(), tableName_.c_str(),
            OGRGeometryTypeToName(geomField->GetType()));
    }

    try
    {
        dataSource_->ExecuteSQL(sql.c_str());
    }
    catch (const odbc::Exception& ex)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined,
            "Failed to execute create geometry field %s: %s",
            geomField->GetNameRef(), ex.what());
        return OGRERR_FAILURE;
    }

    auto newGeomField = cpl::make_unique<OGRGeomFieldDefn>(
                clmName.c_str(), geomField->GetType());
    newGeomField->SetNullable(geomField->IsNullable());
    newGeomField->SetSpatialRef(geomField->GetSpatialRef());
    featureDefn_->AddGeomFieldDefn(std::move(newGeomField));
    geomColumns_.push_back(
        {clmName, geomField->GetType(), srid, geomField->IsNullable() != 0});

    ResetPreparedStatements();

    return OGRERR_NONE;
}

/************************************************************************/
/*                            DeleteField()                             */
/************************************************************************/

OGRErr OGRHanaTableLayer::DeleteField(int field)
{
    if (!updateMode_)
    {
        CPLError(
            CE_Failure, CPLE_NotSupported, UNSUPPORTED_OP_READ_ONLY,
            "DeleteField");
        return OGRERR_FAILURE;
    }

    if (field < 0 || field >= featureDefn_->GetFieldCount())
    {
        CPLError(CE_Failure, CPLE_NotSupported, "Field index is out of range");
        return OGRERR_FAILURE;
    }

    CPLString clmName = featureDefn_->GetFieldDefn(field)->GetNameRef();
    CPLString sql = StringFormat(
        "ALTER TABLE %s DROP (%s)",
        GetFullTableNameQuoted(schemaName_, tableName_).c_str(),
        QuotedIdentifier(clmName).c_str());

    try
    {
        dataSource_->ExecuteSQL(sql.c_str());
    }
    catch (const odbc::Exception& ex)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined, "Failed to drop column %s: %s",
            clmName.c_str(), ex.what());
        return OGRERR_FAILURE;
    }

    auto it = std::find_if(
        attrColumns_.begin(), attrColumns_.end(),
        [&](const AttributeColumnDescription& cd) {
            return cd.name == clmName;
        });
    attrColumns_.erase(it);
    OGRErr ret = featureDefn_->DeleteFieldDefn(field);

    ResetPreparedStatements();

    return ret;
}

/************************************************************************/
/*                            AlterFieldDefn()                          */
/************************************************************************/

OGRErr OGRHanaTableLayer::AlterFieldDefn(
    int field, OGRFieldDefn* newFieldDefn, int flagsIn)
{
    if (!updateMode_)
    {
        CPLError(
            CE_Failure, CPLE_NotSupported, UNSUPPORTED_OP_READ_ONLY,
            "AlterFieldDefn");
        return OGRERR_FAILURE;
    }

    if (field < 0 || field >= featureDefn_->GetFieldCount())
    {
        CPLError(CE_Failure, CPLE_NotSupported, "Field index is out of range");
        return OGRERR_FAILURE;
    }

    OGRFieldDefn* fieldDefn = featureDefn_->GetFieldDefn(field);
    CPLString clmName = launderColumnNames_
                            ? LaunderName(newFieldDefn->GetNameRef())
                            : CPLString(newFieldDefn->GetNameRef());

    try
    {
        if ((flagsIn & ALTER_NAME_FLAG)
            && (strcmp(fieldDefn->GetNameRef(), newFieldDefn->GetNameRef())
                != 0))
        {
            CPLString sql = StringFormat(
                "RENAME COLUMN %s TO %s",
                GetFullColumnNameQuoted(
                    schemaName_, tableName_, fieldDefn->GetNameRef())
                    .c_str(),
                QuotedIdentifier(clmName).c_str());
            dataSource_->ExecuteSQL(sql.c_str());
        }

        if ((flagsIn & ALTER_TYPE_FLAG)
            || (flagsIn & ALTER_WIDTH_PRECISION_FLAG)
            || (flagsIn & ALTER_NULLABLE_FLAG)
            || (flagsIn & ALTER_DEFAULT_FLAG))
        {
            CPLString fieldTypeInfo = GetFieldTypeInfo(*newFieldDefn).name;
            if ((flagsIn & ALTER_NULLABLE_FLAG)
                && fieldDefn->IsNullable() != newFieldDefn->IsNullable())
            {
                if (fieldDefn->IsNullable())
                    fieldTypeInfo += " NULL";
                else
                    fieldTypeInfo += " NOT NULL";
            }

            if ((flagsIn & ALTER_DEFAULT_FLAG)
                && ((fieldDefn->GetDefault() == nullptr
                     && newFieldDefn->GetDefault() != nullptr)
                    || (fieldDefn->GetDefault() != nullptr
                        && newFieldDefn->GetDefault() == nullptr)
                    || (fieldDefn->GetDefault() != nullptr
                        && newFieldDefn->GetDefault() != nullptr
                        && strcmp(
                               fieldDefn->GetDefault(),
                               newFieldDefn->GetDefault())
                               != 0)))
            {
                fieldTypeInfo +=
                    " DEFAULT "
                    + ((fieldDefn->GetType() == OFTString)
                           ? Literal(newFieldDefn->GetDefault())
                           : CPLString(newFieldDefn->GetDefault()));
            }

            CPLString sql = StringFormat(
                "ALTER TABLE %s ALTER(%s %s)",
                GetFullTableNameQuoted(schemaName_, tableName_).c_str(),
                QuotedIdentifier(clmName).c_str(), fieldTypeInfo.c_str());

            dataSource_->ExecuteSQL(sql.c_str());
        }
    }
    catch (const odbc::Exception& ex)
    {
        CPLError(
            CE_Failure, CPLE_AppDefined, "Failed to alter field %s: %s",
            fieldDefn->GetNameRef(), ex.what());
        return OGRERR_FAILURE;
    }

    // TODO change an entry in attrColumns_;
    // Update field definition
    if (flagsIn & ALTER_NAME_FLAG)
        fieldDefn->SetName(newFieldDefn->GetNameRef());

    if (flagsIn & ALTER_TYPE_FLAG)
    {
        fieldDefn->SetSubType(OFSTNone);
        fieldDefn->SetType(newFieldDefn->GetType());
        fieldDefn->SetSubType(newFieldDefn->GetSubType());
    }

    if (flagsIn & ALTER_WIDTH_PRECISION_FLAG)
    {
        fieldDefn->SetWidth(newFieldDefn->GetWidth());
        fieldDefn->SetPrecision(newFieldDefn->GetPrecision());
    }

    if (flagsIn & ALTER_NULLABLE_FLAG)
        fieldDefn->SetNullable(newFieldDefn->IsNullable());

    if (flagsIn & ALTER_DEFAULT_FLAG)
        fieldDefn->SetDefault(newFieldDefn->GetDefault());

    rebuildQueryStatement_ = true;
    ResetReading();
    ResetPreparedStatements();

    return OGRERR_NONE;
}

/************************************************************************/
/*                          ClearBatches()                              */
/************************************************************************/

void OGRHanaTableLayer::ClearBatches()
{
    if (!insertFeatureStmtWithFID_.isNull())
        insertFeatureStmtWithFID_->clearBatch();
    if (!insertFeatureStmtWithoutFID_.isNull())
        insertFeatureStmtWithoutFID_->clearBatch();
    if (!updateFeatureStmt_.isNull())
        updateFeatureStmt_->clearBatch();
}

/************************************************************************/
/*                          SetCustomColumnTypes()                      */
/************************************************************************/

void OGRHanaTableLayer::SetCustomColumnTypes(const char* columnTypes)
{
    if (columnTypes == nullptr)
        return;

    const char* ptr = columnTypes;
    const char* start = ptr;
    while (*ptr != '\0')
    {
        if (*ptr == '(')
        {
            // Skip commas inside brackets, for example decimal(20,5)
            while (*ptr != '\0' && *ptr != ')')
            {
                ++ptr;
            }
        }

        ++ptr;

        if (*ptr == ',' || *ptr == '\0')
        {
            std::size_t len = static_cast<std::size_t>(ptr - start);
            const char* sep = std::find(start, start + len, '=');
            if (sep != nullptr)
            {
                std::size_t pos = static_cast<std::size_t>(sep - start);
                customColumnDefs_.push_back(
                    {CPLString(start, pos),
                     CPLString(start + pos + 1, len - pos - 1)});
            }

            start = ptr + 1;
        }
    }
}

/************************************************************************/
/*                          StartTransaction()                          */
/************************************************************************/

OGRErr OGRHanaTableLayer::StartTransaction()
{
    return dataSource_->StartTransaction();
}

/************************************************************************/
/*                          CommitTransaction()                         */
/************************************************************************/

OGRErr OGRHanaTableLayer::CommitTransaction()
{
    if (!HasPendingFeatures())
        return OGRERR_NONE;

    try
    {
        if (!deleteFeatureStmt_.isNull()
            && deleteFeatureStmt_->getBatchDataSize() > 0)
            deleteFeatureStmt_->executeBatch();
        if (!insertFeatureStmtWithFID_.isNull()
            && insertFeatureStmtWithFID_->getBatchDataSize() > 0)
            insertFeatureStmtWithFID_->executeBatch();
        if (!insertFeatureStmtWithoutFID_.isNull()
            && insertFeatureStmtWithoutFID_->getBatchDataSize() > 0)
            insertFeatureStmtWithoutFID_->executeBatch();
        if (!updateFeatureStmt_.isNull()
            && updateFeatureStmt_->getBatchDataSize() > 0)
            updateFeatureStmt_->executeBatch();

        ClearBatches();
    }
    catch (const odbc::Exception& ex)
    {
        ClearBatches();
        CPLError(
            CE_Failure, CPLE_AppDefined, "Failed to execute batch insert: %s",
            ex.what());
        return OGRERR_FAILURE;
    }

    dataSource_->CommitTransaction();
    return OGRERR_NONE;
}

/************************************************************************/
/*                          RollbackTransaction()                       */
/************************************************************************/

OGRErr OGRHanaTableLayer::RollbackTransaction()
{
    ClearBatches();
    return dataSource_->RollbackTransaction();
}
