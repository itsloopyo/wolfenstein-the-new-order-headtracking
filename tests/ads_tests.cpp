// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
//
// The two aim-down-sights suites the shared ADS spec asks every mod for.
//
// Neither is reachable from a game session. The entry-pose rules are invisible
// unless you happen to aim while your head is within ten degrees of the yaw
// seam, or open a menu mid-aim; the gate ordering is invisible unless a menu and
// the sights are up at the same moment. Both were wrong in the first cut of the
// reference mod, and both are cheap to lock here.

#include "ads.h"
#include "tracking_gate.h"

#include "check_harness.h"

#include <cmath>

namespace {

using checks::Check;
using checks::CheckNear;

using cameraunlock::ads::AdsEntryPose;
using cameraunlock::ads::AdsFade;
using cameraunlock::ads::AdsMode;
using wolf_ht::EvaluateGate;
using wolf_ht::GateInputs;
using wolf_ht::GateReason;
using wolf_ht::GateVerdict;
using wolf_ht::PoseApplies;

AdsEntryPose::Pose MakePose(float pitch, float yaw, float roll, float x, float y, float z) {
    AdsEntryPose::Pose p;
    p.pitch = pitch;
    p.yaw = yaw;
    p.roll = roll;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

// ---- Entry pose --------------------------------------------------------------

void TestHipFirePassesThrough() {
    AdsEntryPose entry;
    const AdsEntryPose::Pose absolute = MakePose(7.0f, 21.0f, -3.0f, 1.0f, 2.0f, 3.0f);
    const AdsEntryPose::Pose out = entry.Relative(false, true, absolute);

    CheckNear(out.pitch, 7.0f, 1e-5f, "hip fire passes pitch through");
    CheckNear(out.yaw, 21.0f, 1e-5f, "hip fire passes yaw through");
    CheckNear(out.roll, -3.0f, 1e-5f, "hip fire passes roll through");
    CheckNear(out.x, 1.0f, 1e-5f, "hip fire passes x through");
    CheckNear(out.y, 2.0f, 1e-5f, "hip fire passes y through");
    CheckNear(out.z, 3.0f, 1e-5f, "hip fire passes z through");
    Check(!entry.HasEntry(), "hip fire holds no entry pose");
}

void TestEntryFrameIsIdentityInYawPitchAndPosition() {
    AdsEntryPose entry;
    const AdsEntryPose::Pose absolute = MakePose(7.0f, 21.0f, -3.0f, 1.0f, 2.0f, 3.0f);
    const AdsEntryPose::Pose out = entry.Relative(true, true, absolute);

    CheckNear(out.pitch, 0.0f, 1e-5f, "entry frame zeroes pitch");
    CheckNear(out.yaw, 0.0f, 1e-5f, "entry frame zeroes yaw");
    CheckNear(out.x, 0.0f, 1e-5f, "entry frame zeroes x");
    CheckNear(out.y, 0.0f, 1e-5f, "entry frame zeroes y");
    CheckNear(out.z, 0.0f, 1e-5f, "entry frame zeroes z");
}

// Zeroing roll would level a head tilt the player is actively holding and lean
// it back in as they move: two horizon jolts per aim, and it moves no aim point.
void TestRollIsNeverMadeRelative() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(0.0f, 0.0f, 12.0f, 0, 0, 0));
    const AdsEntryPose::Pose out = entry.Relative(true, true, MakePose(0.0f, 0.0f, 20.0f, 0, 0, 0));
    CheckNear(out.roll, 20.0f, 1e-5f, "roll stays absolute through the aim");
}

void TestPositionGoesRelative() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(0, 0, 0, 1.0f, 2.0f, 3.0f));
    const AdsEntryPose::Pose out = entry.Relative(true, true, MakePose(0, 0, 0, 1.5f, 2.5f, 4.0f));
    CheckNear(out.x, 0.5f, 1e-5f, "x is measured from the entry frame");
    CheckNear(out.y, 0.5f, 1e-5f, "y is measured from the entry frame");
    CheckNear(out.z, 1.0f, 1e-5f, "z is measured from the entry frame");
}

