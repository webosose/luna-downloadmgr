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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glib.h>
#include <setting/DownloadSettings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <util/DownloadUtils.h>
#include <util/Logging.h>

#define KEY_STRING(cat,name,var) \
{\
    gchar* _vs;\
    GError* _error = 0;\
    _vs=g_key_file_get_string(keyfile,cat,name,&_error);\
    if( !_error && _vs ) { var=(const char*)_vs; g_free(_vs); }\
    else g_error_free(_error); \
}

#define KEY_MEMORY_STRING(cat,name,var) \
{\
    gchar* _vs;\
    GError* _error = 0;\
    _vs=g_key_file_get_string(keyfile,cat,name,&_error);\
    if( !_error && _vs ) { var=::MemStringToBytes((const char*)_vs); g_free(_vs); }\
    else g_error_free(_error); \
}

#define KEY_BOOLEAN(cat,name,var) \
{\
    gboolean _vb;\
    GError* _error = 0;\
    _vb=g_key_file_get_boolean(keyfile,cat,name,&_error);\
    if( !_error ) { var=_vb; }\
    else g_error_free(_error); \
}

#define KEY_INTEGER(cat,name,var) \
{\
    int _v = 0;\
    GError* _error = 0;\
    _v=g_key_file_get_integer(keyfile,cat,name,&_error);\
    if( !_error ) { var=_v; }\
    else g_error_free(_error); \
}

#define KEY_UINT64(cat,name,var) \
{ \
    uint64_t _v = 0;\
    GError* _error = 0;\
    char * tmp_str = g_key_file_get_value(keyfile,cat,name,&_error); \
    if (tmp_str) { \
        _v = g_ascii_strtoull(tmp_str, NULL, 10); \
        g_free (tmp_str); \
    } \
    if(! _error ) { var=_v; }\
    else g_error_free(_error); \
}

#define KEY_UINT32(cat,name,var) \
{ \
    uint32_t _v;\
    GError* _error = 0;\
    char * tmp_str = g_key_file_get_value(keyfile,cat,name,&_error); \
    if (tmp_str) { \
        _v = g_ascii_strtoul(tmp_str, NULL, 10); \
        g_free (tmp_str); \
    } \
    if(! _error ) { var=_v; }\
    else g_error_free(_error); \
}

static const char* kDownloadSettingsFile = "/etc/palm/downloadManager.conf";
static const char* kDownloadSettingsFilePlatform = "/etc/palm/downloadManager-platform.conf";

static bool localSpaceOnFs(const std::string& path, uint64_t& spaceFreeKB, uint64_t& spaceTotalKB, bool useFake = false, uint64_t fakeFreeSize = 0)
{
    struct statvfs64 fs_stats;
    memset(&fs_stats, 0, sizeof(fs_stats));

    if (::statvfs64(path.c_str(), &fs_stats) != 0) {
        //failed to execute statvfs...treat this as if there was no free space
        LOG_DEBUG("%s: Failed to execute statvfs on %s", __FUNCTION__, path.c_str());
        return false;
    }

    if (useFake) {
        fs_stats.f_bfree = fakeFreeSize / fs_stats.f_frsize;
        LOG_DEBUG("%s: USING FAKE STATFS VALUES! (free bytes specified as: %llu, free blocks simulated to: %llu )", __FUNCTION__, fakeFreeSize, fs_stats.f_bfree);
    }

    spaceFreeKB = (((uint64_t) (fs_stats.f_bfree) * (uint64_t) (fs_stats.f_frsize)) >> 10);
    spaceTotalKB = (((uint64_t) (fs_stats.f_blocks) * (uint64_t) (fs_stats.f_frsize)) >> 10);
    LOG_DEBUG("%s: [%s] KB free = %llu, KB total = %llu", __FUNCTION__, path.c_str(), spaceFreeKB, spaceTotalKB);
    return true;
}

static DownloadSettings* s_settings = 0;

DownloadSettings* DownloadSettings::Settings()
{
    if (s_settings)
        return s_settings;

    s_settings = new DownloadSettings();
    s_settings->load();

    return s_settings;
}

//static
bool DownloadSettings::validateDownloadPath(const std::string& path)
{

    //do not allow /../ in the path. This will avoid complicated parsing to check for valid paths
    if (path.find("..") != std::string::npos)
        return false;

    //the prefix /var or /media has to be anchored at 0
    if ((path.find("/var") == 0) || (path.find("/media") == 0))
        return true;

    return false;
}

