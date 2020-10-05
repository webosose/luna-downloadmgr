// Copyright (c) 2012-2020 LG Electronics, Inc.
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

#include "DownloadManager.h"
#include "DownloadSettings.h"
#include "DownloadUtils.h"

#include <algorithm>
#include <vector>
#include <iostream>
#include <cstring>
#include <stdio.h>
#include <sstream>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <unistd.h>
#include <pbnjson.hpp>
#include "UrlRep.h"
#include "Logging.h"
#include "Time.h"
#include "JUtil.h"
#include "Utils.h"

#define TIMEOUT_INTERVAL_SEC 10

extern GMainLoop* gMainLoop;
static const char* downloadPrefix = ".";

unsigned long  DownloadManager::s_ticketGenerator   = 1;

//#define CURL_COOKIE_SHARING
static CURLSH* s_curlShareHandle = 0;

// curl callback functions
// these functions redirect the callback to a function within the instance of download manager
size_t DownloadManager::cbCurlReadFromFile(void* ptr, size_t size, size_t nmemb, void *stream) {
    return DownloadManager::instance().cbReadEvent ((CURL *)stream, size*nmemb, (unsigned char *)ptr);
}

size_t DownloadManager::cbCurlWriteToFile(void* ptr, size_t size, size_t nmemb, void *stream) {
    return DownloadManager::instance().cbWriteEvent ((CURL *)stream, size*nmemb, (unsigned char *)ptr);
}

size_t DownloadManager::cbCurlHeaderInfo(void * ptr,size_t size,size_t nmemb,void * stream) {
    return DownloadManager::instance().cbHeader ((CURL *)stream, size*nmemb, (const char *)ptr);
}

void DownloadManager::cbGlibcurl(void* data) {
    DownloadManager::instance().cbGlib();
}

int DownloadManager::cbCurlSetSocketOptions(void *clientp,curl_socket_t curlfd,curlsocktype purpose) {
    return DownloadManager::instance().cbSetSocketOptions(clientp,curlfd,purpose);
}


// These few next functions are somewhat trivial for now, but defined explicitly rather than inline in the code where used, so that their
// behavior can be changed easily
std::string DownloadManager::getDownloadPath() {
    return (m_downloadPath);
}

std::string DownloadManager::generateTempPath( const std::string& resourceName ) {

    return (m_downloadPath + resourceName);
}

DownloadManager::DownloadManager()
   :
    m_wanConnectionTypePrevious(WanConnectionUnknown),
    m_wanConnectionStatus(InetConnectionUnknownState) ,
    m_wanConnectionType(WanConnectionUnknown),
    m_wifiConnectionStatus(InetConnectionUnknownState) ,
    m_btpanConnectionStatus(InetConnectionUnknownState),
    m_wiredConnectionStatus(InetConnectionUnknownState),
    m_wanInterfaceName(DownloadSettings::instance().wanInterfaceName),
    m_wifiInterfaceName(DownloadSettings::instance().wifiInterfaceName),
    m_btpanInterfaceName(DownloadSettings::instance().btpanInterfaceName),
    m_wiredInterfaceName(DownloadSettings::instance().wiredInterfaceName),
    m_serviceHandle(NULL),
    m_storageDaemonToken(0),
    m_activeTaskCount(0),
    m_glibCurlInitialized(false),
    m_fscking(false),
    m_brickMode(false),
    m_msmExitClean(true),
    m_mode(false),
    m_sleepDClientId("")
{
    //LOG_DEBUG ("%s:%d gMainLoop is %p", __FILE__, __LINE__, gMainLoop);
    m_mainLoop = gMainLoop;

    m_pDlDb = DownloadHistoryDb::instance();
}

DownloadManager::~DownloadManager()
{
    this->stopService();
    shutdownGlibCurl();
    delete m_pDlDb;
}

void DownloadManager::init()
{

#if !defined(TARGET_DESKTOP)
    if (DownloadSettings::instance().downloadPathMedia.size()) {
        m_downloadPath = DownloadSettings::instance().downloadPathMedia;
    }
    else {
        m_downloadPath = "/media/internal/downloads/";
        DownloadSettings::instance().downloadPathMedia = "/media/internal/downloads/";
    }
    m_userDiskRootPath = "/media/internal/";
#else
    m_downloadPath = std::string(getenv("HOME"))+std::string("/downloads/");
    m_userDiskRootPath = std::string(getenv("HOME"))+std::string("/downloads/");
#endif

    unsigned long maxKey = 0;
    if (!m_pDlDb->getMaxKey(maxKey)) {
        LOG_DEBUG ("Function getMaxKey() failed");
    }

    if (maxKey != 0) {
        s_ticketGenerator = maxKey+1;           //yes, i know...problem if maxKey = MAX_ULONG
    }

    //add trailing "/" to the paths created above if needed, because the rest of the code expects it
    //(in case the Settings provided paths didn't have the trailing / ...can't guarantee what the user puts in)

    if (m_downloadPath.at(m_downloadPath.size()-1) != '/')
        m_downloadPath += "/";
    if (m_userDiskRootPath.at(m_userDiskRootPath.size()-1) != '/')
        m_userDiskRootPath += "/";

    m_authCookie = "";
    if (g_mkdir_with_parents(m_downloadPath.c_str(), 0755) == -1) {
        LOG_DEBUG ("Function g_mkdir_with_parents() failed");
    }
    //LOG_DEBUG ( "Download path is %s", m_downloadPath.c_str() );

    // RECOVERY: everything in the database that was marked "running" or "queued" , or "paused" , gets reassigned to "cancelled"
    m_pDlDb->changeStateForAll("running","cancelled");
    m_pDlDb->changeStateForAll("queued","cancelled");
    m_pDlDb->changeStateForAll("interrupted","cancelled");

    //initialize us as a luna service

    this->startService();

    return;
}
// DownloadManager::isValidOverridePath
// Will check is the specified path is valid, and if necessary, create it
// Valid paths are currently considered as anything underneath m_downloadPath

bool    DownloadManager::isValidOverridePath(const std::string& path) {

    //do not allow /../ in the path. This will avoid complicated parsing to check for valid paths
    if (path.find("..") != std::string::npos)
        return false;

    //mkdir -p the path requested just in case
    //LOG_DEBUG ("executing g_mkdir on: %s\n",path.c_str());
    if (g_mkdir_with_parents(path.c_str(), 0755) == -1) {
        LOG_DEBUG ("Function g_mkdir_with_parents() failed");
    }
    //LOG_DEBUG ("g_mkdir exit status = %d\n",exit_status);

    return true;
}

bool    DownloadManager::isValidOverrideFile(const std::string& file) {

    //do not allow / in the file. This will avoid complicated parsing to check for sneaky path overrides in the filename
    if (file.find("/") != std::string::npos)
        return false;

    if (file.find_first_not_of('.') == std::string::npos)
        return false;

    return true;
}

bool DownloadManager::isPathInMedia(const std::string& path) {

    //do not allow /../ in the path. This will avoid complicated parsing to check for valid paths
    if (path.find("..") != std::string::npos)
        return false;

    //the prefix /media/internal has to be anchored at 0
#if !defined (TARGET_DESKTOP)
    if (path.find("/media/internal") == 0 )
#else
    if (path.find (getenv("HOME")) == 0)
#endif
        return true;

    return false;
}

bool DownloadManager::isPrivileged(const std::string& sender)
{
    if(sender.find("com.palm.") == 0 || sender.find("com.webos.") == 0 || sender.find("com.lge.") == 0 )
        return true;

    return false;
}

bool DownloadManager::isPathInVar(const std::string& path) {

    //do not allow /../ in the path. This will avoid complicated parsing to check for valid paths
    if (path.find("..") != std::string::npos)
        return false;

    //the prefix /var has to be anchored at 0
    if (path.find("/var") == 0 )
        return true;

    return false;

}

