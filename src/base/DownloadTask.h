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

#ifndef BASE_DOWNLOADTASK_H_
#define BASE_DOWNLOADTASK_H_

#include "glib.h"
#include <string>
#include <pbnjson.hpp>
#include "../external/glibcurl.h"
#include "CurlDescriptor.h"

class DownloadTask {
public:
    DownloadTask();
    virtual ~DownloadTask();
    void setMimeType(const std::string& type);
    std::string toJSONString();
    pbnjson::JValue toJSON();
    std::string destToJSON();

    void setLocationHeader(const std::string& s)
    {
        m_httpHeaderLocation = s;
    }
    void setUpdateInterval(uint64_t interval = 0);

    // functions for counting maximum redirections.
    int getRemainingRedCounts()
    {
        return m_remainingRedCounts;
    }

    void decreaseRedCounts()
    {
        m_remainingRedCounts--;
    }

    void setRemainingRedCounts(const int currentRedCounts)
    {
        m_remainingRedCounts = currentRedCounts;
    }

    unsigned long m_ticket;
    std::string m_destPath;
    std::string m_destFile;
    std::string m_url;
    std::string m_cookieHeader;
    std::string m_detectedMIMEType;
    std::string m_authToken;
    std::string m_deviceId;

    std::string m_downloadPrefix;
    bool m_opt_keepOriginalFilenameOnRedirect;

    uint64_t m_initialOffsetBytes;
    uint64_t m_bytesCompleted;
    uint64_t m_bytesTotal;
    std::pair<uint64_t, uint64_t> m_rangeSpecified;

    //the following do NOT go into the json record of the task
    uint64_t m_lastUpdateAt;          // sets the bytemark at which the last subscription update was sent
    uint64_t m_updateInterval;
    CurlDescriptor m_curlDesc;
    FILE* m_fp;
    bool m_isQueued;
    std::string m_httpHeaderLocation; //for 301/302 Redirect codes, unused otherwise
    int m_numErrors;
    std::string m_ownerId;
    std::string m_connectionName;
    bool m_canHandlePause;
    bool m_autoResume;
    bool m_appendTargetFile;

    // rfc2616 (HTTP/1.1) recommends maximum of five redirections.
    static const int MAXREDIRECTIONS = 5;

private:
    // Remaining redirection counts
    int m_remainingRedCounts;
};

#endif /*BASE_DOWNLOADTASK_H_*/
