// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

// The HeadTracking.ini reader of the last build that read the file in its
// pre-canonical layout, frozen so a player updating from any older build is
// converted exactly as that build read the file. Nothing in this folder is ever
// edited. Three things differ from the reader it was taken from: it fills this
// frozen copy of that build's Config and defaults rather than the runtime type,
// it is handed the file's path rather than the folder holding it, and it never
// writes the file (a missing file reads as the defaults, which is what the old
// reader read from the default file it created there). NormalizeUdpPort, which
// the old reader took from a part of core that is not frozen, is copied beside
// it, and so is the mod's config_sanitize.h.

#include <cstdint>
#include <string>
#include <vector>

#include "cameraunlock/config/legacy_import.h"

namespace wolf_ht::legacy {

// Default hotkey bindings, as Windows virtual key codes.
inline constexpr int kDefaultToggleKey = 0x23;           // End
inline constexpr int kDefaultCycleModeKey = 0x21;        // Page Up
inline constexpr int kDefaultYawModeKey = 0x22;          // Page Down
inline constexpr int kDefaultChordToggleKey = 0x59;      // Y, as Ctrl+Shift+Y
inline constexpr int kDefaultChordCycleModeKey = 0x47;   // G, as Ctrl+Shift+G
inline constexpr int kDefaultChordYawModeKey = 0x48;     // H, as Ctrl+Shift+H

inline constexpr float kDefaultLocalSmoothing = 0.0f;
inline constexpr float kDefaultRemoteSmoothing = 0.15f;

struct Config {
    std::uint16_t udp_port = 4242;
    bool enable_on_startup = true;

    // Every action has a nav-cluster key and a Ctrl+Shift+<key> chord, and both
    // fire it.
    int toggle_key = kDefaultToggleKey;
    int cycle_mode_key = kDefaultCycleModeKey;
    int yaw_mode_key = kDefaultYawModeKey;
    int chord_toggle_key = kDefaultChordToggleKey;
    int chord_cycle_mode_key = kDefaultChordCycleModeKey;
    int chord_yaw_mode_key = kDefaultChordYawModeKey;

    bool world_space_yaw = true;

    float local_smoothing = kDefaultLocalSmoothing;
    float remote_smoothing = kDefaultRemoteSmoothing;

    // The startup tracking mode: true starts in rotation and position, false in
    // rotation only. The mode hotkey still reaches every mode either way.
    bool position_enabled = true;
    // Metres. LimitY bounded the downward lean as well as the upward one.
    float limit_x = 0.30f;
    float limit_y = 0.20f;
    float limit_z = 0.40f;
    float limit_z_back = 0.10f;
};

enum class ReadStatus {
    Read,
    // No file at the path. The Config holds what it held, the defaults for a
    // default-constructed one.
    Absent,
};

// Reads the HeadTracking.ini at `ini_path` over `out`. Keys that are absent, or
// whose value the boundary checks refuse, leave the member of `out` as it was,
// so a default-constructed Config yields the shipped defaults.
ReadStatus Read(const std::string& ini_path, Config& out);

// Every section and key Read reads, in the order it reads them.
std::vector<cameraunlock::config::LegacyKey> ReadKeys();

}  // namespace wolf_ht::legacy
