// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
//
// CameraUnlock.ini: the committed file against the table that renders it, what
// the owner creates on a first start and on the first start after an update,
// and what a toggle's save changes.
//
// Every owner here reads and creates Defaults.ini at a scratch path
// (DefaultsFile::At), never the developer's own.
//
// `--render-config <path>` writes the table's fresh render to <path> and exits,
// which is how `pixi run render-config` rewrites the committed file.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "check_harness.h"
#include "config.h"

#include "cameraunlock/config/config_table.h"

namespace {

namespace cfg = ::cameraunlock::config;
namespace fs = std::filesystem;
using checks::Check;
using wolf_ht::Config;

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

std::string Fresh() {
    return cfg::RenderCanonicalFresh(wolf_ht::config::MakeTable(), cfg::RenderHeader{wolf_ht::config::kDisplayName});
}

class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() / ("wolf-canonical-config-" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root_);
        fs::create_directories(root_ / "global");
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    fs::path Fresh(const std::string& leaf) {
        const fs::path dir = root_ / (leaf + std::to_string(next_++));
        fs::create_directories(dir);
        return dir;
    }
    // One Defaults.ini for every owner, created by the first with the built-in values.
    cfg::DefaultsFile Defaults() const { return cfg::DefaultsFile::At(DefaultsPath().wstring()); }
    fs::path DefaultsPath() const { return root_ / "global" / "Defaults.ini"; }

private:
    fs::path root_;
    int next_ = 0;
};

cfg::ConfigLoadResult<Config> LoadIn(const Scratch& scratch, const fs::path& dir) {
    cfg::ConfigOwner<Config> owner(wolf_ht::config::MakeOwnerOptions(dir.wstring(), scratch.Defaults()));
    return owner.Load();
}

std::vector<std::string> Lines(const std::string& s) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') {
            lines.push_back(s.substr(start, i + 1 - start));
            start = i + 1;
        }
    }
    if (start < s.size()) lines.push_back(s.substr(start));
    return lines;
}

// The lines of `after` that differ from `before`, which must have as many.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before);
    const std::vector<std::string> b = Lines(after);
    if (a.size() != b.size()) return {"the line count changed"};
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

bool HasLine(const std::string& file, const std::string& line) {
    for (const std::string& l : Lines(file)) {
        if (l == line + "\r\n") return true;
    }
    return false;
}

bool LogSays(const std::vector<std::string>& log, const char* fragment) {
    for (const std::string& line : log) {
        if (line.find(fragment) != std::string::npos) return true;
    }
    return false;
}

void TestTheCommittedFileIsTheFreshRender() {
    Check(ReadBytes(WOLF_COMMITTED_CONFIG) == Fresh(),
          "CameraUnlock.ini is the table's fresh render; run pixi run render-config");
}

// A first start with no HeadTracking.ini creates the committed file and a
// Defaults.ini holding core's built-in values, and runs on the defaults every
// published build shipped.
void TestAFirstStartCreatesTheCommittedFile(Scratch& scratch) {
    const fs::path dir = scratch.Fresh("created");
    const cfg::ConfigLoadResult<Config> loaded = LoadIn(scratch, dir);
    Check(loaded.status == cfg::ConfigLoadStatus::Created, "a first start creates CameraUnlock.ini");
    Check(ReadBytes(dir / "CameraUnlock.ini") == ReadBytes(WOLF_COMMITTED_CONFIG),
          "the created file is the committed file");
    Check(!fs::exists(dir / "HeadTracking.ini"), "no HeadTracking.ini is written");
    Check(ReadBytes(scratch.DefaultsPath()) == ReadBytes(WOLF_DEFAULTS_FIXTURE),
          "the created Defaults.ini is core's fixture");

    const Config& c = loaded.config;
    Check(c.udp_port == 4242, "UdpPort 4242");
    Check(c.enable_on_startup, "tracking on at startup");
    Check(c.rotation_enabled && c.position_enabled, "rotation and position at startup");
    Check(c.world_space_yaw, "yaw about world up");
    Check(c.local_smoothing == 0.0f && c.remote_smoothing == 0.15f, "smoothing 0 local, 0.15 remote");
    Check(c.position.limit_x == 0.30f && c.position.limit_y == 0.20f && c.position.limit_y_down == 0.20f &&
              c.position.limit_z == 0.40f && c.position.limit_z_back == 0.10f,
          "the position limits every published build shipped");
    Check(c.toggle_key_name == "End, Ctrl+Shift+Y", "ToggleKey is End, Ctrl+Shift+Y");
    Check(c.cycle_tracking_mode_key_name == "PageUp, Ctrl+Shift+G", "CycleTrackingModeKey is PageUp, Ctrl+Shift+G");
    Check(c.yaw_mode_key_name == "PageDown, Ctrl+Shift+H", "YawModeKey is PageDown, Ctrl+Shift+H");
}

