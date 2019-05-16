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

#include "DownloadSettings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>

#include "DownloadUtils.h"
#include "Logging.h"

static const char* kDownloadSettingsFile = "/etc/palm/downloadManager.conf";
static const char* kDownloadSettingsFilePlatform = "/etc/palm/downloadManager-platform.conf";

#if 0

#define SETTINGS_TRACE(...) \
do { \
    fprintf(stdout, "DownloadSettings:: " ); \
    fprintf(stdout, __VA_ARGS__); \
} while (0)

#else

#define SETTINGS_TRACE(...) (void)0

#endif

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

unsigned long MemStringToBytes(const char* ptr);

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
    : downloadPathMedia("/media/internal/downloads"),
      schemaPath(WEBOS_INSTALL_WEBOS_SYSCONFDIR "/schemas/luna-downloadmgr/"),
      wiredInterfaceName("eth0"),
      wifiInterfaceName("wlan0"),
      wanInterfaceName("ppp0"),
      btpanInterfaceName("bsl0"),
      autoResume(false),
      resumeAggression(true),
      appCompatibilityMode(true),
      preemptiveFreeSpaceCheck(true),
      dbg_fake1xForWan(false),
      dbg_forceNovacomOnAtStartup(false),
      localPackageInstallNoSafety(false),
      maxDownloadManagerQueueLength(128),
      maxDownloadManagerConcurrent(2),
      maxDownloadManagerRecvSpeed(64 * 1024),
      freespaceLowmarkFullPercent(FREESPACE_LOWMARK_FULL_PCT),
      freespaceMedmarkFullPercent(FREESPACE_MEDMARK_FULL_PCT),
      freespaceHighmarkFullPercent(FREESPACE_HIGHMARK_FULL_PCT),
      freespaceCriticalmarkFullPercent(FREESPACE_CRITICALMARK_FULL_PCT),
      freespaceStopmarkRemainingKBytes(0),
      dbg_useStatfsFake(false),
      dbg_statfsFakeFreeSizeBytes(0)
{
}