// download: function to start a download
int DownloadManager::download (const std::string& caller,
    const std::string& uri,
    const std::string& mime,
    const std::string& overrideTargetDir,
    const std::string& overrideTargetFile,
    const unsigned long ticket,
    bool  keepOriginalFilenameOnRedirect,
    const std::string& authToken,
    const std::string& deviceId,
    Connection interface,
    bool canHandlePause,
    bool autoResume,
    bool appendTargetFile,
    const std::string& cookieHeader,
    const std::pair<uint64_t,uint64_t> range,
    const int remainingRedCounts)
{
    LOG_INFO_PAIRS_ONLY (LOGID_DOWNLOAD_START, 8, PMLOGKS("Caller", caller.c_str()),
                                                PMLOGKFV("ticket", "%lu", ticket),
                                                PMLOGKS("uri", uri.c_str()),
                                                PMLOGKS("mime", mime.empty() ? "unknown" : mime.c_str()),
                                                PMLOGKS("targetDir",overrideTargetDir.empty() ? "default-directory" : overrideTargetDir.c_str()),
                                                PMLOGKS("targetFile",overrideTargetFile.empty()? "default-filename": overrideTargetFile.c_str()),
                                                PMLOGKS("authToken",authToken.empty() ? "No-AuthToken": authToken.c_str()),
                                                PMLOGKS("deviceId",deviceId.empty() ?"Not-Available": deviceId.c_str()));

    // if the queue is already full, no point in continuing
    if (m_queue.size() >= DownloadSettings::instance().maxDownloadManagerQueueLength) {
        return DOWNLOADMANAGER_STARTSTATUS_QUEUEFULL;
    }

    if (interface == ANY) {
        //determine a good interface to use
    if (m_wiredConnectionStatus == DownloadManager::InetConnectionConnected)
        interface = Wired;
        else if (m_wifiConnectionStatus == DownloadManager::InetConnectionConnected)
            interface = Wifi;
        else if (m_wanConnectionStatus == DownloadManager::InetConnectionConnected)
            interface = Wan;
        else if (m_btpanConnectionStatus == DownloadManager::InetConnectionConnected)
            interface = Btpan;

        //else leave as any
//      LOG_DEBUG ("%s: interface was specified as ANY, picked %s",__func__,DownloadManager::connectionId2Name(interface).c_str());
    }

    if ((interface == Wan) && ((m_wanConnectionType == WanConnection1x) && !s_allow1x))
    {
        LOG_DEBUG ("%s: Interface picked as WAN but 1x mode is active on WAN and 1x isn't allowed...aborting",__FUNCTION__);
        return DOWNLOADMANAGER_STARTSTATUS_NOSUITABLEINTERFACE;
    }
    if ((interface == ANY) && ((m_wanConnectionType == WanConnection1x) && !s_allow1x))
    {
        LOG_DEBUG ("%s: Interface picked as ANY but 1x mode is active on WAN and 1x isn't allowed. Cannot chance downloads starting on the WAN interface...aborting",__FUNCTION__);
        return DOWNLOADMANAGER_STARTSTATUS_NOSUITABLEINTERFACE;
    }

    //LOG_DEBUG ("%s: Interface %s and allow1x is %s",__FUNCTION__,DownloadManager::connectionId2Name(interface).c_str(),(s_allow1x ? "TRUE" : "FALSE"));
    int curlSetOptRc=0;
    bool createTempFile = false; // this is used for urls that dont have file names

    //parse the URI/URL
    UrlRep parsedUrl = UrlRep::fromUrl(uri.c_str());

    //Check the source uri for security. It should be http, https or ftp scheme
    if (parsedUrl.scheme != "http" && parsedUrl.scheme != "https" && parsedUrl.scheme != "ftp")
    {
        //failed security check
        LOG_WARNING_PAIRS (LOGID_SECURITY_CHECK_FAIL, 2, PMLOGKS("uri", uri.c_str()), PMLOGKS("scheme", parsedUrl.scheme.c_str()), "this scheme is not allowed");
        return DOWNLOADMANAGER_STARTSTATUS_FAILEDSECURITYCHECK;
    }

    DownloadTask* task = new DownloadTask;
    TransferTask * p_ttask = new TransferTask(task);

    if (!m_glibCurlInitialized)
        startupGlibCurl();

    task->setRemainingRedCounts(remainingRedCounts);
    task->ticket = ticket;
    task->opt_keepOriginalFilenameOnRedirect = keepOriginalFilenameOnRedirect;
    task->ownerId = caller;

    //LOG_DEBUG ("Override Target Dir : specified as [%s]",overrideTargetDir.c_str());
    if (!overrideTargetDir.empty()
            && overrideTargetDir != std::string("")
            && isValidOverridePath(overrideTargetDir))
    {
        //side-effect: ovrDir will be created if necessary, and if the path was considered to be Ok
            task->destPath = overrideTargetDir;
    }
    else {
        if (isValidOverridePath(m_downloadPath))
            task->destPath = m_downloadPath;
    }

    //LOG_DEBUG ("Override Target Filename : specified as [%s]",overrideTargetFile.c_str());
    if (!overrideTargetFile.empty()) {
        if (isValidOverrideFile(overrideTargetFile)) {
            task->destFile = overrideTargetFile;
            task->opt_keepOriginalFilenameOnRedirect = true;        //specifying a file override implies this
        }
        else {
            createTempFile = true; // create a temp file if the overrideTargetFile is invalid
        }
    }
    else {
        if (parsedUrl.valid && !parsedUrl.resource.empty())
            task->destFile = parsedUrl.resource;
        else
            createTempFile = true; // create a temp file if no file is specified in download url
    }

    if (task->destPath.at((task->destPath.length()-1)) != '/')
        task->destPath = task->destPath + std::string("/");

    //check free space on disk
    uint64_t spaceFreeKB = 0;
    uint64_t spaceTotalKB = 0;
    bool stopMarkReached = false;
    if (! DownloadManager::spaceOnFs(task->destPath,spaceFreeKB,spaceTotalKB))
    {
        //can't stat the filesys...treat the same as out of space
        delete p_ttask;
        return DOWNLOADMANAGER_STARTSTATUS_FILESYSTEMFULL;
    }

    this->filesystemStatusCheck(spaceFreeKB,spaceTotalKB,NULL,&stopMarkReached);

    if (DownloadSettings::instance().preemptiveFreeSpaceCheck)
    {
        if (stopMarkReached)        ///
        {
            delete p_ttask;
            return DOWNLOADMANAGER_STARTSTATUS_FILESYSTEMFULL;
        }
    }

    task->url = uri;
    task->cookieHeader = cookieHeader;
    task->setMimeType("application/x-binary");  //default to this...pretty generic
    task->bytesCompleted = 0;
    task->bytesTotal = 0;
    task->canHandlePause = canHandlePause;
    task->autoResume = autoResume;
    task->appendTargetFile = appendTargetFile;
    task->rangeSpecified = range;

    if (createTempFile == false) { // only if filename is provided
        task->downloadPrefix = downloadPrefix;
        std::string tmpFilePath = task->destPath + task->downloadPrefix + task->destFile;
        std::string finalFilePath = task->destPath + task->destFile;
        // if app has specified the file, use that directly
        if (overrideTargetFile.empty()) {
            // check if the file already exists
            if (g_file_test (finalFilePath.c_str(), G_FILE_TEST_EXISTS)
                    || g_file_test (tmpFilePath.c_str(), G_FILE_TEST_EXISTS))
            {
                // NOV-70363: Downloading the same file twice should not fail, must create unique file
                //LOG_DEBUG ("file of name %s or %s exists, will rename", finalFilePath.c_str(), tmpFilePath.c_str());

                // file does exist. make a unique file adding [x] before the extension
                size_t extPos = task->destFile.rfind ('.');
                int addExt = 0;
                std::string fileName, fileExt;
                if (extPos == std::string::npos)  {
                    fileName = task->destFile;
                }
                else {
                    fileName = task->destFile.substr (0, extPos);
                    fileExt = task->destFile.substr (extPos);
                }

                std::stringstream newFileName;
                std::stringstream newTempFile;
                std::stringstream newFinalFile;

                do {
                    ++ addExt;
                    newFileName.str("");
                    newFileName << fileName;
                    newFileName << "_" << addExt;
                    newFileName << fileExt;
                    //LOG_DEBUG ("new file name :%s", newFileName.str().c_str());

                    newTempFile.str("");
                    newTempFile << task->destPath;
                    newTempFile << task->downloadPrefix;
                    newTempFile << newFileName.str();

                    //LOG_DEBUG ("new temp file :%s", newTempFile.str().c_str());

                    newFinalFile.str("");
                    newFinalFile << task->destPath;
                    newFinalFile << newFileName.str();

                    //LOG_DEBUG ("new final file :%s", newFinalFile.str().c_str());

                } while (g_file_test (newFinalFile.str().c_str(), G_FILE_TEST_EXISTS)
                        || g_file_test (newTempFile.str().c_str(), G_FILE_TEST_EXISTS));

                LOG_DEBUG ( " Download : File Exist - Renaming existing file %s to %s", task->destFile.c_str() ,newFileName.str().c_str() );
                tmpFilePath = newTempFile.str();
                finalFilePath = newFinalFile.str();
                task->destFile = newFileName.str();
            }
        }

        //LOG_DEBUG ( "DownloadManager url=%s task->destPath=[%s] task->destFile=[%s]", uri.c_str(), task->destPath.c_str(),task->destFile.c_str());

        //open the file for writing
        if (appendTargetFile)
        {
            task->fp = fopen(tmpFilePath.c_str(), "r+b");

            if ((task->fp) && (range.second != 0) && (range.second > range.first))
            {
                if (fseek(task->fp, range.first, SEEK_SET) != 0)
                {
                    if (fclose(task->fp) != 0) {
                        LOG_DEBUG ("Function fclose() failed");
                    }
                    task->fp = NULL;
                }
            }
        }
        else
            task->fp = fopen(tmpFilePath.c_str(), "wb");
    }
    else {
        if (task->destPath.at((task->destPath.length()-1)) != '/')
            task->destPath = task->destPath + std::string("/");

        std::string templateStr = task->destPath + std::string("fileXXXXXX");
        char * templateFileName = new char[templateStr.length()+2];
        strncpy(templateFileName,templateStr.c_str(),sizeof(templateStr.length()+2));
        mode_t mask = umask(S_IRWXG | S_IRWXO);
        int fd = mkstemp(templateFileName);
        (void) umask(mask);

        task->destPath = "";
        task->destFile = "";
        if (splitFileAndPath(std::string(templateFileName),task->destPath,task->destFile) < 0) {
            LOG_DEBUG ("Wrong file path: %s", templateFileName);
        }
        delete[] templateFileName;

        if (fd == -1) {
            delete p_ttask;
            return DOWNLOADMANAGER_STARTSTATUS_FILESYSTEMFULL;
        }

        task->fp = fdopen(fd, "wb");
    }

    if (task->fp  == NULL) {
        delete p_ttask;
        return DOWNLOADMANAGER_STARTSTATUS_FILESYSTEMFULL;
    }

    //LOG_DEBUG ("File pointer for [%s]/[%s] is %x\n",task->destPath.c_str(),task->destFile.c_str(),(int)(task->fp));
    //allocate a curl handle for this download, and set its parameters
    CURL * curlHandle;
    curlHandle = curl_easy_init();

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_SHARE, s_curlShareHandle)) != CURLE_OK)
        LOG_DEBUG ("curl set opt: CURLOPT_SHARE failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_CAPATH, DOWNLOADMANAGER_TRUSTED_CERT_PATH)) != CURLE_OK)
        LOG_DEBUG ("curl set opt: CURLOPT_CAPATH failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_URL,task->url.c_str())) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_URL failed [%d]\n",curlSetOptRc);

    if ( !(task->cookieHeader.empty()) &&
        (curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_COOKIE,task->cookieHeader.c_str())) != CURLE_OK) {
            LOG_DEBUG ("curl set opt: CURLOPT_COOKIE failed [%d]\n",curlSetOptRc);
        }

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_NOSIGNAL,1L)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_NOSIGNAL failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_CONNECTTIMEOUT,60L)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_CONNECTTIMEOUT failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_LOW_SPEED_LIMIT,10L)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_LOW_SPEED_LIMIT failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_LOW_SPEED_TIME,10L)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_LOW_SPEED_TIME failed [%d]\n",curlSetOptRc);

    // if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_VERBOSE, 1)) != CURLE_OK )
    //      LOG_DEBUG ("curl set opt: CURLOPT_VERBOSE failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_NOPROGRESS, 1)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_NOPROGRESS failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_BUFFERSIZE, DOWNLOADMANAGER_DLBUFFERSIZE)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_BUFFERSIZE failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_SOCKOPTDATA, task)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_SOCKOPTDATA failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_SOCKOPTFUNCTION, cbCurlSetSocketOptions)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_SOCKOPTFUNCTION failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA,curlHandle)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_WRITEDATA failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, cbCurlWriteToFile)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_WRITEFUNCTION failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_WRITEHEADER,curlHandle)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_WRITEHEADER failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_HEADERFUNCTION, cbCurlHeaderInfo)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_HEADERFUNCTION failed [%d]\n",curlSetOptRc);

    if (interface == Wired) {
        if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_INTERFACE,const_cast<char*>(m_wiredInterfaceName.c_str()))) != CURLE_OK )
                LOG_DEBUG ("%s: [INTERFACE-CHOICE]: curl set opt: CURLOPT_INTERFACE failed [%d] for ticket %lu",__FUNCTION__,curlSetOptRc,task->ticket);
        task->connectionName = DownloadManager::connectionId2Name(Wired);
    }
    else if (interface == Wifi) {
        if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_INTERFACE,const_cast<char*>(m_wifiInterfaceName.c_str()))) != CURLE_OK )
            LOG_DEBUG ("%s: [INTERFACE-CHOICE]: curl set opt: CURLOPT_INTERFACE failed [%d] for ticket %lu",__FUNCTION__,curlSetOptRc,task->ticket);
        task->connectionName = DownloadManager::connectionId2Name(Wifi);
    }
    else if (interface == Wan) {
        if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_INTERFACE,const_cast<char*>(m_wanInterfaceName.c_str()))) != CURLE_OK )
            LOG_DEBUG ("%s: [INTERFACE-CHOICE]: curl set opt: CURLOPT_INTERFACE failed [%d] for ticket %lu",__FUNCTION__,curlSetOptRc,task->ticket);
        task->connectionName = DownloadManager::connectionId2Name(Wan);
    }
    else if (interface == Btpan) {
        if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_INTERFACE,const_cast<char*>(m_btpanInterfaceName.c_str()))) != CURLE_OK )
            LOG_DEBUG ("%s: [INTERFACE-CHOICE]: curl set opt: CURLOPT_INTERFACE failed [%d] for ticket %lu",__FUNCTION__,curlSetOptRc,task->ticket);
        task->connectionName = DownloadManager::connectionId2Name(Btpan);
    }
    else
    {
        task->connectionName = DownloadManager::connectionId2Name(ANY);     //TODO: get rid of this; really shouldn't get this far if there was no connection available
    }

//    LOG_DEBUG ("%s: [INTERFACE-CHOICE]: tried picking if=[%s] for ticket %lu (see above for any failed setopts)",__FUNCTION__,task->connectionName.c_str(),task->ticket);

    if ((range.second != 0) && (range.second > range.first))
    {
        //range specified...
        if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_RESUME_FROM_LARGE, range.first)) != CURLE_OK )
            LOG_DEBUG ("curl set opt: CURLOPT_RESUME_FROM_LARGE failed [%d]\n",curlSetOptRc);
        else
            LOG_DEBUG ("Using range: %llu - %llu\n",range.first,range.second);
    }

    if (!authToken.empty() && !deviceId.empty()) {
        struct curl_slist *slist=NULL;
        std::string authTokenHeader =  std::string("Auth-Token: ") + authToken;
        std::string deviceIdHeader =  std::string("Device-Id: ") + deviceId;
        slist = curl_slist_append(slist,authTokenHeader.c_str());
        slist = curl_slist_append(slist,deviceIdHeader.c_str());
        if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_HTTPHEADER, slist)) != CURLE_OK) {
            LOG_DEBUG ("curl set opt: CURLOPT_HTTPHEADER failed [%d]\n",curlSetOptRc);
        }

        if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_HTTPHEADER, slist)) != CURLE_OK )
            LOG_DEBUG ("curl set opt: CURLOPT_HTTPHEADER failed [%d]\n",curlSetOptRc);

        if (!task->curlDesc.setHeaderList(slist)) {
            LOG_DEBUG ("Function setHeaderList() failed");
        }
        task->deviceId = deviceId;
        task->authToken = authToken;
        //LOG_DEBUG ("added deviceId %s and authToken %s", task->deviceId.c_str(), task->authToken.c_str());
    }
    if (!task->curlDesc.setHandle(curlHandle)) {
        LOG_DEBUG ("Function setHandle() failed");
    }

    //map the curl handle to the download task here, so that we can find the task in the callback
    m_handleMap[task->curlDesc]=p_ttask;
    //..and also map ticket to the download task, so that it can be found by luna requests querying the download status of a ticket
    m_ticketMap[task->ticket]=task;

    // check whether to enqueue this or start the download immediately
    if (m_activeTaskCount < DownloadSettings::instance().maxDownloadManagerConcurrent) {
        //add it to the pool of inprogress handles (this is all inside glib curl)
        m_activeTaskCount++;
        requestWakeLock(true);
        task->queued = false;
        if (glibcurl_add(task->curlDesc.getHandle()) != 0) {
            LOG_DEBUG ("Function glibcurl_add() failed");
        }
        //LOG_DEBUG ("starting download of ticket [%lu] for url [%s]\n", task->ticket, task->url.c_str());
        m_pDlDb->addHistory(task->ticket,caller,task->connectionName,"running",task->toJSONString());
    } else {
        task->queued = true;
        m_queue.push_back(task->ticket);
        //LOG_DEBUG ("queued download of ticket [%lu]\n", task->ticket);
        m_pDlDb->addHistory(task->ticket,caller,task->connectionName,"queued",task->toJSONString());
    }

    return task->ticket;
}

int DownloadManager::resumeDownload(const unsigned long ticket,const std::string& authToken,const std::string& deviceId,std::string& r_err)
{
    DownloadHistoryDb::DownloadHistory history;

    //retrieve the ticket from the history
    if (getDownloadHistory(ticket,history) == 0) {
        r_err = "Download ticket specified does not exist in history";
        return DOWNLOADMANAGER_RESUMESTATUS_NOTINHISTORY;
    }
    gchar* escaped_errtext = g_strescape(history.m_downloadRecordJsonString.c_str(),NULL);
    if (escaped_errtext)
    {
        LOG_INFO_PAIRS_ONLY(LOGID_DOWNLOAD_RESUME,2,PMLOGKFV("ticket","%lu",ticket),PMLOGKS("History",history.m_downloadRecordJsonString.empty() ? "(no history string)" : escaped_errtext));
        g_free(escaped_errtext);
    }
    else
    {
        LOG_DEBUG("Failed to allocate memory in g_strescape function at %s", __FUNCTION__);
    }
    int rc = resumeDownload(history,false,authToken,deviceId,r_err);
    LOG_DEBUG ("%s: [RESUME] resume returned %d , err string: %s",__FUNCTION__,rc,(r_err.empty() ? "(no error)" : r_err.c_str()));
    return rc;
}

int DownloadManager::resumeDownload(const DownloadHistoryDb::DownloadHistory& history,bool autoResume,std::string& r_err)
{
    LOG_DEBUG(" resumeDownload - history json string is [%s]", history.m_downloadRecordJsonString.c_str() );
    int rc = resumeDownload(history,autoResume,std::string(),std::string(),r_err);
    LOG_DEBUG(" resumeDownload status - return value is [%d] , error string is [%s]", rc, (r_err.empty() ? "(no error)" : r_err.c_str()) );
    return rc;
}

