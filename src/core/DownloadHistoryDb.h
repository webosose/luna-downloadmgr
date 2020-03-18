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

#ifndef CORE_DOWNLOADHISTORYDB_H_
#define CORE_DOWNLOADHISTORYDB_H_

#include <string>
#include <vector>
#include <sqlite3.h>

#define DOWNLOADHISTORYDB_HISTORYSTATUS_OK                    0
#define DOWNLOADHISTORYDB_HISTORYSTATUS_GENERALERROR          1
#define DOWNLOADHISTORYDB_HISTORYSTATUS_HISTORYERROR          2
#define DOWNLOADHISTORYDB_HISTORYSTATUS_NOTINHISTORY          3

#include "DownloadHistory.h"

class DownloadHistoryDb {
public:
    static DownloadHistoryDb* instance();

    bool addHistory(unsigned long ticket, const std::string& caller, const std::string interface, const std::string& state, const std::string& downloadRecordString);
    bool addHistory(const DownloadHistory& history);

    int getDownloadHistoryFull(unsigned long ticket, std::string& r_caller, std::string& r_interface, std::string& r_state, std::string& r_history);
    std::string getDownloadHistoryRecord(unsigned long ticket);
    int getDownloadHistoryRecord(unsigned long ticket, DownloadHistory& r_historyRecord);
    int getDownloadHistoryRecordsForOwner(const std::string& owner, std::vector<DownloadHistory>& r_historyRecords);
    int getDownloadHistoryRecordsForState(const std::string& state, std::vector<DownloadHistory>& r_historyRecords);
    int getDownloadHistoryRecordsForInterface(const std::string& interface, std::vector<DownloadHistory>& r_historyRecords);
    int getDownloadHistoryRecordsForStateAndInterface(const std::string& state, const std::string& interface, std::vector<DownloadHistory>& r_historyRecords);

    int changeStateForAll(const std::string& oldState, const std::string& newState);

    int clear();
    void clearByTicket(const unsigned long ticket);
    void clearByOwner(const std::string& caller);
    int clearByGlobbedOwner(const std::string& caller);
    virtual ~DownloadHistoryDb();
    bool getMaxKey(unsigned long& maxKey);

private:
    DownloadHistoryDb();

    bool openDownloadHistoryDb(std::string& errmsg);
    void closeDownloadHistoryDb();

    bool checkTableConsistency();
    bool integrityCheckDb();

    static DownloadHistoryDb* s_dlhist_instance;
    sqlite3* m_dlDb;
};

#endif /*CORE_DOWNLOADHISTORYDB_H_*/
