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

#ifndef __DownloadManager_h__
#define __DownloadManager_h__

#include <list>
#include <string>
#include <map>
#include "glibcurl.h"
#include <glib.h>
#include <sqlite3.h>
#include <vector>
#include <stdint.h>
#include <utility>

#include <luna-service2/lunaservice.h>

#include "TransferTask.h"
#include "DownloadHistoryDb.h"
#include "Watchdog.h"
#include "Singleton.hpp"

#define     DOWNLOADMANAGER_DLBUFFERSIZE        1024*512
#define     DOWNLOADMANAGER_UPDATEINTERVAL      1024*100
#define     DOWNLOADMANAGER_UPDATENUM           20
#define     DOWNLOADMANAGER_ERRORTHRESHOLD      10

#define     DOWNLOADMANAGER_TRUSTED_CERT_PATH   "/var/ssl/trustedcerts"

#define     DOWNLOADMANAGER_STARTSTATUS_GENERALERROR            -1
#define     DOWNLOADMANAGER_STARTSTATUS_FILEDOESNOTEXIST        -2
#define     DOWNLOADMANAGER_STARTSTATUS_QUEUEFULL               -3
#define     DOWNLOADMANAGER_STARTSTATUS_FILESYSTEMFULL          -4
#define     DOWNLOADMANAGER_STARTSTATUS_CURLERROR               -5
#define     DOWNLOADMANAGER_STARTSTATUS_NOSUITABLEINTERFACE     -6
#define     DOWNLOADMANAGER_STARTSTATUS_FAILEDSECURITYCHECK     -7

#define     DOWNLOADMANAGER_COMPLETIONSTATUS_GENERALERROR       -1
#define     DOWNLOADMANAGER_COMPLETIONSTATUS_CONNECTTIMEOUT     -2
#define     DOWNLOADMANAGER_COMPLETIONSTATUS_FILECORRUPT        -3
#define     DOWNLOADMANAGER_COMPLETIONSTATUS_FILESYSTEMERROR    -4
#define     DOWNLOADMANAGER_COMPLETIONSTATUS_HTTPERROR          -5
#define     DOWNLOADMANAGER_COMPLETIONSTATUS_WRITEERROR         -6
#define     DOWNLOADMANAGER_COMPLETIONSTATUS_INTERRUPTED        11
#define     DOWNLOADMANAGER_COMPLETIONSTATUS_CANCELLED          12

#define     DOWNLOADMANAGER_RESUMESTATUS_GENERALERROR           0
#define     DOWNLOADMANAGER_RESUMESTATUS_NOTINHISTORY           -1
#define     DOWNLOADMANAGER_RESUMESTATUS_NOTINTERRUPTED         -2
#define     DOWNLOADMANAGER_RESUMESTATUS_NOTDOWNLOAD            -3
#define     DOWNLOADMANAGER_RESUMESTATUS_QUEUEFULL              -4
#define     DOWNLOADMANAGER_RESUMESTATUS_HISTORYCORRUPT         -5
#define     DOWNLOADMANAGER_RESUMESTATUS_CANNOTACCESSTEMP       -6
#define     DOWNLOADMANAGER_RESUMESTATUS_INTERFACEDOWN          -7
#define     DOWNLOADMANAGER_RESUMESTATUS_FILESYSTEMFULL         -8
#define     DOWNLOADMANAGER_RESUMESTATUS_OK                     1

#define     DOWNLOADMANAGER_PAUSESTATUS_GENERALERROR            0
#define     DOWNLOADMANAGER_PAUSESTATUS_NOSUCHDOWNLOADTASK      -1
#define     DOWNLOADMANAGER_PAUSESTATUS_OK                      1

#define     DOWNLOADMANAGER_UPLOADSTATUS_OK                      0
#define     DOWNLOADMANAGER_UPLOADSTATUS_GENERALERROR            1
#define     DOWNLOADMANAGER_UPLOADSTATUS_INVALIDPARAM            2

class DownloadManager: public Singleton<DownloadManager>
{
public:

    unsigned long generateNewTicket();
    enum {
            keepOriginalFilenameOnRedirect = 16
    };

    enum ConnectionStatus {
            InetConnectionUnknownState,
            InetConnectionConnected,
            InetConnectionDisconnected
    };

    enum WanConnectionType {
            WanConnectionUnknown,
            WanConnection1x,
            WanConnectionHS
    };

    enum Connection {
        ANY,
        Wifi,
        Wan,
        Btpan,
        Wired
    };

    int download (const std::string& caller,
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
            const int remainingRedCounts);