//TODO: Time to start getting rid of "luna" in visible pathnames
DownloadSettings::DownloadSettings()
    : m_downloadPathMedia("/media/internal/downloads"),
      m_schemaPath(WEBOS_INSTALL_WEBOS_SYSCONFDIR "/schemas/luna-downloadmgr/"),
      m_wiredInterfaceName("eth0"),
      m_wifiInterfaceName("wlan0"),
      m_wanInterfaceName("ppp0"),
      m_btpanInterfaceName("bsl0"),
      m_autoResume(false),
      m_resumeAggression(true),
      m_appCompatibilityMode(true),
      m_preemptiveFreeSpaceCheck(true),
      m_dbg_fake1xForWan(false),
      m_dbg_forceNovacomOnAtStartup(false),
      m_localPackageInstallNoSafety(false),
      m_maxDownloadManagerQueueLength(128),
      m_maxDownloadManagerConcurrent(2),
      m_maxDownloadManagerRecvSpeed(64 * 1024),
      m_freespaceLowmarkFullPercent(FREESPACE_LOWMARK_FULL_PCT),
      m_freespaceMedmarkFullPercent(FREESPACE_MEDMARK_FULL_PCT),
      m_freespaceHighmarkFullPercent(FREESPACE_HIGHMARK_FULL_PCT),
      m_freespaceCriticalmarkFullPercent(FREESPACE_CRITICALMARK_FULL_PCT),
      m_freespaceStopmarkRemainingKBytes(0),
      m_dbg_useStatfsFake(false),
      m_dbg_statfsFakeFreeSizeBytes(0)
{
}

DownloadSettings::~DownloadSettings()
{
}

