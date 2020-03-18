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

#ifndef LOGGING_H
#define LOGGING_H

#include <PmLogLib.h>

/** LOGIDs */
#define LOGID_DOWNLOAD_START                            "DOWNLOAD_START"                     // start - download req
#define LOGID_DOWNLOAD_COMPLETE                         "DOWNLOAD_COMPLETE"                  // complete - download req
#define LOGID_DOWNLOAD_PAUSE                            "DOWNLOAD_PAUSE"                     // pause - download req
#define LOGID_DOWNLOAD_RESUME                           "DOWNLOAD_RESUME"                    // resume - download req
#define LOGID_DOWNLOAD_CANCEL                           "DOWNLOAD_CANCEL"                    // Cancel - download req
#define LOGID_DOWNLOAD_FAIL                             "DOWNLOAD_FAIL"                      // Fail - download req
#define LOGID_REQ_DOWNLOAD_HISTORY_JSON_FAIL            "DNLD_HIST_JSON_FAIL"                // Failed to find param in message - getAllHistory
#define LOGID_DNLD_HIST_DB_STMT_PREPARE_FAIL            "DNLD_HIST_DBSTMT_FAIL"              // Failed to prepare sql statement
#define LOGID_DNLD_STATUS_QUERY_JSON_FAIL               "DNLD_STATUS_QUERY_FAIL"             // "ticket" Parameter not found in downloadStatusQuery
#define LOGID_GCURL_FDMAX_WARNING                       "GLIBCURL_FDMAX_REGISTER_WARNING"    // FD's reached max - registerUnregisterFds
#define LOGID_DB_OPEN_ERROR                             "DB_OPEN_ERROR"                      // downloadhistory db open error
#define LOGID_DB_INTEGRITY_ERROR                        "DB_INTEGRITY_ERROR"                 // failed to check download DB integrity and couldn't recreate it
#define LOGID_DB_RECREATION_FAIL                        "DB_RECREATION_FAIL"                 // failed to create downloadhistory table
#define LOGID_INTERFACE_FAIL_ON_RESUME                  "INTERFACE_FAIL_ON_RESUME"           // Connection interface fail on resume
#define LOGID_SECURITY_CHECK_FAIL                       "SECURITY_CHECK_FAIL"                // failed security check - file scheme is not allowed for download
#define LOGID_RESUME_FSEEK_FAIL                         "RESUME_FSEEK_FAIL"                  // fseek fail during resume download
#define LOGID_NOTABLE_TO_PAUSE                          "NOTABLE_TO_PAUSE"                   // cannot handle this pause request, it will be canceled
#define LOGID_CURL_FAIL_HTTP_STATUS                     "CURL_FAIL_HTTP_STATUS"              // failed to retrieve HTTP status code from handle
#define LOGID_CURL_FAIL_HTTP_CONNECT                    "CURL_FAIL_HTTP_CONNECT"             // failed to retrieve HTTP connect code from handle
#define LOGID_CURL_FAIL_MAXCONNECTION                   "CURL_FAIL_MAXCONNECTION"            // curl_multi_setopt: CURLMOPT_MAXCONNECTS failed
#define LOGID_UNKNOWN_MSG_CBGLIB                        "UNKNOWN_MSG_CBGLIB"                 // CURL message unknown
#define LOGID_EXCEED_ERROR_THRESHOLD                    "EXCEED_ERROR_THRESHOLD"             // task will be discarded, marked as 'removed' - errors are more than THRESHOLD
#define LOGID_SUBSCRIPTIONREPLY_FAIL_ON_PAUSE           "SUBSCRIPTIONREPLY_FAIL_ON_PAUSE"          // failed to update pause status to subscribers
#define LOGID_SUBSCRIPTIONREPLY_FAIL_ON_WRITEDATA       "SUBSCRIPTIONREPLY_FAIL_ON_WRITEDATA"      // failed to update write-progress to subscribers
#define LOGID_SUBSCRIPTIONREPLY_FAIL_ON_READDATA        "SUBSCRIPTIONREPLY_FAIL_ON_READDATA"       // failed to update read-progress to subscribers
#define LOGID_SUBSCRIPTIONREPLY_FAIL_ON_COMPLETION      "SUBSCRIPTIONREPLY_FAIL_ON_COMPLETION"     // failed to update download completion info to subscribers
#define LOGID_SUBSCRIPTIONREPLY_FAIL_ON_CANCEL          "SUBSCRIPTIONREPLY_FAIL_ON_CANCEL"         // failed to update cancellation status to subscribers - Cancel
#define LOGID_SUBSCRIPTIONREPLY_FAIL_ON_CANCELHISTORY   "SUBSCRIPTIONREPLY_FAIL_ON_CANCELHISTORY"  // failed to update cancellation status to subscribers - CancelHistory
#define LOGID_RECEIVED_FSCK_SIGNAL                      "RECEIVED_FSCK_SIGNAL"                // received fsck signal from storaged
#define LOGID_DOWNLOAD_CB_NO_TARGET                     "DOWNLOAD_CB_NO_TARGET"               // 'target' Parameter missing" in download
#define LOGID_CNCLDWNLOAD_CB_NO_TICKET                  "CNCLDNLD_CB_NO_TICKET"               // 'ticket' Parameter missing in cancelDownload
#define LOGID_DLTDWNLDED_FLE_CB_NO_TARGET               "DELTDNLDED_FLE_CB_NO_TARGET"         // 'ticket' Parameter missing in deleteDownloadedFile
#define LOGID_SETTCKTBASE_CB_NO_V                       "SETTCKTBASE_CB_NO_V"                 // 'v' Parameter missing in setTicketBase
#define LOGID_CHCKPATH_CB_NO_PATH                       "CHCKPATH_CB_NO_PATH"                 // 'path' Parameter missing in checkPath
#define LOGID_CNCTNMGR_GETSTUS_ERR                      "CNCTNMGR_GETSTUS_ERR"                // call to connectionmanager/getstatus failed
#define LOGID_CNCTNMGR_SERSTUS_PARM_MISS                "CNCTNMGR_SERSTUS_PARM_MISS"          // connectionmanager ServiceState json doesnt have connected param
#define LOGID_SETNOVACOM_ERR                            "SETNOVACOM_ERR"                      // failed to force novacom to On state
#define LOGID_CALL_CNCTNMGR_ERR                         "CALL_CNCTNMGR_ERR"                   // call to connectionmanager/getstatus failed
#define LOGID_SRVC_INIT_FAIL                            "SRVC_INIT_FAIL"                      // Appinstalld service initialization failed
#define LOGID_CONF_FILE_ERR                             "CONF_FILE_ERR"                       // Conf file data is empty to load settings
#define LOGID_SCHEMA_IO_ERROR                           "SCHEMA_IO_ERR"                       // JSchema resolve fail
#define LOGID_JSON_PARSE_SYNTX_ERR                      "JSON_SYNTX_ERR"                      // json parse syntax error
#define LOGID_JSON_PARSE_SCHMA_ERR                      "JSON_SCHMA_ERR"                      // json parse schema error
#define LOGID_JSON_PARSE_MISC_ERR                       "JSON_MISC_ERR"                       // json parse misc error
#define LOGID_JSON_PARSE_BAD_OBJ                        "JSON_BAD_OBJ"                        // json parse bad object
#define LOGID_JSON_PARSE_BAD_ARRY                       "JSON_BAD_ARRY"                       // json parse bad array
#define LOGID_JSON_PARSE_BAD_STR                        "JSON_BAD_STR"                        // json parse bad string
#define LOGID_JSON_PARSE_BAD_NUM                        "JSON_BAD_NUM"                        // json parse bad number
#define LOGID_JSON_PARSE_BAD_BOOLEAN                    "JSON_BAD_BOOL"                       // json parse bad boolean
#define LOGID_JSON_PARSE_BAD_NULL                       "JSON_BAD_NULL"                       // json parse bad null
#define LOGID_JSON_PARSE_FAIL                           "JSON_PARSE_FAIL"                     // json parse failed

