// Copyright (c) 2015-2019 LG Electronics, Inc.
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
#include "DownloadManager.h"
#include "Watchdog.h"

static inline gboolean is_idle(gpointer ctx)
{
    DownloadManager *m = static_cast<DownloadManager *>(ctx);

    unsigned int n_active_tasks = m->howManyTasksActive();
    int n_intr_tasks = m->howManyTasksInterrupted();

    WATCHDOG_LOG_DEBUG("Active tasks: %u; Interrupted tasks: %d;", n_active_tasks, n_intr_tasks);

    /* DownloadManager can be shutted down if:
     *
     * 1) There are no subscribed clients.
     * 2) There are no active tasks.
     * 3) There are no paused tasks.
     *
     */

    return n_active_tasks == 0 && n_intr_tasks == 0;
}

static gboolean exit_on_idle(gpointer ctx)
{
    extern GMainLoop* gMainLoop;
    if (is_idle(ctx)) {
        WATCHDOG_LOG_WARNING("There are no tasks and no subscribers, exiting...");
        g_main_loop_quit(gMainLoop);
    }

    return FALSE;
}

gboolean watchdog_handler(gpointer ctx)
{
    if (is_idle(ctx))
        g_timeout_add_seconds(WATCHDOG_TIMEOUT, exit_on_idle, ctx);

    return TRUE;
}
