#pragma once

// Stands in for the published build's headtracking_mod.h in the hotkey oracle library only: the
// four actions the published Hotkeys::Start binds, each counting its calls so the test can see
// which action a key press ran.

namespace wolf_ht {

class HeadTrackingMod {
public:
    int toggles = 0;
    int cycles = 0;
    int yaw_toggles = 0;
    int ads_cycles = 0;

    void ToggleEnabled() { ++toggles; }
    void CycleTrackingMode() { ++cycles; }
    void ToggleYawMode() { ++yaw_toggles; }
    void CycleAdsMode() { ++ads_cycles; }
};

}  // namespace wolf_ht