int DownloadManager::resumeDownload(const DownloadHistoryDb::DownloadHistory& history,bool autoResume,const std::string& authToken,const std::string& deviceId,std::string& r_err)
{

    //parse the history json string
    pbnjson::JValue root = JUtil::parse(history.m_downloadRecordJsonString.c_str(), std::string(""));
    if (root.isNull()) {
        r_err = "Couldn't parse history string: "+history.m_downloadRecordJsonString;
        return DOWNLOADMANAGER_RESUMESTATUS_NOTINHISTORY;
    }

    if (history.m_state != "interrupted")
    {
        r_err = "Specified download was not interrupted";
        return DOWNLOADMANAGER_RESUMESTATUS_NOTINTERRUPTED;
    }

    if (isInterfaceUp (ANY) == false) {
        r_err = "no data connection available";
        return DOWNLOADMANAGER_RESUMESTATUS_INTERFACEDOWN;
    }


    uint64_t totalSize = 0;
    if (!root.hasKey("e_amountTotal"))
    {
        /// Old style entry, with 32 bit ints
        if (!root.hasKey("amountTotal")) {
            r_err = "amountTotal not found in the history record";
            return DOWNLOADMANAGER_RESUMESTATUS_HISTORYCORRUPT;
        }
        else
        {
            totalSize = root["amountTotal"].asNumber<int64_t>();
        }
    }
    else
    {
        totalSize = strtouq((root["e_amountTotal"].asString()).c_str(),0,10);
    }

    uint64_t completedSize = 0;
    if (!root.hasKey("e_amountReceived"))
    {
        if (!root.hasKey("amountReceived")) {
            r_err = " amountReceived not found in the history record";
            return DOWNLOADMANAGER_RESUMESTATUS_HISTORYCORRUPT;
        }
        else
        {
            completedSize = root["amountReceived"].asNumber<int64_t>();
        }
    }
    else
    {
        completedSize = strtouq((root["e_amountReceived"].asString()).c_str(),0,10);
    }

    uint64_t initialOffset = 0;
    if (!root.hasKey("e_initialOffset"))
    {
        if (!root.hasKey("initialOffset")) {
            r_err = " initialOffset not found in the history record";
            return DOWNLOADMANAGER_RESUMESTATUS_HISTORYCORRUPT;
        }
        else
        {
            initialOffset = root["initialOffset"].asNumber<int64_t>();
        }
    }
    else
    {
        initialOffset = strtouq((root["e_initialOffset"].asString()).c_str(),0,10);
    }

    std::string uri = "";
    if (root["sourceUrl"].asString(uri) != CONV_OK) {
        r_err = "sourceUrl not found in the history record";
        return DOWNLOADMANAGER_RESUMESTATUS_HISTORYCORRUPT;
    }

    std::string cookieHeader = "";
    // if it's there, use it, but no problem if missing
    cookieHeader = root["cookieHeader"].asString();

    std::string destTempFile = "";
    if (root["target"].asString(destTempFile) != CONV_OK) {
        r_err = "target (dest tmp file name) not found in history record";
        return DOWNLOADMANAGER_RESUMESTATUS_HISTORYCORRUPT;
    }

    std::string destTempPrefix = "";
    destTempPrefix = root["destTempPrefix"].asString();

    //check free space on disk
    uint64_t spaceFreeKB = 0;
    uint64_t spaceTotalKB = 0;
    if (! DownloadManager::spaceOnFs(destTempFile,spaceFreeKB,spaceTotalKB))
    {
        //can't stat the filesys...treat the same as out of space
        return DOWNLOADMANAGER_RESUMESTATUS_FILESYSTEMFULL;
    }

    bool stopMarkReached = false;
    this->filesystemStatusCheck(spaceFreeKB,spaceTotalKB,NULL,&stopMarkReached);

    uint64_t remainSize = totalSize - completedSize;
    if (DownloadSettings::instance().preemptiveFreeSpaceCheck)
    {
        if ((spaceFreeKB < (remainSize >> 10)) || (stopMarkReached))
        {
            return DOWNLOADMANAGER_RESUMESTATUS_FILESYSTEMFULL;
        }
    }
    //the target must exist
    if (g_file_test (destTempFile.c_str(), G_FILE_TEST_EXISTS) == false)
    {
        LOG_DEBUG ("%s: Did not find partial file [%s] on the filesys! Restarting download",__FUNCTION__,destTempFile.c_str());
        //if not, the resume-from offset will be 0 and it'll be just like a normal download (not resume)
        completedSize = 0;
    }
    else
    {
        LOG_DEBUG ("%s: Will attempt to resume partial file [%s] at pos = %llu",__FUNCTION__,destTempFile.c_str(),completedSize);
    }

    std::string destFinalFile = "";
    if (root["destFile"].asString(destFinalFile) != CONV_OK) {
        r_err = "destFile (dest file name) not found in history record";
        return DOWNLOADMANAGER_RESUMESTATUS_HISTORYCORRUPT;
    }

    std::string destFinalPath = "";
    if (root["destPath"].asString(destFinalPath) != CONV_OK) {
        r_err = "destPath (dest path name) not found in history record";
        return DOWNLOADMANAGER_RESUMESTATUS_HISTORYCORRUPT;
    }
    bool canHandlePause = false;
    if (!root.hasKey("canHandlePause")) {
        LOG_DEBUG ("canHandlePause not found in the history record, assuming false");
    }
    else {
        canHandlePause = root["canHandlePause"].asBool();
    }

    bool taskAutoResume = true;
    if (!root.hasKey("autoResume"))
        LOG_DEBUG("autoResume not found in the history record, assuming true");
    else
        taskAutoResume = root["autoResume"].asBool();
    if (autoResume && !taskAutoResume)
    {
        r_err = "this task does not support auto resume";
        return DOWNLOADMANAGER_RESUMESTATUS_GENERALERROR;
    }

    std::string deviceIdToUse;
    if (deviceId.empty()) {
        deviceIdToUse = root["deviceId"].asString();
    }
    else {
        deviceIdToUse = deviceId;
    }

    std::string authTokenToUse;
    if (authToken.empty()) {
        authTokenToUse = root["authToken"].asString();
    }
    else {
        authTokenToUse = authToken;
    }

    //done with the input object

    //ok, it was interrupted, and its fields are valid. Create a new task for it, but with the original ticket number

    // if the queue is already full, no point in continuing
    if (m_queue.size() >= DownloadSettings::instance().maxDownloadManagerQueueLength) {
        return DOWNLOADMANAGER_RESUMESTATUS_QUEUEFULL;
    }

    FILE * fp = NULL;
    std::string wrmode;
    if (completedSize == 0)         //to deal with problems in the write to out-of-space disk issue
        wrmode = "wb";
    else
        wrmode = "ab";

    if ((fp = fopen(destTempFile.c_str(),wrmode.c_str())) == NULL) {
        r_err = "cannot open temp file in write/update mode";
        return DOWNLOADMANAGER_RESUMESTATUS_CANNOTACCESSTEMP;
    }

    //seek to the correct place
    //LOG_DEBUG ("%s: file ptr is currently at %lu; moving file ptr to %u...",__FUNCTION__,ftell(fp),completedSize);
    if (fseek(fp,completedSize-initialOffset,SEEK_SET) != 0)
    {
        LOG_WARNING_PAIRS (LOGID_RESUME_FSEEK_FAIL, 1, PMLOGKFV("ptr", "%lu", ftell(fp)), "moving file ptr failed");
        if (fclose(fp) != 0) {
            LOG_DEBUG ("Function fclose() failed");
        }
        r_err = "cannot open temp file: seek-set failed";
        return DOWNLOADMANAGER_RESUMESTATUS_CANNOTACCESSTEMP;
    }

    DownloadTask* p_dlTask = new DownloadTask;
    TransferTask * p_ttask = new TransferTask(p_dlTask);

    p_dlTask->fp = fp;

    if (!m_glibCurlInitialized)
        startupGlibCurl();

    p_dlTask->bytesCompleted = completedSize;
    p_dlTask->bytesTotal = totalSize;
    p_dlTask->setUpdateInterval();
    p_dlTask->initialOffsetBytes = initialOffset;

    //parse the URI/URL
    //UrlRep parsedUrl = UrlRep::fromUrl(uri.c_str());

    p_dlTask->ticket = history.m_ticket;
    p_dlTask->opt_keepOriginalFilenameOnRedirect = false;
    p_dlTask->url = uri;
    p_dlTask->cookieHeader = cookieHeader;
    p_dlTask->destPath = destFinalPath;
    p_dlTask->destFile = destFinalFile;
    p_dlTask->downloadPrefix = destTempPrefix;
    p_dlTask->ownerId = history.m_owner;
    p_dlTask->canHandlePause = canHandlePause;
    p_dlTask->autoResume = taskAutoResume;

     //LOG_DEBUG ("%s: Interface %s and allow1x is %s",__FUNCTION__,history.m_interface.c_str(),(s_allow1x ? "TRUE" : "FALSE"));
    if (isInterfaceUp(connectionName2Id(history.m_interface)))
    {
        p_dlTask->connectionName = history.m_interface;
    }
    else
    {
        LOG_WARNING_PAIRS_ONLY (LOGID_INTERFACE_FAIL_ON_RESUME, 1, PMLOGKS("interface name", history.m_interface.c_str()));
        //determine a good interface to use
        if (m_wiredConnectionStatus == DownloadManager::InetConnectionConnected)
        p_dlTask->connectionName = connectionId2Name (Wired);
        else if (m_wifiConnectionStatus == DownloadManager::InetConnectionConnected)
        p_dlTask->connectionName = connectionId2Name (Wifi);
        else if (m_wanConnectionStatus == DownloadManager::InetConnectionConnected)
        p_dlTask->connectionName = connectionId2Name (Wan);
        else if (m_btpanConnectionStatus == DownloadManager::InetConnectionConnected)
        p_dlTask->connectionName = connectionId2Name (Btpan);
    }

    CURL * curlHandle;
    curlHandle = curl_easy_init();
    int curlSetOptRc;

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_SHARE, s_curlShareHandle)) != CURLE_OK)
        LOG_DEBUG ("curl set opt: CURLOPT_SHARE failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_CAPATH, DOWNLOADMANAGER_TRUSTED_CERT_PATH)) != CURLE_OK)
        LOG_DEBUG ("curl set opt: CURLOPT_CAPATH failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_URL,p_dlTask->url.c_str())) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_URL failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_NOSIGNAL,1L)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_NOSIGNAL failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_CONNECTTIMEOUT,30L)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_CONNECTTIMEOUT failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_LOW_SPEED_LIMIT,10L)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_LOW_SPEED_LIMIT failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_LOW_SPEED_TIME,10L)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_LOW_SPEED_TIME failed [%d]\n",curlSetOptRc);

    //  if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_VERBOSE, 1)) != CURLE_OK )
    //      LOG_DEBUG ("curl set opt: CURLOPT_VERBOSE failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_NOPROGRESS, 1)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_NOPROGRESS failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_BUFFERSIZE, DOWNLOADMANAGER_DLBUFFERSIZE)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_BUFFERSIZE failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_SOCKOPTDATA, p_dlTask)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_SOCKOPTDATA failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_SOCKOPTFUNCTION, cbCurlSetSocketOptions)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_SOCKOPTFUNCTION failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA,curlHandle)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_WRITEDATA failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, cbCurlWriteToFile)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_WRITEFUNCTION failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_WRITEHEADER,curlHandle)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_WRITEHEADER failed [%d]\n",curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_HEADERFUNCTION, cbCurlHeaderInfo)) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_HEADERFUNCTION failed [%d]\n",curlSetOptRc);

    if (DownloadManager::connectionName2Id(p_dlTask->connectionName) == Wired) {
                if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_INTERFACE,const_cast<char*>(m_wiredInterfaceName.c_str()))) != CURLE_OK )
                        LOG_DEBUG ("curl set opt: CURLOPT_INTERFACE failed [%d]\n",curlSetOptRc);
    }
    else if (DownloadManager::connectionName2Id(p_dlTask->connectionName) == Wifi) {
        if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_INTERFACE,const_cast<char*>(m_wifiInterfaceName.c_str()))) != CURLE_OK )
            LOG_DEBUG ("curl set opt: CURLOPT_INTERFACE failed [%d]\n",curlSetOptRc);

    }
    else if (DownloadManager::connectionName2Id(p_dlTask->connectionName) == Wan) {
        if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_INTERFACE,const_cast<char*>(m_wanInterfaceName.c_str()))) != CURLE_OK )
            LOG_DEBUG ("curl set opt: CURLOPT_INTERFACE failed [%d]\n",curlSetOptRc);

    }
    else if (DownloadManager::connectionName2Id(p_dlTask->connectionName) == Btpan) {
        if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_INTERFACE,const_cast<char*>(m_btpanInterfaceName.c_str()))) != CURLE_OK )
            LOG_DEBUG ("curl set opt: CURLOPT_INTERFACE failed [%d]\n",curlSetOptRc);

    }

    if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)(p_dlTask->bytesCompleted))) != CURLE_OK )
        LOG_DEBUG ("curl set opt: CURLOPT_RESUME_FROM_LARGE failed [%d]\n",curlSetOptRc);

    if (!authTokenToUse.empty() && !deviceIdToUse.empty()) {
        struct curl_slist *slist=NULL;
        std::string authTokenHeader =  std::string("Auth-Token: ") + authTokenToUse;
        std::string deviceIdHeader =  std::string("Device-Id: ") + deviceIdToUse;
        slist = curl_slist_append(slist,authTokenHeader.c_str());
        slist = curl_slist_append(slist,deviceIdHeader.c_str());

        if ((curlSetOptRc = curl_easy_setopt(curlHandle, CURLOPT_HTTPHEADER, slist)) != CURLE_OK )
            LOG_DEBUG ("curl set opt: CURLOPT_HTTPHEADER failed [%d]\n",curlSetOptRc);

        if (!p_dlTask->curlDesc.setHeaderList(slist)) {
            LOG_DEBUG ("Function setHeaderList() failed");
        }
        p_dlTask->deviceId = deviceIdToUse;
        p_dlTask->authToken = authTokenToUse;
    }
    if (!p_dlTask->curlDesc.setHandle(curlHandle)) {
        LOG_DEBUG ("Function setHandle() failed");
    }

    //map the curl handle to the download task here, so that we can find the task in the callback
    m_handleMap[p_dlTask->curlDesc]=p_ttask;
    //..and also map ticket to the download task, so that it can be found by luna requests querying the download status of a ticket
    m_ticketMap[p_dlTask->ticket]=p_dlTask;

    // check whether to enqueue this or start the download immediately
    if (m_activeTaskCount < DownloadSettings::instance().maxDownloadManagerConcurrent) {
        //add it to the pool of inprogress handles (this is all inside glib curl)
        m_activeTaskCount++;
        requestWakeLock(true);
        p_dlTask->queued = false;
        if (glibcurl_add(p_dlTask->curlDesc.getHandle()) != 0) {
            LOG_DEBUG ("Function glibcurl_add() failed");
        }
        //LOG_DEBUG ("starting (resuming) download of ticket [%lu] for url [%s] on interface [%s]\n", p_dlTask->ticket, p_dlTask->url.c_str(),p_dlTask->connectionName.c_str());
        m_pDlDb->addHistory(p_dlTask->ticket,p_dlTask->ownerId,p_dlTask->connectionName,"running",p_dlTask->toJSONString());
    } else {
        p_dlTask->queued = true;
        m_queue.push_back(p_dlTask->ticket);
        //LOG_DEBUG ("queued download of ticket [%lu]\n", p_dlTask->ticket);
        m_pDlDb->addHistory(p_dlTask->ticket,p_dlTask->ownerId,p_dlTask->connectionName,"queued",p_dlTask->toJSONString());
    }

    return DOWNLOADMANAGER_RESUMESTATUS_OK;

}

void DownloadManager::resumeAll()
{
    //go through all interrupted downloads from the db and resume each
    std::vector<DownloadHistoryDb::DownloadHistory> interrupteds;
    std::string err;
    int rc=0;

    if (!m_pDlDb) {
        LOG_DEBUG ("Function resumeAll() failed: No m_pDlDb");
        return;
    }
    LOG_DEBUG ("%s: [RESUME]",__FUNCTION__);

    if (m_pDlDb->getDownloadHistoryRecordsForState("interrupted",interrupteds) == 0) {
        LOG_DEBUG ("Function getDownloadHistoryRecordsForState() failed");
    }
    for (std::vector<DownloadHistoryDb::DownloadHistory>::iterator it = interrupteds.begin();it != interrupteds.end();++it)
    {
        //TODO: handle error for each resume and optionally report to subscription of download ticket
        int rcTemp = resumeDownload(*it,false,err);
        LOG_DEBUG(" resumeDownload status - return value is [%d] , error string is [%s]", rcTemp, (err.empty() ? "(no error)" : err.c_str()) );
        ++rc;
    }

    LOG_DEBUG ("Function resumeAll() finished: rc(%d)", rc);
    return;
}