    int resumeDownload(const unsigned long ticket,const std::string& authToken,const std::string& deviceId,std::string& r_err);
    int resumeDownload(const DownloadHistoryDb::DownloadHistory& history,bool autoResume,std::string& r_err);
    int resumeDownload(const DownloadHistoryDb::DownloadHistory& history,bool autoResume,const std::string& authToken,const std::string& deviceId,std::string& r_err);
    int resumeAll();
    int resumeAllForInterface(Connection interface, bool autoResume);
    int resumeDownloadOnAlternateInterface(DownloadHistoryDb::DownloadHistory& history,Connection newInterface,bool autoResume);
    int resumeMultipleOnAlternateInterface(Connection oldInterface,Connection newInterface,bool autoResume);
    int pauseDownload(const unsigned long ticket,bool allowQueuedToStart=true);
    int pauseAll();
    int pauseAllForInterface(Connection interface);

#define SWAPTOIF_ERROR_INVALIDIF        -1
#define SWAPTOIF_ERROR_NOSUCHTICKET     -2
#define SWAPTOIF_SUCCESS                1
    int swapToInterface(const unsigned long int ticket,const Connection newInterface);

#define SWAPALLTOIF_ERROR_INVALIDIF             -1
#define SWAPALLTOIF_ERROR_ATLEASTONEFAIL        -2
#define SWAPALLTOIF_SUCCESS                     1
    int swapAllActiveToInterface(const Connection newInterface);

    uint32_t uploadPOSTFile(const std::string& file,
            const std::string& url,
            const std::string& filePostLabel,
            std::vector<PostItem>& postHeaders,
            std::vector<std::string>& httpHeaders,
            std::vector<kvpair>& cookies,
            const std::string& contentType);
    uint32_t uploadPOSTBuffer(const std::string& file,
            const std::string& url,
            std::vector<std::string>& httpHeaders,
            const std::string& contentType);

    bool cancel (unsigned long ticket);
    void cancelFromHistory(DownloadHistoryDb::DownloadHistory& history);
    void cancelAll();

    int getJSONListOfAllDownloads(std::vector<std::string>& list);

    bool getDownloadTaskCopy(unsigned long ticket,DownloadTask& task);
    bool getDownloadHistory(unsigned long ticket,std::string& r_caller,std::string& r_interface, std::string& r_state,std::string& r_history);
    bool getDownloadHistory(unsigned long ticket,DownloadHistoryDb::DownloadHistory& r_history);
    bool getDownloadHistoryAllByCaller(const std::string& ownerCaller,std::vector<DownloadHistoryDb::DownloadHistory>& r_histories);
    int clearDownloadHistory();
    int clearDownloadHistoryByGlobbedOwner(const std::string& caller);

    std::string getDownloadPath();
    std::string getAltIpkDownloadPath();

    static DownloadManager::Connection connectionName2Id(const std::string& name);
    static std::string connectionId2Name(const DownloadManager::Connection id);
    static bool is1xConnection(const std::string& networkType);
    bool isInterfaceUp(DownloadManager::Connection connectionId);

    static bool spaceCheckOnFs(const std::string& path,uint64_t thresholdKB=1024);
    static bool spaceOnFs(const std::string& path,uint64_t& spaceFreeKB,uint64_t& spaceTotalKB);

    unsigned int howManyTasksActive();
    int          howManyTasksInterrupted();

    bool    currentlyInBrickMode() { return m_brickMode; }

    bool postDownloadUpdate (const std::string& owner, const unsigned long ticket, const std::string& payload);

    friend class UploadTask;

    // download specific
    static bool cbDeleteDownloadedFile(LSHandle* lshandle, LSMessage *message,void *user_data);
    static bool cbDownloadStatusQuery(LSHandle* lshandle, LSMessage *message,void *user_data);
    static bool cbDownload(LSHandle* lshandle, LSMessage *message,void *user_data);
    static bool cbDownloadAndLaunch(LSHandle* lshandle, LSMessage *message,void *user_data);
    static bool cbResumeDownload(LSHandle* lshandle, LSMessage *message,void *user_data);
    static bool cbPauseDownload(LSHandle* lshandle, LSMessage *message,void *user_data);
    static bool cbCancelDownload(LSHandle* lshandle, LSMessage *message,void *user_data);
    static bool cbCancelAllDownloads(LSHandle* lshandle, LSMessage *message,void *user_data);
    static bool cbListPendingDownloads(LSHandle * lshandle,LSMessage *msg,void * user_data);
    static bool cbGetAllHistory(LSHandle * lshandle,LSMessage *msg, void * user_data);
    static bool cbClearDownloadHistory(LSHandle * lshandle,LSMessage *msg,void * user_data);

    void filesystemStatusCheck(const uint64_t& spaceFreeKB,const uint64_t& spaceTotalKB,bool * criticalAlertRaised = 0, bool * stopMarkReached = 0);

    // upload specific
    static bool cbUpload(LSHandle* lshandle, LSMessage *message,void *user_data);
    //static bool cbUploadStatusQuery(LSHandle* lshandle, LSMessage *message,void *user_data);
    //static bool cbCancelUpload(LSHandle* lshandle, LSMessage *message,void *user_data);

    // status specific

    static bool cbConnectionManagerServiceState(LSHandle* lshandle, LSMessage *message,void *user_data);
    static bool cbConnectionManagerConnectionStatus(LSHandle* lshandle, LSMessage *message,void *user_data);

