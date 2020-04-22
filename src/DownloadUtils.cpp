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

#include "DownloadUtils.h"
#include "Logging.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <glib.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <algorithm>

bool filecopy (const std::string& srcFile, const std::string& destFile)
{
    std::ifstream s;
    std::ofstream d;
    s.open (srcFile.c_str(), std::ios::binary);
    d.open (destFile.c_str(), std::ios::binary);
    if (!s.is_open() || !d.is_open())
        return false;
    d << s.rdbuf ();
    s.close();
    d.close();
    return true;
}

bool doesExistOnFilesystem(const char * pathAndFile) {

    if (pathAndFile == NULL)
        return false;

    struct stat buf;
    if (-1 == ::stat(pathAndFile, &buf ) )
        return false;
    return true;


}

int filesizeOnFilesystem(const char * pathAndFile)
{
    if (pathAndFile == NULL)
        return 0;

    struct stat buf;
    if (-1 == ::stat(pathAndFile, &buf ) )
        return 0;
    return buf.st_size;
}

std::string trimWhitespace(const std::string& s,const std::string& drop)
{
    std::string::size_type first = s.find_first_not_of( drop );
    std::string::size_type last  = s.find_last_not_of( drop );

    if( first == std::string::npos || last == std::string::npos ) return std::string( "" );
    return s.substr( first, last - first + 1 );
}


int splitFileAndPath(const std::string& srcPathAndFile,std::string& pathPart,std::string& filePart)
{
    std::vector<std::string> parts;
//  LOG_DEBUG ("splitFileAndPath - input [%s]\n",srcPathAndFile.c_str());
    int s = splitStringOnKey(parts,srcPathAndFile,std::string("/"));
    if ((s == 1) && (srcPathAndFile.at(srcPathAndFile.length()-1) == '/')) {
        //only path part
        pathPart = srcPathAndFile;
        filePart = "";
    }
    else if (s == 1) {
        //only file part
        if (srcPathAndFile.at(0) == '/') {
            pathPart = "/";
        }
        else {
            pathPart = "";
        }
        filePart = parts.at(0);
    }
    else if (s >= 2) {
        for (int i=0;i<s-1;i++) {
            if ((parts.at(i)).size() == 0)
                continue;
            pathPart += std::string("/")+parts.at(i);
 //         LOG_DEBUG ("splitFileAndPath - path is now [%s]\n",pathPart.c_str());
        }
        pathPart += std::string("/");
        filePart = parts.at(s-1);
    }

    return s;
}

int splitFileAndExtension(const std::string& srcFileAndExt,std::string& filePart,std::string& extensionPart)
{
    std::vector<std::string> parts;
    int s = splitStringOnKey(parts,srcFileAndExt,std::string("."));
    if (s == 1) {
        //only file part; no extension
        filePart = parts.at(0);
    }
    else if (s >= 2) {
        filePart += parts.at(0);
        for (int i=1;i<s-1;i++)
            filePart += std::string(".")+parts.at(i);
        extensionPart = parts.at(s-1);
    }
    return s;
}

int splitStringOnKey(std::vector<std::string>& returnSplitSubstrings,const std::string& baseStr,const std::string& delims)
{
    std::string::size_type start = 0;
    std::string::size_type mark = 0;
    std::string extracted;

    int i=0;
    while (start < baseStr.size()) {
        //find the start of a non-delims
        start = baseStr.find_first_not_of(delims,mark);
        if (start == std::string::npos)
            break;
        //find the end of the current substring (where the next instance of delim lives, or end of the string)
        mark = baseStr.find_first_of(delims,start);
        if (mark == std::string::npos)
            mark = baseStr.size();

        extracted = baseStr.substr(start,mark-start);
        if (extracted.size() > 0) {
            //valid string...add it
            returnSplitSubstrings.push_back(extracted);
            ++i;
        }
        start=mark;
    }

    return i;
}

bool isNonErrorProcExit(int ecode,int normalCode)
{

    if (!WIFEXITED(ecode))
        return false;
    if (WEXITSTATUS(ecode) != normalCode)
        return false;

    return true;
}

//Should follow conventions for virtualHost naming in webkit: WebCore/platform/KURL.cpp
//CAUTION: modifying this to return strange paths is potentially dangerous. See ApplicationInstaller.cpp "remove" case where this fn is used
std::string getHtml5DatabaseFolderNameForApp(const std::string& appId,std::string appFolderPath)
{
    if (appFolderPath.length() == 0)
        return std::string("");

    replace(appFolderPath.begin(),appFolderPath.end(),'/','.');
    std::string r = std::string("file_")+appFolderPath+std::string("_0");
    return r;
}

int postSubscriptionUpdate(const std::string& key,const std::string& postMessage,LSHandle * serviceHandle)
{
    if (serviceHandle == NULL)
        return true;            //nothing to do

    LSSubscriptionIter *iter=NULL;
    LSError lserror;
    LSErrorInit(&lserror);
    bool retVal = false;
    //acquire the subscription and reply.

//  LOG_DEBUG ("DL-UPDATE: %s",postMessage.c_str());
    int rc=0;

    if (serviceHandle) {
        retVal = LSSubscriptionAcquire(serviceHandle, key.c_str(), &iter, &lserror);
        if (retVal) {
        while (LSSubscriptionHasNext(iter)) {
            LSMessage *message = LSSubscriptionNext(iter);
            if (!LSMessageReply(serviceHandle,message,postMessage.c_str(),&lserror)) {
            LSErrorPrint(&lserror,stderr);
            LSErrorFree(&lserror);
            //mark the return code bitfield to indicate at least one bus failure
            rc |= ERRMASK_POSTSUBUPDATE;
            }
        }

        LSSubscriptionRelease(iter);
        }
        else {
        LSErrorFree(&lserror);
        }
    }

    return rc;
}

bool processSubscription(LSHandle * serviceHandle, LSMessage * message,const std::string& key)
{

    if ((serviceHandle == NULL) || (message == NULL))
        return false;

    LSError lsError;
    LSErrorInit(&lsError);

    if (LSMessageIsSubscription(message)) {
        if (!LSSubscriptionAdd(serviceHandle, key.c_str(),
                message, &lsError)) {
            LSErrorFree(&lsError);
            return false;
        }
        else
            return true;
    }
    return false;
}

uint32_t removeSubscriptions(const std::string& key,LSHandle * serviceHandle)
{
    if (key.size() == 0)
        return false;

    if (serviceHandle == NULL)
        return false;

    LSSubscriptionIter *iter=NULL;
    LSError lserror;
    LSErrorInit(&lserror);

    //acquire the subscription
    uint32_t rc=0;
    bool retVal = false;

    if (serviceHandle) {
        retVal = LSSubscriptionAcquire(serviceHandle, key.c_str(), &iter, &lserror);
        if (retVal) {
            while (LSSubscriptionHasNext(iter)) {
                if (!LSSubscriptionNext(iter)) {
                    break;
                }
                LSSubscriptionRemove(iter);
                    ++rc;
            }
            LSSubscriptionRelease(iter);
        }
        else {
            LSErrorFree(&lserror);
        }
    }

    return rc;
}

