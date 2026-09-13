// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <atomic>

#include "cameraunlock/ads/ads_blend.h"
#include "cameraunlock/ads/ads_fade.h"
#include "cameraunlock/ads/ads_mode.h"
#include "cameraunlock/ads/entry_pose.h"

namespace wolf_ht {

// What head tracking does while the iron sights or a scope are up.
//
// The cycle, the value strings, the toast wording, the fade shape and the
// entry-relative pose all come from cameraunlock-core; this class is the wiring
// that holds them together for one mod - the mode, and the two calls the render
// thread makes each frame. Nothing here restates a constant or a string that
// core already owns.
//
// No file, no clock and no logging, so the whole ADS behaviour is exercisable
// frame by frame in tests/ads_tests.cpp. Writing the mode back to the INI and
// announcing it are the caller's, in HeadTrackingMod::CycleAdsMode.
//
// Wolfenstein is a THREE-SLOT game: raising the sights hides the HUD crosshair
// and hands the player irons or an optic, and several weapons are scoped, so
// there is no aim indicator left on screen for a mod's reticle compensation to
// move onto the impact point. See the Controls section of README.md for what
// `marker` does in this mod today.
class AdsController {
public:
    void Start(cameraunlock::ads::AdsMode mode) { m_mode.store(mode); }

    cameraunlock::ads::AdsMode Mode() const { return m_mode.load(); }

    // Advances the cycle and answers with the mode it reached.
    //
    // There is no separate "re-run the verdict" step around this because there
    // is no cached verdict to re-run: the gate is evaluated from Mode() on every
    // rendered frame, so a change made mid-aim lands on the next frame of that
    // same aim rather than on the next one.
    cameraunlock::ads::AdsMode Cycle() {
        const cameraunlock::ads::AdsMode next = cameraunlock::ads::NextAdsMode(m_mode.load());
        m_mode.store(next);
        return next;
    }

    // Once per rendered frame, on a frame the gate lets the pose through.
    // `absolute` is this frame's pose at the engine boundary - id Tech degrees
    // and inches - and `live` says the rotation is a real sample rather than the
    // nothing a dropped tracker publishes.
    cameraunlock::ads::AdsEntryPose::Pose Apply(
            bool aiming, bool live, const cameraunlock::ads::AdsEntryPose::Pose& absolute,
            unsigned long long nowMs) {
        // The sights, straight from the game's own aim state - never the gate's
        // verdict. In `paused` the gate answer is derived FROM this transition,
        // so feeding it back in would make the fade chase itself.
        const float scale = m_fade.Update(aiming, nowMs);
        // The entry pose has to OUTLIVE the aim by the length of the ride back.
        // Core's AdsEntryPose drops it the moment `aiming` goes false and then
        // returns the absolute pose, which in the tracked modes makes the blend
        // interpolate a value with itself - `absolute * scale + absolute * rest`
        // is `absolute` at every scale - so the 250ms return is inert and
        // lowering the weapon steps the view by the whole entry angle in one
        // frame. Holding it until the fade is home is what makes the way out the
        // mirror of the way in. A scale below 1 is exactly "not back at the hip
        // yet"; `paused` ignores `relative` either way.
        //
        // It also has to be captured on the first aiming frame whatever the mode
        // is, or switching to a tracked mode mid-aim would have nothing to
        // measure from and the view would jump by the whole head angle.
        //
        // A re-aim that lands INSIDE the ride back therefore keeps the entry it
        // already has, and the second aim carries on from the first one's
        // reference rather than re-centring on the barrel. That is deliberate,
        // and it is the lesser of two defects rather than a free win. Dropping
        // the entry on the rising edge instead does re-centre the sights, and
        // costs a measured 29.6 degree step in a single frame on the commonest
        // input there is - a tap of the aim button with the head turned. The
        // blend is `absolute * scale + relative * (1 - scale)`, so swapping what
        // `relative` is measured from moves the pose by the whole head travel
        // since the old entry, at a moment when `1 - scale` is still ~1. AdsFade
        // cannot cushion it: it sizes each leg by the distance the SCALE has to
        // travel, which just after a release is ~0.01, giving a 2ms leg.
        //
        // So the entry survives until the ride is genuinely home, which is what
        // `scale < 1.0f` says, and a re-aim after that re-centres normally.
        const bool holdEntry = aiming || scale < 1.0f;
        const cameraunlock::ads::AdsEntryPose::Pose relative =
            m_entry.Relative(holdEntry, live, absolute);
        return cameraunlock::ads::BlendAdsPose(m_mode.load(), scale, absolute, relative);
    }

    // Once per rendered frame the gate closes for any reason OTHER than the
    // sights being up: a menu, a video, a level load, the master toggle. Drops
    // the entry pose and puts the transition back at the hip, so the next aim
    // re-enters against that frame's head rather than one from before the
    // suppression.
    //
    // A tracker dropout is deliberately not on that list and does not reach
    // here. TrackerFeed holds the last known pose across a gap rather than
    // withdrawing it, so the gate stays open, `live` stays true, and there is no
    // stale entry to drop - the pose the aim is measured against never went
    // away.
    void Suppress() {
        m_fade.Reset();
        m_entry.Reset();
    }

private:
    std::atomic<cameraunlock::ads::AdsMode> m_mode{cameraunlock::ads::kDefaultAdsMode};

    // Render thread only, both of them.
    cameraunlock::ads::AdsFade m_fade;
    cameraunlock::ads::AdsEntryPose m_entry;
};

}  // namespace wolf_ht