// A plain subtraction reads this 20 degree move as -340 and whips the view a
// full turn the wrong way.
void TestYawCrossesTheSeamTheShortWay() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(0.0f, 170.0f, 0.0f, 0, 0, 0));
    const AdsEntryPose::Pose out =
        entry.Relative(true, true, MakePose(0.0f, -170.0f, 0.0f, 0, 0, 0));
    CheckNear(out.yaw, 20.0f, 1e-4f, "yaw crosses the -180/180 seam the short way");

    // And the other way round.
    AdsEntryPose back;
    back.Relative(true, true, MakePose(0.0f, -170.0f, 0.0f, 0, 0, 0));
    const AdsEntryPose::Pose out2 =
        back.Relative(true, true, MakePose(0.0f, 170.0f, 0.0f, 0, 0, 0));
    CheckNear(out2.yaw, -20.0f, 1e-4f, "the seam is short in both directions");
}

// Interpolators publish nothing on a suppressed frame. Capturing then freezes a
// pre-suppression pose and holds the whole aim at that offset.
void TestCaptureIsGatedOnALiveRotation() {
    AdsEntryPose entry;
    const AdsEntryPose::Pose stale = MakePose(5.0f, 5.0f, 0.0f, 0, 0, 0);
    const AdsEntryPose::Pose out = entry.Relative(true, false, stale);
    Check(!entry.HasEntry(), "no entry pose is captured from a dead rotation");
    CheckNear(out.yaw, 5.0f, 1e-5f, "a dead rotation passes through untouched");

    const AdsEntryPose::Pose fresh = MakePose(9.0f, 9.0f, 0.0f, 0, 0, 0);
    const AdsEntryPose::Pose out2 = entry.Relative(true, true, fresh);
    Check(entry.HasEntry(), "the first live frame captures the entry pose");
    CheckNear(out2.yaw, 0.0f, 1e-5f, "the entry pose is the first LIVE frame, not the stale one");
}

void TestLoweringTheWeaponDropsTheEntryPose() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(0.0f, 30.0f, 0.0f, 0, 0, 0));
    Check(entry.HasEntry(), "aiming holds an entry pose");

    const AdsEntryPose::Pose down = entry.Relative(false, true, MakePose(0.0f, 30.0f, 0.0f, 0, 0, 0));
    Check(!entry.HasEntry(), "lowering the weapon drops the entry pose");
    CheckNear(down.yaw, 30.0f, 1e-5f, "the absolute pose comes straight back");

    // The next aim measures from where the head is NOW, not from the last aim.
    const AdsEntryPose::Pose again = entry.Relative(true, true, MakePose(0.0f, 30.0f, 0.0f, 0, 0, 0));
    CheckNear(again.yaw, 0.0f, 1e-5f, "the next aim re-enters from the current head pose");
}

// The entry pose has to outlive the aim by the length of the ride back: dropped
// a frame early and the relative pose becomes the absolute pose, so the return
// ramp interpolates a value with itself and the view steps by the whole entry
// offset in one frame.
void TestSuppressionResetsTheEntryPose() {
    wolf_ht::AdsController ads;
    ads.Start(AdsMode::Tracked);

    const AdsEntryPose::Pose held = MakePose(0.0f, 40.0f, 0.0f, 0, 0, 0);
    unsigned long long now = 1000;
    ads.Apply(true, true, held, now);
    now += AdsFade::kLowerMs + 1;
    const AdsEntryPose::Pose aiming = ads.Apply(true, true, held, now);
    CheckNear(aiming.yaw, 0.0f, 1e-4f, "a settled tracked aim sits on the entry-relative pose");

    // A menu opens: the whole ADS state goes back to the hip.
    ads.Suppress();
    now += 500;
    const AdsEntryPose::Pose after = ads.Apply(true, true, held, now);
    CheckNear(after.yaw, 40.0f, 1e-4f,
              "a suppression drops the entry pose and the fade, so the aim re-enters clean");
}

// ---- The fade, as this mod drives it ----------------------------------------

