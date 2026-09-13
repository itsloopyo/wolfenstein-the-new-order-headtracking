// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "logging.h"

#include <windows.h>

#include <string>

#include "cameraunlock/os/module_paths.h"

namespace wolf_ht {

void OpenLogFile() {
    const std::wstring dir = cameraunlock::os::HostExeDirectory();
    // Core returns empty rather than falling back to the current directory,
    // precisely so the log never lands somewhere the player will not find it.
    // Honouring that means refusing too: a bare "HeadTracking.log" resolves
    // against whatever working directory the launcher set, and the player is
    // told throughout the README to look next to WolfNewOrder_x64.exe. No log at
    // all is a better report than a log in a place nobody names.
    if (dir.empty()) {
        // Otherwise this is a mod that loaded and left nothing anywhere at all:
        // no log, no line, nothing to triage. OutputDebugStringA needs no file.
        OutputDebugStringA("[WolfensteinTheNewOrderHeadTracking] could not resolve the game "
                           "directory; no log will be written\n");
        return;
    }
    cameraunlock::logging::Open(dir + L"\\HeadTracking.log");
}

}  // namespace wolf_ht
