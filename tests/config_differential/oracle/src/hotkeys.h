// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include "cameraunlock/input/hotkey_poller.h"

namespace wolf_ht {

struct Config;
class HeadTrackingMod;

// Every action gets a nav-cluster key and a Ctrl+Shift+<letter> chord from the
// Y/G/H cluster, and both fire it. The chord exists for keyboards with no nav
// cluster; the letters and their order are the fleet-wide convention, so the
// same action sits on the same chord in every mod.
class Hotkeys {
public:
    void Start(HeadTrackingMod& mod, const Config& config);

private:
    cameraunlock::input::HotkeyPoller m_poller;
};

}  // namespace wolf_ht
