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

namespace wolf_ht::config {

namespace {

namespace cfg = ::cameraunlock::config;
using C = cfg::schema::Concept;

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

    // Each action had a nav-cluster key, which did not fire while Ctrl and Shift were both held,
    // and a chord key, which fired only while they were.
    std::vector<cfg::DroppedValue> dropped;
    const auto bindings = [&dropped](int key, const char* key_name, int chord, const char* chord_name) {
        std::string list = cfg::LegacyVirtualKeyToBindings(key, "Hotkeys", key_name, dropped);
        const std::string chord_key = cfg::LegacyVirtualKeyToBindings(chord, "Hotkeys", chord_name, dropped);
        if (!chord_key.empty()) list += (list.empty() ? "Ctrl+Shift+" : ", Ctrl+Shift+") + chord_key;
        return list;
    };
    out.toggle_key_name = bindings(read.toggle_key, "ToggleKey", read.chord_toggle_key, "ChordToggleKey");
    out.cycle_tracking_mode_key_name =
        bindings(read.cycle_mode_key, "CycleModeKey", read.chord_cycle_mode_key, "ChordCycleModeKey");
    out.yaw_mode_key_name = bindings(read.yaw_mode_key, "YawModeKey", read.chord_yaw_mode_key, "ChordYawModeKey");

    // A setting the player never changed from what the dev build shipped follows Defaults.ini.
    // LimitY stood for both vertical bounds, and each hotkey for its key and its chord key together.
    const legacy::Config shipped;
    cfg::LegacyFollowsDefaultsIni follows;
    follows.Setting(C::UdpPort, read.udp_port, shipped.udp_port);
    follows.Setting(C::EnableOnStartup, read.enable_on_startup, shipped.enable_on_startup);
    follows.Setting(C::WorldSpaceYaw, read.world_space_yaw, shipped.world_space_yaw);
    follows.TrackingMode(read.position_enabled, shipped.position_enabled);
    follows.Setting(C::LocalSmoothing, read.local_smoothing, shipped.local_smoothing);
    follows.Setting(C::RemoteSmoothing, read.remote_smoothing, shipped.remote_smoothing);
    follows.Setting(C::PositionLimitX, read.limit_x, shipped.limit_x);
    follows.Setting(C::PositionLimitY, read.limit_y, shipped.limit_y);
    follows.Setting(C::PositionLimitYDown, read.limit_y, shipped.limit_y);
    follows.Setting(C::PositionLimitZ, read.limit_z, shipped.limit_z);
    follows.Setting(C::PositionLimitZBack, read.limit_z_back, shipped.limit_z_back);
    follows.Setting(C::ToggleKey,
                    read.toggle_key == shipped.toggle_key && read.chord_toggle_key == shipped.chord_toggle_key);
    follows.Setting(C::CycleTrackingModeKey, read.cycle_mode_key == shipped.cycle_mode_key &&
                                                 read.chord_cycle_mode_key == shipped.chord_cycle_mode_key);
    follows.Setting(C::YawModeKey,
                    read.yaw_mode_key == shipped.yaw_mode_key && read.chord_yaw_mode_key == shipped.chord_yaw_mode_key);

    return absent ? cfg::ImportResult::Absent(std::move(dropped), {}, follows.Concepts())
                  : cfg::ImportResult::Imported(std::move(dropped), {}, follows.Concepts());
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
