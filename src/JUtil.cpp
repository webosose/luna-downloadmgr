// Copyright (c) 2013-2019 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "JUtil.h"
#include "Utils.h"
#include "Logging.h"
#include "DownloadSettings.h"

class DefaultResolver: public pbnjson::JResolver {
public:
    pbnjson::JSchema resolve(const ResolutionRequest &request, JSchemaResolutionResult &result)
    {
        //TODO : If we use cached schema here, resolve fail. Is it pbnjson bug? or misusage?
        pbnjson::JSchema resolved = JUtil::instance().loadSchema(request.resource(), false);
        if (!resolved.isInitialized()) {
            LOG_WARNING_PAIRS(LOGID_SCHEMA_IO_ERROR, 1, PMLOGKS("ERRTEXT", request.resource().c_str()), "");
            result = SCHEMA_IO_ERROR;
            return pbnjson::JSchema::NullSchema();
        }

        result = SCHEMA_RESOLVED;
        return resolved;
    }
};

//TODO : JErrorHandler doesn't pass detail reason. Do we have to modify pbnjson library?
class DefaultErrorHandler: public pbnjson::JErrorHandler {
public:
    DefaultErrorHandler() :
            m_code(JUtil::Error::None), m_detailCode(0)
    {
    }

    JUtil::Error::ErrorCode getCode()
    {
        return m_code;
    }

    std::string getReason()
    {
        return m_reason;
    }

    virtual void syntax(pbnjson::JParser *ctxt, SyntaxError code, const std::string& reason)
    {
        LOG_WARNING_PAIRS(LOGID_JSON_PARSE_SYNTX_ERR, 2, PMLOGKFV("ERRCODE", "%d", code), PMLOGKS("REASON", reason.c_str()), "");

    }

    virtual void schema(pbnjson::JParser *ctxt, SchemaError code, const std::string& reason)
    {
        m_code = JUtil::Error::Schema;
        m_detailCode = code;
        m_reason = makeReason(m_detailCode, reason);
        LOG_WARNING_PAIRS(LOGID_JSON_PARSE_SCHMA_ERR, 2, PMLOGKFV("ERRCODE", "%d", code), PMLOGKS("REASON", reason.c_str()), "");

    }

    virtual void misc(pbnjson::JParser *ctxt, const std::string& reason)
    {
        if (m_code == JUtil::Error::None) {
            m_code = JUtil::Error::Parse;
            m_reason = reason;
        }
        LOG_WARNING_PAIRS(LOGID_JSON_PARSE_MISC_ERR, 1, PMLOGKS("REASON", reason.c_str()), "");
    }

    virtual void badObject(pbnjson::JParser *ctxt, BadObject code)
    {
        LOG_WARNING_PAIRS(LOGID_JSON_PARSE_BAD_OBJ, 1, PMLOGKFV("ERRCODE", "%d", code), "");

    }

    virtual void badArray(pbnjson::JParser *ctxt, BadArray code)
    {
        LOG_WARNING_PAIRS(LOGID_JSON_PARSE_BAD_ARRY, 1, PMLOGKFV("ERRCODE", "%d", code), "");

    }

    virtual void badString(pbnjson::JParser *ctxt, const std::string& str)
    {
        LOG_WARNING_PAIRS(LOGID_JSON_PARSE_BAD_STR, 1, PMLOGKS("ERRTEXT", str.c_str()), "");

    }

    virtual void badNumber(pbnjson::JParser *ctxt, const std::string& number)
    {
        LOG_WARNING_PAIRS(LOGID_JSON_PARSE_BAD_NUM, 1, PMLOGKS("ERRCODE", number.c_str()), "");
    }

    virtual void badBoolean(pbnjson::JParser *ctxt)
    {
        LOG_WARNING_PAIRS(LOGID_JSON_PARSE_BAD_BOOLEAN, 0, "json parse bad boolean.");

    }

    virtual void badNull(pbnjson::JParser *ctxt)
    {
        LOG_WARNING_PAIRS(LOGID_JSON_PARSE_BAD_NULL, 0, "json parse bad null.");

    }

