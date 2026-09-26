// Compiled into the hotkey oracle library only, with `cameraunlock` and `wolf_ht` renamed, so
// "hotkeys.h" and "config.h" here are the published build's and the poller, keyboard and mod
// under them are oracle_fake's.
#include "config.h"
#include "headtracking_mod.h"
#include "hotkeys.h"
#include "oracle_adapter.h"

#include "cameraunlock/input/hotkey_poller.h"

namespace cameraunlock::input {

int& FakeHeld() {
    static int held = 0;
    return held;
}

std::vector<FakeRegistration>& FakeRegistrations() {
    static std::vector<FakeRegistration> registrations;
    return registrations;
}

}  // namespace cameraunlock::input

namespace wolf_oracle_view {

FireTable OracleFires(const OracleKeys& keys) {
    namespace input = cameraunlock::input;
    wolf_ht::Config c;
    c.toggle_key = keys.toggle;
    c.cycle_mode_key = keys.cycle_mode;
    c.yaw_mode_key = keys.yaw_mode;
    c.ads_mode_key = keys.ads_mode;
    c.chord_toggle_key = keys.chord_toggle;
    c.chord_cycle_mode_key = keys.chord_cycle_mode;
    c.chord_yaw_mode_key = keys.chord_yaw_mode;
    c.chord_ads_mode_key = keys.chord_ads_mode;
    wolf_ht::HeadTrackingMod mod;

    input::FakeRegistrations().clear();
    wolf_ht::Hotkeys hotkeys;
    hotkeys.Start(mod, c);
    const std::vector<input::FakeRegistration> registered = input::FakeRegistrations();

    FireTable table;
    table.reserve((kLastKey - kFirstKey + 1) * kHeldStates);
    for (int vk = kFirstKey; vk <= kLastKey; ++vk) {
        for (int held = 0; held < kHeldStates; ++held) {
            mod = wolf_ht::HeadTrackingMod{};
            input::FakeHeld() = held;
            for (const input::FakeRegistration& r : registered) {
                if (r.vk == vk && r.callback) r.callback();
            }
            table.push_back({mod.toggles, mod.cycles, mod.yaw_toggles, mod.ads_cycles});
        }
    }
    input::FakeHeld() = 0;
    return table;
}

}  // namespace wolf_oracle_view
