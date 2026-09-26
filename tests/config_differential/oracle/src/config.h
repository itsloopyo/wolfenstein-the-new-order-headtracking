// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <string>

#include "cameraunlock/ads/ads_mode.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace wolf_ht {

// The shipped default for each smoothing key. Named once here because two
// places need it and they must not drift: the Config members below, and the
// loader, where a value it refuses has to land on the default of the key it
// came from rather than on one shared by both.
inline constexpr float kDefaultLocalSmoothing =
    static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
inline constexpr float kDefaultRemoteSmoothing =
    static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

// Default hotkey bindings, as Windows virtual key codes. Written as codes
// rather than the VK_ macros because this header is included by translation
// units that do not pull in windows.h, and the INI publishes them as codes too.
inline constexpr int kDefaultToggleKey = 0x23;           // End
inline constexpr int kDefaultCycleModeKey = 0x21;        // Page Up
inline constexpr int kDefaultYawModeKey = 0x22;          // Page Down
inline constexpr int kDefaultAdsModeKey = 0x2D;          // Insert
inline constexpr int kDefaultChordToggleKey = 0x59;      // Y, as Ctrl+Shift+Y
inline constexpr int kDefaultChordCycleModeKey = 0x47;   // G, as Ctrl+Shift+G
inline constexpr int kDefaultChordYawModeKey = 0x48;     // H, as Ctrl+Shift+H
inline constexpr int kDefaultChordAdsModeKey = 0x55;     // U, as Ctrl+Shift+U

struct Config {
    // Held as the socket's own type so an out-of-range INI value cannot reach
    // UdpReceiver::Start by silently truncating to a wrong 16-bit port.
    std::uint16_t udp_port = 4242;
    bool enable_on_startup = true;

    // Virtual key codes. Every action has a nav-cluster key and a
    // Ctrl+Shift+<key> chord, and both fire it - the chord is there for
    // keyboards with no nav cluster.
    int toggle_key = kDefaultToggleKey;
    int cycle_mode_key = kDefaultCycleModeKey;
    int yaw_mode_key = kDefaultYawModeKey;
    int ads_mode_key = kDefaultAdsModeKey;
    int chord_toggle_key = kDefaultChordToggleKey;
    int chord_cycle_mode_key = kDefaultChordCycleModeKey;
    int chord_yaw_mode_key = kDefaultChordYawModeKey;
    int chord_ads_mode_key = kDefaultChordAdsModeKey;

    // Head yaw about world up rather than about the camera's own up axis. B.J.
    // is a person standing on the ground for all but the vehicle sequences, so
    // the horizon means something and both positions are defensible - hence the
    // toggle. World up is the default because it is what keeps a glance left
    // level while the player is looking up or down a stairwell.
    bool world_space_yaw = true;

    // What head tracking does while the sights are up. `paused` is the default
    // because it is the mode that cannot be wrong: the game keeps the camera and
    // the sight picture is exactly its own. Anything the file says that is not
    // one of the three values lands here rather than on whichever branch is last,
    // which is also how a mode renamed since an older release wrote this file
    // migrates - to stock ADS, not to head tracking through the irons that the
    // player never asked for.
    cameraunlock::ads::AdsMode ads_mode = cameraunlock::ads::kDefaultAdsMode;

    // No sensitivity, deadzone, response curve or axis inversion lives here,
    // for rotation or for position: the tracker owns pose shaping, so the pose
    // is consumed at 1:1 and one tracker profile behaves the same way in every
    // game. The protocol-to-engine sign conversion the camera does need is a
    // fixed part of the boundary in tracker_feed.cpp, not a setting.

    // Smoothing is chosen per connection from the packet's source address, and
    // both values cover rotation and position alike. A tracker running on this
    // machine is already steady, so local_smoothing is 0.0 and nothing floors
    // it; a phone on WiFi jitters over the network, which is what
    // remote_smoothing is for.
    float local_smoothing = kDefaultLocalSmoothing;
    float remote_smoothing = kDefaultRemoteSmoothing;

    bool position_enabled = true;
    float limit_x = cameraunlock::PositionSettings{}.limit_x;
    float limit_y = cameraunlock::PositionSettings{}.limit_y;
    float limit_z = cameraunlock::PositionSettings{}.limit_z;
    float limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;
};

// Reads HeadTracking.ini from `exe_dir` over `out`. Keys that are absent, or
// whose value the boundary checks in config_sanitize.h reject, leave the
// corresponding member of `out` at whatever it already held - so passing a
// default-constructed Config yields the shipped defaults.
void LoadConfig(const std::string& exe_dir, Config& out);

// Writes the documented default HeadTracking.ini into `exe_dir`, unless one is
// already there. Never overwrites a user's file.
void WriteDefaultConfigIfMissing(const std::string& exe_dir);

// Writes AdsMode back to HeadTracking.ini, so the mode the player cycled to with
// the hotkey is the mode they get next launch. The only setting this mod writes
// back: the other three toggles are session state, this one is a choice.
// An empty `exe_dir` - the game directory could not be resolved - is a no-op and
// says so.
void SaveAdsMode(const std::string& exe_dir, cameraunlock::ads::AdsMode mode);

}  // namespace wolf_ht
