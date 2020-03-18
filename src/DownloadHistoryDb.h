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

#ifndef DOWNLOADHISTORYDB_H_
#define DOWNLOADHISTORYDB_H_

#include <string>
#include <sqlite3.h>

#define     DOWNLOADHISTORYDB_HISTORYSTATUS_OK                    0
#define     DOWNLOADHISTORYDB_HISTORYSTATUS_GENERALERROR          1
#define     DOWNLOADHISTORYDB_HISTORYSTATUS_HISTORYERROR          2
#define     DOWNLOADHISTORYDB_HISTORYSTATUS_NOTINHISTORY          3

class DownloadHistoryDb {
public:

    class DownloadHistory {
    public:
        unsigned long m_ticket;
        std::string m_owner;
        std::string m_interface;
        std::string m_state;
        std::string m_downloadRecordJsonString;

        DownloadHistory(const unsigned long ticket, const std::string& owner, const std::string& interface, const std::string& state, const std::string& record)
            : m_ticket(ticket),
              m_owner(owner),
              m_interface(interface),
              m_state(state),
              m_downloadRecordJsonString(record)
        {
        }

        DownloadHistory(const unsigned long ticket, const char * cstr_owner, const char * cstr_interface, const char * cstr_state, const char * cstr_record)
            : m_ticket(ticket)
        {
            if (cstr_owner)
                m_owner = cstr_owner;
            if (cstr_interface)
                m_interface = cstr_interface;
            if (cstr_state)
                m_state = cstr_state;
            if (cstr_record)
                m_downloadRecordJsonString = cstr_record;
        }

        DownloadHistory()
            : m_ticket(0)
        {
        }

        //TODO: no longer needed; (once used for special processing during cloning)
        DownloadHistory(const DownloadHistory& c)
        {
            m_ticket = c.m_ticket;
            m_owner = c.m_owner;
            m_interface = c.m_interface;
            m_state = c.m_state;
            m_downloadRecordJsonString = c.m_downloadRecordJsonString;
        }

        //TODO: no longer needed; (once used for special processing during cloning)
        DownloadHistory& operator=(const DownloadHistory& c)
        {
            if (this == &c)
                return *this;
            m_ticket = c.m_ticket;
            m_owner = c.m_owner;
            m_interface = c.m_interface;
            m_state = c.m_state;
            m_downloadRecordJsonString = c.m_downloadRecordJsonString;
            return *this;
        }
    };

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

private:

    static DownloadHistoryDb* s_dlhist_instance;
    sqlite3* m_dlDb;
};

#endif /*DOWNLOADHISTORYDB_H_*/
