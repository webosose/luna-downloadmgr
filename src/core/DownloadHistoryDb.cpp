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

#include <core/DownloadHistoryDb.h>
#include <list>
#include <string>
#include <map>
#include <glib.h>
#include <vector>
#include <cstring>
#include <strings.h>
#include <unistd.h>
#include <util/Logging.h>


#define VALID_SCHEMA_VER    "system-2"
///////////////////// DOWNLOAD HISTORY DB //////////////////////////////////////////////

DownloadHistoryDb* DownloadHistoryDb::s_dlhist_instance = 0;
static const char* s_dlDbPath = "/var/luna/data/downloadhistory.db";

//static
DownloadHistoryDb* DownloadHistoryDb::instance()
{
    if (!s_dlhist_instance)
        new DownloadHistoryDb();
    return s_dlhist_instance;
}

DownloadHistoryDb::DownloadHistoryDb() :
        m_dlDb(0)
{
    s_dlhist_instance = this;
    std::string err;

    if (!openDownloadHistoryDb(err))
        LOG_WARNING_PAIRS_ONLY(LOGID_DB_OPEN_ERROR, 1, PMLOGKS("detail", err.c_str()));
}

DownloadHistoryDb::~DownloadHistoryDb()
{
    closeDownloadHistoryDb();
    s_dlhist_instance = 0;
}

//returns false if error
bool DownloadHistoryDb::getMaxKey(unsigned long& maxKey)
{

    sqlite3_stmt* statement = 0;
    const char* tail = 0;
    int ret = 0;
    gchar* queryStr = 0;

    bool resultOk = false;
    int rawData;

    if (!m_dlDb)
        return false;

    queryStr = g_strdup_printf("SELECT MAX(ticket) FROM DownloadHistory");
    if (!queryStr)
        goto Done;

    ret = sqlite3_prepare(m_dlDb, queryStr, -1, &statement, &tail);
    if (ret) {
        LOG_DEBUG("Failed to prepare sql statement: %s", queryStr);
        goto Done;
    }

    ret = sqlite3_step(statement);
    if (ret == SQLITE_ROW) {
        resultOk = true;
        rawData = sqlite3_column_int(statement, 0);
    }

Done:

    if (statement)
        sqlite3_finalize(statement);

    if (queryStr)
        g_free(queryStr);

    if (resultOk) {
        maxKey = (unsigned long) rawData;
        return true;
    }

    return false;
}

bool DownloadHistoryDb::addHistory(unsigned long ticket, const std::string& caller, const std::string interface, const std::string& state, const std::string& downloadRecordString)
{
    if (!m_dlDb)
        return false;

    if (downloadRecordString.empty())
        return false;

    gchar* queryStr = sqlite3_mprintf("REPLACE INTO DownloadHistory VALUES (%lu, %Q, %Q, %Q, %Q)", ticket, caller.c_str(), interface.c_str(), state.c_str(), downloadRecordString.c_str());
    if (!queryStr)
        return false;

    int ret = sqlite3_exec(m_dlDb, queryStr, NULL, NULL, NULL);

    if (ret) {
        LOG_DEBUG("Failed to execute query: %s", queryStr);
        sqlite3_free(queryStr);
        return false;
    }

    sqlite3_free(queryStr);

    return true;
}

bool DownloadHistoryDb::addHistory(const DownloadHistory& history)
{
    return addHistory(history.m_ticket, history.m_owner, history.m_interface, history.m_state, history.m_downloadRecordJsonString);
}