void TestPausedFadesThePoseAwayAndBack() {
    wolf_ht::AdsController ads;
    ads.Start(AdsMode::Paused);

    const AdsEntryPose::Pose held = MakePose(10.0f, 40.0f, 6.0f, 0.0f, 0.0f, 2.0f);
    unsigned long long now = 1000;
    const AdsEntryPose::Pose hip = ads.Apply(false, true, held, now);
    CheckNear(hip.yaw, 40.0f, 1e-4f, "paused leaves the hip pose alone");

    const AdsEntryPose::Pose entering = ads.Apply(true, true, held, now);
    CheckNear(entering.yaw, 40.0f, 1e-4f, "the aim does not cut the pose on its first frame");

    now += AdsFade::kLowerMs + 1;
    const AdsEntryPose::Pose down = ads.Apply(true, true, held, now);
    CheckNear(down.yaw, 0.0f, 1e-4f, "paused has run the pose to nothing by the end of the fade");
    CheckNear(down.pitch, 0.0f, 1e-4f, "paused runs pitch down too");
    CheckNear(down.z, 0.0f, 1e-4f, "paused runs the lean down with the rotation");
    CheckNear(down.roll, 6.0f, 1e-4f, "roll is in neither fade - a tilt is not levelled by aiming");

    now += 10;
    ads.Apply(false, true, held, now);
    now += AdsFade::kRaiseMs + 1;
    const AdsEntryPose::Pose back = ads.Apply(false, true, held, now);
    CheckNear(back.yaw, 40.0f, 1e-4f, "lowering the weapon eases the whole pose back");
}

void TestTrackedFadesIntoTheEntryRelativePose() {
    wolf_ht::AdsController ads;
    ads.Start(AdsMode::Tracked);

    unsigned long long now = 1000;
    ads.Apply(true, true, MakePose(0.0f, 40.0f, 0.0f, 0, 0, 0), now);
    now += AdsFade::kLowerMs + 1;
    CheckNear(ads.Apply(true, true, MakePose(0.0f, 40.0f, 0.0f, 0, 0, 0), now).yaw, 0.0f, 1e-4f,
              "a tracked aim arrives at the same place a paused one does");

    // And then tracks on from there.
    CheckNear(ads.Apply(true, true, MakePose(0.0f, 55.0f, 0.0f, 0, 0, 0), now).yaw, 15.0f, 1e-4f,
              "a tracked aim keeps tracking from the entry frame");
}

// The way out has to mirror the way in. Core's AdsEntryPose drops the entry the
// instant `aiming` goes false and then returns the absolute pose, which makes
// the tracked-mode blend interpolate a value with itself - so the whole 250ms
// return does nothing and the view steps by the entry offset in one frame.
// AdsController holds the entry alive until the fade is home; without that hold
// the first check below reads 55 instead of 15.
void TestLoweringInTrackedModeDoesNotStepThePose() {
    wolf_ht::AdsController ads;
    ads.Start(AdsMode::Tracked);

    const AdsEntryPose::Pose entry = MakePose(0.0f, 40.0f, 0.0f, 0, 0, 0);
    const AdsEntryPose::Pose turned = MakePose(0.0f, 55.0f, 0.0f, 0, 0, 0);

    unsigned long long now = 1000;
    ads.Apply(true, true, entry, now);
    now += AdsFade::kLowerMs + 1;
    const float aiming = ads.Apply(true, true, turned, now).yaw;
    CheckNear(aiming, 15.0f, 1e-4f, "settled aim sits at the entry-relative pose");

    // One frame later, sights down. The pose must not have moved appreciably.
    now += 16;
    const float firstFrameDown = ads.Apply(false, true, turned, now).yaw;
    Check(std::fabs(firstFrameDown - aiming) < 5.0f,
          "lowering does not step the view by the whole entry angle in one frame");

    // And over the length of the raise it eases the rest of the way to the
    // absolute pose rather than arriving there instantly.
    now += AdsFade::kRaiseMs + 1;
    CheckNear(ads.Apply(false, true, turned, now).yaw, 55.0f, 1e-4f,
              "once the raise is done the absolute pose is back");
}