void DownloadManager::resumeAllForInterface(Connection interface, bool autoResume)
{
    //go through all interrupted downloads from the db and resume each
    std::vector<DownloadHistoryDb::DownloadHistory> interrupteds;
    std::string err;
    int rc=0;

    if (!m_pDlDb) {
        LOG_DEBUG ("Function resumeAllForInterface() failed: No m_pDlDb");
        return;
    }

    std::string ifaceName = DownloadManager::connectionId2Name(interface);
	LOG_DEBUG("Resume Download Interfaces : Interfaces - %s", ifaceName.c_str());
    if (m_pDlDb->getDownloadHistoryRecordsForStateAndInterface("interrupted",ifaceName,interrupteds) == 0) {
        LOG_DEBUG ("Function getDownloadHistoryRecordsForStateAndInterface() failed");
    }
    for (std::vector<DownloadHistoryDb::DownloadHistory>::iterator it = interrupteds.begin();it != interrupteds.end();++it)
    {
        //TODO: handle error for each resume and optionally report to subscription of download ticket
        int rcTemp = resumeDownload(*it,autoResume,err);
        LOG_DEBUG(" resumeDownload status - return value is [%d] , error string is [%s]", rcTemp, (err.empty() ? "(no error)" : err.c_str()) );
        ++rc;
    }

    LOG_DEBUG ("Function resumeAllForInterface() finished: rc(%d)", rc);
    return;
}

int DownloadManager::resumeDownloadOnAlternateInterface(DownloadHistoryDb::DownloadHistory& history,Connection newInterface,bool autoResume)
{
    std::string err;
    history.m_interface = DownloadManager::connectionId2Name(newInterface);
    return resumeDownload(history,autoResume,err);
}

void DownloadManager::resumeMultipleOnAlternateInterface(Connection oldInterface,Connection newInterface,bool autoResume)
{
    //go through all interrupted downloads from the db and resume each
    std::vector<DownloadHistoryDb::DownloadHistory> interrupteds;
    std::string err;
    int rc=0;

    if (!m_pDlDb) {
        LOG_DEBUG ("Function resumeMultipleOnAlternateInterface() failed: No m_pDlDb");
        return;
    }

    std::string ifaceName = DownloadManager::connectionId2Name(oldInterface);
    if (m_pDlDb->getDownloadHistoryRecordsForStateAndInterface("interrupted",ifaceName,interrupteds) == 0) {
        LOG_DEBUG ("Function getDownloadHistoryRecordsForStateAndInterface() failed");
    }
    for (std::vector<DownloadHistoryDb::DownloadHistory>::iterator it = interrupteds.begin();it != interrupteds.end();++it)
    {
        //TODO: handle error for each resume and optionally report to subscription of download ticket
        int ret = resumeDownloadOnAlternateInterface(*it,newInterface,autoResume);
        if (ret != 1) {
            LOG_DEBUG ("Wrong resumeStatus after resumeDownloadOnAlternateInterface(): %d", ret);
        }
        ++rc;
    }

    LOG_DEBUG ("Function resumeMultipleOnAlternateInterface() finished: rc(%d)", rc);
    return;
}

/*
 * MODIFIES: m_handleMap , m_ticketMap
 *
 */
int DownloadManager::pauseDownload(const unsigned long ticket,bool allowQueuedToStart)
{
    std::map<long,DownloadTask*>::iterator iter = m_ticketMap.find (ticket);
    if (iter == m_ticketMap.end() || iter->second == NULL) {
        //nothing to do..this task didn't exist
        return DOWNLOADMANAGER_PAUSESTATUS_NOSUCHDOWNLOADTASK;
    }

    if (!iter->second->canHandlePause) {
        LOG_WARNING_PAIRS (LOGID_NOTABLE_TO_PAUSE, 1, PMLOGKFV("ticket", "%lu", ticket), "cannot handle this pause request, it will be canceled");
        if (!cancel(ticket)) {
            LOG_DEBUG ("Function cancel() failed: id(%lu)", ticket);
        }
        return DOWNLOADMANAGER_PAUSESTATUS_NOSUCHDOWNLOADTASK;
    }

    TransferTask * _task = removeTask_dl(ticket);

    if (_task == NULL) {
        //nothing to do..this task didn't exist
        return DOWNLOADMANAGER_PAUSESTATUS_NOSUCHDOWNLOADTASK;
    }

    DownloadTask * task = _task->p_downloadTask;
    LOG_INFO_PAIRS_ONLY(LOGID_DOWNLOAD_PAUSE,2, PMLOGKFV("ticket","%lu",task->ticket), PMLOGKS("url",task->url.c_str()));


    LSError lserror;
    LSErrorInit(&lserror);

    pbnjson::JValue payloadJsonObj = task->toJSON();
    std::string payload;

    payloadJsonObj.put("completionStatusCode", DOWNLOADMANAGER_COMPLETIONSTATUS_INTERRUPTED);

    std::string dest = task->destPath + task->downloadPrefix + task->destFile;
    payloadJsonObj.put("interrupted", true);
    payloadJsonObj.put("completed", false);
    payloadJsonObj.put("aborted", false);
    payloadJsonObj.put("target", dest);

    payload = JUtil::toSimpleString(payloadJsonObj);

    if (!(DownloadSettings::instance().appCompatibilityMode)) {
        if (!postDownloadUpdate(task->ownerId, ticket,payload)) {
            LOG_WARNING_PAIRS (LOGID_SUBSCRIPTIONREPLY_FAIL_ON_PAUSE, 1, PMLOGKFV("ticket", "%lu", ticket), "failed to update pause status to subscribers");
        }
    }

    std::string historyString = JUtil::toSimpleString(payloadJsonObj);
    //add to database record
    m_pDlDb->addHistory(task->ticket,task->ownerId,task->connectionName,"interrupted",historyString);
    if (!removeTask_dl(task->ticket)) {
        LOG_DEBUG ("Function removeTask_dl() failed");
    }
    delete _task;

    // if an active task has been paused, the next download should start
    if (!m_queue.empty() && (m_activeTaskCount < DownloadSettings::instance().maxDownloadManagerConcurrent) && allowQueuedToStart) {
        unsigned long queuedTicket = m_queue.front();
        m_queue.pop_front();
        std::map<long,DownloadTask*>::iterator iter = m_ticketMap.find(queuedTicket);
        if (iter != m_ticketMap.end()) {
            DownloadTask* nextDownload = iter->second;
            nextDownload->queued = false;
            m_activeTaskCount++;
            requestWakeLock(true);
            if (glibcurl_add(nextDownload->curlDesc.getHandle()) != 0) {
                LOG_DEBUG ("Function glibcurl_add() failed");
            }
            //LOG_DEBUG ("%s: starting download of ticket [%lu] for url [%s] deviceId %s authToken %s\n", __PRETTY_FUNCTION__,
                //nextDownload->ticket, nextDownload->url.c_str(), nextDownload->authToken.c_str(), nextDownload->deviceId.c_str());
            m_pDlDb->addHistory(nextDownload->ticket,nextDownload->ownerId,task->connectionName,"running",nextDownload->toJSONString());
        }
    }
    return DOWNLOADMANAGER_PAUSESTATUS_OK;
}

void DownloadManager::pauseAll()
{
    //run through the whole ticket map and call pause download on all...flag pause() so that it doesn't start queued downloads
    //Can't do this directly because the pause() fn modifies the map I'm iterating on. Do it via intermediate list
    std::list<long> tickets;
    for (std::map<long,DownloadTask*>::iterator it = m_ticketMap.begin();it != m_ticketMap.end();++it)
        tickets.push_back(it->first);
    for (std::list<long>::iterator it = tickets.begin();it != tickets.end();++it)
        if (pauseDownload(*it,false) != 1) {
            LOG_DEBUG ("Function pauseDownload() failed");
        }

    return;
}

void DownloadManager::pauseAllForInterface(Connection interface)
{
    //run through the whole ticket map and call pause download on all that match the connection specified...flag pause() so that it doesn't start queued downloads
    //Can't do this directly because the pause() fn modifies the map I'm iterating on. Do it via intermediate list
    std::list<long> tickets;
    for (std::map<long,DownloadTask*>::iterator it = m_ticketMap.begin();it != m_ticketMap.end();++it) {
        if (interface == DownloadManager::connectionName2Id(it->second->connectionName))
            tickets.push_back(it->first);
    }
    for (std::list<long>::iterator it = tickets.begin();it != tickets.end();++it)
    {
        LOG_DEBUG( "Pausing download for TICKET - %ld",*it );
        if (pauseDownload(*it,false) != 1) {
            LOG_DEBUG ("Function pauseDownload() failed");
        }
    }
    return;
}

int DownloadManager::swapToInterface(const unsigned long int ticket,const Connection newInterface)
{
    //the interface cannot be ANY
    if (newInterface == ANY)
        return SWAPTOIF_ERROR_INVALIDIF;

    //find the ticket in the map
    std::map<long,DownloadTask*>::iterator it = m_ticketMap.find(ticket);
    if (it == m_ticketMap.end())
        return SWAPTOIF_ERROR_NOSUCHTICKET;

    DownloadTask * pDltask = it->second;
    //if the task interface is ANY, cannot be swapped
    if (pDltask->connectionName == DownloadManager::connectionId2Name(ANY))
        return SWAPTOIF_ERROR_INVALIDIF;
    //check the interface the task is on right now
    if (pDltask->connectionName == DownloadManager::connectionId2Name(newInterface))
        return SWAPTOIF_SUCCESS;        //already on the specified interface

    if (pDltask->queued == false)
    {
        //remove the curl descriptor from glib_curl temporarily         TODO: check for error
        if (glibcurl_remove(pDltask->curlDesc.getHandle()) != 0) {
            LOG_DEBUG ("Function glibcurl_remove() failed");
        }
    }
    //change its interface option

    std::string ifaceName;
    switch (newInterface)
    {
    case Wired:
        ifaceName = m_wiredInterfaceName;
        break;
    case Wifi:
        ifaceName = m_wifiInterfaceName;
        break;
    case Wan:
        ifaceName = m_wanInterfaceName;
        break;
    case Btpan:
        ifaceName = m_btpanInterfaceName;
    case ANY:
        return SWAPTOIF_ERROR_INVALIDIF;        //to make switch happy
    }

    pDltask->connectionName = DownloadManager::connectionId2Name(newInterface);
    int curlSetOptRc;
    if (( curlSetOptRc = curl_easy_setopt(pDltask->curlDesc.getHandle(), CURLOPT_INTERFACE,const_cast<char*>(ifaceName.c_str()))) != CURLE_OK )
        LOG_DEBUG ("%s: curl set opt: CURLOPT_INTERFACE to if=[%s] failed [%d]",__FUNCTION__,ifaceName.c_str(),curlSetOptRc);

    if ((curlSetOptRc = curl_easy_setopt(pDltask->curlDesc.getHandle(), CURLOPT_RESUME_FROM_LARGE, (uint64_t)(pDltask->bytesCompleted))) != CURLE_OK )
            LOG_DEBUG ("%s: curl set opt: CURLOPT_RESUME_FROM_LARGE failed [%d]",__FUNCTION__,curlSetOptRc);

    if (pDltask->queued == false)
    {
        //re-add the handle
        if (glibcurl_add(pDltask->curlDesc.getHandle()) != 0) {
            LOG_DEBUG ("Function glibcurl_add() failed");
        }
        //change its history record to reflect the new interface
        m_pDlDb->addHistory(pDltask->ticket,pDltask->ownerId,pDltask->connectionName,"running",pDltask->toJSONString());
    }
    else
    {
        //change its history record to reflect the new interface
        m_pDlDb->addHistory(pDltask->ticket,pDltask->ownerId,pDltask->connectionName,"queued",pDltask->toJSONString());
    }

    return SWAPTOIF_SUCCESS;
}

int DownloadManager::swapAllActiveToInterface(const Connection newInterface)
{
    if (newInterface == ANY)
        return SWAPALLTOIF_ERROR_INVALIDIF;

    bool success = true;
    for (std::map<long,DownloadTask*>::iterator it = m_ticketMap.begin();it != m_ticketMap.end();++it)
    {
        if (swapToInterface(it->first,newInterface) != SWAPTOIF_SUCCESS)
            success = false;
    }

    if (!success)
        return SWAPALLTOIF_ERROR_ATLEASTONEFAIL;
    return SWAPALLTOIF_SUCCESS;
}

// handle the header response from the server
size_t DownloadManager::cbHeader(CURL * taskHandle,size_t headerSize,const char * headerText)
{

    if (headerText == NULL)
    {
        //LOG_DEBUG ("%s: header text was null. Function-Exit-Early",__FUNCTION__);
        return headerSize;
    }
    //try and retrieve the task from the curl handle
    TransferTask * _task = getTask(taskHandle);
    if (_task == NULL)
    {
        //LOG_DEBUG ("%s: TransferTask not found in handle map. Function-Exit-Early",__FUNCTION__);
        return headerSize;
    }

    std::string header = headerText;

    ////LOG_DEBUG ("cbHeader(): %s\n",header.c_str());
    //find the :
    size_t labelendpos = header.find(":",0);
    if (labelendpos == std::string::npos) {
        //LOG_DEBUG ("%s: header string = %s (Function-Exit-Early)",__FUNCTION__,header.c_str());
        return headerSize;
    }

    std::string headerLabel = header.substr(0,labelendpos);
    std::transform(headerLabel.begin(), headerLabel.end(), headerLabel.begin(), tolower);

    std::string headerContent = header.substr(labelendpos+1,header.size());
    headerContent = trimWhitespace(headerContent);

    if (headerLabel.compare("location") == 0) {
        //possible redirect code...store it for later
        //LOG_DEBUG ("%s: Location header found: %s",__FUNCTION__,header.c_str());
        _task->setLocationHeader(headerContent);
        return headerSize;
    }

    //PAST THIS POINT, IT MUST BE A DOWNLOAD TASK

    if (_task->type != TransferTask::DOWNLOAD_TASK)
    {
        //LOG_DEBUG ("%s: TransferTask is not a Download. Function-Exit-Early",__FUNCTION__);
        return headerSize;
    }

    DownloadTask * task = _task->p_downloadTask;

    //LOG_DEBUG ("cbHeader(): headerLabel = %s , headerContent = %s\n",headerLabel.c_str(),headerContent.c_str());
    if (headerLabel.compare("content-length") == 0) {

        //LOG_DEBUG ("%s: BYTES RECEIVED SO FAR = %lu , BYTES TOTAL = %lu",__FUNCTION__,task->bytesCompleted,task->bytesTotal);
        //parse the length
        uint64_t contentLength = strtouq(headerContent.c_str(),NULL,10);

        if (contentLength == 0)
        {
            //LOG_DEBUG ("%s: Content-Length was 0",__FUNCTION__);
            return headerSize;          //get out early - content length 0 is useless info
        }
        else
        {
            //LOG_DEBUG ("%s: Content-Length was %lu",__FUNCTION__,contentLength);
        }
        //if initially the content-length was 0 and then some data was downloaded, and now the download is resuming,
        //              then the content length will be incorrect because the server is sending the REMAINING length
        //              ...fix it up by adding what was already received

        if ((task->bytesCompleted > 0) && (task->bytesTotal == 0))
        {
            task->bytesTotal = contentLength + task->bytesCompleted;
            task->setUpdateInterval();
            LOG_DEBUG ("%s: Fixing up Content-Length to %llu, and this looks like a Resume download",__FUNCTION__,task->bytesTotal);
        }
        else if (task->bytesCompleted == 0)
        {
            // THIS IS A FRESHLY STARTED DOWNLOAD, NOT A RESUME
            //LOG_DEBUG ("%s: Content-Length = %lu, and this is a FreshStart download",__FUNCTION__,task->bytesTotal);
            task->bytesTotal = contentLength;
            task->setUpdateInterval();
            //LOG_DEBUG ("%s: Updated Content-Length = %lu, and this is a FreshStart download",__FUNCTION__,task->bytesTotal);
            // 1.3.5 - try and "truncate" the file to the size reported, so that the space is "taken"
            /*
            int fdes;
            if ((fdes = fileno(task->fp)) != -1)
            {
                ::ftruncate(fdes,task->bytesTotal);
            }
            */

        }
    }
    else if (headerLabel.compare("content-type") == 0) {
        //get the MIME type that the server is reporting
        task->setMimeType(headerContent);
    }

//    LOG_DEBUG ("%s Function-Exit",__FUNCTION__);
    return headerSize;
}

