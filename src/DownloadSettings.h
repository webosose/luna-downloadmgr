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

#ifndef __Settings_h__
#define __Settings_h__

#include <string>
#include <vector>
#include <set>
#include <glib.h>
#include <stdint.h>
#include "Singleton.hpp"
#include "webospaths.h"

// low mark is hard-defaulted to 80%
#define FREESPACE_LOWMARK_FULL_PCT      90
// medium mark at 90%
#define FREESPACE_MEDMARK_FULL_PCT      95
// high (warning) mark is hard defaulted to 95%
#define FREESPACE_HIGHMARK_FULL_PCT     98
//the critical mark is 99%
#define FREESPACE_CRITICALMARK_FULL_PCT 99

class DownloadSettings : public Singleton<DownloadSettings>
{
public:

    std::string     downloadPathMedia;              //default >> /media/internal/downloads
    std::string     schemaPath;                     //default >> @WEBOS_INSTALL_WEBOS_SYSCONFDIR@/schemas/luna-downloadmgr/
    std::string     wiredInterfaceName;             //eth0
    std::string     wifiInterfaceName;              //wlan0
    std::string     wanInterfaceName;               //ppp0
    std::string     btpanInterfaceName;             //bsl0
    bool            autoResume;                     //false
    bool            resumeAggression;               //true
    bool            appCompatibilityMode;           //true
    bool            preemptiveFreeSpaceCheck;       //true
    bool            dbg_fake1xForWan;               //false.   set to true to make it look like any connected WAN status is a 1x connection (for testing low coverage/1x scenarios)
    bool            dbg_forceNovacomOnAtStartup;    //false. set to true to make the downloadmanager service flip novacom access to enabled (aka dev mode switch) to debug "full erase" scenarios where
                                                    // the erase disables it
    bool            localPackageInstallNoSafety;    //false. set to true to not check for status of a previous install of a package of the same name, when performing a local install of that package
                                                    //  (in essence a hard override of the system to force an install to complete. Use with extreme caution. BARLEYWINE HACKATHON )

    unsigned int    maxDownloadManagerQueueLength;
    int             maxDownloadManagerConcurrent;
    unsigned int    maxDownloadManagerRecvSpeed;

    uint32_t        freespaceLowmarkFullPercent;
    uint32_t        freespaceMedmarkFullPercent;
    uint32_t        freespaceHighmarkFullPercent;
    uint32_t        freespaceCriticalmarkFullPercent;
    uint64_t        freespaceStopmarkRemainingKBytes;

    bool            dbg_useStatfsFake;
    uint64_t        dbg_statfsFakeFreeSizeBytes;

private:
    void load();
    DownloadSettings();
    ~DownloadSettings();

    friend class Singleton<DownloadSettings>;

        static bool validateDownloadPath(const std::string& path);

};

#endif // Settings