int DownloadHistoryDb::getDownloadHistoryFull(unsigned long ticket, std::string& r_caller, std::string& r_interface, std::string& r_state, std::string& r_history)
{

    sqlite3_stmt* statement = 0;
    const char* tail = 0;
    int ret = 0;
    gchar* queryStr = 0;

    std::string result;

    if (!m_dlDb)
        return 0;

    queryStr = g_strdup_printf("SELECT * FROM DownloadHistory WHERE ticket=%lu", ticket);
    if (!queryStr) {
        ret = 0;
        goto Done;
    }

    ret = sqlite3_prepare(m_dlDb, queryStr, -1, &statement, &tail);
    if (ret) {
        LOG_WARNING_PAIRS(LOGID_DNLD_HIST_DB_STMT_PREPARE_FAIL, 1, PMLOGKS("statement", queryStr), "Failed to prepare sql statement");
        ret = 0;
        goto Done;
    }

    ret = sqlite3_step(statement);
    if (ret == SQLITE_ROW) {
        const char* res = (const char*) sqlite3_column_text(statement, 1);
        if (res)
            r_caller = res;
        res = (const char*) sqlite3_column_text(statement, 2);
        if (res)
            r_interface = res;
        res = (const char*) sqlite3_column_text(statement, 3);
        if (res)
            r_state = res;
        res = (const char*) sqlite3_column_text(statement, 4);
        if (res)
            r_history = res;
        ret = 1;
    } else
        ret = 0;

Done:

    if (statement)
        sqlite3_finalize(statement);

    if (queryStr)
        g_free(queryStr);

    return ret;
}

std::string DownloadHistoryDb::getDownloadHistoryRecord(unsigned long ticket)
{
    sqlite3_stmt* statement = 0;
    const char* tail = 0;
    int ret = 0;
    gchar* queryStr = 0;

    std::string result;

    if (!m_dlDb)
        return result;

    queryStr = g_strdup_printf("SELECT history FROM DownloadHistory WHERE ticket=%lu", ticket);
    if (!queryStr)
        goto Done;

    ret = sqlite3_prepare(m_dlDb, queryStr, -1, &statement, &tail);
    if (ret) {
        LOG_DEBUG("Failed to prepare sql statement: %s", queryStr);
        goto Done;
    }

    ret = sqlite3_step(statement);
    if (ret == SQLITE_ROW) {
        const unsigned char* res = sqlite3_column_text(statement, 0);
        if (res)
            result = (const char*) res;
    }

Done:

    if (statement)
        sqlite3_finalize(statement);

    if (queryStr)
        g_free(queryStr);

    return result;
}

int DownloadHistoryDb::getDownloadHistoryRecord(unsigned long ticket, DownloadHistory& r_historyRecord)
{
    sqlite3_stmt* statement = 0;
    const char* tail = 0;
    int ret = 0;
    int rc = 0;
    char* queryStr = 0;

    std::string result;

    if (!m_dlDb)
        return rc;

    queryStr = sqlite3_mprintf("SELECT * FROM DownloadHistory WHERE ticket = %lu", ticket);

    if (!queryStr)
        goto Done;

    ret = sqlite3_prepare(m_dlDb, queryStr, -1, &statement, &tail);
    if (ret) {
        LOG_DEBUG("Failed to prepare sql statement: %s", queryStr);
        goto Done;
    }

    ret = sqlite3_step(statement);
    while (ret == SQLITE_ROW) {
        unsigned long int ticketDb = (unsigned long int) sqlite3_column_int(statement, 0);
        const char* res[4];
        res[0] = (const char *) sqlite3_column_text(statement, 1);
        res[1] = (const char *) sqlite3_column_text(statement, 2);
        res[2] = (const char *) sqlite3_column_text(statement, 3);
        res[3] = (const char *) sqlite3_column_text(statement, 4);
        if (res[0]) {
            r_historyRecord = DownloadHistory(ticketDb, res[0], res[1], res[2], res[3]);
            ++rc;
            break;
        }
        ret = sqlite3_step(statement);
    }

Done:

    if (statement)
        sqlite3_finalize(statement);

    if (queryStr)
        sqlite3_free(queryStr);

    return rc;
}