void DownloadManager::cbGlib()
{
    CURLMsg* msg;
    int inQueue;
    TransferTask * _task = NULL;
    long l_httpCode = 0;
    long l_httpConnectCode;
    DownloadTask * dl_task = NULL;
    UploadTask * ul_task = NULL;
//    LOG_DEBUG ("%s Function-Entry",__FUNCTION__);

    while (1) {
        msg = curl_multi_info_read(glibcurl_handle(), &inQueue);
        if (msg == 0) {
            break;
        }

        if (msg->msg == CURLMSG_DONE)  {

            //LOG_DEBUG ("%s: CURLMSG_DONE\n", __PRETTY_FUNCTION__);
            /// Note: Error handling will NOT be done here...it will be done in completed()
            ///...but store the results here before removeTask annihilates the curl handle

            l_httpCode = 0;
            l_httpConnectCode = 0;
            CURLcode inforc = curl_easy_getinfo(msg->easy_handle,CURLINFO_RESPONSE_CODE,&l_httpCode);
            if (inforc != CURLE_OK) {
                LOG_WARNING_PAIRS_ONLY (LOGID_CURL_FAIL_HTTP_STATUS, 1, PMLOGKS("reason", "failed to retrieve HTTP status code from handle"));
            }

            inforc = curl_easy_getinfo(msg->easy_handle,CURLINFO_HTTP_CONNECTCODE,&l_httpConnectCode);
            if (inforc != CURLE_OK) {
                LOG_WARNING_PAIRS_ONLY (LOGID_CURL_FAIL_HTTP_CONNECT, 1, PMLOGKS("reason", "failed to retrieve HTTP connect code from handle"));
            }

            CURLcode resultCode = msg->data.result;

            //is it a download or an upload
            _task = removeTask(msg->easy_handle);

            if (_task == NULL)
                break;

            if (_task->type == TransferTask::DOWNLOAD_TASK) {

                //complete this transfer..remove the task...
                dl_task = _task->p_downloadTask;
                if (dl_task != NULL) {
                    if (dl_task->curlDesc.setResultCode(resultCode) != 0) {
                        LOG_DEBUG ("Function setResultCode() failed");
                    }
                    dl_task->curlDesc.setHttpResultCode(l_httpCode);
                    dl_task->curlDesc.setHttpConnectCode(l_httpConnectCode);
                }
            }
            else if (_task->type == TransferTask::UPLOAD_TASK) {
                ul_task = _task->p_uploadTask;
                if (ul_task != NULL) {
                    ul_task->setCURLCode(resultCode);
                    ul_task->setHTTPCode(l_httpCode);
                }
            }

            //complete the task
            completed(_task);
        }
        else {
            LOG_WARNING_PAIRS_ONLY (LOGID_UNKNOWN_MSG_CBGLIB, 1, PMLOGKFV("msg code", "%d", msg->msg));
        }
    }

//  LOG_DEBUG ("%s Function-Exit",__FUNCTION__);
}

size_t DownloadManager::cbWriteEvent (CURL * taskHandle,size_t payloadSize,unsigned char * payload)
{
//  LOG_DEBUG ("%s Function-Entry",__FUNCTION__);

    //note: returning != payloadSize from this will kill the transfer

    //try and retrieve the task from the curl handle
    TransferTask * _task = getTask(taskHandle);
    if (_task == NULL)
    {
        //LOG_DEBUG ("%s: TransferTask not found in handle map. Function-Exit-Early",__FUNCTION__);
        return 0;
    }

    //if the task is marked for deletion, then get out now
    if (_task->m_remove)
    {
        //LOG_DEBUG ("%s: TransferTask is marked to be removed. Function-Exit-Early",__FUNCTION__);
        return 0;
    }

    if ((_task->type != TransferTask::DOWNLOAD_TASK)) {
        //LOG_DEBUG ("%s: TransferTask is not a Download. Function-Exit-Early",__FUNCTION__);
        return 0;
    }
    DownloadTask * task = _task->p_downloadTask;

    //write to file if the fp is not null
    size_t nwritten = 0;
    if (task->fp) {
        nwritten = fwrite(payload,1,payloadSize,task->fp);
        if ((nwritten < (size_t)payloadSize))
        {
            task->numErrors = DOWNLOADMANAGER_ERRORTHRESHOLD;           //hack...fail it immediately  TODO: rewrite this
        }
    }

    if ((task->fp == NULL) || (task->numErrors >= DOWNLOADMANAGER_ERRORTHRESHOLD)) {
        LOG_WARNING_PAIRS (LOGID_EXCEED_ERROR_THRESHOLD, 2, PMLOGKFV("total bytes", "%zu", nwritten),
                                                            PMLOGKFV("payload size", "%zu", payloadSize),
                                                            "this task will be discarded, marked as 'removed'");
        //null file pointer? or num errors during this transfer was too high... this download isn't going anywhere...complete it
        _task->m_remove = true;
        task->bytesCompleted = 0;           //file is basically unusable here           TODO: investigate issues with append
        payloadSize = 0;            //this will kill the transfer when it is returned, below
        goto Return_cbWriteEvent;
    }

    //update the bytesCompleted
    task->bytesCompleted += payloadSize;
//    LOG_DEBUG ("%s: Task bytes completed now = %ld",__FUNCTION__,task->bytesCompleted);

    if ((task->lastUpdateAt == 0) || (task->bytesCompleted - task->lastUpdateAt >= task->updateInterval)) {
        LSError lserror;
        LSErrorInit(&lserror);
        std::string key = ConvertToString<unsigned long>(task->ticket);
        std::string bytesCompletedStr = ConvertToString<uint32_t>((uint32_t)(task->bytesCompleted));
        std::string e_bytesCompletedStr = ConvertToString<uint64_t>(task->bytesCompleted);
        std::string bytesTotalStr = ConvertToString<uint32_t>((uint32_t)(task->bytesTotal));
        std::string e_bytesTotalStr = ConvertToString<uint64_t>(task->bytesTotal);
        std::string response = std::string("{ \"ticket\":")
            +key
            +std::string(" , \"amountReceived\":")
            +bytesCompletedStr
            +std::string(" , \"e_amountReceived\":\"")
            +e_bytesCompletedStr + std::string("\"")
            +std::string(" , \"amountTotal\":")
            +bytesTotalStr
            +std::string(" , \"e_amountTotal\":\"")
            +e_bytesTotalStr + std::string("\"")
            +std::string(" }");

        if (fdatasync(fileno(task->fp)) != 0) {
            LOG_DEBUG ("Function fdatasync() failed");
        }
        if (!postDownloadUpdate (task->ownerId, task->ticket, response)) {
            LOG_WARNING_PAIRS (LOGID_SUBSCRIPTIONREPLY_FAIL_ON_WRITEDATA, 2, PMLOGKS("ticket", key.c_str()),
                                                                            PMLOGKS("detail", response.c_str()),
                                                                            "failed to update write-progress to subscribers");
        }
        else {
            LOG_DEBUG ("[download progress] sent [%s] to subscriptions for ticket [%s]",response.c_str(),key.c_str());
        }

        task->lastUpdateAt = task->bytesCompleted;
    }

Return_cbWriteEvent:

//  LOG_DEBUG ("%s Function-Exit",__FUNCTION__);
    return payloadSize;
}

size_t DownloadManager::cbReadEvent(CURL* taskHandle,size_t payloadSize,unsigned char * payload)
{
//  LOG_DEBUG ("%s Function-Entry",__FUNCTION__);

    //try and retrieve the task from the curl handle
    TransferTask * _task = getTask(taskHandle);
    if (_task == NULL)
    {
        LOG_DEBUG ("%s: TransferTask not found in handle map. Function-Exit-Early",__FUNCTION__);
        return 0;
    }

    //if the task is marked for deletion, then get out now
    if (_task->m_remove)
    {
        LOG_DEBUG ("%s: TransferTask is marked to be removed. Function-Exit-Early",__FUNCTION__);
        return 0;
    }

    if (_task->type != TransferTask::DOWNLOAD_TASK)
    {
//      LOG_DEBUG ("%s: TransferTask is not a Download. Function-Exit-Early",__FUNCTION__);
        return 0;
    }

    DownloadTask * task = _task->p_downloadTask;

    //read from file if the fp is not null
    size_t nwritten;
    if (task->fp) {
        nwritten = fread(payload,1,payloadSize,task->fp);
        if ((nwritten < (size_t)payloadSize))
        {
            task->numErrors = DOWNLOADMANAGER_ERRORTHRESHOLD;
        }
    }

    if ((task->fp == NULL) || (task->numErrors >= DOWNLOADMANAGER_ERRORTHRESHOLD)) {
        //null file pointer? or num errors during this transfer was too high... this download isn't going anywhere...complete it
//        removeTask(taskHandle);           ///AWKWARD!
//        completed(_task);
//        return 0; //get out
        _task->m_remove = true;
        task->bytesCompleted = 0;           //file is basically unusable here           TODO: investigate issues with append
        LOG_DEBUG ("%s: err case: backing up to %llu bytes",__FUNCTION__,task->bytesCompleted);
        payloadSize = 0;            //this will kill the transfer when it is returned, below
        goto Return_cbReadEvent;
    }

    task->bytesCompleted += payloadSize;
    //LOG_DEBUG ("%s: Task bytes completed now = %ld",__FUNCTION__,task->bytesCompleted);

    if ((task->lastUpdateAt == 0) || (task->bytesCompleted - task->lastUpdateAt >= DOWNLOADMANAGER_UPDATEINTERVAL)) {
        LSError lserror;
        LSErrorInit(&lserror);

        std::string key = ConvertToString<long>(task->ticket);
        std::string bytesCompletedStr = ConvertToString<int32_t>(task->bytesCompleted);
        std::string bytesTotalStr = ConvertToString<int32_t>(task->bytesTotal);
        std::string e_bytesCompletedStr = ConvertToString<uint64_t>(task->bytesCompleted);
        std::string e_bytesTotalStr = ConvertToString<uint64_t>(task->bytesTotal);
        std::string response =
                std::string("{ \"ticket\":") +key
                +std::string(" , \"amountSent\":") +bytesCompletedStr
                +std::string(" , \"e_amountSent\":\"")+e_bytesCompletedStr + std::string("\"")
                +std::string(" , \"amountTotal\":") +bytesTotalStr
                +std::string(" , \"e_amountTotal\":\"")+e_bytesTotalStr + std::string("\"")
                +std::string(" }");

        if (!postDownloadUpdate (task->ownerId, task->ticket, response)) {
            LOG_WARNING_PAIRS (LOGID_SUBSCRIPTIONREPLY_FAIL_ON_READDATA, 2, PMLOGKS("ticket", key.c_str()),
                                                                            PMLOGKS("detail", response.c_str()),
                                                                            "failed to update read-progress to subscribers");
        }

        task->lastUpdateAt = task->bytesCompleted;
    }
Return_cbReadEvent:

//  LOG_DEBUG ("%s Function-Exit",__FUNCTION__);
    return payloadSize;
}


int DownloadManager::cbSetSocketOptions(void *clientp,curl_socket_t curlfd,curlsocktype purpose) {

    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 10;            //some socket implementations have backwards understanding of sec vs usec so just set both to
                                //the desired value
    int ivalue = 1;

    int rval = setsockopt(curlfd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(timeval));
    if (rval != 0) {
        LOG_DEBUG ("DownloadManager::cbSetSocketOptions(): setsockopt RCVTIME error %d",errno);
    }
    rval = setsockopt(curlfd,SOL_SOCKET,SO_KEEPALIVE,&ivalue,sizeof(int));
    if (rval != 0) {
        LOG_DEBUG ("DownloadManager::cbSetSocketOptions(): setsockopt KEEPALIVE error %d",errno);
    }

    //no matter what happens here, say OK to Curl via a 0 return code (or else the socket will abort and close)
    return 0;
}

/*
 * PRIOR TO ENTERING HERE, BE SURE removeTask() WAS CALLED
 *
 */
void DownloadManager::completed (TransferTask * task)
{
//  LOG_DEBUG ("%s Function-Entry",__FUNCTION__);

    if( !task ) {
        LOG_DEBUG ("%s: ERROR: task pointer lost or corrupted (Function-Exit-Early)",__FUNCTION__);
        return;
    }

    if (task->type == TransferTask::DOWNLOAD_TASK)
        completed_dl(task->p_downloadTask);
    if (task->type == TransferTask::UPLOAD_TASK)
        completed_ul(task->p_uploadTask);

    delete task;

//  LOG_DEBUG ("%s Function-Exit",__FUNCTION__);
}

