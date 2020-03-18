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

#ifndef __Watchdog_h__
#define __Watchdog_h__

#include <glib.h>
#include "DownloadSettings.h"
#include "Logging.h"

// Activity check interval in seconds
#define WATCHDOG_TIMEOUT (60)

gboolean watchdog_handler(gpointer ctx);

#define WATCHDOG_LOG_DEBUG(format, args...) PmLogDebug(GetPmLogContext(), format, ##args)
#define WATCHDOG_LOG_WARNING(format, args...) PmLogWarning(GetPmLogContext(), "WATCHDOG", 0, format, ##args)

#endif // __Watchdog_h__