int DownloadHistoryDb::getDownloadHistoryRecordsForOwner(const std::string& owner, std::vector<DownloadHistory>& r_historyRecords)
{
    sqlite3_stmt* statement = 0;
    const char* tail = 0;
    int ret = 0;
    int rc = 0;
    char* queryStr = 0;

    std::string result;

    if (!m_dlDb)
        return rc;

    queryStr = sqlite3_mprintf("SELECT * FROM DownloadHistory WHERE owner GLOB '%q*'", owner.c_str());

    if (!queryStr)
        goto Done;

    ret = sqlite3_prepare(m_dlDb, queryStr, -1, &statement, &tail);
    if (ret) {
        LOG_DEBUG("Failed to prepare sql statement: %s", queryStr);
        goto Done;
    }

    ret = sqlite3_step(statement);
    while (ret == SQLITE_ROW) {
        unsigned long int ticket = (unsigned long int) sqlite3_column_int(statement, 0);
        const char* res[4];
        res[0] = (const char *) sqlite3_column_text(statement, 1);
        res[1] = (const char *) sqlite3_column_text(statement, 2);
        res[2] = (const char *) sqlite3_column_text(statement, 3);
        res[3] = (const char *) sqlite3_column_text(statement, 4);
        if (res[0]) {
            r_historyRecords.push_back(DownloadHistory(ticket, res[0], res[1], res[2], res[3]));
            ++rc;
        }
        ret = sqlite3_step(statement);
    }

Done:

    if (statement)
        sqlite3_finalize(statement);

    if (queryStr)
        sqlite3_free(queryStr);

    return rc;
}

int DownloadHistoryDb::getDownloadHistoryRecordsForState(const std::string& state, std::vector<DownloadHistory>& r_historyRecords)
{
    sqlite3_stmt* statement = 0;
    const char* tail = 0;
    int ret = 0;
    int rc = 0;
    char* queryStr = 0;

    std::string result;

    if (!m_dlDb)
        return rc;

    queryStr = sqlite3_mprintf("SELECT * FROM DownloadHistory WHERE state = %Q", state.c_str());

    if (!queryStr)
        goto Done;

    ret = sqlite3_prepare(m_dlDb, queryStr, -1, &statement, &tail);
    if (ret) {
        LOG_DEBUG("Failed to prepare sql statement: %s", queryStr);
        goto Done;
    }

    ret = sqlite3_step(statement);
    while (ret == SQLITE_ROW) {
        unsigned long int ticket = (unsigned long int) sqlite3_column_int(statement, 0);
        const char* res[4];
        res[0] = (const char *) sqlite3_column_text(statement, 1);
        res[1] = (const char *) sqlite3_column_text(statement, 2);
        res[2] = (const char *) sqlite3_column_text(statement, 3);
        res[3] = (const char *) sqlite3_column_text(statement, 4);
        if (res[2]) {
            r_historyRecords.push_back(DownloadHistory(ticket, res[0], res[1], res[2], res[3]));
            ++rc;
        }
        ret = sqlite3_step(statement);
    }

Done:

    if (statement)
        sqlite3_finalize(statement);

    if (queryStr)
        sqlite3_free(queryStr);

    return rc;
}

int DownloadHistoryDb::getDownloadHistoryRecordsForInterface(const std::string& interface, std::vector<DownloadHistory>& r_historyRecords)
{
    sqlite3_stmt* statement = 0;
    const char* tail = 0;
    int ret = 0;
    int rc = 0;
    char* queryStr = 0;

    std::string result;

    if (!m_dlDb)
        return rc;

    queryStr = sqlite3_mprintf("SELECT * FROM DownloadHistory WHERE interface = %Q", interface.c_str());

    if (!queryStr)
        goto Done;

    ret = sqlite3_prepare(m_dlDb, queryStr, -1, &statement, &tail);
    if (ret) {
        LOG_DEBUG("Failed to prepare sql statement: %s", queryStr);
        goto Done;
    }

    ret = sqlite3_step(statement);
    while (ret == SQLITE_ROW) {
        unsigned long int ticket = (unsigned long int) sqlite3_column_int(statement, 0);
        const char* res[4];
        res[0] = (const char *) sqlite3_column_text(statement, 1);
        res[1] = (const char *) sqlite3_column_text(statement, 2);
        res[2] = (const char *) sqlite3_column_text(statement, 3);
        res[3] = (const char *) sqlite3_column_text(statement, 4);
        if (res[1]) {
            r_historyRecords.push_back(DownloadHistory(ticket, res[0], res[1], res[2], res[3]));
            ++rc;
        }
        ret = sqlite3_step(statement);
    }

Done:

    if (statement)
        sqlite3_finalize(statement);

    if (queryStr)
        sqlite3_free(queryStr);

    return rc;
}