void DownloadManager::completed_dl(DownloadTask* task)
{
    if (task == NULL)
        return;

    LSError lserror;
    bool transferError=false;
    bool interrupted=false;
    //close the file being written to
    if (task->fp) {
        if (fclose(task->fp) != 0) {
            LOG_DEBUG ("function fclose() failed");
        }
        task->fp = NULL;
        //LOG_DEBUG ("DownloadManager::completed(): closed output file");
    }

    long httpConnectCode = task->curlDesc.getHttpConnectCode();
    long resultCode = task->curlDesc.getHttpResultCode();
    long httpResultCode = resultCode;
    //check the curl error code first...a connection/network error could have occurred

    if (resultCode >= 300)
    {
        LOG_DEBUG ("HTTP status code was %ld, HTTP connect code was %ld",resultCode,httpConnectCode);

        //now handle specific http error codes, or at least the ones I can do something about
        if ((resultCode == 301) || (resultCode == 302) || (resultCode == 303) || (resultCode == 307)) {

            // Counting redirections to avoid an infinite loop for redirections
            task->decreaseRedCounts();

            LOG_DEBUG ("remainRedirections : %d", task->getRemainingRedCounts());

            if (task->getRemainingRedCounts() == 0)
            {
                LOG_DEBUG ("It has been completed to try maximum of redirections 5.");
                if (!cancel(task->ticket)) {
                    LOG_DEBUG ("Function cancel() failed: id(%lu)", task->ticket);
                }
                return;
            }

            //HTTP Redirect. If there was a "Location" specified in the headers, then go try it. Otherwise, it's an error
            if (task->httpHeader_Location != std::string("")) {
                //delete the "downloaded" file...probably just a fragment of html that was sent by the server as an informative "moved" message.
                //since I'm not a browser, i'll ignore this
                std::string oldfile = task->destPath +task->destFile;
                int ret = g_remove((const gchar *)(oldfile.c_str()));

                if (0 != ret) {
                    //LOG_DEBUG ("DownloadManager::completed_dl : g_remove error");
                }

                //LOG_DEBUG ("Redirect to [%s]... restarting download\n",task->httpHeader_Location.c_str());
                if (task->opt_keepOriginalFilenameOnRedirect == false)
                    task->destFile = std::string("");

                //LOG_DEBUG ("task: location [%s], destPath [%s], destFile [%s], ticket [%lu]\n",
                //      task->httpHeader_Location.c_str(),task->destPath.c_str(),task->destFile.c_str(), task->ticket);
                ret = download (task->ownerId,
                        task->httpHeader_Location,
                        task->detectedMIMEType,
                        task->destPath,
                        task->destFile,
                        task->ticket,
                        task->opt_keepOriginalFilenameOnRedirect,
                        std::string(""),
                        std::string(""),
                        DownloadManager::connectionName2Id(task->connectionName),
                        task->canHandlePause,
                        task->autoResume,
                        task->appendTargetFile,
                        task->cookieHeader,
                        task->rangeSpecified,
                        task->getRemainingRedCounts());
                if (ret < 0) {
                    LOG_DEBUG ("Function download() is failed (%d)", ret);
                }
                LOG_DEBUG ("[REDIRECT] ticket [%s] is now [%lu]",(task->httpHeader_Location).c_str(),(task->ticket));
                return;
            }
        }
        else if (resultCode >= 400) {
            LOG_DEBUG ("DownloadManager::completed(): Transfer error: HTTP error code = %d\n",(int)resultCode);
            LOG_DEBUG ("DownloadManager::completed(): Transfer error: URL failed = %s\n",task->url.c_str());
            //HTTP error of some kind
            resultCode = DOWNLOADMANAGER_COMPLETIONSTATUS_HTTPERROR;
            transferError = true;
            interrupted = false;
        }
    } //end else - NOT a connection/CURL error
    else if (task->curlDesc.getResultCode() != CURLE_OK)
    {
        LOG_DEBUG ("DownloadManager::completed(): Transfer error: CURL error code = %d\n",(int)task->curlDesc.getResultCode());
        LOG_DEBUG ("DownloadManager::completed(): Transfer error: URL failed = %s\n",task->url.c_str());
        resultCode = task->curlDesc.getResultCode();
        if (resultCode == CURLE_OPERATION_TIMEDOUT) {
            resultCode = DOWNLOADMANAGER_COMPLETIONSTATUS_CONNECTTIMEOUT;
            transferError = false;
            interrupted = true;
        }
        else if (resultCode == CURLE_WRITE_ERROR)
        {
            resultCode = DOWNLOADMANAGER_COMPLETIONSTATUS_WRITEERROR;
            transferError = false;
            interrupted = true;
        }
        else {
            resultCode = DOWNLOADMANAGER_COMPLETIONSTATUS_GENERALERROR;
            if (task->bytesTotal > 0) {
            transferError = false;
            interrupted = true;
            }
            else {
            transferError = true;
            }
        }
    }
    else if (task->bytesCompleted < task->bytesTotal) {
        //sizes don't match...maybe a filesys error
        LOG_DEBUG ("DownloadManager::completed(): Transfer error: bytesCompleted [%llu] < [%llu] bytesTotal...filesys error?",
                    task->bytesCompleted,task->bytesTotal);
        LOG_DEBUG ("DownloadManager::completed(): Transfer error: URL failed = %s\n",task->url.c_str());
        resultCode = DOWNLOADMANAGER_COMPLETIONSTATUS_FILECORRUPT;
        transferError = false;
        interrupted = true;
    }

    if (interrupted && !task->canHandlePause) {
        transferError = true;
        resultCode = DOWNLOADMANAGER_COMPLETIONSTATUS_GENERALERROR;
    }

    pbnjson::JValue payloadJsonObj = task->toJSON();

    //if transfer error, get rid of the file, otherwise rename to the final name

    if (transferError) {            //TODO: key on whether resultCode was a connection-establish related error, rather than bytesTotal=0. This works for now because bytesTotal
                                    // is only populated if the server was successfully contacted at least long enough to get headers
        //this is a true error.
        Utils::remove_file(task->destPath + task->downloadPrefix + task->destFile);
        payloadJsonObj.put("errorCode", (int)task->curlDesc.getResultCode());
        payloadJsonObj.put("errorText", curl_easy_strerror(task->curlDesc.getResultCode()));
        }
    else if (!task->downloadPrefix.empty() && !interrupted) {
        //LOG_DEBUG ("renaming %s to %s", std::string(task->destPath + task->downloadPrefix + task->destFile).c_str(),
        //      std::string(task->destPath+task->destFile).c_str());

        int retVal = rename( std::string(task->destPath + task->downloadPrefix + task->destFile).c_str(),
                std::string(task->destPath + task->destFile).c_str());

        if (0 != retVal) {
            LOG_DEBUG ("renaming failed, setting file system error (%d)", DOWNLOADMANAGER_COMPLETIONSTATUS_FILESYSTEMERROR);
            Utils::remove_file(task->destPath + task->downloadPrefix + task->destFile);
            transferError = true;
            resultCode = DOWNLOADMANAGER_COMPLETIONSTATUS_FILESYSTEMERROR;
        }

        // file sync after rename()
        FILE *fp = fopen(std::string(task->destPath + task->destFile).c_str(), "r+b");
        if (fp) {
            if (fdatasync(fileno(fp)) != 0) {
                LOG_DEBUG ("Function fdatasync() failed");
            }
            if (fclose(fp) != 0) {
                LOG_DEBUG ("Function fclose() failed");
            }
            fp = NULL;
        }
    }

    if ( !transferError && !interrupted && (httpResultCode == 200 || httpResultCode == 206) )
    {
        LOG_INFO_PAIRS_ONLY (LOGID_DOWNLOAD_COMPLETE, 2,
            PMLOGKFV("ticket", "%lu", task->ticket),
            PMLOGKS("URL", task->url.c_str())
            );
    }
    else
    {
        LOG_WARNING_PAIRS_ONLY (LOGID_DOWNLOAD_FAIL, 3,
            PMLOGKFV("ticket", "%lu", task->ticket),
            PMLOGKFV("CURL status code", "%d", task->curlDesc.getResultCode()),
            PMLOGKFV("HTTP status code", "%ld", httpResultCode)
            );
    }

    LSErrorInit(&lserror);
    std::string key = ConvertToString<long>(task->ticket).c_str();                  //Prevent CoW  (DEBUGGING)

    std::string payload;

    payloadJsonObj.put("completionStatusCode", (int64_t)resultCode);
    if (!interrupted) {
        payloadJsonObj.put("httpStatus", (int64_t)httpResultCode);
    }

    std::string dest;
    if (interrupted) {
        //still has the temp name
        dest.append(task->destPath + task->downloadPrefix + task->destFile);
    }
    else {
        //has the regular name
        dest.append(task->destPath + task->destFile);
    }
    payloadJsonObj.put("interrupted", interrupted);
    payloadJsonObj.put("completed", !(interrupted));
    payloadJsonObj.put("aborted", false);
    payloadJsonObj.put("target", dest);

    payload = JUtil::toSimpleString(payloadJsonObj);

    std::string historyString = JUtil::toSimpleString(payloadJsonObj);
    //add to database record
    if (interrupted)
        m_pDlDb->addHistory(task->ticket,task->ownerId,task->connectionName,"interrupted",historyString);
    else
        m_pDlDb->addHistory(task->ticket,task->ownerId,task->connectionName,"completed",historyString);


    if (!postDownloadUpdate (task->ownerId, task->ticket, payload)) {
        LOG_WARNING_PAIRS (LOGID_SUBSCRIPTIONREPLY_FAIL_ON_COMPLETION, 2, PMLOGKS("ticket", key.c_str()),
                                                                        PMLOGKS("detail", payload.c_str()),
                                                                        "failed to update download completion info to subscribers");
    }
    else {
        LOG_DEBUG ("[download complete] sent [%s] to subscriptions for ticket [%s]",payload.c_str(),key.c_str());
    }



    if (!m_queue.empty() && m_activeTaskCount < DownloadSettings::instance().maxDownloadManagerConcurrent) {
        unsigned long queuedTicket = m_queue.front();
        m_queue.pop_front();
        std::map<long,DownloadTask*>::iterator iter = m_ticketMap.find(queuedTicket);
        if (iter != m_ticketMap.end()) {
            DownloadTask* nextDownload = iter->second;
            nextDownload->queued = false;
            m_activeTaskCount++;
            requestWakeLock(true);
            if (glibcurl_add(nextDownload->curlDesc.getHandle()) != 0) {
                LOG_DEBUG ("Function glibcurl_add() failed");
            }
            //LOG_DEBUG ("%s: un-Q-ing a task, starting download of ticket [%lu] for url [%s]\n", __PRETTY_FUNCTION__,
            //      nextDownload->ticket, nextDownload->url.c_str());
            m_pDlDb->addHistory(nextDownload->ticket,nextDownload->ownerId,task->connectionName,"running",nextDownload->toJSONString());
        }
    }
    else if (m_queue.empty() && m_activeTaskCount == 0) {
        if (g_idle_add (DownloadManager::cbIdleSourceGlibcurlCleanup, this) == 0) {
            LOG_DEBUG ("Function g_idle_add() failed");
        }
    }
}

void DownloadManager::completed_ul(UploadTask* task)
{
    if (task == NULL)
        return;

    //post reply on subscription about completion

    //TODO: perhaps rewrite the post function to just take a task ptr????
        if (task->getCURLCode() == CURLE_OK)
            postUploadStatus(task->id(),task->source(),task->url(),true,task->getCURLCode(),task->getHTTPCode(),task->getUploadResponse(),task->getReplyLocation());
        else
            postUploadStatus(task->id(),task->source(),task->url(),false,task->getCURLCode(),task->getHTTPCode(),task->getUploadResponse(),task->getReplyLocation());

    //remove from upload task map
    m_uploadTaskMap.erase(task->id());
}

gboolean DownloadManager::cbIdleSourceGlibcurlCleanup (gpointer data)
{
    DownloadManager* dlm = (DownloadManager*) data;
    if (dlm->m_queue.empty() && dlm->m_activeTaskCount == 0) {
        LOG_DEBUG ("%s: Restarting glibcurl for cleanup", __PRETTY_FUNCTION__);
        dlm->shutdownGlibCurl();
        dlm->startupGlibCurl();
        dlm->requestWakeLock(false);
    }
    return false;
}

bool DownloadManager::cancel( unsigned long ticket )
{
    TransferTask * _task = removeTask(ticket);

    if (_task == NULL) {
            DownloadHistoryDb::DownloadHistory history;
            if (m_pDlDb->getDownloadHistoryRecord(ticket,history)) {
                cancelFromHistory(history);
            }
        return false;
    }

    _task->m_remove = true;

    if (_task->type == TransferTask::UPLOAD_TASK)
    {
        //TODO: when The Great Rewrite comes, make me more OOP-ly
        UploadTask * task = _task->p_uploadTask;
        postUploadStatus(task);
        delete _task;
        return true;
    }

    DownloadTask * task = _task->p_downloadTask;
    //LOG_DEBUG ("canceling download [%lu] of [%s]", task->ticket, task->url.c_str());
    LOG_INFO_PAIRS_ONLY(LOGID_DOWNLOAD_CANCEL,2,PMLOGKFV("ticket", "%lu", task->ticket),PMLOGKS("url",task->url.c_str()));

    //alert any subscribers that this task was aborted
    LSError lserror;
    LSErrorInit(&lserror);
    //bool LSSubscriptionReply(LSHandle *sh, const char *key,const char *payload, LSError *lserror);
    std::string key = ConvertToString<long>(task->ticket).c_str();                  //Prevent CoW  (DEBUGGING)

    pbnjson::JValue jsonPayloadObj = task->toJSON();
    std::string historyString = JUtil::toSimpleString(jsonPayloadObj);
    jsonPayloadObj.put("target", (task->destPath + task->downloadPrefix + task->destFile));
    jsonPayloadObj.put("completionStatusCode", DOWNLOADMANAGER_COMPLETIONSTATUS_CANCELLED);
    jsonPayloadObj.put("aborted", true);
    jsonPayloadObj.put("completed", false);
    jsonPayloadObj.put("interrupted", false);

    std::string payload = JUtil::toSimpleString(jsonPayloadObj);
    if (!postDownloadUpdate (task->ownerId, task->ticket, payload)) {
        LOG_WARNING_PAIRS (LOGID_SUBSCRIPTIONREPLY_FAIL_ON_CANCEL, 2, PMLOGKS("ticket", key.c_str()),
                                                                    PMLOGKS("detail", payload.c_str()),
                                                                    "failed to update cancellation status to subscribers");
        return false;
    }

    // remove file if the download has been cancelled.
    //LOG_DEBUG ("unlinking file %s", std::string(task->destPath + task->downloadPrefix + task->destFile).c_str());
    Utils::remove_file(task->destPath + task->downloadPrefix + task->destFile);

    //add to database recordis1xConnection
    m_pDlDb->addHistory(task->ticket,task->ownerId,task->connectionName,"cancelled",historyString);

    //get rid of the task object (it was already removed from the maps at the start of cancel() )
    delete _task;

    // if an active task has been cancelled, the next download should start
    if (!m_queue.empty() && (m_activeTaskCount < DownloadSettings::instance().maxDownloadManagerConcurrent)) {
        unsigned long queuedTicket = m_queue.front();
        m_queue.pop_front();
        std::map<long,DownloadTask*>::iterator iter = m_ticketMap.find(queuedTicket);
        if (iter != m_ticketMap.end()) {
        DownloadTask* nextDownload = iter->second;
        nextDownload->queued = false;
        m_activeTaskCount++;
        requestWakeLock(true);
        if (glibcurl_add(nextDownload->curlDesc.getHandle()) != 0) {
            LOG_DEBUG ("Function glibcurl_add() failed");
        }
        //LOG_DEBUG ("%s: starting download of ticket [%lu] for url [%s]\n", __PRETTY_FUNCTION__,
        //      nextDownload->ticket, nextDownload->url.c_str());
        }
    }
    return true;
}

