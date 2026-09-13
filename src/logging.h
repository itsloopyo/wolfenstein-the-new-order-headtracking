// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include "cameraunlock/logging/file_log.h"

namespace wolf_ht {

// Opens HeadTracking.log beside WolfNewOrder_x64.exe. The core truncates on open
// and keeps the previous launch as HeadTracking.prev.log, so a session never
// appends to an older one.
void OpenLogFile();

// The process-wide log lives in cameraunlock-core. Alias it so call sites in
// the files adapted from sibling mods read Log::Line(...) unqualified.
namespace Log = ::cameraunlock::logging;

}  // namespace wolf_ht
