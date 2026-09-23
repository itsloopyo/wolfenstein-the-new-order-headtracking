// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <utility>
#include <vector>

#include "config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"
#include "cameraunlock/protocol/port_utils.h"

namespace wolf_ht {

namespace {

constexpr char kIniName[] = "HeadTracking.ini";

// The file a fresh install lands with. Values here must stay in step with the
// Config struct's member initialisers - the config_defaults test locks that by
// generating this file and loading it back over a poisoned Config.
constexpr char kDefaultIniText[] =
    "; Wolfenstein: The New Order Head Tracking - configuration\n"
    "; Edit values, restart the game to apply.\n"
    ";\n"
    "; Controls (all remappable, see [Hotkeys]):\n"
    ";           End  / Ctrl+Shift+Y   toggle tracking\n"
    ";           PgUp / Ctrl+Shift+G   cycle tracking mode (rotation and position\n"
    ";                                 / rotation only / position only)\n"
    ";           PgDn / Ctrl+Shift+H   yaw about world up / about the view axis\n"
    ";\n"
    "; There is no recenter key. Centre your head in your tracker (OpenTrack's\n"
    "; Center bind, or your phone app's CENTER button) - this mod uses the pose it\n"
    "; is sent, exactly as sent, so one centre anywhere is the whole story.\n"
    ";\n"
    "; Head tracking stays on while you aim down sights; leaning eases out while\n"
    "; the sights are up. Nothing about aiming is a setting.\n"
    ";\n"
    "; Field of view is a game setting, not a mod setting. Wolfenstein has its own\n"
    "; Field of View slider under Options > Video. This mod rotates and moves the\n"
    "; camera and never writes its field of view.\n"
    "\n"
    "[Network]\n"
    "; The port your tracker sends OpenTrack UDP packets to.\n"
    "UdpPort=4242\n"
    "\n"
    "[General]\n"
    "EnableOnStartup=1\n"
    "; 1 = head yaw turns about world up, so a glance left stays level while you\n"
    "; are looking up or down a stairwell. 0 = yaw turns about the view axis.\n"
    "WorldSpaceYaw=1\n"
    "\n"
    "[Hotkeys]\n"
    "; Windows virtual key codes, in hex. Each action has a nav-cluster key and a\n"
    "; Ctrl+Shift+<key> chord, and both fire it - remap either or both.\n"
    "; Common codes: End 0x23, Delete 0x2E, PgUp 0x21, PgDn 0x22,\n"
    "; F1-F12 0x70-0x7B, A-Z 0x41-0x5A, numpad 0-9 0x60-0x69.\n"
    "ToggleKey=0x23\n"
    "CycleModeKey=0x21\n"
    "YawModeKey=0x22\n"
    "ChordToggleKey=0x59\n"
    "ChordCycleModeKey=0x47\n"
    "ChordYawModeKey=0x48\n"
    "\n"
    "; Head movement is used exactly as your tracker sends it. There is no\n"
    "; sensitivity, deadzone or axis inversion here on purpose: set those in\n"
    "; OpenTrack or your phone app once, and every game behaves the same way.\n"
    "\n"
    "[Rotation]\n"
    "; Smoothing covers rotation and position alike, and the value used is picked\n"
    "; per connection from where the tracker sends from. 0.0 none .. 1.0 heavy.\n"
    "; LOCAL means the packets arrive from 127.0.0.1. A tracker running on this\n"
    "; same PC that sends to this PC's LAN address counts as REMOTE - the mod sees\n"
    "; a transport, not a machine.\n"
    "LocalSmoothing=0.0\n"
    "RemoteSmoothing=0.15\n"
    "\n"
    "[Position]\n"
    "Enabled=1\n"
    "; How far the view may lean from where the game put it, in metres.\n"
    "; 0 to 0.50 on each axis. A value past either end is pulled back to it and the\n"
    "; log says so; 0 is a real setting and pins that axis.\n"
    "LimitX=0.30\n"
    "LimitY=0.20\n"
    "LimitZ=0.40\n"
    "LimitZBack=0.10\n";

std::string IniPath(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

// A value the mod refused is exactly what a "my INI setting does nothing" bug
// report needs to show, so every substitution is logged rather than swallowed.
float UseSanitized(const char* name, float raw, float clean) {
    if (raw != clean) {
        Log::Line("[config] %s=%.4f is out of range or not finite; using %.4f",
                  name, raw, clean);
    }
    return clean;
}

// Present but unparseable was the one INI failure with nothing in the log to
// show for it. Every reader in IniReader answers it with the fallback the caller
// passed, and each caller here passes the value the Config already holds, so the
// VALUE was already right - the key kept what it had. What was missing is any
// way to tell that from a key the user never wrote, which is the difference
// between a triageable "my setting is ignored" report and an untriageable one.
// Every other refusal in this file is logged; these were not.
//
// Two probes with opposite fallbacks separate the two cases without duplicating
// the parser: agree, and the reader parsed the text; differ, and it echoed each
// fallback straight back.
//
// The trap this exists for is a bool with a trailing comment. IniReader's own
// header documents it: GetPrivateProfileString does not treat ';' as a comment
// introducer, and ReadBool matches the WHOLE value, so `Enabled=0 ; no lean`
// matches nothing, position tracking stays on, and the user is told why.
bool ParsedBool(const cameraunlock::IniReader& ini, const char* section,
                       const char* key, bool& out) {
    const bool as_true = ini.ReadBool(section, key, true);
    if (as_true != ini.ReadBool(section, key, false)) return false;
    out = as_true;
    return true;
}

bool ReadFlag(const cameraunlock::IniReader& ini, const char* section,
                     const char* key, bool current) {
    const std::string text = ini.ReadString(section, key, "");
    if (text.empty()) return current;

    bool parsed = false;
    if (ParsedBool(ini, section, key, parsed)) return parsed;

    Log::Line("[config] %s=%s is not 0 or 1 (or true/false, yes/no, on/off); keeping %d. "
              "A trailing ; comment is part of the value here - put comments on their own "
              "line above the key.",
              key, text.c_str(), current ? 1 : 0);
    return current;
}

// Shared by every float key: refuse text that will not parse, and hand what does
// parse to the caller's own boundary check. `current` is what the key keeps when
// the text is unreadable; where an out-of-range or non-finite number lands is
// `sanitize`'s business, because the two smoothing keys do not share a default.
template <typename Sanitize>
float ReadFloatValue(const cameraunlock::IniReader& ini, const char* section,
                            const char* key, float current, Sanitize sanitize) {
    // The value with a trailing comment taken off and trimmed, then a parse
    // that requires the WHOLE token. GetPrivateProfileString does not treat ';'
    // as a comment introducer, so the comment has to come off first; and a
    // prefix parse is the trap this exists for, because strtod stops at the
    // first character it cannot use and reports nothing. `RemoteSmoothing=0,15`
    // - written by anyone whose keyboard decimal separator is a comma - reads
    // as 0.0 under a prefix parse. That is inside the valid range, so no check
    // downstream questions it, and it silently produces the un-smoothed phone
    // on WiFi that RemoteSmoothing exists to prevent.
    //
    // Core's parser rather than a local one, so the mod and the shared config
    // schema cannot disagree about what a number is. Inf and NaN still count as
    // parsed and go on to the finite checks in config_sanitize.h; only text no
    // parser can read reaches the refusal below.
    const std::string text = cameraunlock::config::ReadRawValue(ini, section, key);
    if (text.empty()) return current;

    float raw = 0.0f;
    if (!cameraunlock::config::ParseFloatStrict(text, raw)) {
        // The decimal-separator advice only where there is a separator to be
        // wrong about: telling someone who wrote `LimitY=0.2 metres` to use a
        // dot is a diagnostic pointing at the wrong thing.
        Log::Line("[config] %s=%s is not a number; keeping %.4f.%s", key, text.c_str(), current,
                  text.find(',') == std::string::npos
                      ? ""
                      : " Use a dot for the decimal point, not a comma.");
        return current;
    }
    return UseSanitized(key, raw, sanitize(raw));
}

// `shipped_default` is the default of the key being read, not one shared by
// both smoothing keys: LocalSmoothing falls back to 0.0, RemoteSmoothing to
// 0.15. A single fallback would answer a malformed RemoteSmoothing with the
// LOCAL default, so a phone on WiFi would get no smoothing at all on raw
// network jitter, which is the one case RemoteSmoothing exists to cover.
float ReadSmoothing(const cameraunlock::IniReader& ini, const char* section,
                           const char* key, float current, float shipped_default) {
    return ReadFloatValue(ini, section, key, current, [shipped_default](float raw) {
        return SanitizeSmoothing(raw, shipped_default);
    });
}

// Once per retired key that is actually present, so a config carrying both
// [Rotation] Smoothing and [Position] Smoothing is told about both. A
// process-wide "warn once" here named only the first of the two, and the player
// who fixed that one had to launch again to be told about the other.
//
// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& ini,
                                    const char* section, const char* key) {
    if (ini.ReadString(section, key, "").empty()) return;
    Log::Line(
        "[config] key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

// Virtual key codes are published as hex and that is how the shipped INI writes
// them, so that is how they are read: a bare 24 is 0x24, not 36. IniReader's
// ReadHex cannot tell an absent key from an unreadable one, and both matter
// here - the first is the common case, the second is a user who typed a key
// name and needs to be told it is codes only.
bool ParseVirtualKey(const std::string& text, int& out) {
    const char* start = text.c_str();
    if (text.size() > 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        start += 2;
    }
    char* end = nullptr;
    const long value = std::strtol(start, &end, 16);
    if (end == start) return false;

    // The whole value has to be the code, not just the front of it. Half the
    // key names a user would try are made of hex digits - "End" reads as 0xE
    // and "Delete" as 0xDE, both perfectly bindable keys and neither the one
    // that was asked for. Only trailing space and a comment are allowed past
    // the number.
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0' && *end != ';' && *end != '#') return false;

    out = static_cast<int>(value);
    return true;
}

// A key the mod refused leaves that action on its previous binding rather than
// on nothing, so a mistyped code costs the user one hotkey and says so.
int ReadKey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const std::string text = ini.ReadString("Hotkeys", key, "");
    if (text.empty()) return fallback;

    int raw = 0;
    if (!ParseVirtualKey(text, raw)) {
        Log::Line("[config] %s=%s is not a virtual key code (0x24, or 24 read as hex); "
                  "keeping 0x%X", key, text.c_str(), fallback);
        return fallback;
    }
    // Core's, not a local copy. The mod carried a verbatim duplicate of this
    // function down to the comment, in a translation unit that already includes
    // core's declaration, so a correction to the fleet's rule would silently not
    // have reached this mod.
    if (!cameraunlock::config::IsBindableVirtualKey(raw)) {
        Log::Line("[config] %s=%s is not a key that can be bound; keeping 0x%X",
                  key, text.c_str(), fallback);
        return fallback;
    }
    return raw;
}

float ReadLimit(const cameraunlock::IniReader& ini, const char* key, float current) {
    return ReadFloatValue(ini, "Position", key, current, [current](float raw) {
        return SanitizePositionLimit(raw, current);
    });
}

// GetPrivateProfileString answers with the FIRST section of a given name, and
// the first occurrence of a key inside it. A second [Position] block - the
// natural thing to write when pasting a value out of a forum post, rather than
// hunting for the block already in the file - is therefore read by nobody, and
// nothing anywhere says so. The mod cannot honour it without replacing the
// parser, so it reports it instead: a setting the user can see being ignored is
// the whole difference between a triageable report and an untriageable one.
//
// A separate pass over the file rather than something folded into the readers,
// because the readers ask for one key at a time and this question is about the
// shape of the whole file.

// Matching is case-insensitive because GetPrivateProfileString matches that
// way. Only the matching: the user is always shown the spelling that is in
// their file, since the whole point of the line is to send them to it and a
// lowercased name they cannot search for does not.
std::string Folded(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string Trimmed(const std::string& text) {
    constexpr char kSpace[] = " \t\r\n";
    const std::size_t first = text.find_first_not_of(kSpace);
    if (first == std::string::npos) return std::string();
    return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
}

void WarnAboutDuplicateEntries(const std::string& path) {
    std::ifstream file(path);
    if (!file) return;

    std::vector<std::string> sections;
    std::vector<std::pair<std::string, std::string>> keys;
    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        // ';' only. GetPrivateProfileString does not treat '#' as a comment
        // introducer, so to the parser that actually reads this file a line
        // starting with one is a key whose name begins with '#'. Skipping it
        // here would make this scan and that parser disagree about the file.
        const std::string text = Trimmed(line);
        if (text.empty() || text[0] == ';') continue;

        if (text[0] == '[') {
            const std::size_t close = text.find(']');
            if (close == std::string::npos) continue;
            const std::string name = Trimmed(text.substr(1, close - 1));
            section = Folded(name);
            if (section.empty()) continue;
            if (std::find(sections.begin(), sections.end(), section) != sections.end()) {
                Log::Line("[config] %s has more than one [%s] section. Only the FIRST one is "
                          "read - every key in the later one is ignored. Merge them.",
                          kIniName, name.c_str());
            } else {
                sections.push_back(section);
            }
            continue;
        }

        const std::size_t equals = text.find('=');
        if (equals == std::string::npos) continue;

        // A key with no name, or one written above the first section header, is
        // not something GetPrivateProfileString can read either, so there is no
        // ignored setting to report.
        const std::string name = Trimmed(text.substr(0, equals));
        if (name.empty() || section.empty()) continue;

        const std::pair<std::string, std::string> entry(section, Folded(name));
        if (std::find(keys.begin(), keys.end(), entry) != keys.end()) {
            Log::Line("[config] %s sets %s more than once in the same section. Only the FIRST "
                      "one is read.", kIniName, name.c_str());
        } else {
            keys.push_back(entry);
        }
    }
}

// One reader per INI section, in the order the shipped file lays them out. Each
// takes the whole Config because a section owns a contiguous run of its fields,
// and each key keeps whatever `out` already held when the file does not say
// otherwise - which is what makes a default-constructed Config yield the
// shipped defaults.

void ReadNetworkSection(const cameraunlock::IniReader& ini, Config& out) {
    bool portValid = false;
    const int read = ini.ReadInt("Network", "UdpPort", out.udp_port);
    out.udp_port = cameraunlock::NormalizeUdpPort(read, out.udp_port, portValid);
    if (portValid) return;

    // Which of the two things went wrong is decided by re-parsing the raw text,
    // not by the value ReadInt handed back. GetPrivateProfileIntA answers 0 for
    // anything it cannot read, so `UdpPort=abc` and `UdpPort=0` arrive here
    // identical, and reporting both as a range error names a cause the input
    // does not support. This was also the one refusal in the file that never
    // echoed what the player actually wrote.
    //
    // Read through core's ReadRawValue, the same reader the float path uses, so
    // an inline comment is truncated and the value trimmed. Plain ReadString
    // hands back `4242 ; my port` whole.
    const std::string text = cameraunlock::config::ReadRawValue(ini, "Network", "UdpPort");
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    (void)parsed;
    const bool numeric = !text.empty() && end != nullptr && *end == '\0';
    if (numeric) {
        Log::Line("[config] UdpPort=%s is outside 1024-65535; using %u", text.c_str(),
                  static_cast<unsigned>(out.udp_port));
    } else {
        Log::Line("[config] UdpPort=%s is not a whole number; using %u", text.c_str(),
                  static_cast<unsigned>(out.udp_port));
    }
}

void ReadGeneralSection(const cameraunlock::IniReader& ini, Config& out) {
    out.enable_on_startup = ReadFlag(ini, "General", "EnableOnStartup", out.enable_on_startup);
    out.world_space_yaw   = ReadFlag(ini, "General", "WorldSpaceYaw",   out.world_space_yaw);
}

struct Binding {
    const char* key;
    int code;
};

// ReadKey validates each binding on its own, and nothing downstream rejects two
// actions landing on the same code: the poller keys on nothing, so a duplicate
// simply runs both handlers off one press. That was the only INI mistake in this
// file with nothing at all in the log.
//
// Within a group only. A nav key and a chord letter that happen to match are
// fine and must not be reported: the nav bindings are suppressed while
// Ctrl+Shift is held and the chord bindings need it, so the two can never fire
// off the same press.
void WarnAboutCollidingHotkeys(const Binding* bindings, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) {
            if (bindings[i].code != bindings[j].code) continue;
            Log::Line("[config] %s and %s are both 0x%X, so one press runs both actions; "
                      "give them different keys",
                      bindings[i].key, bindings[j].key, bindings[i].code);
        }
    }
}

void ReadHotkeysSection(const cameraunlock::IniReader& ini, Config& out) {
    out.toggle_key            = ReadKey(ini, "ToggleKey",         out.toggle_key);
    out.cycle_mode_key        = ReadKey(ini, "CycleModeKey",      out.cycle_mode_key);
    out.yaw_mode_key          = ReadKey(ini, "YawModeKey",        out.yaw_mode_key);
    out.chord_toggle_key      = ReadKey(ini, "ChordToggleKey",    out.chord_toggle_key);
    out.chord_cycle_mode_key  = ReadKey(ini, "ChordCycleModeKey", out.chord_cycle_mode_key);
    out.chord_yaw_mode_key    = ReadKey(ini, "ChordYawModeKey",   out.chord_yaw_mode_key);

    const Binding nav[] = {
        {"ToggleKey", out.toggle_key},
        {"CycleModeKey", out.cycle_mode_key},
        {"YawModeKey", out.yaw_mode_key},
    };
    const Binding chord[] = {
        {"ChordToggleKey", out.chord_toggle_key},
        {"ChordCycleModeKey", out.chord_cycle_mode_key},
        {"ChordYawModeKey", out.chord_yaw_mode_key},
    };
    WarnAboutCollidingHotkeys(nav, sizeof(nav) / sizeof(nav[0]));
    WarnAboutCollidingHotkeys(chord, sizeof(chord) / sizeof(chord[0]));
}

void ReadRotationSection(const cameraunlock::IniReader& ini, Config& out) {
    out.local_smoothing    = ReadSmoothing(ini, "Rotation", "LocalSmoothing",  out.local_smoothing,
                                           kDefaultLocalSmoothing);
    out.remote_smoothing   = ReadSmoothing(ini, "Rotation", "RemoteSmoothing", out.remote_smoothing,
                                           kDefaultRemoteSmoothing);

    // Both sections carried the retired key, so both are warned about here
    // rather than from the section each one sits in - the warning is about the
    // key having been replaced, not about where it was written.
    WarnRetiredSmoothingKey(ini, "Rotation", "Smoothing");
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");
}

void ReadPositionSection(const cameraunlock::IniReader& ini, Config& out) {
    out.position_enabled   = ReadFlag(ini, "Position", "Enabled",          out.position_enabled);
    out.limit_x            = ReadLimit(ini, "LimitX",     out.limit_x);
    out.limit_y            = ReadLimit(ini, "LimitY",     out.limit_y);
    out.limit_z            = ReadLimit(ini, "LimitZ",     out.limit_z);
    out.limit_z_back       = ReadLimit(ini, "LimitZBack", out.limit_z_back);
}

}  // namespace

