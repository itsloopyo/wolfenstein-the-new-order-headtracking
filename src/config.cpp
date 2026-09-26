// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"
#include "logging.h"

#include "cameraunlock/config/head_tracking_config_table.h"
#include "cameraunlock/input/key_bindings.h"

namespace wolf_ht::config {

namespace {

namespace cfg = ::cameraunlock::config;
using C = cfg::schema::Concept;
using cameraunlock::input::KeyModifiers;

// The two bindings every published build registered for one action: the
// nav-cluster code, which did not fire while Ctrl and Shift were both held, and
// the chord letter, which fired only while they were. The frozen reader refuses
// any code outside 0x01-0xFE, so both format.
std::string KeyList(int nav, int chord) {
    return cameraunlock::input::FormatKeyBindings(
        {{KeyModifiers::kNone, nav}, {KeyModifiers::kCtrl | KeyModifiers::kShift, chord}});
}

cfg::ImportResult RunImport(const cfg::LegacyInput& input, Config& out) {
    legacy::Config read;
    // The published build read the file through the game folder's ANSI form. Where the folder
    // had none it read nothing and ran on the defaults, so the import reads nothing either.
    const bool absent = input.ansi_lossy || legacy::Read(input.ansi_path, read) == legacy::ReadStatus::Absent;

    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.world_space_yaw = read.world_space_yaw;
    out.local_smoothing = read.local_smoothing;
    out.position.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.position.remote_smoothing = read.remote_smoothing;

    // [Position] Enabled chose the startup mode, rotation and position or rotation only; the
    // mode hotkey reached every mode either way.
    out.rotation_enabled = true;
    out.position_enabled = read.position_enabled;

    out.position.limit_x = read.limit_x;
    // LimitY bounded the lean down as well as up.
    out.position.limit_y = read.limit_y;
    out.position.limit_y_down = read.limit_y;
    out.position.limit_z = read.limit_z;
    out.position.limit_z_back = read.limit_z_back;

    out.toggle_key_name = KeyList(read.toggle_key, read.chord_toggle_key);
    out.cycle_tracking_mode_key_name = KeyList(read.cycle_mode_key, read.chord_cycle_mode_key);
    out.yaw_mode_key_name = KeyList(read.yaw_mode_key, read.chord_yaw_mode_key);

    return absent ? cfg::ImportResult::Absent({}) : cfg::ImportResult::Imported({});
}

std::optional<cfg::ConfigOwner<Config>> g_owner;

void WriteLines(const std::vector<std::string>& lines) {
    for (const std::string& line : lines) Log::Line("[config] %s", line.c_str());
}

}  // namespace

cfg::ConfigTable<Config> MakeTable() {
    cfg::ConfigTable<Config> table = cfg::HeadTrackingConfigTable<Config>(
        {C::UdpPort, C::EnableOnStartup, C::WorldSpaceYaw, C::RotationEnabled, C::LocalSmoothing,
         C::RemoteSmoothing, C::PositionEnabled, C::PositionLimitX, C::PositionLimitY, C::PositionLimitYDown,
         C::PositionLimitZ, C::PositionLimitZBack, C::ToggleKey, C::CycleTrackingModeKey, C::YawModeKey});
    table.Select(C::WorldSpaceYaw).Writable()
        .Select(C::RotationEnabled).Writable()
        .Select(C::PositionEnabled).Writable();
    return table;
}

cfg::LegacyImport<Config> MakeLegacyImport() {
    cfg::LegacyImport<Config> import;
    import.run = &RunImport;
    import.keys = legacy::ReadKeys();
    return import;
}

cfg::ConfigOwnerOptions<Config> MakeOwnerOptions(const std::wstring& exe_dir, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = exe_dir + L"\\" + kConfigFileName;
    options.table = MakeTable();
    options.import = MakeLegacyImport();
    options.legacy_path = exe_dir + L"\\" + kLegacyFileName;
    options.header.display_name = kDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

Config Load(const std::wstring& exe_dir) {
    g_owner.emplace(MakeOwnerOptions(exe_dir, cfg::DefaultsFile::PerUser()));
    cfg::ConfigLoadResult<Config> loaded = g_owner->Load();
    WriteLines(loaded.log);
    const Config& c = loaded.config;
    Log::Line("[config] %s: udp port %d, tracking %s at startup, rotation %d position %d, yaw about %s, "
              "smoothing local %.2f remote %.2f, limits x %.2f y %.2f down %.2f z %.2f back %.2f, "
              "keys [%s] [%s] [%s]",
              cfg::ConfigLoadStatusName(loaded.status), c.udp_port, c.enable_on_startup ? "on" : "off",
              c.rotation_enabled ? 1 : 0, c.position_enabled ? 1 : 0,
              c.world_space_yaw ? "world up" : "the view axis", c.local_smoothing, c.remote_smoothing,
              c.position.limit_x, c.position.limit_y, c.position.limit_y_down, c.position.limit_z,
              c.position.limit_z_back, c.toggle_key_name.c_str(), c.cycle_tracking_mode_key_name.c_str(),
              c.yaw_mode_key_name.c_str());
    return loaded.config;
}

void Save(const std::function<void(Config&)>& change) {
    const cfg::ConfigSaveResult saved = g_owner->Save(change);
    WriteLines(saved.log);
    if (saved.status != cfg::ConfigSaveStatus::Saved) {
        Log::Line("[config] %s: %s", cfg::ConfigSaveStatusName(saved.status), saved.reason.c_str());
    }
}

}  // namespace wolf_ht::config