// The newest published build put its first-run output on a player's disk and
// nothing else. Imported with Defaults.ini at the built-in values, it gives the
// same bytes a new player gets, and HeadTracking.ini is left as it was.
void TestTheFirstUpdateGivesTheCommittedFile(Scratch& scratch) {
    const std::string firstRun = ReadBytes(fs::path(WOLF_DIFFERENTIAL_DATA) / "dev-first-run.ini");
    const fs::path dir = scratch.Fresh("upgrade");
    WriteBytes(dir / "HeadTracking.ini", firstRun);
    const cfg::ConfigLoadResult<Config> loaded = LoadIn(scratch, dir);
    Check(loaded.status == cfg::ConfigLoadStatus::Migrated, "the first start after an update imports");
    Check(ReadBytes(dir / "CameraUnlock.ini") == ReadBytes(WOLF_COMMITTED_CONFIG),
          "the published first run imports into the committed file");
    Check(ReadBytes(dir / "HeadTracking.ini") == firstRun, "HeadTracking.ini keeps its bytes");

    const cfg::ConfigLoadResult<Config> again = LoadIn(scratch, dir);
    Check(again.status == cfg::ConfigLoadStatus::Canonical, "the next start reads CameraUnlock.ini");
    Check(LogSays(again.log, "is left as it was and is not read"),
          "the next start says HeadTracking.ini is not read");
}

// A player's own values: each chord letter joins its action's list, LimitY
// sets both vertical limits, and [Position] Enabled=0 is the rotation only
// start. Rows whose value differs from Defaults.ini hold the value.
void TestAnEditedFileImportsItsValues(Scratch& scratch) {
    const fs::path dir = scratch.Fresh("edited");
    const std::string legacy =
        "[Network]\nUdpPort=5555\n"
        "[General]\nWorldSpaceYaw=0\n"
        "[Hotkeys]\nToggleKey=0x70\nChordToggleKey=0x4A\nChordYawModeKey=0xBA\n"
        "[Rotation]\nRemoteSmoothing=0.3\n"
        "[Position]\nEnabled=0\nLimitY=0.35\n";
    WriteBytes(dir / "HeadTracking.ini", legacy);
    const cfg::ConfigLoadResult<Config> loaded = LoadIn(scratch, dir);
    Check(loaded.status == cfg::ConfigLoadStatus::Migrated, "an edited file imports");
    const std::string file = ReadBytes(dir / "CameraUnlock.ini");
    Check(HasLine(file, "UdpPort=5555"), "UdpPort=5555");
    Check(HasLine(file, "WorldSpaceYaw=false"), "WorldSpaceYaw=false");
    Check(HasLine(file, "ToggleKey=F1, Ctrl+Shift+J"), "ToggleKey=F1, Ctrl+Shift+J");
    Check(HasLine(file, "CycleTrackingModeKey=default"), "an untouched hotkey follows Defaults.ini");
    Check(HasLine(file, "YawModeKey=PageDown, Ctrl+Shift+0xBA"), "a code with no key name is written as 0x");
    Check(HasLine(file, "RemoteSmoothing=0.3"), "RemoteSmoothing=0.3");
    Check(HasLine(file, "LocalSmoothing=default"), "an untouched value follows Defaults.ini");
    Check(HasLine(file, "RotationEnabled=true") && HasLine(file, "PositionEnabled=false"),
          "Enabled=0 is the rotation only start, written as the pair");
    Check(HasLine(file, "PositionLimitY=0.35") && HasLine(file, "PositionLimitYDown=0.35"),
          "LimitY sets both vertical limits");
    Check(ReadBytes(dir / "HeadTracking.ini") == legacy, "HeadTracking.ini keeps its bytes");
}