/** use these for key-value pair printing with no free text format*/
#define LOG_CRITICAL_PAIRS_ONLY(...)    PmLogCritical(GetPmLogContext(), ##__VA_ARGS__, " ")
#define LOG_ERROR_PAIRS_ONLY(...)       PmLogError(GetPmLogContext(), ##__VA_ARGS__, " ")
#define LOG_WARNING_PAIRS_ONLY(...)     PmLogWarning(GetPmLogContext(), ##__VA_ARGS__, " ")
#define LOG_INFO_PAIRS_ONLY(...)        PmLogInfo(GetPmLogContext(), ##__VA_ARGS__, " ")

/** use these for key-value pair printing with free text format*/
#define LOG_CRITICAL_PAIRS(...)         PmLogCritical(GetPmLogContext(), ##__VA_ARGS__)
#define LOG_ERROR_PAIRS(...)            PmLogError(GetPmLogContext(), ##__VA_ARGS__)
#define LOG_WARNING_PAIRS(...)          PmLogWarning(GetPmLogContext(), ##__VA_ARGS__)
#define LOG_INFO_PAIRS(...)             PmLogInfo(GetPmLogContext(), ##__VA_ARGS__)

/** use these for no pairs */
#define LOG_DEBUG(...)         PmLogDebug(GetPmLogContext(), ##__VA_ARGS__)

/** use just for temporal debugging */
#ifdef DOWNLOADMGR_LOCAL_DEBUG_MODE
#define LOG_LINE()                      PmLogDebug(GetPmLogContext(), "%s:%d", __FUNCTION__, __LINE__)
#define LOG_LINE_PRETTY()               PmLogDebug(GetPmLogContext(), "%s:%d", __PRETTY_FUNCTION__, __LINE__)
#else
#define LOG_LINE()
#define LOG_LINE_PRETTY()
#endif // DOWNLOADMGR_LOCAL_DEBUG_MODE

extern PmLogContext GetPmLogContext();

#endif // LOGGING_H