void DownloadManager::cancelFromHistory(DownloadHistoryDb::DownloadHistory& history)
{
    //LOG_DEBUG ("%s: canceling download ticket [%lu]",__PRETTY_FUNCTION__, history.m_ticket);

    //alert any subscribers that this task was aborted
    LSError lserror;
    LSErrorInit(&lserror);
    std::string key = ConvertToString<long>(history.m_ticket);

    bool extractError = false;
    std::string uri = "";
    std::string destTempPrefix = "";
    std::string destFinalFile = "";
    std::string destFinalPath = "";
    //parse the history json string
    pbnjson::JValue root = JUtil::parse(history.m_downloadRecordJsonString.c_str(), std::string(""));
    if (root.isNull()) {
        extractError = true;
    }
    else {

        if (root["sourceUrl"].asString(uri) != CONV_OK) {
            extractError = true;
        }

        if (root["destTempPrefix"].asString(destTempPrefix) != CONV_OK) {
            extractError = true;
        }

        if (root["destFile"].asString(destFinalFile) != CONV_OK) {
            extractError = true;
        }

        if (root["destPath"].asString(destFinalPath) != CONV_OK) {
            extractError = true;
        }
    }

    std::string payload = std::string("{\"ticket\":")+key
    +(!extractError ? std::string(" , \"url\":\"")+uri+std::string("\"") : std::string(""))
    +std::string(" , \"aborted\":true")
    +std::string(" , \"completed\":false }");
    if (!postDownloadUpdate (history.m_owner, history.m_ticket, payload)) {
        LOG_WARNING_PAIRS (LOGID_SUBSCRIPTIONREPLY_FAIL_ON_CANCELHISTORY, 2, PMLOGKS("ticket", key.c_str()),
                                                                    PMLOGKS("detail", payload.c_str()),
                                                                    "failed to update cancellation status to subscribers");
    }

    // remove file if the download has been cancelled.
    if (!extractError) {
        //LOG_DEBUG ("%s: unlinking file %s",__PRETTY_FUNCTION__, std::string(destFinalPath + destTempPrefix + destFinalFile).c_str());
        Utils::remove_file(destFinalPath + destTempPrefix + destFinalFile);
    }

    //add to database record
    m_pDlDb->addHistory(history.m_ticket,history.m_owner,history.m_interface,"cancelled",payload);          ///TODO: PAYLOAD still has old 'state'...will fix this when states are removed from history json

}

void DownloadManager::cancelAll()
{
    //can't delete from within map iterator because it will alter the map (nor use erase to get next iterator because removing a task is a multi-step procedure)
    // put all the keys into an array, then iterate

    int sz = m_ticketMap.size();
    long * keyArray = new long[sz];
    int i=0;
    std::map<long,DownloadTask *>::iterator iter = m_ticketMap.begin();
    while (iter != m_ticketMap.end()) {
        keyArray[i] = iter->first;
        iter++;
        ++i;
    }

    for (i=0;i<sz;i++) {
        if (!cancel(keyArray[i])) {
            LOG_DEBUG ("Function cancel() failed: id:(%ld)", keyArray[i]);
        }
    }
    delete[] keyArray;
}

/**
 *
 * returns number of downloads currently on-going (doesn't count already completed ones)
 *
 * will populate the downloadList passed in wis1xConnectionith JSON strings of each download (see DownloadTask toJSONString() )
 */

int DownloadManager::getJSONListOfAllDownloads(std::vector<std::string>& downloadList) {

    //walk one of the download maps via an iterator
    std::map<long,DownloadTask*>::iterator iter = m_ticketMap.begin();
    int i =0;
    while (iter != m_ticketMap.end()) {

        DownloadTask * task = iter->second;
        if (task == NULL)
            continue;       //this shouldn't happen!

                //TODO: maybe a harsher response for debugging purposes; error of this type can't really be handled here
                //- but if it happens, root cause should be found and fixed
        pbnjson::JValue jobj = task->toJSON();
        jobj.put("lastUpdateAt", (int64_t)task->lastUpdateAt);
        jobj.put("queued", task->queued);
        jobj.put("owner", task->ownerId);
        jobj.put("connectionName", task->connectionName);
        downloadList.push_back(JUtil::toSimpleString(jobj));
        iter++;
        ++i;
    }
    return i;
}

TransferTask * DownloadManager::removeTask_ul(uint32_t ticket)
{

    UploadTask * task = NULL;
    //map to upload task
    std::map<uint32_t,UploadTask *>::iterator iter = m_uploadTaskMap.find(ticket);
    if (iter == m_uploadTaskMap.end())
        return NULL;
    task = iter->second;

    m_uploadTaskMap.erase(iter);
    if (task == NULL) {
        return NULL;
    }

    //round about way to get at the TransferTask that contains the UploadTask that was found
    //TODO : clean it up

    TransferTask * _task = getTask(task->getCURLHandlePtr());

    //remove it from the curl descriptor map
    m_handleMap.erase(task->getCURLHandlePtr());

    //remove from glibcurl's pool
    if (glibcurl_remove(task->getCURLHandlePtr()) != 0) {
        LOG_DEBUG ("Function glibcurl_remove() failed");
    }

    return _task;
}

//static
bool DownloadManager::spaceCheckOnFs(const std::string& path,uint64_t thresholdKB)
{
    struct statvfs64 fs_stats;
    memset(&fs_stats,0,sizeof(fs_stats));

    if (::statvfs64(path.c_str(),&fs_stats) != 0)
    {
        //failed to execute statvfs...treat this as if there was no free space
        LOG_DEBUG ("%s: Failed to execute statvfs on %s",__FUNCTION__,path.c_str());
        return false;
    }

    if (DownloadSettings::instance().dbg_useStatfsFake)
    {
        fs_stats.f_bfree = DownloadSettings::instance().dbg_statfsFakeFreeSizeBytes / fs_stats.f_frsize;
        LOG_DEBUG ("%s: USING FAKE STATFS VALUES! (free bytes specified as: %llu, free blocks simulated to: %llu )",
                        __FUNCTION__,DownloadSettings::instance().dbg_statfsFakeFreeSizeBytes,fs_stats.f_bfree);
    }

    uint64_t kbfree = ( ((uint64_t)(fs_stats.f_bfree) * (uint64_t)(fs_stats.f_frsize)) >> 10);
    LOG_DEBUG ("%s: [%s] KB free = %llu vs. %llu KB threshold",__FUNCTION__,path.c_str(),kbfree,thresholdKB);
    if (kbfree  >= thresholdKB)
        return true;
    return false;
}

//static
bool DownloadManager::spaceOnFs(const std::string& path,uint64_t& spaceFreeKB,uint64_t& spaceTotalKB)
{
    struct statvfs64 fs_stats;
    memset(&fs_stats,0,sizeof(fs_stats));

    if (::statvfs64(path.c_str(),&fs_stats) != 0)
    {
        //failed to execute statvfs...treat this as if there was no free space
        LOG_DEBUG ("%s: Failed to execute statvfs on %s",__FUNCTION__,path.c_str());
        return false;
    }

    if (DownloadSettings::instance().dbg_useStatfsFake)
    {
        fs_stats.f_bfree = DownloadSettings::instance().dbg_statfsFakeFreeSizeBytes / fs_stats.f_frsize;
        LOG_DEBUG ("%s: USING FAKE STATFS VALUES! (free bytes specified as: %llu, free blocks simulated to: %llu )",
                __FUNCTION__,DownloadSettings::instance().dbg_statfsFakeFreeSizeBytes,fs_stats.f_bfree);
    }

    spaceFreeKB = ( ((uint64_t)(fs_stats.f_bavail) * (uint64_t)(fs_stats.f_frsize)) >> 10);
    spaceTotalKB = ( ((uint64_t)(fs_stats.f_blocks) * (uint64_t)(fs_stats.f_frsize)) >> 10);
    LOG_DEBUG ("%s: [%s] KB free = %llu, KB total = %llu",__FUNCTION__,path.c_str(),spaceFreeKB,spaceTotalKB);
    return true;
}

/*
 *  Stops the task from downloading further, and removes it from all maps, and cleans up its CURL references
 *
 */

TransferTask * DownloadManager::removeTask_dl(uint32_t ticket) {

//  LOG_DEBUG ("%s Function-Entry",__FUNCTION__);
    DownloadTask * task=NULL;

    //map to a download task...
    std::map<long,DownloadTask*>::iterator iter = m_ticketMap.find(ticket);
    if (iter == m_ticketMap.end())
    {
        LOG_DEBUG ("%s: DownloadTask for ticket %u not found in ticket map. Function-Exit-Early",__FUNCTION__, ticket);
        return NULL;    //not found
    }
    task = iter->second;

    if (task == NULL) {
        //woah! something went wrong here - the ticket is in the map but it maps to a null download task!?
        //nuke the map entry and exit
        m_ticketMap.erase(iter);
        LOG_DEBUG ("%s: DownloadTask null. Function-Exit-Early",__FUNCTION__);
        return NULL;
    }

    TransferTask * _task = getTask(task->curlDesc.getHandle());

    //remove it from the curl descriptor map
    m_handleMap.erase(task->curlDesc);

    //and also remove it from the ticket map
    m_ticketMap.erase(task->ticket);

    if (!task->queued) {
        //remove from glibcurl's inprogress handle pool
        if (!glibcurl_remove(task->curlDesc.getHandle()) == 0) {
            LOG_DEBUG ("Function glibcurl_remove() failed");
        }
        // only decrement the active task count if this was in fact downloading
        m_activeTaskCount--;
    }
    else {
        m_queue.remove(task->ticket);
    }

    //if the curl handle had a header list associated w/ it, free it
    struct curl_slist * headerList;
    if ( (headerList = task->curlDesc.getHeaderList()) != NULL) {
        curl_slist_free_all(headerList);
    }

    //clean it curl-wise
    curl_easy_cleanup(task->curlDesc.getHandle());

    //now that it is no longer valid, mark it null
    if (!task->curlDesc.setHandle(NULL)) {
        LOG_DEBUG ("Function setHandle() failed");
    }

//  LOG_DEBUG ("%s Function-Exit",__FUNCTION__);
    return _task;
}

TransferTask * DownloadManager::removeTask(CURL * handle) {

//  LOG_DEBUG ("%s (handle) Function-Entry",__FUNCTION__);

    TransferTask * task = getTask(handle);

    if (task == NULL)
    {
        LOG_DEBUG ("%s: task is null. Function-Exit-Early",__FUNCTION__);
        return NULL;
    }

    //this is a bit wasteful but prevents from having to maintain 2 copies of essentially identical code
    if (task->type == TransferTask::DOWNLOAD_TASK) {
        if (!removeTask_dl(task->p_downloadTask->ticket)) {
            LOG_DEBUG ("Function removeTask_dl() failed");
        }
    } else if (task->type == TransferTask::UPLOAD_TASK) {
        if (!removeTask_ul(task->p_uploadTask->id())) {
            LOG_DEBUG ("Function removeTask_ul() failed");
        }
    }

//  LOG_DEBUG ("%s Function-Exit",__FUNCTION__);
    return task;
}

TransferTask * DownloadManager::removeTask(uint32_t ticket) {

//  LOG_DEBUG ("%s (uint) Function-Entry",__FUNCTION__);

    TransferTask * task = NULL;

    //this is a bit wasteful but prevents from having to maintain 2 copies of essentially identical code
    if ((task = removeTask_dl(ticket)) != NULL)
        return task;

    return removeTask_ul(ticket);
}

TransferTask * DownloadManager::getTask(CURL * handle)
{

//  LOG_DEBUG ("%s: looking for handle %x",__FUNCTION__,(unsigned int)handle);
    if (handle == NULL)
        return NULL;

    CurlDescriptor cd(handle);
    std::map<CurlDescriptor,TransferTask*>::iterator iter = m_handleMap.find(cd);
    if (iter == m_handleMap.end())
        return NULL;    //not found

    return iter->second;
}

TransferTask * DownloadManager::getTask(CurlDescriptor& cd)
{
    //LOG_DEBUG ("%s: (CD) looking for handle %x",__FUNCTION__,(unsigned int)(cd.getHandle()));
    std::map<CurlDescriptor,TransferTask*>::iterator iter = m_handleMap.find(cd);
    if (iter == m_handleMap.end())
        return NULL;    //not found

    return iter->second;
}

/*
 * Gets a copy of the download task structure by its ticket id. The copy gets pasted into the passed in 'task'
 * If the download task with the given ticket doesn't exist, the function returns false, in which case the contents of 'task' are undefined
 *
 */
bool DownloadManager::getDownloadTaskCopy(unsigned long ticket,DownloadTask& task) {

    std::map<long,DownloadTask*>::iterator iter = m_ticketMap.find(ticket);
    if (iter == m_ticketMap.end())
        return false;   //not found

    DownloadTask * ptrFoundTask = iter->second;
    if (ptrFoundTask == NULL) {
        //whoa! these 'NULL' entries should never occur..perhaps a more severe error flagging should be done here to alert about this case
        return false;
    }

    task.curlDesc = ptrFoundTask->curlDesc;
    task.bytesCompleted = ptrFoundTask->bytesCompleted;
    task.bytesTotal = ptrFoundTask->bytesTotal;
    task.destPath = ptrFoundTask->destPath.c_str();                 //Prevent CoW  (DEBUGGING)
    task.destFile = ptrFoundTask->destFile.c_str();                 //Prevent CoW  (DEBUGGING)
    task.ticket = ptrFoundTask->ticket;
    task.url = ptrFoundTask->url.c_str();                   //Prevent CoW  (DEBUGGING)
    task.setMimeType(ptrFoundTask->detectedMIMEType);   //Prevent CoW  (DEBUGGING)

    return true;
}

bool DownloadManager::getDownloadHistory(unsigned long ticket,std::string& r_caller,std::string& r_interface, std::string& r_state,std::string& r_history)
{

    if (!m_pDlDb)
        return false;

    return (m_pDlDb->getDownloadHistoryFull(ticket,r_caller,r_interface,r_state,r_history) > 0);

}

bool DownloadManager::getDownloadHistory(unsigned long ticket,DownloadHistoryDb::DownloadHistory& r_history)
{
    if (!m_pDlDb)
        return false;

    return (m_pDlDb->getDownloadHistoryRecord(ticket,r_history) > 0);
}

bool DownloadManager::getDownloadHistoryAllByCaller(const std::string& ownerCaller,std::vector<DownloadHistoryDb::DownloadHistory>& r_histories)
{
    if (!m_pDlDb)
        return false;

    return (m_pDlDb->getDownloadHistoryRecordsForOwner(ownerCaller,r_histories) > 0);
}

int DownloadManager::clearDownloadHistory()
{
    int rc = 0;

    if (m_pDlDb)
        rc = m_pDlDb->clear();

    return rc;

}

int DownloadManager::clearDownloadHistoryByGlobbedOwner(const std::string& caller)
{
    int rc = 0;

    if (m_pDlDb)
        rc = m_pDlDb->clearByGlobbedOwner(caller);

    return rc;
}

//static
DownloadManager::Connection DownloadManager::connectionName2Id(const std::string& name)
{
    if (name == "wired")
        return DownloadManager::Wired;
    else if (name == "wifi")
        return DownloadManager::Wifi;
    else if (name == "wan")
        return DownloadManager::Wan;
    else if (name == "btpan")
        return DownloadManager::Btpan;

    return DownloadManager::ANY;
}

//static
std::string DownloadManager::connectionId2Name(const DownloadManager::Connection id)
{
    if (id == DownloadManager::Wired)
        return "wired";
    if (id == DownloadManager::Wifi)
        return "wifi";
    else if (id == DownloadManager::Wan)
        return "wan";
    else if (id == DownloadManager::Btpan)
        return "btpan";

    return "*";
}

//static
bool DownloadManager::is1xConnection(const std::string& networkType)
{
    if (networkType == "1x")
        return true;

    return false;
}

