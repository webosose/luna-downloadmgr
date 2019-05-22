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

#ifndef BASE_UPLOADTASK_H_
#define BASE_UPLOADTASK_H_

#include <vector>
#include <string>
#include <stdint.h>

#include "../external/glibcurl.h"
#include "PostItem.h"

typedef std::pair<std::string, std::string> kvpair;

class UploadTask {
public:
    static const char* TRUSTED_CERT_PATH;

    static UploadTask * newFileUploadTask(const std::string& targeturl, const std::string& sourcefile, const std::string& filemimepartlabel, std::vector<PostItem>& postparts,
            std::vector<std::string>& httpheaders, std::vector<kvpair>& cookies, const std::string& contenttype);
    static UploadTask * newBufferUploadTask(const std::string& targeturl, const std::string& sourcebuffer, std::vector<std::string>& httpheaders, const std::string& contenttype);

    const std::string& url()
    {
        return m_url;
    }
    const std::string& source()
    {
        return m_sourceFile;
    }
    const uint32_t& id()
    {
        return m_ulid;
    }

    CURL * getCURLHandlePtr()
    {
        return m_p_curlHandle;
    }
    const CURLcode& getCURLCode()
    {
        return m_curlResultCode;
    }
    const uint32_t& getHTTPCode()
    {
        return m_httpResultCode;
    }

    const std::vector<PostItem>& getPostParts()
    {
        return m_postParts;
    }
    const std::string& getContentType()
    {
        return m_contentType;
    }

    void setCURLCode(const CURLcode& cc)
    {
        m_curlResultCode = cc;
    }
    void setHTTPCode(const uint32_t& hc)
    {
        m_httpResultCode = hc;
    }

    virtual ~UploadTask();

    void setUploadResponse(const std::string& response)
    {
        m_uploadResponse = response;
    }
    void appendUploadResponse(const std::string& response)
    {
        m_uploadResponse += response;
    }
    const std::string& getUploadResponse()
    {
        return m_uploadResponse;
    }

    void setReplyLocation(const std::string& s)
    {
        m_replyLocationHeader = s;
    }
    const std::string& getReplyLocation() const
    {
        return m_replyLocationHeader;
    }

private:
    UploadTask()
    {
    }

    //propagate into the object and store, postparts and contenttype, as they may be useful in case of redirects, or other transient problems
    UploadTask(const std::string& url, const std::string file, const std::string& data, uint32_t id, std::vector<PostItem> * postparts, const std::string& contenttype, CURL * p_curl);

    UploadTask& operator=(const UploadTask& c)
    {
        return *this;
    }

    UploadTask(const UploadTask& c)
    {
    }

    static uint32_t genNewId();

    void setHTTPHeaders(std::vector<std::string>& headerList);

    std::string m_url;
    std::string m_sourceFile;
    std::string m_sourceData;
    uint32_t m_ulid;         //unique id for this upload
    std::vector<PostItem> m_postParts;
    std::string m_contentType;

    CURL * m_p_curlHandle;
    struct curl_slist * m_p_curlHeaderList;
    struct curl_slist * m_p_curlFilePartHeaderList;
    struct curl_httppost * m_p_httpPostList;
    CURLcode m_curlResultCode;
    uint32_t m_httpResultCode;

    std::string m_uploadResponse;
    std::string m_replyLocationHeader;
};

#endif /*BASE_UPLOADTASK_H_*/