int DownloadHistoryDb::getDownloadHistoryRecordsForStateAndInterface(const std::string& state, const std::string& interface, std::vector<DownloadHistory>& r_historyRecords)
{
    sqlite3_stmt* statement = 0;
    const char* tail = 0;
    int ret = 0;
    int rc = 0;
    char* queryStr = 0;

    std::string result;

    if (!m_dlDb)
        return rc;

    queryStr = sqlite3_mprintf("SELECT * FROM DownloadHistory WHERE state = %Q AND interface = %Q", state.c_str(), interface.c_str());

    if (!queryStr)
        goto Done_getDownloadHistoryRecordsForStateAndInterface;

    ret = sqlite3_prepare(m_dlDb, queryStr, -1, &statement, &tail);
    if (ret) {
        LOG_DEBUG("Failed to prepare sql statement: %s", queryStr);
        goto Done_getDownloadHistoryRecordsForStateAndInterface;
    }

    ret = sqlite3_step(statement);
    while (ret == SQLITE_ROW) {
        unsigned long int ticket = (unsigned long int) sqlite3_column_int(statement, 0);
        const char* res[4];
        res[0] = (const char *) sqlite3_column_text(statement, 1);
        res[1] = (const char *) sqlite3_column_text(statement, 2);
        res[2] = (const char *) sqlite3_column_text(statement, 3);
        res[3] = (const char *) sqlite3_column_text(statement, 4);
        if (res[1]) {
            r_historyRecords.push_back(DownloadHistory(ticket, res[0], res[1], res[2], res[3]));
            ++rc;
        }
        ret = sqlite3_step(statement);
    }

Done_getDownloadHistoryRecordsForStateAndInterface:

    if (statement)
        sqlite3_finalize(statement);

    if (queryStr)
        sqlite3_free(queryStr);

    return rc;
}

int DownloadHistoryDb::changeStateForAll(const std::string& oldState, const std::string& newState)
{
    std::vector<DownloadHistory> historyRecords;
    int rc = 0;
    if ((rc = getDownloadHistoryRecordsForState(oldState, historyRecords)) == 0)
        return 0;       //none found

    for (std::vector<DownloadHistory>::iterator it = historyRecords.begin(); it != historyRecords.end(); ++it) {
        (*it).m_state = newState;
        addHistory(*it);
        ++rc;
    }

    return rc;
}