// The yaw toggle's save writes WorldSpaceYaw and no other byte; the mode
// cycle's writes the pair and no other byte; the next start reads both back.
void TestASaveChangesOnlyItsRows(Scratch& scratch) {
    const fs::path dir = scratch.Fresh("save");
    const fs::path file = dir / "CameraUnlock.ini";
    {
        cfg::ConfigOwner<Config> owner(wolf_ht::config::MakeOwnerOptions(dir.wstring(), scratch.Defaults()));
        Check(owner.Load().status == cfg::ConfigLoadStatus::Created, "the save case starts from a created file");
        const std::string created = ReadBytes(file);

        const cfg::ConfigSaveResult yaw = owner.Save([](Config& c) { c.world_space_yaw = false; });
        Check(yaw.status == cfg::ConfigSaveStatus::Saved, "the yaw toggle saves");
        const std::vector<std::string> yawLines = ChangedLines(created, ReadBytes(file));
        Check(yawLines.size() == 1 && yawLines[0] == "WorldSpaceYaw=false\r\n",
              "the yaw save changes the WorldSpaceYaw line and nothing else");
        Check(LogSays(yaw.log, "WorldSpaceYaw=false is now set for this game"),
              "the save says WorldSpaceYaw no longer follows Defaults.ini");

        const std::string afterYaw = ReadBytes(file);
        const cfg::ConfigSaveResult mode = owner.Save([](Config& c) {
            c.rotation_enabled = false;
            c.position_enabled = true;
        });
        Check(mode.status == cfg::ConfigSaveStatus::Saved, "the mode cycle saves");
        const std::vector<std::string> modeLines = ChangedLines(afterYaw, ReadBytes(file));
        Check(modeLines.size() == 2 && modeLines[0] == "RotationEnabled=false\r\n" &&
                  modeLines[1] == "PositionEnabled=true\r\n",
              "the mode save changes the pair and nothing else");

        bool refused = false;
        try {
            owner.Save([](Config& c) { c.enable_on_startup = false; });
        } catch (const std::exception&) {
            refused = true;
        }
        Check(refused, "EnableOnStartup is not a row a toggle can save");
    }
    const cfg::ConfigLoadResult<Config> again = LoadIn(scratch, dir);
    Check(again.status == cfg::ConfigLoadStatus::Canonical, "the saved file reads back");
    Check(!again.config.world_space_yaw, "the saved yaw mode comes back");
    Check(!again.config.rotation_enabled && again.config.position_enabled, "the saved tracking mode comes back");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
        WriteBytes(argv[2], Fresh());
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }
    try {
        Scratch scratch;
        TestTheCommittedFileIsTheFreshRender();
        TestAFirstStartCreatesTheCommittedFile(scratch);
        TestTheFirstUpdateGivesTheCommittedFile(scratch);
        TestAnEditedFileImportsItsValues(scratch);
        TestASaveChangesOnlyItsRows(scratch);
    } catch (const std::exception& e) {
        std::printf("  [FAIL] threw: %s\n", e.what());
        return 1;
    }
    return checks::Summarize("config");
}