void LoadConfig(const std::string& exe_dir, Config& out) {
    const std::string path = IniPath(exe_dir);
    cameraunlock::IniReader ini;
    if (!ini.Open(path)) {
        Log::Line("[config] could not open %s - using built-in defaults", path.c_str());
        return;
    }

    WarnAboutDuplicateEntries(path);

    ReadNetworkSection(ini, out);
    ReadGeneralSection(ini, out);
    ReadHotkeysSection(ini, out);
    ReadRotationSection(ini, out);
    ReadPositionSection(ini, out);
}

void WriteDefaultConfigIfMissing(const std::string& exe_dir) {
    const std::string path = IniPath(exe_dir);

    // CREATE_NEW rather than "does it exist?" followed by a truncating open: the
    // two steps can straddle a file the user (or a second launch) writes in
    // between, and never overwriting a user's config is the whole promise here.
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_EXISTS) return;
        Log::Line("[config] could not create %s (%lu) - the game directory is not writable. "
                  "Built-in defaults are in use and edits there will not be read.",
                  path.c_str(), error);
        return;
    }

    // A short write leaves a file that parses as a config but is missing keys,
    // which then reads as "the mod ignores my setting". Say so instead.
    constexpr DWORD kTextBytes = static_cast<DWORD>(sizeof(kDefaultIniText) - 1);
    DWORD written = 0;
    const BOOL ok = WriteFile(file, kDefaultIniText, kTextBytes, &written, nullptr);
    const DWORD writeError = GetLastError();
    CloseHandle(file);
    if (!ok || written != kTextBytes) {
        Log::Line("[config] %s was created but only %lu of %lu bytes could be written (%lu); "
                  "delete it and restart the game for a complete default config.",
                  path.c_str(), written, kTextBytes, writeError);
    }
}

}  // namespace wolf_ht
