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

/* COMMENT:
 *
 *  Moved DownloadTask struct out of DownloadManager class and made it its own class...Eventually, make a proper task
 *  structure with TransferTask as a superclass, and UploadTask and DownloadTask as its subclasses.
 *
 * For the time being, TransferTask will be just a "fat node" container that will have a pointer to either a DownloadTask or
 * an UploadTask
 *
 */

class CurlDescriptor {

public:
    CurlDescriptor()
        : m_handle(0),
          m_headerList(0),
          m_resultCode((CURLcode) 0),
          m_httpResultCode(0),
          m_httpConnectCode(0)
    {
    }

    CurlDescriptor(CURL * curlHandle)
        : m_handle(curlHandle),
          m_headerList(NULL),
          m_resultCode((CURLcode) 0),
          m_httpResultCode(0),
          m_httpConnectCode(0)
    {
    }

    CurlDescriptor(const CurlDescriptor& cd)
    {
        m_handle = cd.m_handle;
        m_headerList = cd.m_headerList;
        m_resultCode = cd.m_resultCode;
        m_httpResultCode = cd.m_httpResultCode;
        m_httpConnectCode = cd.m_httpConnectCode;
    }

    ~CurlDescriptor()
    {
    }

    bool operator==(const CurlDescriptor& cd) const
    {
        return (m_handle == cd.m_handle);
    }

    bool operator>=(const CurlDescriptor& cd) const
    {
        return (m_handle >= cd.m_handle);
    }

    bool operator<=(const CurlDescriptor& cd) const
    {
        return (m_handle <= cd.m_handle);
    }

    bool operator>(const CurlDescriptor& cd) const
    {
        return (m_handle > cd.m_handle);
    }

    bool operator<(const CurlDescriptor& cd) const
    {
        return (m_handle < cd.m_handle);
    }

    bool operator!=(const CurlDescriptor& cd) const
    {
        return (m_handle != cd.m_handle);
    }

    CurlDescriptor& operator=(const CurlDescriptor& cd)
    {
        if (*this == cd) {  //WARN: careful if this class gets expanded... == must always mean that they're logically equivalent in every meaningful sense
            return *this;   //TODO: investigate why logical rather than pointer equivalence was used - maybe just an oversight
        }

        this->m_handle = cd.m_handle;
        this->m_resultCode = cd.m_resultCode;
        //TODO: sanely copy the header list
        this->m_headerList = NULL;
        this->m_httpResultCode = cd.m_httpResultCode;
        this->m_httpConnectCode = cd.m_httpConnectCode;
        return *this;
    }

    CURL* getHandle()
    {
        return m_handle;
    }

    CURL* setHandle(CURL * newHandle)
    {
        CURL * oldHandle = m_handle;
        m_handle = newHandle;
        return oldHandle;
    }

    struct curl_slist* getHeaderList()
    {
        return m_headerList;
    }

    struct curl_slist* setHeaderList(struct curl_slist * newHeaderList)
    {
        struct curl_slist * tmp = m_headerList;
        m_headerList = newHeaderList;
        return tmp;
    }

    CURLcode getResultCode()
    {
        return m_resultCode;
    }

    CURLcode setResultCode(CURLcode resultCode)
    {
        CURLcode tmp = m_resultCode;
        m_resultCode = resultCode;
        return tmp;
    }

    long getHttpResultCode()
    {
        return m_httpResultCode;
    }

    long setHttpResultCode(long httpResultCode)
    {
        long tmp = m_httpResultCode;
        m_httpResultCode = httpResultCode;
        return tmp;
    }

    long getHttpConnectCode()
    {
        return m_httpConnectCode;
    }

    long setHttpConnectCode(long httpConnectCode)
    {
        long tmp = m_httpConnectCode;
        m_httpConnectCode = httpConnectCode;
        return tmp;
    }

private:
    CURL* m_handle;
    struct curl_slist* m_headerList;
    CURLcode m_resultCode;
    long m_httpResultCode;
    long m_httpConnectCode;
};

class DownloadTask {
public:
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

    DownloadTask();
    ~DownloadTask();
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