void DownloadSettings::load()
{
    GKeyFile* keyfile;
    GKeyFileFlags flags;
    GError* error = 0;

    keyfile = g_key_file_new();
    if (!keyfile)
        return;
    flags = GKeyFileFlags(G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS);

    if (!g_key_file_load_from_file(keyfile, kDownloadSettingsFile, flags, &error)) {
        g_key_file_free(keyfile);
        if (error)
            g_error_free(error);
        return;
    }

    // Fill in with the macros above.
    KEY_STRING("General", "DownloadPathMedia", m_downloadPathMedia);
    //validate path, reset to default if necessary
    if (!validateDownloadPath(m_downloadPathMedia)) {
        m_downloadPathMedia = "/media/internal/downloads";
    }

    KEY_STRING("General", "WIREDInterfaceName", m_wiredInterfaceName);
    KEY_STRING("General", "WANInterfaceName", m_wanInterfaceName);
    KEY_STRING("General", "WIFIInterfaceName", m_wifiInterfaceName);
    KEY_STRING("General", "BTPANInterfaceName", m_btpanInterfaceName);

    KEY_BOOLEAN("General", "AutoResume", m_autoResume);
    KEY_BOOLEAN("General", "BeAggressive__Bee_Eee_AGGRESSIVE", m_resumeAggression);
    KEY_BOOLEAN("General", "AppCompatibilityMode", m_appCompatibilityMode);
    KEY_BOOLEAN("General", "PreemptiveFsCheck", m_preemptiveFreeSpaceCheck);

    KEY_INTEGER("DownloadManager", "MaxQueueLength", m_maxDownloadManagerQueueLength);
    KEY_INTEGER("DownloadManager", "MaxConcurrent", m_maxDownloadManagerConcurrent);
    KEY_INTEGER("DownloadManager", "MaxRecvSpeed", m_maxDownloadManagerRecvSpeed);

    KEY_INTEGER("Filesystem", "SpaceFullLowmarkPercent", m_freespaceLowmarkFullPercent);
    KEY_INTEGER("Filesystem", "SpaceFullMedmarkPercent", m_freespaceMedmarkFullPercent);
    KEY_INTEGER("Filesystem", "SpaceFullHighmarkPercent", m_freespaceHighmarkFullPercent);
    KEY_INTEGER("Filesystem", "SpaceFullCriticalmarkPercent", m_freespaceCriticalmarkFullPercent);

    KEY_UINT64("Filesystem", "SpaceRemainStopmarkKB", m_freespaceStopmarkRemainingKBytes);

    if ((m_freespaceLowmarkFullPercent <= 0) || (m_freespaceLowmarkFullPercent >= 100)) {
        m_freespaceLowmarkFullPercent = FREESPACE_LOWMARK_FULL_PCT;
    }
    if ((m_freespaceMedmarkFullPercent <= m_freespaceLowmarkFullPercent) || (m_freespaceMedmarkFullPercent >= 100)) {
        m_freespaceMedmarkFullPercent = FREESPACE_MEDMARK_FULL_PCT;
    }
    if ((m_freespaceHighmarkFullPercent <= m_freespaceMedmarkFullPercent) || (m_freespaceHighmarkFullPercent >= 100)) {
        m_freespaceHighmarkFullPercent = FREESPACE_HIGHMARK_FULL_PCT;
    }
    if ((m_freespaceCriticalmarkFullPercent <= m_freespaceHighmarkFullPercent) || (m_freespaceCriticalmarkFullPercent >= 100)) {
        m_freespaceCriticalmarkFullPercent = FREESPACE_CRITICALMARK_FULL_PCT;
    }

    KEY_BOOLEAN("Debug", "UseFakeStatfsValues", m_dbg_useStatfsFake);
    KEY_UINT64("Debug", "FakeStatfsFreeSizeInBytes", m_dbg_statfsFakeFreeSizeBytes);

    uint64_t freeSpaceFs = 0;
    uint64_t totalSpaceFs = 0;

    if (localSpaceOnFs(m_downloadPathMedia, freeSpaceFs, totalSpaceFs, m_dbg_useStatfsFake, m_dbg_statfsFakeFreeSizeBytes)) {
        //make sure the stop mark is sane...it should be: totalSpaceFs - freespaceStopmarkRemainingKBytes > totalSpaceFs * freespaceCriticalmarkFullPercent/100
        uint64_t remainKBat99Pct = (uint64_t) ((double) totalSpaceFs * ((double) m_freespaceCriticalmarkFullPercent / 100.0)); //oh good lord, let me count the ways that this leaves room for overflow =(
        LOG_DEBUG("Info: space remaining at 99%% for the current filesys is %llu KB", remainKBat99Pct);

        if (remainKBat99Pct < m_freespaceStopmarkRemainingKBytes) {
            m_freespaceStopmarkRemainingKBytes = remainKBat99Pct;
            LOG_DEBUG("(the SpaceRemainStopmarkKB specification was incorrectly set; resetting to the 99%% mark, which is %llu KB)", m_freespaceStopmarkRemainingKBytes);
        }
    }

    LOG_DEBUG("Info: Using the following filesystem alert limits: Low = %u%% , Med = %u%% , High = %u%% , Critical = %u%% , stop mark @ %llu KB remaining", m_freespaceLowmarkFullPercent,
            m_freespaceMedmarkFullPercent, m_freespaceHighmarkFullPercent, m_freespaceCriticalmarkFullPercent, m_freespaceStopmarkRemainingKBytes);

    KEY_BOOLEAN("Debug", "Fake1x", m_dbg_fake1xForWan);
    KEY_BOOLEAN("Debug", "ForceNovacomOnAtStartup", m_dbg_forceNovacomOnAtStartup);

    //BARLEYWINE HACKATHON
    KEY_BOOLEAN("Install", "InstallLocalNoSafety", m_localPackageInstallNoSafety);

    g_key_file_free(keyfile);
    keyfile = g_key_file_new();
    if (!keyfile) {
        return;
    }

    if (!g_key_file_load_from_file(keyfile, kDownloadSettingsFilePlatform, flags, &error)) {
        g_key_file_free(keyfile);
        if (error)
            g_error_free(error);
        return;
    }

    // Fill in with the macros above.
    KEY_STRING("General", "DownloadPathMedia", m_downloadPathMedia);
    //validate path, reset to default if necessary
    if (!validateDownloadPath(m_downloadPathMedia)) {
        m_downloadPathMedia = "/media/internal/downloads";
    }

    KEY_STRING("General", "WIREDInterfaceName", m_wiredInterfaceName);
    KEY_STRING("General", "WANInterfaceName", m_wanInterfaceName);
    KEY_STRING("General", "WIFIInterfaceName", m_wifiInterfaceName);
    KEY_STRING("General", "BTPANInterfaceName", m_btpanInterfaceName);

    KEY_BOOLEAN("General", "AutoResume", m_autoResume);
    KEY_BOOLEAN("General", "BeAggressive__Bee_Eee_AGGRESSIVE", m_resumeAggression);
    KEY_BOOLEAN("General", "AppCompatibilityMode", m_appCompatibilityMode);

    KEY_INTEGER("DownloadManager", "MaxQueueLength", m_maxDownloadManagerQueueLength);
    KEY_INTEGER("DownloadManager", "MaxConcurrent", m_maxDownloadManagerConcurrent);
    KEY_INTEGER("DownloadManager", "MaxRecvSpeed", m_maxDownloadManagerRecvSpeed);

    g_key_file_free(keyfile);

    g_mkdir_with_parents(m_downloadPathMedia.c_str(), 0755);
}
