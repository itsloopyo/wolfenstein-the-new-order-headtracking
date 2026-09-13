// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "hotkeys.h"

#include <system_error>

#include "cameraunlock/input/chord_hotkeys.h"
#include "config.h"
#include "headtracking_mod.h"
#include "logging.h"

namespace wolf_ht {

namespace {

// ~60Hz. Fast enough that a tap is never missed, slow enough that the thread is
// invisible next to the game.
constexpr int kPollIntervalMs = 16;

}  // namespace

void Hotkeys::Start(HeadTrackingMod& mod, const Config& config) {
    using cameraunlock::input::ChordGuarded;
    using cameraunlock::input::NavGuarded;

    // Each action is one call and nothing else: the mod method owns both the
    // state change and the log line, so a nav key and its chord cannot drift
    // apart or report the transition twice.
    const auto toggle = [&mod]() { mod.ToggleEnabled(); };
    const auto cycleMode = [&mod]() { mod.CycleTrackingMode(); };
    const auto yawMode = [&mod]() { mod.ToggleYawMode(); };
    const auto adsMode = [&mod]() { mod.CycleAdsMode(); };

    // The nav bindings are suppressed while Ctrl+Shift is held, so the chord
    // path is the sole trigger for a chord and a nav key rebound onto a chord
    // letter cannot fire twice.
    m_poller.SetToggleKey(config.toggle_key, NavGuarded(toggle));
    m_poller.AddHotkey(config.cycle_mode_key, NavGuarded(cycleMode));
    m_poller.AddHotkey(config.yaw_mode_key, NavGuarded(yawMode));
    m_poller.AddHotkey(config.ads_mode_key, NavGuarded(adsMode));

    m_poller.AddHotkey(config.chord_toggle_key, ChordGuarded(toggle));
    m_poller.AddHotkey(config.chord_cycle_mode_key, ChordGuarded(cycleMode));
    m_poller.AddHotkey(config.chord_yaw_mode_key, ChordGuarded(yawMode));
    m_poller.AddHotkey(config.chord_ads_mode_key, ChordGuarded(adsMode));

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
    Log::Line("[hotkeys] toggle 0x%X / Ctrl+Shift+0x%X, cycle mode 0x%X / Ctrl+Shift+0x%X, "
              "yaw mode 0x%X / Ctrl+Shift+0x%X, ADS mode 0x%X / Ctrl+Shift+0x%X",
              config.toggle_key, config.chord_toggle_key,
              config.cycle_mode_key, config.chord_cycle_mode_key,
              config.yaw_mode_key, config.chord_yaw_mode_key,
              config.ads_mode_key, config.chord_ads_mode_key);
}

}  // namespace wolf_ht
