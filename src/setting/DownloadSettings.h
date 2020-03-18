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

#ifndef __Settings_h__
#define __Settings_h__

#include <string>
#include <vector>
#include <set>
#include <glib.h>
#include <stdint.h>
#include <util/Singleton.hpp>
#include "webospaths.h"

// low mark is hard-defaulted to 80%
#define FREESPACE_LOWMARK_FULL_PCT      90
// medium mark at 90%
#define FREESPACE_MEDMARK_FULL_PCT      95
// high (warning) mark is hard defaulted to 95%
#define FREESPACE_HIGHMARK_FULL_PCT     98
//the critical mark is 99%
#define FREESPACE_CRITICALMARK_FULL_PCT 99

class DownloadSettings: public Singleton<DownloadSettings> {
public:
    std::string m_downloadPathMedia;              //default >> /media/internal/downloads
    std::string m_schemaPath;                     //default >> @WEBOS_INSTALL_WEBOS_SYSCONFDIR@/schemas/luna-downloadmgr/
    std::string m_wiredInterfaceName;             //eth0
    std::string m_wifiInterfaceName;              //wlan0
    std::string m_wanInterfaceName;               //ppp0
    std::string m_btpanInterfaceName;             //bsl0
    bool m_autoResume;                     //false
    bool m_resumeAggression;               //true
    bool m_appCompatibilityMode;           //true
    bool m_preemptiveFreeSpaceCheck;       //true
    bool m_dbg_fake1xForWan;               //false.   set to true to make it look like any connected WAN status is a 1x connection (for testing low coverage/1x scenarios)
    bool m_dbg_forceNovacomOnAtStartup;    //false. set to true to make the downloadmanager service flip novacom access to enabled (aka dev mode switch) to debug "full erase" scenarios where
                                         // the erase disables it
    bool m_localPackageInstallNoSafety;    //false. set to true to not check for status of a previous install of a package of the same name, when performing a local install of that package
                                         //  (in essence a hard override of the system to force an install to complete. Use with extreme caution. BARLEYWINE HACKATHON )

    unsigned int m_maxDownloadManagerQueueLength;
    int m_maxDownloadManagerConcurrent;
    unsigned int m_maxDownloadManagerRecvSpeed;

    uint32_t m_freespaceLowmarkFullPercent;
    uint32_t m_freespaceMedmarkFullPercent;
    uint32_t m_freespaceHighmarkFullPercent;
    uint32_t m_freespaceCriticalmarkFullPercent;
    uint64_t m_freespaceStopmarkRemainingKBytes;

    bool m_dbg_useStatfsFake;
    uint64_t m_dbg_statfsFakeFreeSizeBytes;

    static DownloadSettings* Settings();

private:
    void load();
    DownloadSettings();
    ~DownloadSettings();

    friend class Singleton<DownloadSettings> ;

    static bool validateDownloadPath(const std::string& path);

};

#endif // Settings

