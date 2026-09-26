// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include "cameraunlock/input/hotkey_poller.h"

namespace wolf_ht {

struct Config;
class HeadTrackingMod;

// Every action fires on any key in its list from CameraUnlock.ini. The default
// lists are a nav-cluster key and a Ctrl+Shift+<letter> chord from the Y/G/H
// cluster, for keyboards with no nav cluster.
class Hotkeys {
public:
    void Start(HeadTrackingMod& mod, const Config& config);

private:
    cameraunlock::input::HotkeyPoller m_poller;
};

}  // namespace wolf_ht
