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

//->Start of API documentation comment block
/** @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @brief This service is responsible for downloading and uploading files to and from the device.
 @{
 @}
 */
//->End of API documentation comment block
#include <vector>
#include <iostream>
#include <cstring>
#include <stdio.h>
#include <sstream>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

#include <boost/regex.hpp>
#include <core/DownloadManager.h>
#include <pbnjson.hpp>
#include <setting/DownloadSettings.h>
#include <util/DownloadUtils.h>
#include <util/JUtil.h>
#include <util/Logging.h>
#include <util/UrlRep.h>
#include <util/Utils.h>

bool DownloadManager::s_allow1x = false;                //a lunabus fn can set this to true to allow 1x connections

static void turnNovacomOn(LSHandle * lshandle);

///////// --------------------------------------------------------------------------- LUNA BUS FUNCTIONS ----------------------------------------

static LSMethod s_methods[] = {
    { "deleteDownloadedFile", DownloadManager::cbDeleteDownloadedFile },
    { "downloadStatusQuery", DownloadManager::cbDownloadStatusQuery },
    { "download", DownloadManager::cbDownload },
    { "resumeDownload", DownloadManager::cbResumeDownload },
    { "pauseDownload", DownloadManager::cbPauseDownload },
    { "cancelDownload", DownloadManager::cbCancelDownload },
    { "cancelUpload", DownloadManager::cbCancelDownload },  //just an alias and a bit of a misnomer: cancelDownload will cancel either an upload or download
    { "cancelAllDownloads", DownloadManager::cbCancelAllDownloads },
    { "listPending", DownloadManager::cbListPendingDownloads },
    { "getAllHistory", DownloadManager::cbGetAllHistory },
    { "clearHistory", DownloadManager::cbClearDownloadHistory },
    { "upload", DownloadManager::cbUpload },
    { "is1xMode", DownloadManager::cbConnectionType },
    { "allow1x", DownloadManager::cbAllow1x },
    { 0, 0 },
};

void DownloadManager::startService()
{
    bool result;
    LSError lsError;
    LSErrorInit(&lsError);

    LOG_DEBUG("DownloadManager (service) starting...");

    result = LSRegister("com.webos.service.downloadmanager", &m_serviceHandle, &lsError);
    if (!result)
        goto Done;

    result = LSRegisterCategory(m_serviceHandle, "/", s_methods, NULL, NULL, &lsError);
    if (!result)
        goto Done;

    LOG_DEBUG("Calling LSGmainAttach on service = %p, m_mainLoop = %p", m_serviceHandle, m_mainLoop);
    result = LSGmainAttach(m_serviceHandle, m_mainLoop, &lsError);
    if (!result)
        goto Done;

    if ((result = LSCall(m_serviceHandle, "luna://com.palm.bus/signal/registerServerStatus", "{\"serviceName\":\"com.palm.sleep\", \"subscribe\":true}", cbSleepServiceState, this, NULL, &lsError))
            == false)
        goto Done;

    if ((result = LSCall(m_serviceHandle, "luna://com.palm.bus/signal/registerServerStatus", "{\"serviceName\":\"com.webos.service.connectionmanager\", \"subscribe\":true}",
            cbConnectionManagerServiceState, this, NULL, &lsError)) == false) {
        goto Done;
    }

    /*
     * this.subscriptionContactsStatus = this.sendMessage(TelephonyServer.lunabusURI_registerStatus,
     "{\"serviceName\":\"com.palm.contacts\", \"subscribe\":true}","registerServiceStatusResponse");
     */

    // register for storage daemon signals
    result = LSCall(m_serviceHandle, "luna://com.palm.lunabus/signal/addmatch", "{\"category\":\"/storaged\", \"method\":\"MSMAvail\"}", msmAvailCallback, NULL, &m_storageDaemonToken, &lsError);
    if (!result)
        goto Done;

    result = LSCall(m_serviceHandle, "luna://com.palm.lunabus/signal/addmatch", "{\"category\":\"/storaged\", \"method\":\"MSMProgress\"}", msmProgressCallback, NULL, &m_storageDaemonToken, &lsError);
    if (!result)
        goto Done;

    result = LSCall(m_serviceHandle, "luna://com.palm.lunabus/signal/addmatch", "{\"category\":\"/storaged\", \"method\":\"MSMEntry\"}", msmEntryCallback, NULL, &m_storageDaemonToken, &lsError);
    if (!result)
        goto Done;

    result = LSCall(m_serviceHandle, "luna://com.palm.lunabus/signal/addmatch", "{\"category\":\"/storaged\", \"method\":\"MSMFscking\"}", msmFsckingCallback, NULL, &m_storageDaemonToken, &lsError);
    if (!result)
        goto Done;

Done:
    if (!result) {
        LOG_DEBUG("Failed in %s: %s - restarting", __FUNCTION__, lsError.message);
        LSErrorFree(&lsError);
        exit(0);
    } else {
        LOG_DEBUG("DownloadManager on service bus");
    }
}

void DownloadManager::stopService()
{
    LSError lsError;
    LSErrorInit(&lsError);
    bool result;

    result = LSUnregister(m_serviceHandle, &lsError);
    if (!result)
        LSErrorFree(&lsError);

    m_serviceHandle = 0;
    LOG_DEBUG("Download Manager stopped");
}