bool DownloadManager::isInterfaceUp(DownloadManager::Connection connectionId)
{
    ConnectionStatus status;
    switch (connectionId)
    {
    case Wired:
        status = m_wiredConnectionStatus;
        break;
    case Wifi:
        status = m_wifiConnectionStatus;
        break;
    case Wan:
        if (m_wanConnectionStatus == InetConnectionConnected)
        {
            if (!s_allow1x && (m_wanConnectionType == DownloadManager::WanConnection1x))
                status = InetConnectionDisconnected;
            else
                status = InetConnectionConnected;
        }
        else
            status = InetConnectionDisconnected;
        break;
    case Btpan:
        status = m_btpanConnectionStatus;
        break;
    case ANY:
        return ((m_wifiConnectionStatus == InetConnectionConnected) ||
                ((m_wanConnectionStatus == InetConnectionConnected) && ((s_allow1x) || (m_wanConnectionType != DownloadManager::WanConnection1x)) ) ||
                (m_btpanConnectionStatus == InetConnectionConnected) ||
                (m_wiredConnectionStatus == InetConnectionConnected));
    default:
        return false;
    }

    return (status == InetConnectionConnected);
}

unsigned int DownloadManager::howManyTasksActive()
{
    return (m_handleMap.size());
}

int DownloadManager::howManyTasksInterrupted()
{
    std::vector<DownloadHistoryDb::DownloadHistory> interrupteds;

    return m_pDlDb->getDownloadHistoryRecordsForState("interrupted", interrupteds);
}

unsigned long DownloadManager::generateNewTicket() {

    return s_ticketGenerator++;
}

void DownloadManager::startupGlibCurl()
{
    if (m_glibCurlInitialized) {
        return;
    }
    //LOG_DEBUG ("DownloadManager::startupGlibCurl\n");

    m_glibCurlInitialized = true;
    m_activeTaskCount = 0;

    glibcurl_init();
    glibcurl_set_callback(&cbGlibcurl,this);

    s_curlShareHandle = curl_share_init();
    if (curl_share_setopt(s_curlShareHandle, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS) != 0) {
        LOG_DEBUG ("Function curl_share_setopt() failed");
    }
#ifdef CURL_COOKIE_SHARING
    if (curl_share_setopt(s_curlShareHandle, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE) != 0) {
        LOG_DEBUG ("Function curl_share_setopt() failed");
    }
#endif

    CURLMcode retVal;
    retVal = curl_multi_setopt(glibcurl_handle(), CURLMOPT_MAXCONNECTS, 4L);
    if (CURLM_OK != retVal) {
        LOG_WARNING_PAIRS (LOGID_CURL_FAIL_MAXCONNECTION, 1, PMLOGKFV("error code", "%d", retVal),
                                                    "curl_multi_setopt: CURLMOPT_MAXCONNECTS failed");
    }
//
// commented out per my co-worker due to bug in libcurl
//  retVal = curl_multi_setopt(glibcurl_handle(), CURLMOPT_PIPELINING, 1L);
//  if (CURLM_OK != retVal) {
//      LOG_DEBUG ("curl_multi_setopt: CURLMOPT_PIPELINING failed[%d]\n", retVal);
//  }
}

void DownloadManager::shutdownGlibCurl()
{
    if (!m_glibCurlInitialized) {
        LOG_DEBUG ("GlibCurl is not initialized");
        return;
    }

    //LOG_DEBUG ("DownloadManager::shutdownGlibCurl: %d\n", m_activeTaskCount);

    //luna_assert(m_activeTaskCount <= 0);
    m_activeTaskCount = 0;

    m_glibCurlInitialized = false;
    glibcurl_cleanup();

    return;
}

uint32_t DownloadManager::uploadPOSTFile(const std::string& file,
        const std::string& url,
        const std::string& filePostLabel,
        std::vector<PostItem>& postHeaders,
        std::vector<std::string>& httpHeaders,
        std::vector<kvpair>& cookies,
        const std::string& contentType)
{

    //Check the URL for security
    if (!isPathInMedia(file))
    {
        //security check fail...the file must be in /media/internal
        LOG_WARNING_PAIRS ("UPLOAD",1,PMLOGKS("file",file.c_str()),"Security check failed");
        return 0;
    }

    //LOG_DEBUG ("uploadPost called for url [%s] label [%s]", url.c_str(), filePostLabel.c_str());
    if (!m_glibCurlInitialized)
        startupGlibCurl();

    //try and create the UploadTask

    UploadTask * p_ult = UploadTask::newFileUploadTask(url,file,filePostLabel,postHeaders,httpHeaders,cookies,contentType);
    if (p_ult == NULL)
        return 0;

    //place it in the map
    m_uploadTaskMap[p_ult->id()] = p_ult;

    //and the general map, by curl handle
    m_handleMap[CurlDescriptor(p_ult->getCURLHandlePtr())] = new TransferTask(p_ult);

    //start the transfer
    LOG_DEBUG("Starting upload - uploading file [%s] to target url [%s]",p_ult->source().c_str(), p_ult->url().c_str() );

    if (glibcurl_add(p_ult->getCURLHandlePtr()) != 0) {
        LOG_DEBUG ("Function glibcurl_add() failed");
    }

    return p_ult->id();
}

uint32_t DownloadManager::uploadPOSTBuffer(const std::string& buffer,
        const std::string& url,
        std::vector<std::string>& httpHeaders,
        const std::string& contentType)
{

    //LOG_DEBUG ("uploadPost called for url [%s]", url.c_str());
    if (!m_glibCurlInitialized)
        startupGlibCurl();

    //try and create the UploadTask

    UploadTask * p_ult = UploadTask::newBufferUploadTask(url,buffer,httpHeaders,contentType);
    if (p_ult == NULL)
        return 0;

    //place it in the map
    m_uploadTaskMap[p_ult->id()] = p_ult;

    //and the general map, by curl handle
    m_handleMap[CurlDescriptor(p_ult->getCURLHandlePtr())] = new TransferTask(p_ult);

    //start the transfer
        //LOG_DEBUG ("%s: starting upload of file [%s] to url [%s]\n", __PRETTY_FUNCTION__,
         //       p_ult->source().c_str(), p_ult->url().c_str());

    if (glibcurl_add(p_ult->getCURLHandlePtr()) != 0) {
        LOG_DEBUG ("Function glibcurl_add() failed");
    }

    return p_ult->id();
}

void DownloadManager::postUploadStatus(UploadTask * pUlTask)
{
    if (pUlTask == NULL)
        return;

    postUploadStatus(pUlTask->id(),pUlTask->source(),pUlTask->url(),false,pUlTask->getCURLCode(),pUlTask->getHTTPCode(),pUlTask->getUploadResponse(),pUlTask->getReplyLocation());
}

void DownloadManager::postUploadStatus (uint32_t id,const std::string& sourceFile,const std::string& url,
        bool completed,CURLcode curlCode,uint32_t httpCode,const std::string& responseString,const std::string& location)
{
    pbnjson::JValue responseRoot = pbnjson::Object();

    responseRoot.put("ticket", (int64_t)id);
    responseRoot.put("sourceFile", sourceFile);
    responseRoot.put("url", url);
    responseRoot.put("completed", completed);
    responseRoot.put("completionCode", (int)curlCode);
    responseRoot.put("httpCode", (int64_t)httpCode);
    responseRoot.put("responseString", responseString);
    responseRoot.put("location", location);

    std::string submsg = JUtil::toSimpleString(responseRoot);
    std::string subkey = std::string("UPLOAD_")+ConvertToString<uint32_t>(id);
    if (postSubscriptionUpdate(subkey,submsg,m_serviceHandle) != 0) {
        LOG_DEBUG ("Function postSubscriptionUpdate() failed");
    }
}


bool DownloadManager::is1xConnection()
{
    if ((m_wanConnectionStatus == InetConnectionConnected) && (m_wanConnectionType == WanConnection1x))
        return true;

    return false;
}

bool DownloadManager::is1xDownloadAllowed()
{
    return DownloadManager::s_allow1x;
}

bool DownloadManager::canDownloadNow()
{
    bool available = (m_wifiConnectionStatus == InetConnectionConnected)
    || (m_wanConnectionStatus == InetConnectionConnected)
    || (m_btpanConnectionStatus == InetConnectionConnected)
    || (m_wiredConnectionStatus == InetConnectionConnected);

    if (!available) {
    //LOG_DEBUG ("no connection available");
    return false;
    }

    if (m_wiredConnectionStatus == InetConnectionConnected) {
        return true;
    }

    if (m_wifiConnectionStatus == InetConnectionConnected) {
//  LOG_DEBUG ("wifi is avail, download ok now");
    return true;
    }

    if (m_wanConnectionStatus == InetConnectionConnected
        && (!is1xConnection() || (is1xConnection() && is1xDownloadAllowed())))
    {
//  LOG_DEBUG ("wan connection download ok now");
    return true;
    }

    if (m_btpanConnectionStatus == InetConnectionConnected) {
//  LOG_DEBUG ("btpan available, download ok now");
    return true;
    }

    LOG_DEBUG ("cannot download now");
    return false;
}

size_t DownloadManager::cbUploadResponse(void *ptr, size_t size, size_t nmemb, void *data) {

    size_t realsize = size * nmemb;

    if (data == NULL) {
        LOG_DEBUG ("DownloadManager::cbUploadResponse() data ptr == NULL; data loss!");
        return realsize;            //DATA LOSS!
    }

    UploadTask * p_ult = (UploadTask *)data;
    p_ult->appendUploadResponse(std::string(((char *)ptr),realsize));
    return realsize;
}

bool DownloadManager::msmAvailCallback(LSHandle* handle, LSMessage* message, void* ctxt)
{
    return DownloadManager::instance().msmAvail(message);
}

bool DownloadManager::msmProgressCallback(LSHandle* handle, LSMessage* message, void* ctxt)
{
    return DownloadManager::instance().msmProgress(message);
}

bool DownloadManager::msmEntryCallback(LSHandle* handle, LSMessage* message, void* ctxt)
{
    return DownloadManager::instance().msmEntry(message);
}

bool DownloadManager::msmFsckingCallback(LSHandle* handle, LSMessage* message, void* ctxt)
{
    return DownloadManager::instance().msmFscking(message);
}

bool DownloadManager::msmAvail(LSMessage* message)
{
    const char* str = LSMessageGetPayload( message );
    if( !str )
        return false;

    pbnjson::JValue payload = JUtil::parse(str, std::string(""));
    if (payload.isNull())
        return false;

    if (!payload.hasKey("mode-avail"))
        return false;

    //LOG_DEBUG ("MSM available: %s", payload["mode-avail"].asBool() ? "true" : "false");

    if (payload["mode-avail"].asBool() == FALSE) {

        if (m_brickMode) {

            // did we have an unclean shutdown of brick mode
            if (!m_msmExitClean) {
                LOG_DEBUG ("%s: user unplugged phone without ejecting", __PRETTY_FUNCTION__);

                // FIXME: Should pop up an error message;
            }

            if (m_brickMode) {
                //LOG_DEBUG ("%s: exiting brick mode", __PRETTY_FUNCTION__);
                m_brickMode = false;
            }

            if (m_fscking) {
                //LOG_DEBUG ("%s: fsck ended", __PRETTY_FUNCTION__);
                m_fscking = false;
            }
        }
    }

    return true;
}

bool DownloadManager::msmProgress(LSMessage* message)
{
    const char* str = LSMessageGetPayload( message );
    if( !str )
        return false;

    pbnjson::JValue payload = JUtil::parse(str, std::string(""));
    if (payload.isNull())
        return false;

    if (!payload.hasKey("stage"))
        return false;

    std::string stageText = payload["stage"].asString();
    //LOG_DEBUG ("MSM Progress: %s", stageText);

    if (strcasecmp(stageText.c_str(), "attempting") == 0) {
        // going into brick mode

        if (!m_brickMode) {

            // Go into brick mode
            //LOG_DEBUG ("%s: entering brick mode", __PRETTY_FUNCTION__);

            // are we going into media sync or USB drive mode?
            if (payload.hasKey("enterIMasq"))
                m_mode = payload["enterIMasq"].asBool();

            m_brickMode = true;
            m_msmExitClean = false;
            pauseAll();
        }
    }
    else if (strcasecmp(stageText.c_str(), "failed") == 0) {

        // failed going into brick mode.
        if (m_brickMode) {

            //LOG_DEBUG ("%s: exiting brick mode", __PRETTY_FUNCTION__);
            m_brickMode = false;
            resumeAll();
        }

        if (m_fscking) {
            //LOG_DEBUG ("%s: fsck ended", __PRETTY_FUNCTION__);
            m_fscking = false;
        }
    }

    return true;
}

bool DownloadManager::msmEntry(LSMessage* message)
{
    const char* str = LSMessageGetPayload( message );
    if( !str )
        return false;

    pbnjson::JValue payload = JUtil::parse(str, std::string(""));
    pbnjson::JValue mode;
    if (payload.isNull())
        return false;

    if (!payload.hasKey("new-mode"))
        return false;

    std::string modeText = payload["new-mode"].asString();
    //LOG_DEBUG ("MSM Mode: %s", modeText);

    if (strcasecmp(modeText.c_str(), "phone") == 0) {

        m_msmExitClean = true;

        if (m_brickMode) {
            //LOG_DEBUG ("%s: exiting brick mode", __PRETTY_FUNCTION__);
            m_brickMode = false;
            resumeAll();
        }

        if (m_fscking) {
            //LOG_DEBUG ("%s: fsck ended", __PRETTY_FUNCTION__);
            m_fscking = false;
        }
    }
    else if (strcasecmp(modeText.c_str(), "brick") == 0) {

        m_msmExitClean = false;

        if (!m_brickMode) {

            //LOG_DEBUG ("%s: entering brick mode", __PRETTY_FUNCTION__);

            // are we going into media sync or USB drive mode?
            mode = payload["enterIMasq"];

            m_brickMode = true;
            pauseAll();
        }
    }

    return false;
}

bool DownloadManager::msmFscking(LSMessage* message)
{
    LOG_WARNING_PAIRS_ONLY (LOGID_RECEIVED_FSCK_SIGNAL, 1, PMLOGKS("detail", "received fsck signal from storaged"));

    // something bad has happened and storaged is now erasing all your precious media files
    if (!m_brickMode)
        return false;

    m_msmExitClean = false;
    m_fscking = true;

    return true;
}

bool DownloadManager::postDownloadUpdate (const std::string& owner, const unsigned long ticket, const std::string& payload)
{
    LSError lserror;
    LSErrorInit(&lserror);

    std::string key = ConvertToString<long>(ticket);
    if (postSubscriptionUpdate(key,payload,m_serviceHandle) != 0) {
        LSErrorPrint (&lserror, stderr);
        LSErrorFree(&lserror);
    }
    return true;
}

bool DownloadManager::diskSpaceAtStopMarkLevel()
{
    uint64_t freeSpaceKB = 0;
    uint64_t totalSpaceKB = 0;

    if (!DownloadManager::spaceOnFs("/media/internal", freeSpaceKB, totalSpaceKB))
        return true;

    if (freeSpaceKB <= DownloadSettings::instance().freespaceStopmarkRemainingKBytes)
        return true;

    return false;
}
