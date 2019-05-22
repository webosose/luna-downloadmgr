// Copyright (c) 2012-2019 LG Electronics, Inc.
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

#include <base/DownloadTask.h>
#include <core/DownloadManager.h>
#include <pbnjson.hpp>
#include <util/DownloadUtils.h>
#include <util/JUtil.h>
#include <util/Utils.h>

DownloadTask::DownloadTask()
    : m_ticket(0),
      m_opt_keepOriginalFilenameOnRedirect(false),
      m_initialOffsetBytes(0),
      m_bytesCompleted(0),
      m_bytesTotal(0),
      m_rangeSpecified(std::pair<uint64_t, uint64_t>(0, 0)),
      m_lastUpdateAt(0),
      m_updateInterval(DownloadManager::UPDATE_INTERVAL),
      m_curlDesc(0),
      m_fp(0),
      m_isQueued(false),
      m_numErrors(0),
      m_canHandlePause(false),
      m_autoResume(true),
      m_appendTargetFile(false),
      m_remainingRedCounts(MAXREDIRECTIONS)
{
}

DownloadTask::~DownloadTask()
{
    if (m_fp)
        fclose(m_fp);
}

void DownloadTask::setMimeType(const std::string& type)
{
    m_detectedMIMEType = type;
    size_t len = m_detectedMIMEType.size();
    while (len > 0 && (m_detectedMIMEType[len - 1] == '\n' || m_detectedMIMEType[len - 1] == '\r')) {
        m_detectedMIMEType.erase(len - 1, 1);
        --len;
    }
}

std::string DownloadTask::destToJSON()
{
    std::string dest = m_destPath + m_destFile;
    pbnjson::JValue jobj = pbnjson::Object();
    jobj.put("target", dest);
    std::string s = JUtil::toSimpleString(jobj);
    return s;
}

std::string DownloadTask::toJSONString()
{
    pbnjson::JValue jobj = toJSON();
    std::string s = JUtil::toSimpleString(jobj);
    return s;
}

pbnjson::JValue DownloadTask::toJSON()
{
    pbnjson::JValue jobj = pbnjson::Object();
    jobj.put("ticket", (int64_t) m_ticket);
    jobj.put("url", m_url);
    jobj.put("sourceUrl", m_url);
    jobj.put("deviceId", m_deviceId);
    jobj.put("authToken", m_authToken);

    std::string dest = m_destPath + m_downloadPrefix + m_destFile;
    std::string lbuff;

    jobj.put("target", dest);
    jobj.put("destTempPrefix", m_downloadPrefix);
    jobj.put("destFile", m_destFile);    //the final filename, regardless of temp prefixes/decorations
    jobj.put("destPath", m_destPath);    // ""       path            ""
    jobj.put("mimetype", m_detectedMIMEType);

    lbuff = Utils::toString(m_bytesCompleted);
    jobj.put("amountReceived", (int32_t) m_bytesCompleted);    //possible overflow
    jobj.put("e_amountReceived", lbuff);

    lbuff = Utils::toString(m_bytesTotal);
    jobj.put("amountTotal", (int32_t) m_bytesTotal);    //possible overflow
    jobj.put("e_amountTotal", lbuff);

    lbuff = Utils::toString(m_initialOffsetBytes);
    jobj.put("initialOffset", (int32_t) m_initialOffsetBytes);    //possible overflow
    jobj.put("e_initialOffsetBytes", lbuff);

    lbuff = Utils::toString(m_rangeSpecified.first);
    jobj.put("e_rangeLow", lbuff);
    lbuff = Utils::toString(m_rangeSpecified.second);
    jobj.put("e_rangeHigh", lbuff);

    jobj.put("canHandlePause", m_canHandlePause);
    jobj.put("autoResume", m_autoResume);
    jobj.put("cookieHeader", m_cookieHeader);

    return jobj;
}

void DownloadTask::setUpdateInterval(uint64_t interval)
{
    if (interval == 0) {
        if (m_bytesTotal > DownloadManager::UPDATE_INTERVAL * DownloadManager::UPDATE_NUM)
            m_updateInterval = m_bytesTotal / DownloadManager::UPDATE_NUM;
        else
            m_updateInterval = DownloadManager::UPDATE_INTERVAL;
    } else
        m_updateInterval = interval;

    if (m_updateInterval > DownloadManager::UPDATE_INTERVAL * DownloadManager::UPDATE_NUM)
        m_updateInterval = DownloadManager::UPDATE_INTERVAL * DownloadManager::UPDATE_NUM;
}