//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_download download

 start a download

 @par Parameters
 Name | Required | Type | Description
 -----|--------|------|----------
 target | yes | Integer | target url to download.
 mime | no | String | mime type string, not used in curl now, instead "application/x-binary" is set.
 authToken | no | String | used for curl in "Auth-Token:"
 cookieToken | no | String | cookieToken.
 cookieHeader | no | String | cookieHeader.
 deviceId | no | String | used for curl in "Device-Id:"
 targetDir | no | String | target directory where the download file is placed.
 targetFilename | no | String | target file name that the download file will have. If it's omitted, it will have unique name. If there is the same file name, downloadmgr internally generate unique name.
 keepFilenameOnRedirect | Boolean | String | If True, it will follow redirects until it download the actual file.
 canHandlePause | no | Boolean | True if it can be paused.
 appendTargetFile | no | Boolean | if true and if target file already exist, append download data not create new one.
 e_rangeLow | no | String | the offset in number of bytes that you want the transfer to start from. used for curl option (refer curl_easy_setopt(), CURLOPT_RESUME_FROM_LARGE)
 e_rangeHigh | no | String | not used now, but must be bigger than e_rangeLow
 interface | no | String | one of the following state - ("wifi", "wan", "btpan"), it internally set to ANY if it is not one of them. If it is any it will determin a good interface as follows in order ( wifi, wan, btpan )

 @par Returns(Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 ticket | yes | Integer | download task's ticket. It is used for querying download task.
 url | yes | Integer | target url to download.
 target | no | String | target path+file name of downloading file.
 returnValue | yes | Boolean | Indicates if the call was successful
 errorCode | no | Boolean | Describes the error if call was not successful
 subscribed | no | Boolean | True if subscribed

 @par Returns(Subscription)
 Please refer Returns(Subscription) of com_webos_service_downloadmanager_downloadStatusQuery
 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbDownload(LSHandle* lshandle, LSMessage *msg, void *user_data)
{

    LSError lserror;

    std::string result;
    LSErrorInit(&lserror);

    bool success = false;
    bool subscribed = false;
    bool retVal = false;
    std::string errorCode;
    std::string errorText;

    std::string caller;
    std::string key = "0";
    std::string targetUrl = "";
    std::string targetMime = "";
    std::string authToken = "";
    std::string cookieToken = "";
    std::string cookieHeader = "";
    std::string deviceId = "";
    std::string overrideTargetDir = "";
    std::string overrideTargetFile = "";
    std::string interfaceName = "";
    std::string strInt = "";
    ConnectionType conn;
    bool shouldKeepOriginalFilename = false;
    bool canHandlePause = false;
    bool autoResume = true;
    bool appendTargetFile = false;
    unsigned long ticket_id = 0;
    int start_rc = 0;
    const char * ccptr = NULL;
    std::pair<uint64_t, uint64_t> range = std::pair<uint64_t, uint64_t>(0, 0);

    DownloadTask task;
    JUtil::Error error;

    pbnjson::JValue root = JUtil::parse(LSMessageGetPayload(msg), "DownloadService.download", &error);
    if (root.isNull()) {
        success = false;
        errorCode = ConvertToString<int>(StartStatus_GENERALERROR);
        errorText = error.detail();
        goto Done;
    }

    ccptr = LSMessageGetApplicationID(msg);
    if (ccptr == NULL)
        ccptr = LSMessageGetSenderServiceName(msg);
    if (ccptr == NULL)
        ccptr = LSMessageGetSender(msg);
    caller = ccptr ? ccptr : "unknown";

    targetUrl = root["target"].asString();
    targetMime = root["mime"].asString();
    authToken = root["authToken"].asString();
    cookieToken = root["cookieToken"].asString();
    cookieHeader = root["cookieHeader"].asString();
    //TODO: since this doesn't change (ever!), it should be retrieved from the "system" (perhaps HostBase-derived object???)
    deviceId = root["deviceId"].asString();
    overrideTargetDir = root["targetDir"].asString();

    // only privileged service don't have restrictions
    if (!isPrivileged(caller)) {
        //check its validity right here. Can't do it later because InstallService (direct)calls get mixed in
        // and they can't have path restrictions
        if (!isValidOverridePath(overrideTargetDir) || (!isPathInMedia(overrideTargetDir))) {
            LOG_DEBUG("%s: override target dir security check failed: path [%s] not allowed", __FUNCTION__, overrideTargetDir.c_str());
            overrideTargetDir = "";
        }
    }
    LOG_DEBUG("luna-bus download: target dir override: [%s]", overrideTargetDir.c_str());

    overrideTargetFile = root["targetFilename"].asString();
    shouldKeepOriginalFilename = root["keepFilenameOnRedirect"].asBool();
    canHandlePause = root["canHandlePause"].asBool();
    autoResume = root["autoResume"].asBool();
    appendTargetFile = root["appendTargetFile"].asBool();

    strInt = root["e_rangeLow"].asString();
    range.first = strtouq(strInt.c_str(), 0, 10);

    strInt = root["e_rangeHigh"].asString();
    range.second = strtouq(strInt.c_str(), 0, 10);

    interfaceName = root["interface"].asString();
    if (interfaceName == "wired")
        conn = ConnectionType_Wired;
    else if (interfaceName == "wifi")
        conn = ConnectionType_Wifi;
    else if (interfaceName == "wan")
        conn = ConnectionType_Wan;
    else if (interfaceName == "btpan")
        conn = ConnectionType_Btpan;
    else
        conn = ConnectionType_ANY;

    //try and start the download

    ticket_id = DownloadManager::instance().generateNewTicket();

    LOG_DEBUG("%s: target = %s,  ticket = %lu targetDir %s targetFilename %s authtoken = %s , devid = %s , cookie = %s\n", __PRETTY_FUNCTION__, targetUrl.c_str(), ticket_id, overrideTargetDir.c_str(),
            overrideTargetFile.c_str(), authToken.c_str(), deviceId.c_str(), cookieToken.c_str());

    start_rc = DownloadManager::instance().download(caller, targetUrl, targetMime, overrideTargetDir, overrideTargetFile, ticket_id, shouldKeepOriginalFilename, authToken, deviceId, conn,
            canHandlePause, autoResume, appendTargetFile, cookieHeader, range, DownloadTask::MAXREDIRECTIONS);

    if (start_rc < 0) {
        //error!
        errorCode = ConvertToString<int>(start_rc);
        errorText = "start returned with an error code";
        success = false;
        goto Done;
    }

    ticket_id = (unsigned int) start_rc;

    //retrieve the info of this ticket from download manager's task map

    if (DownloadManager::instance().getDownloadTaskCopy(ticket_id, task) == false) {
        //download task not found!
        errorCode = ConvertToString<int>(StartStatus_GENERALERROR);
        errorText = "download task not found after start called";
        success = false;
        goto Done;
    }

    success = true;
    key = ConvertToString<long>(ticket_id);

    if (LSMessageIsSubscription(msg)) {

        retVal = LSSubscriptionAdd(lshandle, key.c_str(), msg, &lserror);

        if (!retVal) {
            subscribed = false;
            LSErrorPrint(&lserror, stderr);
            LSErrorFree(&lserror);
        } else
            subscribed = true;

    }

Done:
    if (success) {
        std::string payload = std::string("\"ticket\":") + key + std::string(" , \"url\":\"") + task.m_url + std::string("\"") + std::string(" , \"target\":\"") + task.m_destPath + task.m_destFile
                + std::string("\"");

        result = std::string("{\"returnValue\":true , ") + payload;
    } else
        result = std::string("{\"returnValue\":false , \"errorCode\":\"") + errorCode + std::string("\",\"errorText\":\"") + errorText + std::string("\"");

    if (subscribed) {
        result += std::string(", \"subscribed\":true }");
    } else {
        result += std::string(", \"subscribed\":false }");
    }

    const char* r = result.c_str();
    if (!LSMessageReply(lshandle, msg, r, &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;
}

//static
//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_resumeDownload resumeDownload

 resume a download

 @par Parameters
 Name | Required | Type | Description
 -----|--------|------|----------
 ticket | yes | Integer | Download ID from download

 @par Returns (Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 returnValue | yes | Boolean | Indicates if the call was successful
 errorCode | no | Boolean | Describes the error if call was not successful
 subscribed | no | Boolean | True if subscribed

 @par Returns (Subscription)
 None
 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbResumeDownload(LSHandle* lshandle, LSMessage *message, void *user_data)
{
    LSError lserror;
    LSErrorInit(&lserror);

    const char* str = LSMessageGetPayload(message);
    if (!str)
        return false;

    std::string errorCode;
    std::string errorText;
    std::string extendedErrorText;
    std::string authToken;
    std::string deviceId;
    std::string key;
    bool subscribed = false;
    int rc = 0;
    int ticket = 0;
    JUtil::Error error;

    pbnjson::JValue root = JUtil::parse(str, "DownloadService.resumeDownload", &error);
    if (root.isNull()) {
        errorCode = ConvertToString<int>(ResumeStatus_GENERALERROR);
        errorText = error.detail();
        goto Done_cbResumeDownload;
    }

    //get the ticket number parameter
    ticket = root["ticket"].asNumber<int>();
    authToken = root["authToken"].asString();
    deviceId = root["deviceId"].asString();

    key = ConvertToString<long>(ticket);

    if (LSMessageIsSubscription(message)) {

        if (!LSSubscriptionAdd(lshandle, key.c_str(), message, &lserror)) {
            subscribed = false;
            LSErrorPrint(&lserror, stderr);
            LSErrorFree(&lserror);
        } else
            subscribed = true;

    }

    rc = DownloadManager::instance().resumeDownload(ticket, authToken, deviceId, extendedErrorText);

    errorCode = ConvertToString<int>(rc);
    switch (rc) {
    case ResumeStatus_QUEUEFULL:
        errorText = "Download queue is full; cannot resume at this time";
        break;
    case ResumeStatus_HISTORYCORRUPT:
    case ResumeStatus_NOTDOWNLOAD:
    case ResumeStatus_NOTINTERRUPTED:
    case ResumeStatus_NOTINHISTORY:
    case ResumeStatus_GENERALERROR:
        errorText = "Ticket provided does not correspond to an interrupted transfer in history";
        break;
    case ResumeStatus_CANNOTACCESSTEMP:
        errorText = "Cannot access temporary file for append";
        break;
    default:
        errorText = extendedErrorText;
        break;
    }

Done_cbResumeDownload:
    root = pbnjson::Object();
    root.put("subscribed", subscribed);
    if (rc <= 0) {
        root.put("returnValue", false);
        root.put("errorCode", errorCode);
        root.put("errorText", errorText);
    } else
        root.put("returnValue", true);

    LSErrorInit(&lserror);
    if (!LSMessageReply(lshandle, message, JUtil::toSimpleString(root).c_str(), &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;
}

//static
//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_pauseDownload pauseDownload

 pause a download

 @par Parameters
 Name | Required | Type | Description
 -----|--------|------|----------
 ticket | yes | Integer | Download ID from download

 @par Returns (Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 returnValue | yes | Boolean | Indicates if the call was successful
 errorCode | no | Boolean | Describes the error if call was not successful
 subscribed | no | Boolean | True if subscribed

 @par Returns (Subscription)
 None
 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbPauseDownload(LSHandle* lshandle, LSMessage *message, void *user_data)
{
    LSError lserror;
    LSErrorInit(&lserror);

    const char* str = LSMessageGetPayload(message);
    if (!str)
        return false;

    std::string errorCode;
    std::string errorText;

    int rc = 0;
    int ticket = 0;

    JUtil::Error error;

    pbnjson::JValue root = JUtil::parse(str, "DownloadService.pauseDownload", &error);
    if (root.isNull()) {
        errorCode = ConvertToString<int>(PauseStatus_GENERALERROR);
        errorText = error.detail();
        goto Done_cbPauseDownload;
    }

    //get the ticket number parameter
    ticket = root["ticket"].asNumber<int>();

    rc = DownloadManager::instance().pauseDownload(ticket);

    switch (rc) {
    case PauseStatus_NOSUCHDOWNLOADTASK:
        errorCode = ConvertToString<int>(rc);
        errorText = "Ticket provided does not correspond to a downloading transfer";
        break;
    }

Done_cbPauseDownload:

    root = pbnjson::Object();
    if (rc <= 0) {
        root.put("returnValue", false);
        root.put("errorCode", errorCode);
        root.put("errorText", errorText);
    } else
        root.put("returnValue", true);

    if (!LSMessageReply(lshandle, message, JUtil::toSimpleString(root).c_str(), &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;
}

//static
//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_cancelDownload cancelDownload

 cancel either an upload or a download that is in progress

 @par Parameters
 Name | Required | Type | Description
 -----|--------|------|----------
 ticket | yes | Integer | Download ID from download

 @par Returns (Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 ticket | yes | Integer | Download ID from download
 returnValue | yes | Boolean | Indicates if the call was successful
 subscribed | no | Boolean | True if subscribed
 aborted | No | Boolean | Was download aborted flag
 completed | No | Boolean | Was download completed flag
 completionStatusCode | No | Integer | status code
 errorCode | no | Boolean | Describes the error if call was not successful

 @par Returns (Subscription)
 None
 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbCancelDownload(LSHandle* lshandle, LSMessage *msg, void *user_data)
{

    LSError lserror;
    LSErrorInit(&lserror);

    unsigned long ticket_id = 0;

    bool success = false;
    JUtil::Error error;

    pbnjson::JValue root = JUtil::parse(LSMessageGetPayload(msg), "DownloadService.cancelDownload", &error);
    if (root.isNull()) {
        success = false;
        goto Done;
    }

    ticket_id = root["ticket"].asNumber<int64_t>();

    success = true;
    //kill this task
    DownloadManager::instance().cancel(ticket_id);

Done:
    pbnjson::JValue repleyJsonObj = pbnjson::Object();
    repleyJsonObj.put("ticket", (int64_t) ticket_id);
    repleyJsonObj.put("returnValue", success);
    if (!success)
        repleyJsonObj.put("errorText", error.detail());
    else {
        //to retain compatibility with older clients, inject some status here
        repleyJsonObj.put("aborted", true);
        repleyJsonObj.put("completed", false);
        repleyJsonObj.put("completionStatusCode", CompletionStatus_CANCELLED);
    }
    if (!LSMessageReply(lshandle, msg, JUtil::toSimpleString(repleyJsonObj).c_str(), &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;
}

//static
//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_cancelAllDownloads cancelAllDownloads

 cancel all downloads that are in progress

 @par Parameters
 None

 @par Returns (Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 returnValue | yes | Boolean | Indicates if the call was successful

 @par Returns (Subscription)
 None
 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbCancelAllDownloads(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSError lserror;
    LSErrorInit(&lserror);
    std::string result;

    DownloadManager::instance().cancelAll();

    result = "{ \"returnValue\":true }";

    const char* r = result.c_str();
    if (!LSMessageReply(lshandle, msg, r, &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;
}

//static
//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_listPendingDownloads listPendingDownloads

 list pending downloads

 @par Parameters
 None

 @par Returns (Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 returnValue | yes | Boolean | Indicates if the call was successful
 count | yes | Integer | Number of downloads in progress
 downloads | No | object | Array containing downloads

 @par Returns (Subscription)
 None
 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbListPendingDownloads(LSHandle * lshandle, LSMessage *msg, void * user_data)
{
    LSError lserror;
    LSErrorInit(&lserror);
    std::string result;
    std::vector<std::string> list;

    int n = DownloadManager::instance().getJSONListOfAllDownloads(list);

    result = "{ \"returnValue\":true , \"count\":" + ConvertToString<int>(n);
    if (n) {
        result += std::string(", \"downloads\": [ ");
        result += list.at(0);
        for (size_t i = 1; i < list.size(); i++) {
            result += std::string(", ") + list.at(i);
        }
        result += std::string(" ] ");
    }

    result += std::string("}");

    const char* r = result.c_str();
    if (!LSMessageReply(lshandle, msg, r, &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;
}

//static
//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_getAllHistory getAllHistory

 get all history

 @par Parameters
 Name | Required | Type | Description
 -----|--------|------|----------
 owner | yes | String | owner

 @par Returns (Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 returnValue | yes | Boolean | Indicates if the call was successful
 subscribed | no | Boolean | True if subscribed
 errorCode | no | Boolean | Describes the error if call was not successful
 items | Yes | Object | Array of items

 @par Returns (Subscription)
 None
 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbGetAllHistory(LSHandle * lshandle, LSMessage *msg, void * user_data)
{
    LSError lserror;
    std::string historyCaller;
    std::string errorText;
    std::vector<DownloadHistory> historyList;
    LSErrorInit(&lserror);
    bool retVal = false;
    JUtil::Error error;

    pbnjson::JValue root = JUtil::parse(LSMessageGetPayload(msg), "DownloadService.getAllHistory", &error);
    if (root.isNull()) {
        errorText = error.detail();
        goto Done;
    }

    historyCaller = root["owner"].asString();
    LOG_DEBUG("Requested for download-history by owner [%s]", historyCaller.c_str());
    retVal = DownloadManager::instance().getDownloadHistoryAllByCaller(historyCaller, historyList);

    if (!retVal)
        errorText = "not_found";

Done:
    pbnjson::JValue repleyJsonObj = pbnjson::Object();
    if (retVal) {
        //go through every result returned
        pbnjson::JValue resultArray = pbnjson::Array();

        for (std::vector<DownloadHistory>::iterator it = historyList.begin(); it != historyList.end(); ++it) {
            pbnjson::JValue item = pbnjson::Object();
            item.put("interface", it->m_interface);
            item.put("ticket", (int64_t) it->m_ticket);
            item.put("state", it->m_state);
            item.put("recordString", it->m_downloadRecordJsonString);

            pbnjson::JValue statusObj = JUtil::parse(it->m_downloadRecordJsonString.c_str(), std::string(""));
            if (!statusObj.isNull()) {
                for (pbnjson::JValue::ObjectIterator it = statusObj.begin(); it != statusObj.end(); ++it) {
                    std::string strKey = (*it).first.asString();
                    if (strKey == std::string("target")) {
                        std::string strVal = (*it).second.asString();
                        if (doesExistOnFilesystem(strVal.c_str())) {
                            item.put("fileExistsOnFilesys", true);
                            item.put("fileSizeOnFilesys", filesizeOnFilesystem(strVal.c_str()));
                        } else {
                            item.put("fileExistsOnFilesys", false);
                        }
                    }
                }
            }
            resultArray.append(item);
        }
        repleyJsonObj.put("returnValue", true);
        repleyJsonObj.put("items", resultArray);

    } else {
        //total fail
        repleyJsonObj.put("returnValue", false);
        repleyJsonObj.put("errorText", errorText);
    }

    LSErrorInit(&lserror);
    if (!LSMessageReply(lshandle, msg, JUtil::toSimpleString(repleyJsonObj).c_str(), &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;
}

//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_deleteDownloadedFile deleteDownloadedFile

 delete a downloaded file based on the ticket number

 @par Parameters
 Name | Required | Type | Description
 -----|--------|------|----------
 ticket | yes | Integer | ticket of the file to be deleted.

 @par Returns(Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 ticket  | yes | Integer | ticket of the deleted file.
 returnValue | yes | Boolean | Indicates if the call was successful
 errorCode | no | String | Describes the error if call was not successful

 @par Returns (Subscription)
 None
 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbDeleteDownloadedFile(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSError lserror;
    std::string result;
    std::string historyCaller;
    std::string historyState;
    std::string historyInterface;
    std::string errorText;
    std::string targetStr;
    std::string key = "0";

    DownloadTask task;
    unsigned long ticket_id = 0;

    LSErrorInit(&lserror);
    JUtil::Error error;
    bool success = false;

    pbnjson::JValue root = JUtil::parse(LSMessageGetPayload(msg), "DownloadService.deleteDownloadedFile", &error);
    pbnjson::JValue resultRoot;
    if (root.isNull()) {
        success = false;
        errorText = error.detail();
        goto Done;
    }

    ticket_id = root["ticket"].asNumber<int64_t>();
    key = ConvertToString<long>(ticket_id);

    //retrieve the info of this ticket from download manager's task map

    if (DownloadManager::instance().getDownloadTaskCopy(ticket_id, task)) {
        //found it in currently downloading tasks
        //can't delete it while it is downloading
        success = false;
        errorText = std::string("cannot delete since file is still downloading");
        goto Done;
    } else if (DownloadManager::instance().getDownloadHistory(ticket_id, historyCaller, historyInterface, historyState, result)) { //try the db history
        //parse out the destination
        resultRoot = JUtil::parse(result.c_str(), std::string(""));
        if (resultRoot.isNull()) {
            success = false;
            errorText = std::string("cannot delete; missing target property in history record");
            goto Done;
        }
        if (!resultRoot.hasKey("target")) {
            success = false;
            errorText = std::string("cannot delete; bad target property in history record");
            goto Done;
        }
        success = true;
        targetStr = resultRoot["target"].asString();
        deleteFile(targetStr.c_str());      //if the file is not found, no big deal; consider it deleted!
        result = result = std::string("{\"ticket\":") + key + std::string(" , \"returnValue\":true }");
    } else {
        success = false;
        errorText = std::string("requested download record not found");
    }

Done:
    if (!success) {
        result = std::string("{\"ticket\":") + key + std::string(" , \"returnValue\":false , \"errorText\":\"") + errorText + std::string("\" }");
    }
    const char* r = result.c_str();
    if (!LSMessageReply(lshandle, msg, r, &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;
}

//static
//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_clearHistory clearHistory

 get all history

 @par Parameters
 None

 @par Returns (Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 returnValue | yes | Boolean | Indicates if the call was successful

 @par Returns (Subscription)
 None
 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbClearDownloadHistory(LSHandle * lshandle, LSMessage *msg, void * user_data)
{
    LOG_DEBUG("Requst for clearing the download History");
    LSError lserror;
    LSErrorInit(&lserror);
    std::string historyCaller;
    std::string errorText;
    int errorCode = 0;
    JUtil::Error error;

    pbnjson::JValue root = JUtil::parse(LSMessageGetPayload(msg), "DownloadService.clearHistory", &error);
    if (root.isNull()) {
        errorCode = HistoryStatus_GENERALERROR;
        errorText = error.detail();
        goto Done;
    }

    if (!root.hasKey("owner")) {
        errorCode = DownloadManager::instance().clearDownloadHistory();
    } else {
        historyCaller = root["owner"].asString();
        errorCode = DownloadManager::instance().clearDownloadHistoryByGlobbedOwner(historyCaller);
    }

    switch (errorCode) {
    case HistoryStatus_HISTORYERROR:
        errorText = std::string("Internal error");
        break;
    case HistoryStatus_NOTINHISTORY:
        errorText = std::string("Fail to find owner");
        break;
    }

Done:
    //TODO: should probably return something more reflective of the actual result
    pbnjson::JValue replyJsonObj = pbnjson::Object();
    if (replyJsonObj.isNull())
        return false;

    if (errorCode > 0) {
        replyJsonObj.put("returnValue", false);
        replyJsonObj.put("errorCode", errorCode);
        replyJsonObj.put("errorText", errorText);
    } else
        replyJsonObj.put("returnValue", true);

    if (!LSMessageReply(lshandle, msg, JUtil::toSimpleString(replyJsonObj).c_str(), &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;
}

void DownloadManager::filesystemStatusCheck(const uint64_t& freeSpaceKB, const uint64_t& totalSpaceKB, bool * criticalAlertRaised, bool * stopMarkReached)
{
    uint32_t pctFull = 100 - (uint32_t) (0.5 + ((double) freeSpaceKB / (double) totalSpaceKB) * (double) 100.0);
    pctFull = (pctFull <= 100 ? pctFull : 100);
    LOG_DEBUG("%s: Percent Full = %u (from free space KB = %llu , total space KB = %llu", __FUNCTION__, pctFull, freeSpaceKB, totalSpaceKB);

    if (pctFull < DownloadSettings::Settings()->m_freespaceLowmarkFullPercent)
        return;

    bool critical = false;
    bool stopMark = false;
    if (freeSpaceKB <= DownloadSettings::Settings()->m_freespaceStopmarkRemainingKBytes)
        stopMark = true;
    else if (pctFull >= DownloadSettings::Settings()->m_freespaceCriticalmarkFullPercent)
        critical = true;

    if (criticalAlertRaised)
        *criticalAlertRaised = critical;
    if (stopMarkReached)
        *stopMarkReached = stopMark;
}

//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_downloadStatusQuery downloadStatusQuery

 get the status of an download in progress, or anything in history

 @par Parameters
 Name | Required | Type | Description
 -----|--------|------|----------
 ticket | yes | Integer | ticket of the file to be queried.
 subscribe | no | boolean | subscribe download ticket.

 @par Returns(Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 ticket  | yes | Integer | ticket of the deleted file.
 url  | no | Integer | If the download is in progress, target url.
 amountReceived | no | Integer | If the download is in progress, amount of received in integer format
 e_amountReceived | no | String | If the download is in progress, amount of received in string format(%llu)
 amountTotal | no | Integer | If the download is in progress, amount of total in integer format
 e_amountTotal | no | String | If the download is in progress, amount of total in string format(%llu)
 owner | no | String | If the download is completed, ownerID (the App ID which requested the download)
 interface | no | String | If the download is completed, one of the following state - ("wifi", "wan", "btpan")
 state | no | String | If the download is completed, one of the following state - ("running", "queued", "paused", "cancelled")
 returnValue | yes | Boolean | Indicates if the call was successful
 errorCode | no | Boolean | Describes the error if call was not successful
 subscribed | no | Boolean | True if subscribed

 @par Returns(Subscription)
 The subscription message is posted periodically while a downloading task is in progress.
 And it is also posted when the task is paused or cancelled or completed.
 Name | Required | Type | Description
 -----|--------|------|----------
 ticket | yes | Integer | ticket of the download task.
 url  | yes | String | url.
 sourceUrl  | yes | String | sourceUrl.
 deviceId  | yes | String | deviceId.
 authToken  | yes | String | authToken.
 target  | yes | String | the actual downloading file name including full path and file name.(destPath + destTempPrefix + destFile)
 destTempPrefix  | yes | String | destTempPrefix.
 destFile  | yes | String | destFile.
 destPath  | yes | String | destPath.
 mimetype  | yes | String | mimetype.
 amountReceived  | yes | String | amountReceived.
 e_amountReceived  | yes | Integer | amountReceived in number format(%llu).
 amountTotal  | yes | String | amountTotal.
 e_amountTotal  | yes | Integer | e_amountTotal in number format(%llu).
 initialOffset  | yes | String | initialOffset.
 e_initialOffsetBytes  | yes | Integer | e_initialOffsetBytes in number format(%llu).
 e_rangeLow  | yes | String | e_rangeLow.
 e_rangeHigh  | yes | String | e_rangeHigh.
 canHandlePause  | yes | Boolean | canHandlePause.
 cookieHeader  | yes | String | cookieHeader.
 completionStatusCode | yes | Integer | status code. please refer DOWNLOADMANAGER_COMPLETIONSTATUS_*
 httpStatus | no | Integer| http status result code if not interrupted.
 interrupted | yes | Boolean | True if it is interrupted.
 completed | yes | Boolean | True if it is completed.
 aborted | yes | Boolean | True if it is aborted.
 target | yes | String | target url to download.

 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbDownloadStatusQuery(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSError lserror;
    std::string result;
    std::string historyCaller;
    std::string historyState;
    std::string historyInterface;
    std::string errorText;
    std::string subscribeKey = "0";

    DownloadTask task;
    unsigned long ticket_id = 0;

    LSErrorInit(&lserror);

    bool fromTicketMap = false;
    bool fromHistory = false;
    bool retVal = false;
    JUtil::Error error;

    pbnjson::JValue root = JUtil::parse(LSMessageGetPayload(msg), "DownloadService.downloadStatusQuery", &error);
    if (root.isNull()) {
        errorText = error.detail();
        goto Done;
    }

    ticket_id = root["ticket"].asNumber<int64_t>();
    subscribeKey = ConvertToString<long>(ticket_id);
    LOG_DEBUG("DownloadStatusQuery : ticket - %lu ", ticket_id);

    //retrieve the info of this ticket from download manager's task map
    if (DownloadManager::instance().getDownloadTaskCopy(ticket_id, task)) {
        //found it in currently downloading tasks
        fromTicketMap = true;
    } else {
        //try the db history
        fromHistory = DownloadManager::instance().getDownloadHistory(ticket_id, historyCaller, historyInterface, historyState, result);
        if (!fromHistory)
            errorText = "ticket_not_found";
    }

Done:
    pbnjson::JValue responseRoot = pbnjson::Object();
    std::string lbuff;
    responseRoot.put("ticket", (int64_t) ticket_id);
    if (fromTicketMap) {
        responseRoot.put("url", task.m_url);

        lbuff = Utils::toString(task.m_bytesCompleted);
        responseRoot.put("amountReceived", (int32_t) (task.m_bytesCompleted));
        responseRoot.put("e_amountReceived", lbuff);

        lbuff = Utils::toString(task.m_bytesTotal);
        responseRoot.put("amountTotal", (int32_t) (task.m_bytesTotal));
        responseRoot.put("e_amountTotal", lbuff);

        if (LSMessageIsSubscription(msg)) {
            retVal = LSSubscriptionAdd(lshandle, subscribeKey.c_str(), msg, &lserror);
            if (!retVal) {
                responseRoot.put("subscribed", false);
                LSErrorPrint(&lserror, stderr);
                LSErrorFree(&lserror);
            } else
                responseRoot.put("subscribed", true);
        }
        responseRoot.put("returnValue", true);
    } else if (fromHistory) {
        pbnjson::JValue resultObject = JUtil::parse(result.c_str(), std::string(""));
        if (resultObject.isNull()) {
            LOG_DEBUG("%s: fromHistory: error in parsing 'result' object [%s]", __FUNCTION__, result.c_str());
            responseRoot.put("returnValue", false);
            responseRoot.put("subscribed", false);
            responseRoot.put("errorText", std::string("db_error"));
        } else {
            for (pbnjson::JValue::ObjectIterator it = resultObject.begin(); it != resultObject.end(); ++it) {
                responseRoot.put((*it).first.asString(), (*it).second);
            }
            responseRoot.put("owner", historyCaller);
            responseRoot.put("interface", historyInterface);
            responseRoot.put("state", historyState);

            if (LSMessageIsSubscription(msg)) {
                retVal = LSSubscriptionAdd(lshandle, subscribeKey.c_str(), msg, &lserror);
                if (!retVal) {
                    responseRoot.put("subscribed", false);
                    LSErrorPrint(&lserror, stderr);
                    LSErrorFree(&lserror);
                } else
                    responseRoot.put("subscribed", true);
            }
            responseRoot.put("returnValue", true);
        }
    } else {
        //total fail
        responseRoot.put("returnValue", false);
        responseRoot.put("subscribed", false);
        responseRoot.put("errorText", errorText);
    }

    LSErrorInit(&lserror);
    if (!LSMessageReply(lshandle, msg, JUtil::toSimpleString(responseRoot).c_str(), &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;
}

/* Upload method:
 * {
 fileName: "/path/to/file",
 fileLabel: <string; the part label (key) for the file part>,
 url: <string; url to which to post>
 contentType: <string; optional; mime type of the file part>
 postParameters: [
 {"key":<string> , "data":<string;literal data> , "contentType":<string; mime type of part> },
 ...
 ],
 cookies: {   "key1": "val1",
 "key2": "val2", ...}, // optional
 customHttpHeaders: [ "val1", "val2", ...] // optional
 }
 *
 */

//static
//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_upload upload

 Uploads a file to the target URI.

 If subscribe = false, the onSuccess handler is called only once, after the initial request.
 To be notified when the upload is complete, set subscribe = true and onSuccess will be called periodically with progress updates.
 The upload is complete when the handler receives a response with completed = true.

 On completing the upload, the upload method sends the following object to all subscribed apps for both success and failure cases:
 Syntax
 @code
 {
 "filename"          : string,
 "url"               : string,
 "fileLabel"         : string,
 "contentType"       : string,
 "postParameters"
 {
 "key"            : string,
 "data"           : string,
 "contentType"    : string
 },
 "cookies" :
 {
 ""     : string,
 ...
 },
 "customHTTPHeaders" : string array
 }
 @endcode

 @par Parameters
 Name | Required | Type | Description
 -----|--------|------|----------
 fileName | yes | String | Path to the local file to be uploaded
 url | yes | String | The URL to which to post the file
 fileLabel | yes | String | The label portion of the file name (as opposed to the file extension)
 contentType | no | String | The MIME type of the file
 postParameters | No | object | An object containing key/data/content triplets to support parameters required by the server to which the file is uploaded


 @par Returns (Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 ticket | yes | Integer | A number uniquely identifying the upload
 returnValue | yes | Boolean | Indicates if the call was successful
 sourceFile | no | String | Path to the local file uploaded
 url | no | string | The URL to which to post the file
 completionCode | no | Integer | Completion status code : 0 -- Success -1 -- General error -2 -- Connect timeout -3 -- Corrupt file -4 -- File system error -5 -- HTTP error 11 -- Interrupted 12 -- Cancelled
 completed | no | Boolean | True if completed
 httpCode | no | Integer | HTTP return code, as described at http://www.w3.org/Protocols/HTTP/HTRESP.html
 responseString | No | String | Server response to the POST request.
 location | No | String |    Uploaded file location.
 subscribed | no | Boolean | True if subscribed
 errorCode | no | String | One of the following error messages: url parameter missing, fileName parameter missing, failed to start upload

 @par Returns (Subscription)
 None
 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbUpload(LSHandle* lshandle, LSMessage* msg, void* user_data)
{

    pbnjson::JValue jo_postheaders;
    std::vector<PostItem> postHeaders;

    pbnjson::JValue jo_httpheaders;
    std::vector<std::string> httpHeaders;

    pbnjson::JValue jo_cookies;
    std::vector<kvpair> cookies;

    std::string contentType;
    std::string filePostLabel;
    std::string targetUrl;
    std::string inputFile;
    std::string errorText;
    int errorCode = 0;

    uint32_t uploadId = 0;
    bool subscribed = false;
    std::string subkey;
    boost::regex regURL("^(https?|ftp):\\/\\/(-\\.)?([^[:space:]/?\\.#-]+\\.?)+(/[^[:space:]]*)?$");
    boost::regex regMIME("^([^[:space:]]+)\\/([^[:space:]]+)$");

    JUtil::Error error;
    pbnjson::JValue root = JUtil::parse(LSMessageGetPayload(msg), "DownloadService.upload", &error);
    if (root.isNull()) {
        errorCode = UploadStatus_GENERALERROR;
        errorText = error.detail();
        goto Done_cbUp;
    }

    //REQUIRED PARAMS
    targetUrl = root["url"].asString();
    inputFile = root["fileName"].asString();

    //OPTIONAL PARAMS
    contentType = root["contentType"].asString();
    filePostLabel = root["fileLabel"].asString();

    jo_postheaders = root["postParameters"];

    /*
     postParameters: [
     {"key":<string> , "data":<string; literal data> , "contentType":<string; mime type of part> },
     ...
     ],
     */

    // check URL parameter validity
    if (!boost::regex_match(targetUrl, regURL)) {
        errorCode = UploadStatus_INVALIDPARAM;
        errorText = "Invalid URL";
        goto Done_cbUp;
    }

    // check MIME parameter validity
    if (root.hasKey("contentType")) {
        if (!boost::regex_match(contentType, regMIME)) {
            errorCode = UploadStatus_INVALIDPARAM;
            errorText = "Invalid MIME type";
            goto Done_cbUp;
        }
    }

    if (!jo_postheaders.isNull()) {
        pbnjson::JValue jo;
        for (int idx = 0; idx < jo_postheaders.arraySize(); ++idx) {
            jo = jo_postheaders[idx];
            if (jo.isNull())
                continue;

            std::string key, data, contentType;
            key = jo["key"].asString();
            data = jo["data"].asString();
            contentType = jo["contentType"].asString();

            // check MIME parameter validity in "postParameters"
            if (jo.hasKey("contentType")) {
                if (!boost::regex_match(contentType, regMIME)) {
                    errorCode = UploadStatus_INVALIDPARAM;
                    errorText = "Invalid MIME type";
                    goto Done_cbUp;
                }
            }

            postHeaders.push_back(PostItem(key, data, ItemType_Value, contentType));
        }
    }

    jo_cookies = root["cookies"];
    if (!jo_cookies.isNull()) {
        for (pbnjson::JValue::ObjectIterator it = jo_cookies.begin(); it != jo_cookies.end(); ++it) {
            cookies.push_back(std::pair<std::string, std::string>((*it).first.asString(), (*it).second.asString()));
        }
    }

    jo_httpheaders = root["customHttpHeaders"];
    if (!jo_httpheaders.isNull()) {
        pbnjson::JValue jo;
        for (int idx = 0; idx < jo_httpheaders.arraySize(); ++idx) {
            jo = jo_httpheaders[idx];
            if (jo.isNull())
                continue;
            std::string s = JUtil::toSimpleString(jo);
            httpHeaders.push_back(s);
        }
    }

    uploadId = DownloadManager::instance().uploadPOSTFile(inputFile, targetUrl, filePostLabel, postHeaders, httpHeaders, cookies, contentType);
    if (uploadId == 0) {
        errorCode = UploadStatus_GENERALERROR;
        errorText = "Failed to start upload";
        goto Done_cbUp;
    }

    subkey = std::string("UPLOAD_") + ConvertToString<uint32_t>(uploadId);
    if (processSubscription(lshandle, msg, subkey)) {
        subscribed = true;
    }

Done_cbUp:

    pbnjson::JValue replyJsonObj = pbnjson::Object();
    if (errorCode > 0) {
        replyJsonObj.put("returnValue", false);
        replyJsonObj.put("errorCode", errorCode);
        replyJsonObj.put("errorText", errorText);
    } else {
        replyJsonObj.put("returnValue", true);
        replyJsonObj.put("ticket", (int64_t) uploadId);
        replyJsonObj.put("subscribed", subscribed);
    }
    LSError lserror;
    LSErrorInit(&lserror);
    if (!LSMessageReply(lshandle, msg, JUtil::toSimpleString(replyJsonObj).c_str(), &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;
}

//static
bool DownloadManager::cbConnectionManagerServiceState(LSHandle* lshandle, LSMessage* message, void* user_data)
{

    LSError lsError;
    LSErrorInit(&lsError);

    const char* str = LSMessageGetPayload(message);
    if (!str)
        return false;

    pbnjson::JValue root = JUtil::parse(str, std::string(""));
    if (root.isNull())
        return true;

    if (root.hasKey("connected")) {
        if (root["connected"].asBool()) {
            //DEBUG: is the force novacom switch on? if yes, tell cnmgr to do it!
            if (DownloadSettings::Settings()->m_dbg_forceNovacomOnAtStartup) {
                turnNovacomOn(DownloadManager::instance().m_serviceHandle);
            }
            //the connection manager is connected...make a call to receive status updates on connections
            if (LSCall(DownloadManager::instance().m_serviceHandle, "luna://com.webos.service.connectionmanager/getstatus", "{\"subscribe\":true}",
                    DownloadManager::cbConnectionManagerConnectionStatus, NULL, NULL, &lsError) == false) {
                LOG_ERROR_PAIRS(LOGID_CNCTNMGR_GETSTUS_ERR, 1, PMLOGKS("ERROR", lsError.message), "");
                LSErrorFree(&lsError);
            }
        } else {
            gchar* escaped_errtext = g_strescape(str, NULL);
            if (escaped_errtext) {
                LOG_ERROR_PAIRS(LOGID_CNCTNMGR_SERSTUS_PARM_MISS, 2, PMLOGKS("ERROR", "called with a message that didn't include connected field"), PMLOGKS("MESSAGE", escaped_errtext), "");
                g_free(escaped_errtext);
            } else {
                LOG_DEBUG("Failed to allocate memory in g_strescape function at %s", __FUNCTION__);
            }
        }
    }

    return true;
}

//static
bool DownloadManager::cbConnectionManagerConnectionStatus(LSHandle* lshandle, LSMessage *message, void *user_data)
{
    LSError lsError;
    LSErrorInit(&lsError);

    const char* str = LSMessageGetPayload(message);
    if (!str)
        return false;

    LOG_DEBUG("Conn Manager - Connection Status payload is %s", str);

    DownloadManager& dlManager = DownloadManager::instance();        //usually not a good idea to save this ptr, but if instance goes away there are bigger problems...
                                                                     // (this avoids excessive function calls since this instance() would be called a lot in here)
    pbnjson::JValue root = JUtil::parse(str, std::string(""));
    if (root.isNull())
        return true;

    bool wiredWentDown = false;
    bool wifiWentDown = false;
    bool wanWentDown = false;
    bool btpanWentDown = false;
    bool wiredWentUp = false;
    bool wifiWentUp = false;
    bool wanWentUp = false;
    bool btpanWentUp = false;
    bool returnValue = false;

    if (root.hasKey("returnValue")) {
        returnValue = root["returnValue"].asBool();
    }
    // network is not allowed..
    if (!returnValue) {
        return true;
    }

    ConnectionStatus wifiConnectionStatusPrevious = dlManager.m_wifiConnectionStatus;
    ConnectionStatus wiredConnectionStatusPrevious = dlManager.m_wiredConnectionStatus;
    ConnectionStatus wanConnectionStatusPrevious = dlManager.m_wanConnectionStatus;
    ConnectionStatus btpanConnectionStatusPrevious = dlManager.m_btpanConnectionStatus;

    pbnjson::JValue topLevelObject;
    topLevelObject = root["wifi"];
    if (root.hasKey("wifi")) {
        std::string state = topLevelObject["state"].asString();
        std::string onInternet;
        if (topLevelObject.hasKey("state")) {
            onInternet = topLevelObject["onInternet"].asString();
            if (!topLevelObject.hasKey("onInternet"))
                onInternet = "no";

            if (state == "connected" && onInternet == "yes") {
                dlManager.m_wifiConnectionStatus = ConnectionStatus_Connected;
                dlManager.m_wifiInterfaceName = topLevelObject["interfaceName"].asString();
                LOG_DEBUG("CONNECTION-STATUS: wifi connected");

                if (wifiConnectionStatusPrevious == ConnectionStatus_Disconnected)
                    wifiWentUp = true;
            } else {
                dlManager.m_wifiConnectionStatus = ConnectionStatus_Disconnected;
                if (wifiConnectionStatusPrevious == ConnectionStatus_Connected)
                    wifiWentDown = true;
                LOG_DEBUG("CONNECTION-STATUS: wifi disconnected");
            }
        }
    }
    topLevelObject = root["wired"];
    if (root.hasKey("wired")) {
        std::string state = topLevelObject["state"].asString();
        std::string onInternet;
        if (topLevelObject.hasKey("state")) {
            onInternet = topLevelObject["onInternet"].asString();
            if (!topLevelObject.hasKey("onInternet"))
                onInternet = "no";

            if (state == "connected" && onInternet == "yes") {
                dlManager.m_wiredConnectionStatus = ConnectionStatus_Connected;
                dlManager.m_wiredInterfaceName = topLevelObject["interfaceName"].asString();
                LOG_DEBUG("CONNECTION-STATUS: wired connected");

                if (wiredConnectionStatusPrevious == ConnectionStatus_Disconnected)
                    wiredWentUp = true;
            } else {
                dlManager.m_wiredConnectionStatus = ConnectionStatus_Disconnected;
                if (wiredConnectionStatusPrevious == ConnectionStatus_Connected)
                    wiredWentDown = true;
                LOG_DEBUG("CONNECTION-STATUS: wired disconnected");
            }
        }
    }
    topLevelObject = root["wan"];
    if (root.hasKey("wan")) {
        pbnjson::JValue contextsArray = topLevelObject["connectedContexts"];
        bool defaultServiceConnected = false;

        for (int i = 0; i < contextsArray.arraySize(); i++) {
            pbnjson::JValue contextObject = contextsArray[i];

            std::string name = contextObject["name"].asString();
            LOG_DEBUG("wan connection info : name : %s, connected : %d, onInternet : %d", name.c_str(), contextObject["connected"].asBool(), contextObject["onInternet"].asBool());

            if ("default" == contextObject["name"].asString() && contextObject["connected"].asBool() && contextObject["onInternet"].asBool()) {
                defaultServiceConnected = true;
                dlManager.m_wanConnectionStatus = ConnectionStatus_Connected;
                dlManager.m_wanInterfaceName = contextObject["interfaceName"].asString();
                LOG_DEBUG("CONNECTION-STATUS: wan connected");

                if (wanConnectionStatusPrevious == ConnectionStatus_Disconnected)
                    wanWentUp = true;
                break;
            }
        }
        if (!defaultServiceConnected) {
            dlManager.m_wanConnectionStatus = ConnectionStatus_Disconnected;
            if (wanConnectionStatusPrevious == ConnectionStatus_Connected)
                wanWentDown = true;
            LOG_DEBUG("CONNECTION-STATUS: wan disconnected");
        }
    }
    topLevelObject = root["btpan"];
    if (root.hasKey("btpan")) {
        //TODO: should perform further queries on Btpan....see getBtPanRouteStatus in connectionmanager
        std::string state = topLevelObject["state"].asString();
        if (topLevelObject.hasKey("state")) {
            if (state == "connected") {
                dlManager.m_btpanConnectionStatus = ConnectionStatus_Connected;
                LOG_DEBUG("CONNECTION-STATUS: btpan connected");
                std::string interfaceName = topLevelObject["interfaceName"].asString();
                if (topLevelObject.hasKey("interfaceName")) {
                    dlManager.m_btpanInterfaceName = interfaceName;
                }
                if (btpanConnectionStatusPrevious == ConnectionStatus_Disconnected)
                    btpanWentUp = true;
            } else if (state == "disconnected") {
                dlManager.m_btpanConnectionStatus = ConnectionStatus_Disconnected;
                LOG_DEBUG("CONNECTION-STATUS: btpan disconnected");
                if (btpanConnectionStatusPrevious == ConnectionStatus_Connected)
                    btpanWentDown = true;
            }
        }
    }

    if (dlManager.m_wiredConnectionStatus == ConnectionStatus_Connected) {
        if (dlManager.m_wifiConnectionStatus == ConnectionStatus_Connected) {
            LOG_DEBUG("Wired and wifi are both connected. Treat wifi is disconnected.");
            dlManager.m_wifiConnectionStatus = ConnectionStatus_Disconnected;
            if (wifiConnectionStatusPrevious == ConnectionStatus_Connected)
                wifiWentDown = true;
            wifiWentUp = false;
        }
        if (dlManager.m_wanConnectionStatus == ConnectionStatus_Connected) {
            LOG_DEBUG("Wired and wan are both connected. Treat wan is disconnected.");
            dlManager.m_wanConnectionStatus = ConnectionStatus_Disconnected;
            if (wanConnectionStatusPrevious == ConnectionStatus_Connected)
                wanWentDown = true;
            wanWentUp = false;
        }
    }
    if (dlManager.m_wifiConnectionStatus == ConnectionStatus_Connected) {
        if (dlManager.m_wanConnectionStatus == ConnectionStatus_Connected) {
            LOG_DEBUG("Wifi and wan are both connected. Treat wan is disconnected.");
            dlManager.m_wanConnectionStatus = ConnectionStatus_Disconnected;
            if (wanConnectionStatusPrevious == ConnectionStatus_Connected)
                wanWentDown = true;
            wanWentUp = false;
        }
    }

    LOG_DEBUG("[CONNECTION-STATUS]: wired - %s, wifi - %s, wan - %s, btpan - %s ", (dlManager.m_wiredConnectionStatus == ConnectionStatus_Connected) ? "connected" : "disconnected",
            (dlManager.m_wifiConnectionStatus == ConnectionStatus_Connected) ? "connected" : "disconnected", (dlManager.m_wanConnectionStatus == ConnectionStatus_Connected) ? "connected" : "disconnected",
            (dlManager.m_btpanConnectionStatus == ConnectionStatus_Connected) ? "connected" : "disconnected");

    LOG_DEBUG("[CONNECTION-STATUS]: allow1x is %s", (s_allow1x ? "TRUE" : "FALSE"));

    // if brick mode is on, then just exit now...all the state variables have been updated at this point
    if (dlManager.currentlyInBrickMode())
        return true;

    //act on changes...

    bool autoResume = DownloadSettings::Settings()->m_autoResume;
    bool resumeAggression = DownloadSettings::Settings()->m_resumeAggression;
    bool previouslyAvailable = (wifiConnectionStatusPrevious == ConnectionStatus_Connected) || (wanConnectionStatusPrevious == ConnectionStatus_Connected)
            || (btpanConnectionStatusPrevious == ConnectionStatus_Connected) || (wiredConnectionStatusPrevious == ConnectionStatus_Connected);
    bool available = (dlManager.m_wifiConnectionStatus == ConnectionStatus_Connected)
            || ((dlManager.m_wanConnectionStatus == ConnectionStatus_Connected) && (dlManager.m_wanConnectionType == WanConnectionType_HS || s_allow1x))
            || (dlManager.m_btpanConnectionStatus == ConnectionStatus_Connected) || (dlManager.m_wiredConnectionStatus == ConnectionStatus_Connected);

    //if nothing is available, but something was previously, then just pause all //TODO: this won't be necessary if interface:ANY is removed and all interfaces are explicit
    if (previouslyAvailable && !available) {
        dlManager.pauseAll();
        return true;
    }

    ConnectionType altInterface = ConnectionType_ANY;
    if (dlManager.m_wiredConnectionStatus == ConnectionStatus_Connected)
        altInterface = ConnectionType_Wired;
    else if (dlManager.m_wifiConnectionStatus == ConnectionStatus_Connected)                    //in precedence order
        altInterface = ConnectionType_Wifi;
    else if ((dlManager.m_wanConnectionStatus == ConnectionStatus_Connected) && (s_allow1x || !(dlManager.m_wanConnectionType == WanConnectionType_1x)))
        altInterface = ConnectionType_Wan;
    else if (dlManager.m_btpanConnectionStatus == ConnectionStatus_Connected)
        altInterface = ConnectionType_Btpan;

    if (wiredWentDown) {
        dlManager.pauseAll(ConnectionType_Wired);
    } else if (wiredWentUp && autoResume) {
        dlManager.resumeAll(ConnectionType_Wired, true);
        dlManager.resumeAll(ConnectionType_ANY, true);
    }
    if (wifiWentDown) {
        dlManager.pauseAll(ConnectionType_Wifi);
    } else if (wifiWentUp && autoResume) {
        dlManager.resumeAll(ConnectionType_Wifi, true);
        dlManager.resumeAll(ConnectionType_ANY, true);
    }
    if (wanWentDown) {
        dlManager.pauseAll(ConnectionType_Wan);
    } else if (wanWentUp && autoResume && (s_allow1x || !(dlManager.m_wanConnectionType == WanConnectionType_1x))) {
        dlManager.resumeAll(ConnectionType_Wan, true);
        dlManager.resumeAll(ConnectionType_ANY, true);
    }
    if (btpanWentDown) {
        dlManager.pauseAll(ConnectionType_Btpan);
    } else if (btpanWentUp && autoResume) {
        dlManager.resumeAll(ConnectionType_Btpan, true);
        dlManager.resumeAll(ConnectionType_ANY, true);
    }

    if (autoResume && available && resumeAggression) {
        if (dlManager.m_wiredConnectionStatus == ConnectionStatus_Disconnected) {
            dlManager.resumeMultipleOnAlternateInterface(ConnectionType_Wired, altInterface, true);
        }
        if (dlManager.m_wifiConnectionStatus == ConnectionStatus_Disconnected) {
            dlManager.resumeMultipleOnAlternateInterface(ConnectionType_Wifi, altInterface, true);
        }
        if ((dlManager.m_wanConnectionStatus == ConnectionStatus_Disconnected) ||
            ((dlManager.m_wanConnectionStatus == ConnectionStatus_Connected) && !s_allow1x && (dlManager.m_wanConnectionType == WanConnectionType_1x))) {
            dlManager.resumeMultipleOnAlternateInterface(ConnectionType_Wan, altInterface, true);
        }
        if (dlManager.m_btpanConnectionStatus == ConnectionStatus_Disconnected) {
            dlManager.resumeMultipleOnAlternateInterface(ConnectionType_Btpan, altInterface, true);
        }
    }

    //performance enhancements:
    if ((wiredWentUp) && (resumeAggression)) {
        LOG_DEBUG("CONNECTION-STATUS: [PERFORMANCE]: swapping all active to Wired");
        //swap all active transfers to wired
        if (dlManager.swapAllActiveToInterface(ConnectionType_Wired) != SWAPALLTOIF_SUCCESS) {
            LOG_DEBUG("CONNECTION-STATUS: [PERFORMANCE]: <ERROR> swapping all active to Wired had at least 1 failure");
        }
    } else if ((wifiWentUp) && (resumeAggression)) {
        LOG_DEBUG("CONNECTION-STATUS: [PERFORMANCE]: swapping all active to Wifi");
        //swap all active transfers to wifi
        if (dlManager.swapAllActiveToInterface(ConnectionType_Wifi) != SWAPALLTOIF_SUCCESS) {
            LOG_DEBUG("CONNECTION-STATUS: [PERFORMANCE]: <ERROR> swapping all active to Wifi had at least 1 failure");
        }
    }
    return true;
}

//static
bool DownloadManager::cbSleepServiceState(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSError lsError;
    LSErrorInit(&lsError);

    pbnjson::JValue root = JUtil::parse(LSMessageGetPayload(msg), std::string(""));
    if (root.isNull())
        return false;

    LOG_DEBUG("cbSleepServiceState payload %s", JUtil::toSimpleString(root).c_str());

    if (root.hasKey("connected")) {
        if (root["connected"].asBool()) {
            if (!LSCall(DownloadManager::instance().m_serviceHandle, "luna://com.palm.sleep/com/palm/power/identify", "{\"clientName\":\"com.webos.service.downloadmanager\",\"subscribe\":true}",
                    cbSleepServiceRegisterForWakeLock, NULL, NULL, &lsError))
                LSErrorFree(&lsError);
        } else {
            DownloadManager::instance().m_sleepDClientId = "";
        }
    }

    return true;
}

bool DownloadManager::cbSleepServiceRegisterForWakeLock(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSError lsError;
    LSErrorInit(&lsError);

    pbnjson::JValue root = JUtil::parse(LSMessageGetPayload(msg), std::string(""));
    LOG_DEBUG("cbSleepServiceRegisterForWakeLock payload %s recieved from sleepd", JUtil::toSimpleString(root).c_str());

    if (root.isNull() || !root["subscribed"].asBool() || !root.hasKey("clientId")) {
        DownloadManager::instance().m_sleepDClientId = "";
        LOG_DEBUG("cbSleepServiceRegisterForWakeLock response is invalid, Reset sleepDClientId");
        return false;
    }

    DownloadManager::instance().m_sleepDClientId = root["clientId"].asString();

    std::string payload = "{\"register\":true,\"clientId\":\"" + DownloadManager::instance().m_sleepDClientId + "\"}";

    if (!LSCall(DownloadManager::instance().m_serviceHandle, "luna://com.palm.sleep/com/palm/power/wakeLockRegister", payload.c_str(), cbSleepServiceWakeLockRegister, NULL, NULL, &lsError)) LSErrorFree(&lsError);
    return true;
}

bool DownloadManager::cbSleepServiceWakeLockRegister(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    pbnjson::JValue root = JUtil::parse(LSMessageGetPayload(msg), std::string(""));
    LOG_DEBUG("cbSleepServiceWakeLockRegister payload %s recieved from sleepd", JUtil::toSimpleString(root).c_str());

    if (root.isNull() || !root["returnValue"].asBool()) {
        DownloadManager::instance().m_sleepDClientId = "";
        LOG_DEBUG("cbSleepServiceWakeLockRegister response is invalid. Reset sleepDClientId");
        return false;
    }

    return true;
}

//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_is1xMode is1xMode

 is1xMode

 @par Parameters
 None

 @par Returns (Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 returnValue | yes | Boolean | Indicates if the call was successful
 subscribed | no | Boolean | True if subscribed
 1x | no | Boolean | Describes if is 1x mode

 @par Returns (Subscription)
 None
 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbConnectionType(LSHandle* lshandle, LSMessage *message, void *user_data)
{
    const char* str = LSMessageGetPayload(message);
    if (!str)
        return false;

    bool is1x = DownloadManager::instance().is1xConnection();

    pbnjson::JValue responseRoot = pbnjson::Object();

    responseRoot.put("returnValue", true);
    responseRoot.put("1x", is1x);

    LSError lserror;
    LSErrorInit(&lserror);
    if (!LSMessageReply(lshandle, message, JUtil::toSimpleString(responseRoot).c_str(), &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;

}

//->Start of API documentation comment block
/**
 @page com_webos_service_downloadmanager com.webos.service.downloadmanager
 @{
 @section com_webos_service_downloadmanager_allow1x allow1x

 allow 1x

 @par Parameters
 Name | Required | Type | Description
 -----|--------|------|----------
 value | yes | Boolean | should allow 1x

 @par Returns (Call)
 Name | Required | Type | Description
 -----|--------|------|----------
 returnValue | yes | Boolean | Indicates if the call was successful
 subscribed | no | Boolean | True if subscribed
 value | Yes | Boolean | allow 1x

 @par Returns (Subscription)
 None
 @}
 */
//->End of API documentation comment block
bool DownloadManager::cbAllow1x(LSHandle* lshandle, LSMessage *message, void *user_data)
{

    LSError lserror;
    LSErrorInit(&lserror);

    std::string errorText;
    JUtil::Error error;

    pbnjson::JValue root = JUtil::parse(LSMessageGetPayload(message), "DownloadService.allow1x", &error);
    if (root.isNull()) {
        errorText = error.detail();
    }

    pbnjson::JValue reply = pbnjson::Object();
    if (errorText.empty()) {
        DownloadManager::s_allow1x = root["value"].asBool();
        reply.put("returnValue", true);
        reply.put("value", DownloadManager::s_allow1x);
    } else {
        reply.put("returnValue", false);
        reply.put("errorText", errorText);
    }

    if (!LSMessageReply(lshandle, message, JUtil::toSimpleString(reply).c_str(), &lserror)) {
        LSErrorPrint(&lserror, stderr);
        LSErrorFree(&lserror);
    }

    return true;

}

static void turnNovacomOn(LSHandle * lshandle)
{
    LSError lserror;
    LSErrorInit(&lserror);
    if (!(LSCallOneReply(lshandle, "luna://com.webos.service.connectionmanager/setnovacommode", "{\"isEnabled\":true, \"bypassFirstUse\":false}",
    NULL, NULL, NULL, &lserror))) {
        LOG_ERROR_PAIRS(LOGID_SETNOVACOM_ERR, 0, " [INSTALLER][DOWNLOADER]  failed to force novacom to On state");
        LSErrorFree(&lserror);
    }
}
bool DownloadManager::requestWakeLock(bool status)
{
    LOG_DEBUG("requestWakeLock is called. status : %d , sleepDClientId : %s", status, (m_sleepDClientId.empty() ? "NULL" : m_sleepDClientId.c_str()));
    if (m_sleepDClientId.empty()) {
        LOG_DEBUG("requestWakeLock- sleepDClientId is empty. Do not call setWakeLock");
        return false;
    }

    if (status && (1 != m_activeTaskCount)) {
        LOG_DEBUG("requestWakeLock is not needed : status - %d, m_activeTaskCount - %d", status, m_activeTaskCount);
        return false;
    }
    LSError lsError;
    LSErrorInit(&lsError);

    std::string payload = "{\"clientId\":\"" + m_sleepDClientId + "\",\"isWakeup\":" + (status ? "true" : "false") + "}";

    LOG_DEBUG("requestWakeLock payload %s", payload.c_str());
    if (!LSCallOneReply(m_serviceHandle, "luna://com.palm.sleep/com/palm/power/setWakeLock", payload.c_str(), cbRequestWakeLock,
    NULL, NULL, &lsError)) {
        LSErrorFree(&lsError);
        return false;
    }
    return true;
}

bool DownloadManager::cbRequestWakeLock(LSHandle* lshandle, LSMessage *msg, void *user_data)
{
    LSError lsError;
    LSErrorInit(&lsError);

    pbnjson::JValue root = JUtil::parse(LSMessageGetPayload(msg), std::string(""));
    LOG_DEBUG("cbRequestWakeLock payload %s recieved from sleepd", JUtil::toSimpleString(root).c_str());
    if (root.isNull() || !root.hasKey("returnValue")) {
        LOG_DEBUG("cbRequestWakeLock response is invalid");
        return false;
    }

    return true;
}