// A re-aim landing inside the 250ms ride back must not STEP the view. The entry
// pose is what `relative` is measured from, so swapping it mid-ride moves the
// pose by the whole head travel since the old entry, at a moment when the blend
// weight on `relative` is still ~1 - and AdsFade cannot cushion that, because it
// sizes each leg by the distance the SCALE has to travel, which just after a
// release is ~0.01. So the entry is held instead, and the second aim carries on
// from the first one's reference.
//
// The cost is real and is the reason this test states the value it does: the
// second aim does NOT re-centre on the barrel. That is the lesser defect, and a
// re-aim after the ride is home re-centres normally - the check below it.
void TestReAimDuringTheRideBackDoesNotStepTheView() {
    wolf_ht::AdsController ads;
    ads.Start(AdsMode::Tracked);

    unsigned long long now = 1000;
    ads.Apply(true, true, MakePose(0.0f, 40.0f, 0.0f, 0, 0, 0), now);
    now += AdsFade::kLowerMs + 1;
    const float settled = ads.Apply(true, true, MakePose(0.0f, 70.0f, 0.0f, 0, 0, 0), now).yaw;
    CheckNear(settled, 30.0f, 1e-4f, "the aim tracks the head away from the entry");

    // Sights down for one frame, then straight back up.
    now += 16;
    const float released = ads.Apply(false, true, MakePose(0.0f, 70.0f, 0.0f, 0, 0, 0), now).yaw;
    now += 16;
    const float reAimed = ads.Apply(true, true, MakePose(0.0f, 70.0f, 0.0f, 0, 0, 0), now).yaw;

    Check(std::fabs(released - settled) < 5.0f, "releasing does not step the view");
    Check(std::fabs(reAimed - released) < 5.0f,
          "a re-aim inside the ride back does not step the view either");
}

// Once the ride back IS home the entry is gone, so the next aim re-centres on
// the barrel the way a first aim does.
void TestAimAfterTheRideIsHomeReCentres() {
    wolf_ht::AdsController ads;
    ads.Start(AdsMode::Tracked);

    unsigned long long now = 1000;
    ads.Apply(true, true, MakePose(0.0f, 40.0f, 0.0f, 0, 0, 0), now);
    now += AdsFade::kLowerMs + 1;
    ads.Apply(true, true, MakePose(0.0f, 70.0f, 0.0f, 0, 0, 0), now);

    // Sights down, and left down long enough for the raise to finish.
    now += 16;
    ads.Apply(false, true, MakePose(0.0f, 70.0f, 0.0f, 0, 0, 0), now);
    now += AdsFade::kRaiseMs + 1;
    ads.Apply(false, true, MakePose(0.0f, 70.0f, 0.0f, 0, 0, 0), now);

    // Aim again: the entry is recaptured at 70, so the settled aim is centred.
    now += 16;
    ads.Apply(true, true, MakePose(0.0f, 70.0f, 0.0f, 0, 0, 0), now);
    now += AdsFade::kLowerMs + 1;
    CheckNear(ads.Apply(true, true, MakePose(0.0f, 70.0f, 0.0f, 0, 0, 0), now).yaw, 0.0f, 1e-4f,
              "an aim after the ride is home re-centres on the barrel");
}

// A tap of the aim button is the most common input there is, and starting each
// leg at its own endpoint makes it remove a fully applied pose in one frame.
void TestTapDoesNotStepThePose() {
    wolf_ht::AdsController ads;
    ads.Start(AdsMode::Paused);

    const AdsEntryPose::Pose held = MakePose(0.0f, 40.0f, 0.0f, 0, 0, 0);
    unsigned long long now = 1000;
    ads.Apply(false, true, held, now);
    now += 16;
    ads.Apply(true, true, held, now);   // pressed
    now += 16;
    const AdsEntryPose::Pose released = ads.Apply(false, true, held, now);  // and let go
    Check(released.yaw > 30.0f,
          "a tap reverses from where the transition got to, it does not step the pose");
}

// ---- The gate ----------------------------------------------------------------

GateInputs Playing(AdsMode mode, bool aiming) {
    GateInputs in;
    in.have_profile = true;
    in.gameplay = true;
    in.enabled = true;
    in.aiming = aiming;
    in.ads_mode = mode;
    return in;
}

void TestPausedClosesTheGateAndStillReportsTheSights() {
    const GateVerdict v = EvaluateGate(Playing(AdsMode::Paused, true));
    Check(v.reason == GateReason::AdsPaused, "paused reports the ADS reason");
    Check(v.aiming, "the sights are reported up even where tracking stands down");
}