bool DownloadHistoryDb::openDownloadHistoryDb(std::string& errmsg)
{
    if (m_dlDb) {
        errmsg = "download history db already open";
        return false;
    }

    gchar* dlDirPath = g_path_get_dirname(s_dlDbPath);
    g_mkdir_with_parents(dlDirPath, 0755);
    g_free(dlDirPath);

    int ret = sqlite3_open(s_dlDbPath, &m_dlDb);
    if (ret) {
        errmsg = "Failed to open download history db";
        return false;
    }

    if (!checkTableConsistency()) {
        errmsg = "Failed to create DownloadHistory table";
        sqlite3_close(m_dlDb);
        m_dlDb = 0;
        return false;
    }

    ret = sqlite3_exec(m_dlDb, "CREATE TABLE IF NOT EXISTS DownloadHistory "
            "(ticket INTEGER PRIMARY KEY, "
            " owner TEXT, "
            " interface TEXT, "
            " state TEXT, "
            " history TEXT);", NULL, NULL, NULL);
    if (ret) {
        errmsg = "Failed to create DownloadHistory table";
        sqlite3_close(m_dlDb);
        m_dlDb = 0;
        return false;
    }

    ret = sqlite3_exec(m_dlDb, "PRAGMA default_cache_size=1;", NULL, NULL,
    NULL);              //100 , 1Kb pages
    if (ret) {
        LOG_DEBUG("Failed to set PRAGMA default_cache_size!!");
    }

    ret = sqlite3_exec(m_dlDb, "PRAGMA temp_store=FILE;", NULL, NULL, NULL);
    if (ret) {
        LOG_DEBUG("Failed to set PRAGMA temp_store!!");
    }

    sqlite3_soft_heap_limit(512 * 1024);      //512Kb

    return true;
}

void DownloadHistoryDb::closeDownloadHistoryDb()
{
    if (!m_dlDb)
        return;

    (void) sqlite3_close(m_dlDb);
    m_dlDb = 0;
}

bool DownloadHistoryDb::checkTableConsistency()
{
    if (!m_dlDb)
        return false;

    int ret;
    std::string query;
    sqlite3_stmt* statement = 0;
    const char* tail = 0;

    //check integrity
    if (!integrityCheckDb()) {
        LOG_WARNING_PAIRS_ONLY(LOGID_DB_INTEGRITY_ERROR, 1, PMLOGKS("integrityCheckDb", "failed to check download DB integrity and couldn't recreate it"));
        return false;
    }

    query = "SELECT owner FROM DownloadHistory WHERE ticket=0";
    ret = sqlite3_prepare(m_dlDb, query.c_str(), -1, &statement, &tail);
    if (ret) {
        LOG_DEBUG("Failed to prepare sql statement: %s (%s)", query.c_str(), sqlite3_errmsg(m_dlDb));
        goto Recreate;
    }

    ret = sqlite3_step(statement);
    if (ret == SQLITE_ROW) {
        // everything OK.
        const char * ver = (const char *) sqlite3_column_text(statement, 0);
        std::string version;
        if (ver != NULL)
            version = ver;
        sqlite3_finalize(statement);
        if (version != VALID_SCHEMA_VER) {
            LOG_DEBUG("Database is the wrong schema version [%s], and should be [%s]", version.c_str(), VALID_SCHEMA_VER);
            goto Recreate;
        }
        return true;
    }

    // Database not consistent. recreate

Recreate:

    sqlite3_finalize(statement);

    (void) sqlite3_exec(m_dlDb, "DROP TABLE DownloadHistory", NULL, NULL, NULL);
    ret = sqlite3_exec(m_dlDb, "CREATE TABLE DownloadHistory "
            "(ticket INTEGER PRIMARY KEY, "
            " owner TEXT, "
            " interface TEXT, "
            " state TEXT, "
            " history TEXT);", NULL, NULL, NULL);
    if (ret) {
        LOG_WARNING_PAIRS(LOGID_DB_RECREATION_FAIL, 1, PMLOGKS("query", "sqlite3_exec"), "failed to create downloadhistory table");
        return false;
    }

    char* queryStr = 0;

    queryStr = sqlite3_mprintf("INSERT INTO DownloadHistory VALUES (0, %Q, 'init' , 'null', 'null' )", VALID_SCHEMA_VER);

    ret = sqlite3_exec(m_dlDb, queryStr, NULL, NULL, NULL);

    if (queryStr)
        sqlite3_free(queryStr);

    if (ret) {
        LOG_WARNING_PAIRS(LOGID_DB_RECREATION_FAIL, 1, PMLOGKS("query", "sqlite3_exec"), "failed to insert init values into downloadhistory table");
        return false;
    }

    return true;
}

