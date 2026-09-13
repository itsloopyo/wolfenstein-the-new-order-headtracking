// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
//
// HeadTracking.ini is the only text this mod reads that it did not write, and
// every float in it ends up in the matrix the camera is drawn from. So this is
// the boundary suite: what the loader accepts, what it refuses, what it
// substitutes when it refuses, and - just as important - that it says so.
// "Never fail silent" is only true if the diagnostic is actually emitted, so
// the last block reads the log back and checks the lines are there.
//
// The first test is the config_defaults check src/config.cpp names: it writes
// the shipped default INI, loads it back over a deliberately poisoned Config,
// and requires every field to land on the built-in default. That is what keeps
// the default file text and the Config member initialisers from drifting apart,
// which is otherwise invisible until a player reports a setting doing nothing.
//
// Each case gets its OWN directory. GetPrivateProfileString caches by path, and
// rewriting one file under one name several times in a run is the shape that
// reads back a previous case's values.

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check_harness.h"
#include "config.h"
#include "config_sanitize.h"
#include "logging.h"

namespace {

using checks::Check;
using checks::CheckNear;

using cameraunlock::ads::AdsMode;
using wolf_ht::Config;

const wchar_t* const kLogPathWide = L"config_tests.log";
const char* const kLogPath = "config_tests.log";

constexpr char kIniName[] = "HeadTracking.ini";

std::string g_root;
std::vector<std::string> g_caseDirs;

std::string ReadWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// %TEMP%\wolf_ht_config_tests_<pid>. Absolute on purpose: a relative path
// handed to GetPrivateProfileString is resolved against the Windows directory,
// not the working directory, so a case built on one would read nothing.
bool MakeRoot() {
    char temp[MAX_PATH] = {};
    const DWORD length = GetTempPathA(static_cast<DWORD>(sizeof(temp)), temp);
    if (length == 0 || length >= sizeof(temp)) return false;

    char dir[MAX_PATH] = {};
    std::snprintf(dir, sizeof(dir), "%swolf_ht_config_tests_%lu", temp, GetCurrentProcessId());
    if (!CreateDirectoryA(dir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    g_root = dir;
    return true;
}

std::string CaseDir(const char* name) {
    const std::string dir = g_root + "\\" + name;
    CreateDirectoryA(dir.c_str(), nullptr);
    // A leftover from an interrupted run would defeat WriteDefaultConfigIfMissing's
    // CREATE_NEW and make the defaults case read a file this run never wrote.
    DeleteFileA((dir + "\\" + kIniName).c_str());
    g_caseDirs.push_back(dir);
    return dir;
}

void WriteIni(const std::string& dir, const std::string& body) {
    const std::string path = dir + "\\" + kIniName;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    Check(file.is_open(), "the test can write an INI to the case directory");
    file << body;
}

// Loads `body` over `seed`, so a check can tell "the loader kept what the key
// already held" apart from "the loader happened to write the shipped default".
Config Load(const char* caseName, const std::string& body, Config seed = Config()) {
    const std::string dir = CaseDir(caseName);
    WriteIni(dir, body);
    wolf_ht::LoadConfig(dir, seed);
    return seed;
}

// Every field set to something no default is, so a field the loader never
// touches shows up as itself rather than passing by accident.
Config Poisoned() {
    Config c;
    c.udp_port = 1234;
    c.enable_on_startup = false;
    c.toggle_key = 0x41;
    c.cycle_mode_key = 0x42;
    c.yaw_mode_key = 0x43;
    c.ads_mode_key = 0x44;
    c.chord_toggle_key = 0x45;
    c.chord_cycle_mode_key = 0x46;
    c.chord_yaw_mode_key = 0x49;
    c.chord_ads_mode_key = 0x4A;
    c.world_space_yaw = false;
    c.ads_mode = AdsMode::Tracked;
    c.local_smoothing = 0.77f;
    c.remote_smoothing = 0.88f;
    c.position_enabled = false;
    c.limit_x = 0.11f;
    c.limit_y = 0.12f;
    c.limit_z = 0.13f;
    c.limit_z_back = 0.14f;
    return c;
}

// ---- The shipped default file has to reproduce the built-in defaults ---------

void TestShippedDefaultIniReproducesTheBuiltInDefaults() {
    const std::string dir = CaseDir("defaults");
    wolf_ht::WriteDefaultConfigIfMissing(dir);
    Check(!ReadWholeFile(dir + "\\" + kIniName).empty(),
          "WriteDefaultConfigIfMissing writes a default HeadTracking.ini");

    Config loaded = Poisoned();
    wolf_ht::LoadConfig(dir, loaded);

    const Config d;
    Check(loaded.udp_port == d.udp_port, "default INI: UdpPort is the built-in default");
    Check(loaded.enable_on_startup == d.enable_on_startup,
          "default INI: EnableOnStartup is the built-in default");
    Check(loaded.world_space_yaw == d.world_space_yaw,
          "default INI: WorldSpaceYaw is the built-in default");
    Check(loaded.ads_mode == d.ads_mode, "default INI: AdsMode is the built-in default");
    Check(loaded.toggle_key == d.toggle_key, "default INI: ToggleKey is the built-in default");
    Check(loaded.cycle_mode_key == d.cycle_mode_key,
          "default INI: CycleModeKey is the built-in default");
    Check(loaded.yaw_mode_key == d.yaw_mode_key, "default INI: YawModeKey is the built-in default");
    Check(loaded.ads_mode_key == d.ads_mode_key, "default INI: AdsModeKey is the built-in default");
    Check(loaded.chord_toggle_key == d.chord_toggle_key,
          "default INI: ChordToggleKey is the built-in default");
    Check(loaded.chord_cycle_mode_key == d.chord_cycle_mode_key,
          "default INI: ChordCycleModeKey is the built-in default");
    Check(loaded.chord_yaw_mode_key == d.chord_yaw_mode_key,
          "default INI: ChordYawModeKey is the built-in default");
    Check(loaded.chord_ads_mode_key == d.chord_ads_mode_key,
          "default INI: ChordAdsModeKey is the built-in default");
    CheckNear(loaded.local_smoothing, d.local_smoothing, 1e-6f,
              "default INI: LocalSmoothing is the built-in default");
    CheckNear(loaded.remote_smoothing, d.remote_smoothing, 1e-6f,
              "default INI: RemoteSmoothing is the built-in default");
    Check(loaded.position_enabled == d.position_enabled,
          "default INI: Position Enabled is the built-in default");
    CheckNear(loaded.limit_x, d.limit_x, 1e-6f, "default INI: LimitX is the built-in default");
    CheckNear(loaded.limit_y, d.limit_y, 1e-6f, "default INI: LimitY is the built-in default");
    CheckNear(loaded.limit_z, d.limit_z, 1e-6f, "default INI: LimitZ is the built-in default");
    CheckNear(loaded.limit_z_back, d.limit_z_back, 1e-6f,
              "default INI: LimitZBack is the built-in default");
}

// Every hotkey check in this file compared the loaded value against a
// default-constructed Config, so the codes themselves were free: a commit that
// changed config.h and the shipped INI text together passed. These are the
// literal codes the shared controls table names, and the two keys it says must
// stay unbound.
void TestTheDefaultBindingsAreTheOnesTheControlsTableNames() {
    const Config d;
    Check(d.toggle_key == 0x23, "toggle is End (0x23)");
    Check(d.cycle_mode_key == 0x21, "cycle tracking mode is Page Up (0x21)");
    Check(d.yaw_mode_key == 0x22, "toggle yaw mode is Page Down (0x22)");
    Check(d.ads_mode_key == 0x2D, "cycle ADS mode is Insert (0x2D)");

    // The chord cluster, in the action order the table fixes so the same action
    // lands on the same chord in every mod.
    Check(d.chord_toggle_key == 0x59, "toggle chord is Ctrl+Shift+Y");
    Check(d.chord_cycle_mode_key == 0x47, "cycle-mode chord is Ctrl+Shift+G");
    Check(d.chord_yaw_mode_key == 0x48, "yaw-mode chord is Ctrl+Shift+H");
    Check(d.chord_ads_mode_key == 0x55, "ADS-mode chord is Ctrl+Shift+U");

    // Home and Ctrl+Shift+T were the recenter pair before mods stopped keeping a
    // centre. Binding either to something else fires on muscle memory, so they
    // stay free.
    const int nav[] = {d.toggle_key, d.cycle_mode_key, d.yaw_mode_key, d.ads_mode_key};
    const int chord[] = {d.chord_toggle_key, d.chord_cycle_mode_key, d.chord_yaw_mode_key,
                         d.chord_ads_mode_key};
    bool homeFree = true, chordTFree = true;
    for (int key : nav) {
        if (key == 0x24) homeFree = false;
    }
    for (int key : chord) {
        if (key == 0x54) chordTFree = false;
    }
    Check(homeFree, "Home (0x24) is left unbound - it was the recenter key");
    Check(chordTFree, "Ctrl+Shift+T (0x54) is left unbound - it was the recenter chord");
}

void TestAnExistingConfigIsNeverOverwritten() {
    const std::string dir = CaseDir("preserve");
    const std::string mine = "[Network]\nUdpPort=5555\n";
    WriteIni(dir, mine);

    wolf_ht::WriteDefaultConfigIfMissing(dir);
    Check(ReadWholeFile(dir + "\\" + kIniName) == mine,
          "an existing HeadTracking.ini is left byte for byte as the player wrote it");
}

void TestAMissingConfigLeavesEveryValueAlone() {
    const std::string dir = CaseDir("absent");
    Config c = Poisoned();
    wolf_ht::LoadConfig(dir, c);
    Check(c.udp_port == 1234 && c.limit_x == 0.11f && c.ads_mode == AdsMode::Tracked,
          "a missing HeadTracking.ini leaves every value exactly as it was");
}

// ---- Network ----------------------------------------------------------------

void TestPortIsRangeChecked() {
    Check(Load("port_ok", "[Network]\nUdpPort=5555\n").udp_port == 5555,
          "a port inside 1024-65535 is taken");
    // 70000 truncates to 4464 if it is cast straight to uint16_t.
    Check(Load("port_high", "[Network]\nUdpPort=70000\n").udp_port == Config().udp_port,
          "a port above 65535 falls back rather than truncating into a wrong port");
    Check(Load("port_low", "[Network]\nUdpPort=80\n").udp_port == Config().udp_port,
          "a privileged port falls back");
    Check(Load("port_neg", "[Network]\nUdpPort=-1\n").udp_port == Config().udp_port,
          "a negative port falls back");
    // ReadInt answers a present-but-unparseable value with 0, not the fallback,
    // so the range check is the only thing standing between this and port 0.
    Check(Load("port_text", "[Network]\nUdpPort=abc\n").udp_port == Config().udp_port,
          "a port that is not a number falls back instead of binding port 0");
}

// ---- Smoothing --------------------------------------------------------------

void TestSmoothingIsFiniteAndInRange() {
    const Config nan_ = Load("smooth_nan", "[Rotation]\nLocalSmoothing=nan\nRemoteSmoothing=nan\n");
    CheckNear(nan_.local_smoothing, wolf_ht::kDefaultLocalSmoothing, 1e-6f,
              "LocalSmoothing=nan lands on the LOCAL default");
    // The bug this guards: one shared fallback would hand a phone on WiFi the
    // local "no smoothing at all" on raw network jitter.
    CheckNear(nan_.remote_smoothing, wolf_ht::kDefaultRemoteSmoothing, 1e-6f,
              "RemoteSmoothing=nan lands on the REMOTE default, not the local one");

    const Config inf = Load("smooth_inf", "[Rotation]\nLocalSmoothing=1e400\n");
    CheckNear(inf.local_smoothing, wolf_ht::kDefaultLocalSmoothing, 1e-6f,
              "a literal that overflows to infinity lands on the default");

    const Config out = Load("smooth_range", "[Rotation]\nLocalSmoothing=5\nRemoteSmoothing=-2\n");
    CheckNear(out.local_smoothing, 1.0f, 1e-6f, "smoothing above 1 is clamped to 1");
    CheckNear(out.remote_smoothing, 0.0f, 1e-6f, "smoothing below 0 is clamped to 0");

    // A configured 0.0 is a real setting, not "unset": nothing may floor it.
    const Config zero = Load("smooth_zero", "[Rotation]\nRemoteSmoothing=0.0\n");
    CheckNear(zero.remote_smoothing, 0.0f, 1e-6f, "RemoteSmoothing=0.0 is honoured, not floored");

    // A decimal comma parses as 0.0 under a prefix parse, which is inside the
    // valid range and would pass every downstream check silently.
    Config seed;
    seed.remote_smoothing = 0.42f;
    const Config comma = Load("smooth_comma", "[Rotation]\nRemoteSmoothing=0,15\n", seed);
    CheckNear(comma.remote_smoothing, 0.42f, 1e-6f,
              "a decimal comma is refused rather than read as 0.0");

    const Config comment = Load("smooth_comment", "[Rotation]\nLocalSmoothing=0.4 ; settle\n");
    CheckNear(comment.local_smoothing, 0.4f, 1e-6f, "a trailing comment on a number is stripped");
}

void TestRetiredSmoothingKeyIsIgnored() {
    Config seed;
    seed.local_smoothing = 0.31f;
    seed.remote_smoothing = 0.32f;
    const Config c = Load("smooth_retired",
                          "[Rotation]\nSmoothing=0.9\n\n[Position]\nSmoothing=0.9\n", seed);
    CheckNear(c.local_smoothing, 0.31f, 1e-6f, "the retired Smoothing key does not set LocalSmoothing");
    CheckNear(c.remote_smoothing, 0.32f, 1e-6f,
              "the retired Smoothing key does not set RemoteSmoothing");
}

// ---- Position ---------------------------------------------------------------

void TestPositionLimitsAreClamped() {
    const Config c = Load("limits",
                          "[Position]\nLimitX=4.0\nLimitY=-1\nLimitZ=0.25\nLimitZBack=inf\n");
    CheckNear(c.limit_x, wolf_ht::kMaxPositionLimit, 1e-6f,
              "a misplaced decimal point on LimitX is pulled back to the documented maximum");
    CheckNear(c.limit_y, 0.0f, 1e-6f,
              "a negative limit is clamped to 0 rather than inverting the clamp bounds");
    CheckNear(c.limit_z, 0.25f, 1e-6f, "a limit inside the range is taken as written");
    CheckNear(c.limit_z_back, Config().limit_z_back, 1e-6f,
              "a non-finite limit falls back instead of putting NaN in the translation");
}

void TestBoolWithATrailingCommentKeepsThePreviousValue() {
    // GetPrivateProfileString does not treat ';' as a comment introducer and
    // ReadBool matches the whole value, so this matches nothing. The value the
    // key keeps is the one it had, and the log has to say why.
    Check(Load("bool_comment", "[Position]\nEnabled=0 ; no lean\n").position_enabled,
          "a bool with a trailing comment keeps its previous value");
    Check(!Load("bool_plain", "[Position]\nEnabled=0\n").position_enabled,
          "a bool on its own is read");
    Check(!Load("bool_word", "[Position]\nEnabled=false\n").position_enabled,
          "false is read as a bool");
}

// ---- Hotkeys ----------------------------------------------------------------

void TestHotkeyCodesAreReadAsHexAndRangeChecked() {
    Check(Load("key_hex", "[Hotkeys]\nToggleKey=0x70\n").toggle_key == 0x70,
          "a 0x-prefixed code is read");
    Check(Load("key_bare", "[Hotkeys]\nToggleKey=70\n").toggle_key == 0x70,
          "a bare code is read as hex, matching how the shipped INI publishes them");
    Check(Load("key_wide", "[Hotkeys]\nToggleKey=0x230\n").toggle_key == Config().toggle_key,
          "a code GetAsyncKeyState cannot poll keeps the previous binding");
    Check(Load("key_shift", "[Hotkeys]\nToggleKey=0x10\n").toggle_key == Config().toggle_key,
          "a modifier key is refused, because the chord guard is what tests those");
    // "End" is made of hex digits: a prefix parse binds 0xE and the player
    // never learns their key name was not understood.
    Check(Load("key_name", "[Hotkeys]\nToggleKey=End\n").toggle_key == Config().toggle_key,
          "a key NAME is refused rather than parsed as the hex prefix it happens to be");
    Check(Load("key_neg", "[Hotkeys]\nToggleKey=-1\n").toggle_key == Config().toggle_key,
          "a negative code keeps the previous binding");
    Check(Load("key_comment", "[Hotkeys]\nToggleKey=0x71 ; F2\n").toggle_key == 0x71,
          "a trailing comment on a key code is allowed");
}

// ---- ADS mode ---------------------------------------------------------------

void TestAdsModeFallsBackToStockAds() {
    Check(Load("ads_tracked", "[General]\nAdsMode=tracked\n").ads_mode == AdsMode::Tracked,
          "a known AdsMode value is read");
    Config seed;
    seed.ads_mode = AdsMode::Tracked;
    Check(Load("ads_junk", "[General]\nAdsMode=wibble\n", seed).ads_mode == AdsMode::Paused,
          "an unknown AdsMode lands on stock ADS, not on tracking through the irons");
}

// ---- File shape --------------------------------------------------------------

void TestOnlyTheFirstOfADuplicatedSectionIsRead() {
    // GetPrivateProfileString answers with the first section of a given name.
    // The loader cannot honour the second block, so it has to report it.
    const Config c = Load("dup_section",
                          "[Position]\nLimitZ=0.25\n\n[Position]\nLimitZ=0.45\n");
    CheckNear(c.limit_z, 0.25f, 1e-6f, "the second [Position] block is not read");
}

// The decimal-point advice is withheld when there is no comma to explain it.
// Only the comma case was covered, so inverting that ternary changed nothing.
void TestANonNumericValueWithNoCommaKeepsTheValue() {
    Config seed;
    seed.limit_z = 0.33f;
    const Config c = Load("limit_text", "[Position]\nLimitZ=near\n", seed);
    CheckNear(c.limit_z, 0.33f, 1e-6f, "text where a limit belongs keeps the previous value");
}

// ---- The refusals have to be visible -----------------------------------------

void CheckLogSays(const std::string& log, const char* fragment, const char* what) {
    Check(log.find(fragment) != std::string::npos, what);
}

void CheckLogSaysNot(const std::string& log, const char* fragment, const char* what) {
    Check(log.find(fragment) == std::string::npos, what);
}

void TestEveryRefusalIsInTheLog(const std::string& log) {
    std::printf("refusals are reported:\n");
    CheckLogSays(log, "UdpPort=70000 is outside 1024-65535",
                 "a port refused for range says so, and echoes what was written");
    // GetPrivateProfileIntA answers 0 for anything it cannot parse, so without
    // the raw-text read this arrives indistinguishable from UdpPort=0 and gets
    // reported as a range error - a cause the input does not support.
    CheckLogSays(log, "UdpPort=abc is not a whole number",
                 "a port that is not a number says THAT, not that it is out of range");
    CheckLogSays(log, "is out of range or not finite", "a clamped float is logged");
    CheckLogSays(log, "is not a number", "text where a number belongs is logged");
    CheckLogSays(log, "Use a dot for the decimal point",
                 "a decimal comma gets the advice that fixes it");
    // The other half of that ternary. Invert it and every check above still
    // passes while "LimitY=0.2 metres" starts telling the user to use a dot,
    // which is advice that fixes nothing.
    CheckLogSaysNot(log, "LimitZ=near is not a number. Use a dot",
                    "a non-numeric value with no comma is NOT given the decimal-point advice");
    CheckLogSays(log, "is not a virtual key code", "an unreadable key code is logged");
    CheckLogSays(log, "is not a key that can be bound", "an unbindable key code is logged");
    CheckLogSays(log, "is not 0 or 1", "a bool that matched nothing is logged");
    CheckLogSays(log, "has been retired and is IGNORED", "the retired smoothing key is logged");
    CheckLogSays(log, "is not paused, marker or tracked", "an unknown AdsMode is logged");
    CheckLogSays(log, "more than one [Position] section", "a duplicated section is logged");
    CheckLogSays(log, "using built-in defaults", "a missing INI is logged");
}

void Cleanup() {
    for (const std::string& dir : g_caseDirs) {
        DeleteFileA((dir + "\\" + kIniName).c_str());
        RemoveDirectoryA(dir.c_str());
    }
    if (!g_root.empty()) RemoveDirectoryA(g_root.c_str());
    std::remove(kLogPath);
    std::remove("config_tests.prev.log");
}

}  // namespace

int main() {
    if (!MakeRoot()) {
        Check(false, "the test can create a temporary directory to hold its INI files");
        return checks::Summarize("config");
    }

    cameraunlock::logging::Open(kLogPathWide);

    TestShippedDefaultIniReproducesTheBuiltInDefaults();
    TestTheDefaultBindingsAreTheOnesTheControlsTableNames();
    TestAnExistingConfigIsNeverOverwritten();
    TestAMissingConfigLeavesEveryValueAlone();
    TestPortIsRangeChecked();
    TestSmoothingIsFiniteAndInRange();
    TestRetiredSmoothingKeyIsIgnored();
    TestPositionLimitsAreClamped();
    TestBoolWithATrailingCommentKeepsThePreviousValue();
    TestHotkeyCodesAreReadAsHexAndRangeChecked();
    TestAdsModeFallsBackToStockAds();
    TestOnlyTheFirstOfADuplicatedSectionIsRead();
    TestANonNumericValueWithNoCommaKeepsTheValue();

    cameraunlock::logging::Close();
    TestEveryRefusalIsInTheLog(ReadWholeFile(kLogPath));

    Cleanup();
    return checks::Summarize("config");
}