void TestTrackedModesLeaveTheGateOpen() {
    const GateVerdict marker = EvaluateGate(Playing(AdsMode::Marker, true));
    Check(marker.reason == GateReason::Gameplay, "marker does not suppress tracking");
    Check(marker.aiming, "marker still reports the sights up");

    const GateVerdict tracked = EvaluateGate(Playing(AdsMode::Tracked, true));
    Check(tracked.reason == GateReason::Gameplay, "tracked does not suppress tracking");
    Check(tracked.aiming, "tracked still reports the sights up");
}

// This mod rebuilds the render view from the game's own view every frame, so a
// pose of zero is what a closed gate would have written anyway. Keeping the gate
// open under AdsPaused is what lets the fade run the pose down over its 150ms
// instead of the gate cutting it in one frame.
void TestPausedKeepsFeedingTheCameraThroughTheAim() {
    Check(PoseApplies(GateReason::AdsPaused),
          "the pose still reaches the camera while a paused aim fades out");
    Check(PoseApplies(GateReason::Gameplay), "ordinary gameplay applies the pose");
    Check(!PoseApplies(GateReason::NotGameplay), "a menu does not apply the pose");
    Check(!PoseApplies(GateReason::Disabled), "the master toggle does not apply the pose");
    Check(!PoseApplies(GateReason::NoBuildProfile), "a dormant mod does not apply the pose");
}

void TestAMenuOutranksTheSights() {
    GateInputs in = Playing(AdsMode::Paused, true);
    in.gameplay = false;
    const GateVerdict v = EvaluateGate(in);
    Check(v.reason == GateReason::NotGameplay, "a menu reports its own reason, not the ADS one");
    Check(!v.aiming, "a menu leaves no stale ADS flag behind");
}

void TestEveryEarlyReturnClearsTheAdsFlag() {
    GateInputs noProfile = Playing(AdsMode::Paused, true);
    noProfile.have_profile = false;
    Check(EvaluateGate(noProfile).reason == GateReason::NoBuildProfile, "no profile is reported");
    Check(!EvaluateGate(noProfile).aiming, "no profile clears the ADS flag");

    GateInputs off = Playing(AdsMode::Paused, true);
    off.enabled = false;
    Check(EvaluateGate(off).reason == GateReason::Disabled, "the master toggle is reported");
    Check(!EvaluateGate(off).aiming, "the master toggle clears the ADS flag");
}

// The engine takes the sights down on paths that fire no event a mod could hook.
// Polling means the state heals on the next frame either way.
void TestTheAdsStateHealsWithoutAnExitEdge() {
    Check(EvaluateGate(Playing(AdsMode::Paused, true)).reason == GateReason::AdsPaused,
          "the sights going up closes the gate");
    Check(EvaluateGate(Playing(AdsMode::Paused, false)).reason == GateReason::Gameplay,
          "the sights going down opens it again with no exit edge needed");
}

void TestNotAimingIsUnaffectedByTheMode() {
    Check(EvaluateGate(Playing(AdsMode::Paused, false)).reason == GateReason::Gameplay,
          "paused does nothing at the hip");
    Check(!EvaluateGate(Playing(AdsMode::Paused, false)).aiming, "the hip reports no sights");
}

}  // namespace

int main() {
    TestHipFirePassesThrough();
    TestEntryFrameIsIdentityInYawPitchAndPosition();
    TestRollIsNeverMadeRelative();
    TestPositionGoesRelative();
    TestYawCrossesTheSeamTheShortWay();
    TestCaptureIsGatedOnALiveRotation();
    TestLoweringTheWeaponDropsTheEntryPose();
    TestSuppressionResetsTheEntryPose();

    TestPausedFadesThePoseAwayAndBack();
    TestTrackedFadesIntoTheEntryRelativePose();
    TestLoweringInTrackedModeDoesNotStepThePose();
    TestReAimDuringTheRideBackDoesNotStepTheView();
    TestAimAfterTheRideIsHomeReCentres();
    TestTapDoesNotStepThePose();

    TestPausedClosesTheGateAndStillReportsTheSights();
    TestTrackedModesLeaveTheGateOpen();
    TestPausedKeepsFeedingTheCameraThroughTheAim();
    TestAMenuOutranksTheSights();
    TestEveryEarlyReturnClearsTheAdsFlag();
    TestTheAdsStateHealsWithoutAnExitEdge();
    TestNotAimingIsUnaffectedByTheMode();

    return checks::Summarize("ads");
}