bool DownloadHistoryDb::integrityCheckDb()
{
    if (!m_dlDb)
        return false;

    sqlite3_stmt* statement = 0;
    const char* tail = 0;
    int ret = 0;
    bool integrityOk = false;

    ret = sqlite3_prepare(m_dlDb, "PRAGMA integrity_check", -1, &statement, &tail);
    if (ret) {
        LOG_DEBUG("Failed to prepare sql statement for integrity_check");
        goto CorruptDb;
    }

    ret = sqlite3_step(statement);
    if (ret == SQLITE_ROW) {
        const unsigned char* result = sqlite3_column_text(statement, 0);
        if (result && strcasecmp((const char*) result, "ok") == 0)
            integrityOk = true;
    }

    sqlite3_finalize(statement);

    if (!integrityOk)
        goto CorruptDb;

    LOG_DEBUG("%s: Integrity check for database passed", __PRETTY_FUNCTION__);

    return true;

CorruptDb:

    LOG_DEBUG("%s: integrity check failed. recreating database", __PRETTY_FUNCTION__);

    sqlite3_close(m_dlDb);
    unlink(s_dlDbPath);

    ret = sqlite3_open_v2(s_dlDbPath, &m_dlDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (ret) {
        LOG_DEBUG("%s: Failed to re-open download history db at [%s]", __PRETTY_FUNCTION__, s_dlDbPath);
        return false;
    }

    return true;
}

int DownloadHistoryDb::clear()
{
    if (!m_dlDb)
        return DOWNLOADHISTORYDB_HISTORYSTATUS_HISTORYERROR;

    (void) sqlite3_exec(m_dlDb, "DROP TABLE DownloadHistory", NULL, NULL, NULL);

    checkTableConsistency();        //this will recreate it

    return DOWNLOADHISTORYDB_HISTORYSTATUS_OK;
}

void DownloadHistoryDb::clearByTicket(const unsigned long ticket)
{
    char* queryStr = 0;

    if (!m_dlDb)
        return;

    queryStr = sqlite3_mprintf("DELETE FROM DownloadHistory WHERE ticket = %lu", ticket);

    if (!queryStr)
        goto Done_clearByOwner;

    (void) sqlite3_exec(m_dlDb, queryStr, NULL, NULL, NULL);

Done_clearByOwner:

    if (queryStr)
        sqlite3_free(queryStr);
}

void DownloadHistoryDb::clearByOwner(const std::string& caller)
{
    char* queryStr = 0;

    if (!m_dlDb)
        return;

    queryStr = sqlite3_mprintf("DELETE FROM DownloadHistory WHERE owner = %Q", caller.c_str());

    if (!queryStr)
        goto Done_clearByOwner;

    (void) sqlite3_exec(m_dlDb, queryStr, NULL, NULL, NULL);

Done_clearByOwner:

    if (queryStr)
        sqlite3_free(queryStr);

}

int DownloadHistoryDb::clearByGlobbedOwner(const std::string& caller)
{
    char* queryStr = 0;
    int rc = 0;

    if (!m_dlDb)
        return DOWNLOADHISTORYDB_HISTORYSTATUS_HISTORYERROR;

    queryStr = sqlite3_mprintf("DELETE FROM DownloadHistory WHERE owner GLOB '%q*'", caller.c_str());

    if (!queryStr) {
        rc = DOWNLOADHISTORYDB_HISTORYSTATUS_HISTORYERROR;
        goto Done_clearByGlobbedOwner;
    }

    (void) sqlite3_exec(m_dlDb, queryStr, NULL, NULL, NULL);

    if (sqlite3_changes(m_dlDb) != 0)
        rc = DOWNLOADHISTORYDB_HISTORYSTATUS_OK;
    else
        rc = DOWNLOADHISTORYDB_HISTORYSTATUS_NOTINHISTORY;

Done_clearByGlobbedOwner:

    if (queryStr)
        sqlite3_free(queryStr);

    return rc;
}
