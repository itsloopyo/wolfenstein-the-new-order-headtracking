// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "hotkeys.h"

#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "config.h"
#include "headtracking_mod.h"
#include "logging.h"

namespace wolf_ht {

namespace {

// ~60Hz. Fast enough that a tap is never missed, slow enough that the thread is
// invisible next to the game.
constexpr int kPollIntervalMs = 16;

// The owner has already read every list through the hotkey codec, so a list
// that does not parse here is a bug, not a player's typo.
std::vector<cameraunlock::input::KeyBinding> Bindings(const std::string& list) {
    cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error("hotkey list '" + list + "' does not parse: " + parsed.error);
    return std::move(parsed.bindings);
}

}  // namespace

void Hotkeys::Start(HeadTrackingMod& mod, const Config& config) {
    // Each action is one call and nothing else: the mod method owns both the
    // state change and the log line, so two keys in one list cannot drift apart
    // or report the transition twice. A binding with no modifiers does not fire
    // while Ctrl and Shift are both held, so Ctrl+Shift+<key> reaches only a
    // binding that names the chord.
    using cameraunlock::input::RegisterKeyBindings;
    RegisterKeyBindings(m_poller, Bindings(config.toggle_key_name), [&mod]() { mod.ToggleEnabled(); });
    RegisterKeyBindings(m_poller, Bindings(config.cycle_tracking_mode_key_name),
                        [&mod]() { mod.CycleTrackingMode(); });
    RegisterKeyBindings(m_poller, Bindings(config.yaw_mode_key_name), [&mod]() { mod.ToggleYawMode(); });

    // Core's Start never returns false - it returns true early when already
    // running and true at the end - so the failure it does have is a rethrow
    // from the thread constructor, std::system_error being the one worth naming.
    // Catching it here rather than letting it unwind matters: by this point the
    // receiver is bound and the render-view detour is live, so the bootstrap
    // handler's "the mod is dormant, the game is unaffected" would be untrue and
    // the [mod] ready line that triage starts from would never be written.
    try {
        m_poller.Start(kPollIntervalMs);
    } catch (const std::system_error& error) {
        Log::Line("[hotkeys] the polling thread could not start (%s); the keys below will not "
                  "respond and tracking stays on whatever EnableOnStartup asked for",
                  error.what());
        return;
    }
    Log::Line("[hotkeys] toggle [%s], cycle mode [%s], yaw mode [%s]", config.toggle_key_name.c_str(),
              config.cycle_tracking_mode_key_name.c_str(), config.yaw_mode_key_name.c_str());
}

}  // namespace wolf_ht