DownloadSettings::~DownloadSettings()
{
}

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
    KEY_STRING("General", "DownloadPathMedia", downloadPathMedia);
    //validate path, reset to default if necessary
    if (!validateDownloadPath(downloadPathMedia)) {
        downloadPathMedia = "/media/internal/downloads";
    }

    KEY_STRING("General", "WIREDInterfaceName", wiredInterfaceName);
    KEY_STRING("General", "WANInterfaceName", wanInterfaceName);
    KEY_STRING("General", "WIFIInterfaceName", wifiInterfaceName);
    KEY_STRING("General", "BTPANInterfaceName", btpanInterfaceName);

    KEY_BOOLEAN("General", "AutoResume", autoResume);
    KEY_BOOLEAN("General", "BeAggressive__Bee_Eee_AGGRESSIVE", resumeAggression);
    KEY_BOOLEAN("General", "AppCompatibilityMode", appCompatibilityMode);
    KEY_BOOLEAN("General", "PreemptiveFsCheck", preemptiveFreeSpaceCheck);

    KEY_INTEGER("DownloadManager", "MaxQueueLength", maxDownloadManagerQueueLength);
    KEY_INTEGER("DownloadManager", "MaxConcurrent", maxDownloadManagerConcurrent);
    KEY_INTEGER("DownloadManager", "MaxRecvSpeed", maxDownloadManagerRecvSpeed);

    KEY_INTEGER("Filesystem", "SpaceFullLowmarkPercent", freespaceLowmarkFullPercent);
    KEY_INTEGER("Filesystem", "SpaceFullMedmarkPercent", freespaceMedmarkFullPercent);
    KEY_INTEGER("Filesystem", "SpaceFullHighmarkPercent", freespaceHighmarkFullPercent);
    KEY_INTEGER("Filesystem", "SpaceFullCriticalmarkPercent", freespaceCriticalmarkFullPercent);

    KEY_UINT64("Filesystem", "SpaceRemainStopmarkKB", freespaceStopmarkRemainingKBytes);

    if ((freespaceLowmarkFullPercent <= 0) || (freespaceLowmarkFullPercent >= 100)) {
        freespaceLowmarkFullPercent = FREESPACE_LOWMARK_FULL_PCT;
    }
    if ((freespaceMedmarkFullPercent <= freespaceLowmarkFullPercent) || (freespaceMedmarkFullPercent >= 100)) {
        freespaceMedmarkFullPercent = FREESPACE_MEDMARK_FULL_PCT;
    }
    if ((freespaceHighmarkFullPercent <= freespaceMedmarkFullPercent) || (freespaceHighmarkFullPercent >= 100)) {
        freespaceHighmarkFullPercent = FREESPACE_HIGHMARK_FULL_PCT;
    }
    if ((freespaceCriticalmarkFullPercent <= freespaceHighmarkFullPercent) || (freespaceCriticalmarkFullPercent >= 100)) {
        freespaceCriticalmarkFullPercent = FREESPACE_CRITICALMARK_FULL_PCT;
    }

    KEY_BOOLEAN("Debug", "UseFakeStatfsValues", dbg_useStatfsFake);
    KEY_UINT64("Debug", "FakeStatfsFreeSizeInBytes", dbg_statfsFakeFreeSizeBytes);

    uint64_t freeSpaceFs = 0;
    uint64_t totalSpaceFs = 0;

    if (localSpaceOnFs(downloadPathMedia, freeSpaceFs, totalSpaceFs, dbg_useStatfsFake, dbg_statfsFakeFreeSizeBytes)) {
        //make sure the stop mark is sane...it should be: totalSpaceFs - freespaceStopmarkRemainingKBytes > totalSpaceFs * freespaceCriticalmarkFullPercent/100
        uint64_t remainKBat99Pct = (uint64_t) ((double) totalSpaceFs * ((double) freespaceCriticalmarkFullPercent / 100.0)); //oh good lord, let me count the ways that this leaves room for overflow =(
        LOG_DEBUG("Info: space remaining at 99%% for the current filesys is %llu KB", remainKBat99Pct);

        if (remainKBat99Pct < freespaceStopmarkRemainingKBytes) {
            freespaceStopmarkRemainingKBytes = remainKBat99Pct;
            LOG_DEBUG("(the SpaceRemainStopmarkKB specification was incorrectly set; resetting to the 99%% mark, which is %llu KB)", freespaceStopmarkRemainingKBytes);
        }
    }

    LOG_DEBUG("Info: Using the following filesystem alert limits: Low = %u%% , Med = %u%% , High = %u%% , Critical = %u%% , stop mark @ %llu KB remaining", freespaceLowmarkFullPercent,
            freespaceMedmarkFullPercent, freespaceHighmarkFullPercent, freespaceCriticalmarkFullPercent, freespaceStopmarkRemainingKBytes);

    KEY_BOOLEAN("Debug", "Fake1x", dbg_fake1xForWan);
    KEY_BOOLEAN("Debug", "ForceNovacomOnAtStartup", dbg_forceNovacomOnAtStartup);

    //BARLEYWINE HACKATHON
    KEY_BOOLEAN("Install", "InstallLocalNoSafety", localPackageInstallNoSafety);

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
    KEY_STRING("General", "DownloadPathMedia", downloadPathMedia);
    //validate path, reset to default if necessary
    if (!validateDownloadPath(downloadPathMedia)) {
        downloadPathMedia = "/media/internal/downloads";
    }

    KEY_STRING("General", "WIREDInterfaceName", wiredInterfaceName);
    KEY_STRING("General", "WANInterfaceName", wanInterfaceName);
    KEY_STRING("General", "WIFIInterfaceName", wifiInterfaceName);
    KEY_STRING("General", "BTPANInterfaceName", btpanInterfaceName);

    KEY_BOOLEAN("General", "AutoResume", autoResume);
    KEY_BOOLEAN("General", "BeAggressive__Bee_Eee_AGGRESSIVE", resumeAggression);
    KEY_BOOLEAN("General", "AppCompatibilityMode", appCompatibilityMode);

    KEY_INTEGER("DownloadManager", "MaxQueueLength", maxDownloadManagerQueueLength);
    KEY_INTEGER("DownloadManager", "MaxConcurrent", maxDownloadManagerConcurrent);
    KEY_INTEGER("DownloadManager", "MaxRecvSpeed", maxDownloadManagerRecvSpeed);

    g_key_file_free(keyfile);

    g_mkdir_with_parents(downloadPathMedia.c_str(), 0755);
}

// Expands "1MB" --> 1048576, "2k" --> 2048, etc.
unsigned long MemStringToBytes(const char* ptr)
{
    char number[32];
    unsigned long r = 0;
    const char* s = ptr;

    while (*ptr && !isalnum(*ptr)) // skip whitespace
        ptr++;
    s = ptr;

    while (isdigit(*ptr))
        ptr++;

    strncpy(number, s, (size_t) (ptr - s));
    number[ptr - s] = 0;

    r = (unsigned long) atol(number);
    switch (*ptr) {
    case 'M':
        r *= 1024 * 1024;
        break;
    case 'k':
    case 'K':
        r *= 1024;
        break;
    }

    return r;
}