    static bool cbSleepServiceState(LSHandle* lshandle, LSMessage *message,void *user_data);
    static bool cbSleepServiceRegisterForWakeLock(LSHandle* lshandle, LSMessage *message,void *user_data);
    static bool cbSleepServiceWakeLockRegister(LSHandle* lshandle, LSMessage *msg, void *user_data);

    bool is1xConnection();
    bool is1xDownloadAllowed();
    bool canDownloadNow();
    static bool cbConnectionType(LSHandle* lshandle, LSMessage *message,void *user_data);
    static gboolean     cbIdleSourceGlibcurlCleanup (gpointer userData);
    // msm

    static bool msmAvailCallback(LSHandle* handle, LSMessage* message, void* ctxt);
    static bool msmProgressCallback(LSHandle* handle, LSMessage* message, void* ctxt);
    static bool msmEntryCallback(LSHandle* handle, LSMessage* message, void* ctxt);
    static bool msmFsckingCallback(LSHandle* handle, LSMessage* message, void* ctxt);

    static bool diskSpaceAtStopMarkLevel();
    bool init();

private:

    void startService();
    void stopService();

    bool msmAvail(LSMessage* message);
    bool msmProgress(LSMessage* message);
    bool msmEntry(LSMessage* message);
    bool msmFscking(LSMessage* message);

    std::string m_downloadPath;
    std::string m_userDiskRootPath;

    WanConnectionType m_wanConnectionTypePrevious;
    ConnectionStatus m_wanConnectionStatus;
    WanConnectionType m_wanConnectionType;
    ConnectionStatus m_wifiConnectionStatus;
    ConnectionStatus m_btpanConnectionStatus;
    ConnectionStatus m_wiredConnectionStatus;

    std::string m_wanInterfaceName;
    std::string m_wifiInterfaceName;
    std::string m_btpanInterfaceName;
    std::string m_wiredInterfaceName;

    std::list<unsigned long> m_queue;
    std::map<CurlDescriptor,TransferTask*> m_handleMap;
    std::map<long,DownloadTask*> m_ticketMap;

    std::map<uint32_t,UploadTask *> m_uploadTaskMap;

    std::string m_authCookie;

    void completed(TransferTask* );
    void completed_dl(DownloadTask*);
    void completed_ul(UploadTask*);

    size_t  cbGlib ();
    size_t  cbReadEvent (CURL* taskHandle,size_t payloadSize=0,unsigned char * payload=NULL);
    size_t  cbWriteEvent (CURL* taskHandle,size_t payloadSize=0,unsigned char * payload=NULL);

    size_t  cbHeader(CURL* taskHandle,size_t headerSize,const char * headerText);
    int cbSetSocketOptions(void *clientp,curl_socket_t curlfd,curlsocktype purpose);

    static size_t cbCurlReadFromFile(void* ptr, size_t size, size_t nmemb, void *stream);
    static size_t cbCurlWriteToFile(void* ptr, size_t size, size_t nmemb, void *stream);
    static size_t cbCurlHeaderInfo(void * ptr,size_t size,size_t nmemb,void * stream);
    static int    cbCurlSetSocketOptions(void *clientp,curl_socket_t curlfd,curlsocktype purpose);
    static size_t cbUploadResponse(void *ptr, size_t size, size_t nmemb, void *data);

    static void cbGlibcurl(void* data);

    void postUploadStatus(UploadTask * pUlTask);
    void postUploadStatus(uint32_t id,const std::string& sourceFile,const std::string& url,bool completed,
                CURLcode curlCode,uint32_t httpCode,const std::string& responseString, const std::string& location);

    bool requestWakeLock(bool status);
    static bool cbRequestWakeLock(LSHandle* lshandle, LSMessage *msg, void *user_data);

    LSHandle * m_serviceHandle;

    LSMessageToken m_storageDaemonToken;
    DownloadHistoryDb * m_pDlDb;
    int m_activeTaskCount;
    bool m_glibCurlInitialized;
    GMainLoop* m_mainLoop;

    std::string generateTempPath( const std::string& resourceName );
    std::string generateAltIpkgTempPath( const std::string& resourceName);
    static bool isValidOverridePath(const std::string& path);
    static bool isValidOverrideFile(const std::string& file);
    static bool isValidAltIpkgOverridePath(const std::string& path);
    static bool isPathInMedia(const std::string& path);
    static bool isPathInVar(const std::string& path);
    static bool isPrivileged(const std::string& sender);

    TransferTask * removeTask_dl(uint32_t ticket);
    TransferTask * removeTask_ul(uint32_t ticket);
    TransferTask * removeTask(CURL * handle);
    TransferTask * removeTask(uint32_t ticket);

    TransferTask * getTask(CURL * handle);
    TransferTask * getTask(CurlDescriptor& cd);

    void startupGlibCurl();
    bool shutdownGlibCurl();

    DownloadManager();
    ~DownloadManager();

    static unsigned long s_ticketGenerator;

    bool m_fscking;
    bool m_brickMode;
    bool m_msmExitClean;
    bool m_mode;

    friend class Singleton<DownloadManager>;

    std::string m_sleepDClientId;
public:
    static bool s_allow1x;              ///TODO: make this a more appropriately protected class member
};


#endif
