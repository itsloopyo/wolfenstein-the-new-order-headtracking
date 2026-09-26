// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <functional>
#include <string>

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/head_tracking_config.h"

// CameraUnlock.ini, next to WolfNewOrder_x64.exe, in cameraunlock-core's
// canonical format. The owner imports HeadTracking.ini, the file every earlier
// build read from the same folder, once while CameraUnlock.ini is absent, and
// never writes it.
namespace wolf_ht {

// No sensitivity, deadzone, response curve or axis inversion lives here, for
// rotation or for position: the tracker owns pose shaping, so the pose is
// consumed at 1:1. The protocol-to-engine sign conversion and the metres to
// id Tech units scale are fixed parts of the boundary in tracker_feed.cpp.
struct Config : cameraunlock::HeadTrackingConfig {};

}  // namespace wolf_ht

namespace wolf_ht::config {

constexpr const char* kDisplayName = "Wolfenstein: The New Order";
constexpr const wchar_t* kConfigFileName = L"CameraUnlock.ini";
constexpr const wchar_t* kLegacyFileName = L"HeadTracking.ini";

cameraunlock::config::ConfigTable<Config> MakeTable();
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for CameraUnlock.ini in `exe_dir`, with HeadTracking.ini
// beside it as the legacy file. The mod passes the player's own Defaults.ini,
// a test one at a scratch path.
cameraunlock::config::ConfigOwnerOptions<Config> MakeOwnerOptions(
    const std::wstring& exe_dir, cameraunlock::config::DefaultsFile defaults);

// Builds the process's owner for CameraUnlock.ini in `exe_dir`, loads it, and
// writes every line the load returned to the log. Bootstrap thread, once, after
// the log is open.
Config Load(const std::wstring& exe_dir);

// Apply-then-save for a toggle: the caller has applied the new value to the
// running game; this writes it through the owner and logs what happened. A
// failed save leaves the session running on the new value. The hotkey thread
// only, never a per-frame path.
void Save(const std::function<void(Config&)>& change);

}  // namespace wolf_ht::config
