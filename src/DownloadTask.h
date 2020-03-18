// Copyright (c) 2012-2018 LG Electronics, Inc.
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

#ifndef DOWNLOADTASK_H_
#define DOWNLOADTASK_H_

#include "glib.h"
#include "glibcurl.h"
#include <string>
#include <pbnjson.hpp>
#include "Time.h"

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
    CurlDescriptor() : _handle(0) , headerList(0) , _resultCode((CURLcode)0) , _httpResultCode(0) , _httpConnectCode(0) {}
    CurlDescriptor(CURL * curlHandle) : _handle(curlHandle) , headerList(NULL) , _resultCode((CURLcode)0) , _httpResultCode(0) , _httpConnectCode(0) {}
    CurlDescriptor(const CurlDescriptor& cd) {
        _handle = cd._handle;
        headerList = cd.headerList;
        _resultCode = cd._resultCode;
        _httpResultCode = cd._httpResultCode;
        _httpConnectCode = cd._httpConnectCode;
    }
    ~CurlDescriptor() {}

    bool operator==(const CurlDescriptor& cd) const { return (_handle == cd._handle);}
    bool operator>=(const CurlDescriptor& cd) const {return (_handle >= cd._handle);}
    bool operator<=(const CurlDescriptor& cd) const {return (_handle <= cd._handle);}
    bool operator>(const CurlDescriptor& cd) const {return (_handle > cd._handle);}
    bool operator<(const CurlDescriptor& cd) const {return (_handle < cd._handle);}
    bool operator!=(const CurlDescriptor& cd) const { return (_handle != cd._handle);}

    CurlDescriptor& operator=(const CurlDescriptor& cd) {
        if (*this == cd) {  //WARN: careful if this class gets expanded... == must always mean that they're logically equivalent in every meaningful sense
            return *this;           //TODO: investigate why logical rather than pointer equivalence was used - maybe just an oversight
        }

        this->_handle = cd._handle;
        this->_resultCode = cd._resultCode;
        //TODO: sanely copy the header list
        this->headerList = NULL;
        this->_httpResultCode = cd._httpResultCode;
        this->_httpConnectCode = cd._httpConnectCode;
        return *this;
    }

    CURL * getHandle() {return _handle;}
    CURL * setHandle(CURL * newHandle) {
        CURL * oldHandle = _handle;
        _handle = newHandle;
        return oldHandle;
    }

    struct curl_slist * getHeaderList() {return headerList;}
    struct curl_slist * setHeaderList(struct curl_slist * newHeaderList) {
        struct curl_slist * tmp = headerList;
        headerList = newHeaderList;
        return tmp;
    }

    CURLcode getResultCode() {return _resultCode;}
    CURLcode setResultCode(CURLcode resultCode) {
        CURLcode tmp = _resultCode;
        _resultCode = resultCode;
        return tmp;
    }

    long getHttpResultCode() { return _httpResultCode;}
    long setHttpResultCode(long httpResultCode) {
        long tmp = _httpResultCode;
        _httpResultCode = httpResultCode;
        return tmp;
    }

    long getHttpConnectCode() { return _httpConnectCode;}
    long setHttpConnectCode(long httpConnectCode)
    {
        long tmp = _httpConnectCode;
        _httpConnectCode = httpConnectCode;
        return tmp;
    }

private:
    CURL * _handle;
    struct curl_slist * headerList;
    CURLcode _resultCode;
    long _httpResultCode;
    long _httpConnectCode;
};

class DownloadTask {

public:
    unsigned long ticket;
    std::string destPath;
    std::string destFile;
    std::string url;
    std::string cookieHeader;
    std::string detectedMIMEType;
    std::string authToken;
    std::string deviceId;

    std::string downloadPrefix;
    bool    opt_keepOriginalFilenameOnRedirect;

    uint64_t initialOffsetBytes;
    uint64_t bytesCompleted;
    uint64_t bytesTotal;
    std::pair<uint64_t,uint64_t> rangeSpecified;

    DownloadTask();
    ~DownloadTask();
    void setMimeType(const std::string& type);
    std::string toJSONString();
    pbnjson::JValue toJSON();
    std::string destToJSON();

    void setLocationHeader(const std::string& s) { httpHeader_Location = s; }
    void setUpdateInterval(uint64_t interval = 0);

    // functions for counting maximum redirections.
    int getRemainingRedCounts() { return remainingRedCounts; }
    void decreaseRedCounts() { remainingRedCounts--; }
    void setRemainingRedCounts(const int currentRedCounts) { remainingRedCounts = currentRedCounts; }

    //the following do NOT go into the json record of the task
    uint64_t lastUpdateAt;          // sets the bytemark at which the last subscription update was sent
    uint64_t updateInterval;
    int applicationPackage;     //  > 0 if the download represents an application package
    CurlDescriptor curlDesc;
    FILE * fp;
    bool queued;
    std::string httpHeader_Location; //for 301/302 Redirect codes, unused otherwise
    int  numErrors;
    std::string ownerId;
    std::string connectionName;
    bool canHandlePause;
    bool autoResume;
    bool appendTargetFile;

    // rfc2616 (HTTP/1.1) recommends maximum of five redirections.
    static const int MAXREDIRECTIONS = 5;

private:
    // Remaining redirection counts
    int remainingRedCounts;
};

#endif /*DOWNLOADTASK_H_*/
