// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include "cameraunlock/ads/ads_mode.h"

namespace wolf_ht {

// Why the frame's head pose is or is not applied, and whether the sights are up
// while it is decided.
//
// A pure function of four booleans and the ADS mode, so the whole walk can be
// exercised without a game. That matters most for the two orderings below,
// neither of which is visible from anywhere else:
//
//  - **ADS is tested LAST.** A player who opens a menu with the sights still up
//    is in a menu, and the log line has to say so; testing ADS first would
//    report "aiming" for a screen nobody is aiming at.
//  - **Every earlier answer leaves the aiming flag false.** The flag drives
//    render-side work, and a stale one left set through a menu keeps that work
//    running against a camera nobody is looking through.
enum class GateReason {
    // Nothing suppresses tracking. Also the answer while the sights are up in
    // the two tracked ADS modes, which do not suppress anything.
    Gameplay,
    // No build profile matched, so the mod is dormant.
    NoBuildProfile,
    // The shell, a level load, a bink video, the pause menu, or an alt-tab.
    NotGameplay,
    // The player switched tracking off.
    Disabled,
    // The sights are up and the ADS mode is `paused`.
    AdsPaused,
};

struct GateInputs {
    bool have_profile = false;
    bool gameplay = false;
    bool enabled = false;
    bool aiming = false;
    cameraunlock::ads::AdsMode ads_mode = cameraunlock::ads::kDefaultAdsMode;
};

struct GateVerdict {
    GateReason reason = GateReason::NoBuildProfile;
    // The sights being up is reported even under AdsPaused, where tracking is
    // standing down: the reason says whether tracking applies, this says what
    // the weapon is doing, and the ADS fade needs the second one.
    bool aiming = false;
};

inline GateVerdict EvaluateGate(const GateInputs& in) {
    GateVerdict out;
    if (!in.have_profile) {
        out.reason = GateReason::NoBuildProfile;
        return out;
    }
    if (!in.gameplay) {
        out.reason = GateReason::NotGameplay;
        return out;
    }
    if (!in.enabled) {
        out.reason = GateReason::Disabled;
        return out;
    }

    out.aiming = in.aiming;
    out.reason = (in.aiming && cameraunlock::ads::AdsSuspendsTracking(in.ads_mode))
                     ? GateReason::AdsPaused
                     : GateReason::Gameplay;
    return out;
}

// Whether the head pose still reaches the camera under this reason.
//
// AdsPaused is open, which looks wrong until you follow what closing it would
// do. The camera hook here rebuilds the render view from the game's own view
// every frame and puts the game's view straight back, so writing a pose of zero
// is byte for byte what writing nothing does - there is nothing baked into the
// camera for a closed gate to peel off. That leaves the ADS fade free to run the
// pose down to zero over its 150ms rather than the gate cutting it in one frame,
// which is the jolt the fade exists to remove.
inline bool PoseApplies(GateReason reason) {
    return reason == GateReason::Gameplay || reason == GateReason::AdsPaused;
}

inline const char* GateReasonText(GateReason reason) {
    switch (reason) {
        case GateReason::Gameplay:       return "gameplay";
        case GateReason::NoBuildProfile: return "no build profile";
        case GateReason::NotGameplay:    return "not in gameplay";
        case GateReason::Disabled:       return "tracking switched off";
        case GateReason::AdsPaused:      return "aiming down sights, ADS mode is paused";
    }
    return "?";
}

}  // namespace wolf_ht
