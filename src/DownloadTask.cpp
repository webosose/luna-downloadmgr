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

#include "DownloadTask.h"
#include "DownloadManager.h"
#include "DownloadUtils.h"
#include "JUtil.h"
#include "Utils.h"
#include <pbnjson.hpp>

DownloadTask::DownloadTask()
    : ticket(0),
      opt_keepOriginalFilenameOnRedirect(false),
      initialOffsetBytes(0),
      bytesCompleted(0),
      bytesTotal(0),
      rangeSpecified(std::pair<uint64_t, uint64_t>(0, 0)),
      lastUpdateAt(0),
      updateInterval(DOWNLOADMANAGER_UPDATEINTERVAL),
      applicationPackage(0),
      curlDesc(0),
      fp(0),
      queued(false),
      numErrors(0),
      canHandlePause(false),
      autoResume(true),
      appendTargetFile(false),
      remainingRedCounts(MAXREDIRECTIONS)
{
}

DownloadTask::~DownloadTask()
{
    if (fp)
        fclose(fp);
}

void DownloadTask::setMimeType(const std::string& type)
{
    detectedMIMEType = type;
    size_t len = detectedMIMEType.size();
    while (len > 0 && (detectedMIMEType[len - 1] == '\n' || detectedMIMEType[len - 1] == '\r')) {
        detectedMIMEType.erase(len - 1, 1);
        --len;
    }
}

std::string DownloadTask::destToJSON()
{
    std::string dest = destPath + destFile;
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
    jobj.put("ticket", (int64_t) ticket);
    jobj.put("url", url);
    jobj.put("sourceUrl", url);
    jobj.put("deviceId", deviceId);
    jobj.put("authToken", authToken);

    std::string dest = destPath + downloadPrefix + destFile;
    std::string lbuff;

    jobj.put("target", dest);
    jobj.put("destTempPrefix", downloadPrefix);
    jobj.put("destFile", destFile);    //the final filename, regardless of temp prefixes/decorations
    jobj.put("destPath", destPath);    // ""       path            ""
    jobj.put("mimetype", detectedMIMEType);

    lbuff = Utils::toString(bytesCompleted);
    jobj.put("amountReceived", (int32_t) bytesCompleted);    //possible overflow
    jobj.put("e_amountReceived", lbuff);

    lbuff = Utils::toString(bytesTotal);
    jobj.put("amountTotal", (int32_t) bytesTotal);    //possible overflow
    jobj.put("e_amountTotal", lbuff);

    lbuff = Utils::toString(initialOffsetBytes);
    jobj.put("initialOffset", (int32_t) initialOffsetBytes);    //possible overflow
    jobj.put("e_initialOffsetBytes", lbuff);

    lbuff = Utils::toString(rangeSpecified.first);
    jobj.put("e_rangeLow", lbuff);
    lbuff = Utils::toString(rangeSpecified.second);
    jobj.put("e_rangeHigh", lbuff);

    jobj.put("canHandlePause", canHandlePause);
    jobj.put("autoResume", autoResume);
    jobj.put("cookieHeader", cookieHeader);

    return jobj;
}

void DownloadTask::setUpdateInterval(uint64_t interval)
{
    if (interval == 0) {
        if (bytesTotal > DOWNLOADMANAGER_UPDATEINTERVAL * DOWNLOADMANAGER_UPDATENUM)
            updateInterval = bytesTotal / DOWNLOADMANAGER_UPDATENUM;
        else
            updateInterval = DOWNLOADMANAGER_UPDATEINTERVAL;
    } else
        updateInterval = interval;

    if (updateInterval > DOWNLOADMANAGER_UPDATEINTERVAL * DOWNLOADMANAGER_UPDATENUM)
        updateInterval = DOWNLOADMANAGER_UPDATEINTERVAL * DOWNLOADMANAGER_UPDATENUM;
}
