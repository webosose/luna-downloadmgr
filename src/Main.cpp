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

#include <core/DownloadManager.h>
#include <cstdlib>
#include <sys/time.h>
#include <sys/resource.h>
#include <glib.h>
#include <malloc.h>
#include <setting/DownloadSettings.h>
#include <util/Logging.h>

//#define PRINT_MALLOC_STATS

#ifdef __cplusplus
extern "C" {
#endif

GMainLoop* gMainLoop = NULL;

#ifdef __cplusplus
}
#endif

static void handle_sigterm(int signal)
{
    LOG_DEBUG("SIGTERM hit! exiting main loop.");
    g_main_loop_quit(gMainLoop);
}

int main(int argc, char** argv)
{
    LOG_DEBUG("LunaDownloadMgr STARTING");

    signal(SIGTERM, handle_sigterm);

    gMainLoop = g_main_loop_new(NULL, FALSE);
    LOG_DEBUG("%s:%d gMainLoop = %p", __FILE__, __LINE__, gMainLoop);

    //g_thread_init is deprecated since glib 2.0
    //g_thread_init(NULL);

    // Load DownloadSettings (first!)
    DownloadSettings* settings = DownloadSettings::Settings();

    LOG_DEBUG("%s: [INSTALLER] [DOWNLOADER] : Debug setting 'Fake1x' is %s", __FUNCTION__, (settings->m_dbg_fake1xForWan ? "ON!" : "OFF"));
    LOG_DEBUG("%s: [INSTALLER] [DOWNLOADER] : Debug setting 'AutoResume' is %s", __FUNCTION__, (settings->m_autoResume ? "ON!" : "OFF"));

    // Initialize the Download Manager
    DownloadManager::instance().init();

    g_main_loop_run(gMainLoop);
    g_main_loop_unref(gMainLoop);

    return 0;
}