    virtual void parseFailed(pbnjson::JParser *ctxt, const std::string& reason)
    {
        LOG_WARNING_PAIRS(LOGID_JSON_PARSE_FAIL, 1, PMLOGKS("ERRTEXT", "Required parameters not provided"), "");

    }

protected:
    static std::string makeReason(unsigned int pbnjsonError, std::string reason)
    {
        switch (pbnjsonError) {
        case JErrorHandler::ERR_SCHEMA_MISSING_REQUIRED_KEY:
            return reason + std::string(" is required but it is missing");
        case JErrorHandler::ERR_SCHEMA_UNEXPECTED_TYPE:
            return reason + std::string(" type is not expected");
        default:
            return reason + std::string(" Unknown error");
        }
        return reason;
    }

protected:
    JUtil::Error::ErrorCode m_code;
    unsigned int m_detailCode;
    std::string m_reason;
};

JUtil::Error::Error()
    : m_code(Error::None)
{
}

JUtil::Error::ErrorCode JUtil::Error::code()
{
    return m_code;
}

std::string JUtil::Error::detail()
{
    return m_detail;
}

void JUtil::Error::set(ErrorCode code, const char *detail)
{
    m_code = code;
    if (!detail) {
        switch (m_code) {
        case Error::None:
            m_detail = "Success";
            break;
        case Error::File_Io:
            m_detail = "Fail to read file";
            break;
        case Error::Schema:
            m_detail = "Fail to read schema";
            break;
        case Error::Parse:
            m_detail = "Fail to parse json";
            break;
        default:
            m_detail = "Unknown error";
            break;
        }
    } else
        m_detail = detail;
}

JUtil::JUtil()
{
}

JUtil::~JUtil()
{
}

pbnjson::JValue JUtil::parse(const char *rawData, const std::string &schemaName, Error *error, pbnjson::JResolver *schemaResolver)
{
    DefaultResolver resolver;
    if (!schemaResolver)
        schemaResolver = &resolver;

    pbnjson::JSchema schema = JUtil::instance().loadSchema(schemaName, true);
    if (!schema.isInitialized()) {
        if (error)
            error->set(Error::Schema);
        return pbnjson::JValue();
    }

    DefaultErrorHandler errorHandler;
    pbnjson::JDomParser parser(schemaResolver);
    if (!parser.parse(rawData, schema, &errorHandler)) {
        if (error)
            error->set(errorHandler.getCode(), errorHandler.getReason().c_str());
        return pbnjson::JValue();
    }

    if (error)
        error->set(Error::None);
    return parser.getDom();
}

pbnjson::JValue JUtil::parseFile(const std::string &path, const std::string &schemaName, Error *error, pbnjson::JResolver *schemaResolver)
{
    std::string rawData = Utils::read_file(path);
    if (rawData.empty()) {
        if (error)
            error->set(Error::File_Io);
        return pbnjson::JValue();
    }

    pbnjson::JValue parsed = parse(rawData.c_str(), schemaName, error, schemaResolver);

    return parsed;
}

std::string JUtil::toSimpleString(pbnjson::JValue json)
{
    return pbnjson::JGenerator::serialize(json, pbnjson::JSchemaFragment("{}"));
}

pbnjson::JSchema JUtil::loadSchema(const std::string& schemaName, bool cache)
{
    if (schemaName.empty())
        return pbnjson::JSchemaFragment("{}");

    if (cache) {
        std::map<std::string, pbnjson::JSchema>::iterator it = m_mapSchema.find(schemaName);
        if (it != m_mapSchema.end())
            return it->second;
    }

    pbnjson::JSchema schema = pbnjson::JSchemaFile(DownloadSettings::instance().schemaPath + schemaName + ".schema");
    if (!schema.isInitialized())
        return schema;

    if (cache) {
        m_mapSchema.insert(std::pair<std::string, pbnjson::JSchema>(schemaName, schema));
    }

    return schema;
}
