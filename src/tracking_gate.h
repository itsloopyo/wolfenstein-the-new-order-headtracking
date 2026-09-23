// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

namespace wolf_ht {

// Why the frame's head pose is or is not applied, and whether the sights are up
// while it is decided.
//
// A pure function of four booleans, so the whole walk can be exercised without
// a game. That matters most for the two orderings below, neither of which is
// visible from anywhere else:
//
//  - **ADS is tested LAST.** A player who opens a menu with the sights still up
//    is in a menu, and the log line has to say so.
//  - **Every earlier answer leaves the aiming flag false.** The flag eases the
//    lean out, and a stale one left set through a menu would bring the next
//    gameplay frame back with the lean still missing.
enum class GateReason {
    // Nothing suppresses tracking. Also the answer while the sights are up:
    // aiming never closes the gate.
    Gameplay,
    // No build profile matched, so the mod is dormant.
    NoBuildProfile,
    // The shell, a level load, a bink video, the pause menu, or an alt-tab.
    NotGameplay,
    // The player switched tracking off.
    Disabled,
};

struct GateInputs {
    bool have_profile = false;
    bool gameplay = false;
    bool enabled = false;
    bool aiming = false;
};

struct GateVerdict {
    GateReason reason = GateReason::NoBuildProfile;
    // The reason says whether tracking applies, this says what the weapon is
    // doing, and the lean fade needs the second one.
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
    out.reason = GateReason::Gameplay;
    return out;
}

inline bool PoseApplies(GateReason reason) { return reason == GateReason::Gameplay; }

inline const char* GateReasonText(GateReason reason) {
    switch (reason) {
        case GateReason::Gameplay:       return "gameplay";
        case GateReason::NoBuildProfile: return "no build profile";
        case GateReason::NotGameplay:    return "not in gameplay";
        case GateReason::Disabled:       return "tracking switched off";
    }
    return "?";
}

}  // namespace wolf_ht
