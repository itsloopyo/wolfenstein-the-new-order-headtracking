// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
//
// Aiming down sights. Head tracking carries straight on through the aim, so
// what is left to lock is small: the lean eases out while the sights are up and
// back when they come down, rotation is never touched on the way, the zoom
// factor scales what it should and nothing else, and the gate reports the
// sights without ever closing on them.

#include "ads.h"
#include "tracking_gate.h"

#include "check_harness.h"

#include <cmath>

namespace {

using checks::Check;
using checks::CheckNear;

using cameraunlock::ads::AdsFade;
using wolf_ht::AdsLeanFade;
using wolf_ht::EvaluateGate;
using wolf_ht::GateInputs;
using wolf_ht::GateReason;
using wolf_ht::HeadPose;
using wolf_ht::PoseApplies;

HeadPose MakePose(float yaw, float pitch, float roll, float x, float y, float z) {
    HeadPose p;
    p.yaw = yaw;
    p.pitch = pitch;
    p.roll = roll;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

void CheckRotationUntouched(const HeadPose& out, const HeadPose& in, const char* what) {
    Check(out.yaw == in.yaw && out.pitch == in.pitch && out.roll == in.roll, what);
}

// ---- The lean fade -----------------------------------------------------------

void TestHipFirePassesThePoseThrough() {
    AdsLeanFade fade;
    const HeadPose pose = MakePose(21.0f, 7.0f, -3.0f, 4.0f, -5.0f, 6.0f);
    const HeadPose out = fade.Apply(false, pose, 1000);
    CheckRotationUntouched(out, pose, "hip fire leaves yaw, pitch and roll alone");
    CheckNear(out.x, 4.0f, 1e-6f, "hip fire leaves x alone");
    CheckNear(out.y, -5.0f, 1e-6f, "hip fire leaves y alone");
    CheckNear(out.z, 6.0f, 1e-6f, "hip fire leaves z alone");
}

void TestSightsUpKeepRotationAndDropTheLean() {
    AdsLeanFade fade;
    const HeadPose pose = MakePose(21.0f, 7.0f, -3.0f, 4.0f, -5.0f, 6.0f);
    unsigned long long now = 1000;
    fade.Apply(true, pose, now);
    now += AdsFade::kLowerMs + 1;
    const HeadPose out = fade.Apply(true, pose, now);
    CheckRotationUntouched(out, pose,
                           "with the sights up, rotation (roll included) is absolute and unscaled");
    CheckNear(out.x, 0.0f, 1e-6f, "with the sights up, x is gone");
    CheckNear(out.y, 0.0f, 1e-6f, "with the sights up, y is gone");
    CheckNear(out.z, 0.0f, 1e-6f, "with the sights up, z is gone");
}

// Raising the sights must not move the view: the first aiming frame is where
// the hip left it.
void TestRaisingTheSightsDoesNotStep() {
    AdsLeanFade fade;
    const HeadPose pose = MakePose(30.0f, 0.0f, 0.0f, 8.0f, 0.0f, 0.0f);
    unsigned long long now = 1000;
    fade.Apply(false, pose, now);
    const HeadPose first = fade.Apply(true, pose, now);
    CheckNear(first.yaw, 30.0f, 1e-6f, "the first aiming frame keeps the head's yaw");
    CheckNear(first.x, 8.0f, 1e-4f, "the first aiming frame has not dropped the lean yet");
}

void TestMidTransitionScalesOnlyTheLean() {
    AdsLeanFade fade;
    const HeadPose pose = MakePose(21.0f, 7.0f, -3.0f, 10.0f, 10.0f, 10.0f);
    unsigned long long now = 1000;
    fade.Apply(true, pose, now);
    now += AdsFade::kLowerMs / 2;
    const HeadPose out = fade.Apply(true, pose, now);
    CheckRotationUntouched(out, pose, "mid-transition, rotation is untouched");
    // Smoothstep at the halfway point is exactly half way.
    CheckNear(out.x, 5.0f, 1e-3f, "mid-transition, x is scaled by the fade");
    CheckNear(out.y, 5.0f, 1e-3f, "mid-transition, y is scaled by the fade");
    CheckNear(out.z, 5.0f, 1e-3f, "mid-transition, z is scaled by the fade");
}

// A tap of the aim button releases a frame after it was pressed. The way back
// has to start from where the way in got to.
void TestAReversalContinuesFromWhereItWas() {
    AdsLeanFade fade;
    const HeadPose pose = MakePose(0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f);
    unsigned long long now = 1000;
    fade.Apply(true, pose, now);
    now += AdsFade::kLowerMs / 2;
    const float halfway = fade.Apply(true, pose, now).x;
    now += 1;
    const float reversed = fade.Apply(false, pose, now).x;
    Check(std::fabs(reversed - halfway) < 0.2f,
          "letting go mid-transition carries on from where the lean was");
    now += AdsFade::kRaiseMs + 1;
    CheckNear(fade.Apply(false, pose, now).x, 10.0f, 1e-6f, "the lean is all the way back");
}

void TestLoweringTheSightsBringsTheLeanBack() {
    AdsLeanFade fade;
    const HeadPose pose = MakePose(15.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f);
    unsigned long long now = 1000;
    fade.Apply(true, pose, now);
    now += AdsFade::kLowerMs + 1;
    fade.Apply(true, pose, now);
    now += 16;
    const HeadPose down = fade.Apply(false, pose, now);
    Check(down.x < 1.0f, "the lean does not jump back the frame the sights drop");
    CheckNear(down.yaw, 15.0f, 1e-6f, "rotation is untouched on the way out");
    now += AdsFade::kRaiseMs + 1;
    CheckNear(fade.Apply(false, pose, now).x, 10.0f, 1e-6f, "the lean returns in full");
}

void TestResetStartsTheNextAimFromTheHip() {
    AdsLeanFade fade;
    const HeadPose pose = MakePose(0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f);
    unsigned long long now = 1000;
    fade.Apply(true, pose, now);
    now += AdsFade::kLowerMs + 1;
    fade.Apply(true, pose, now);
    fade.Reset();
    now += 16;
    CheckNear(fade.Apply(false, pose, now).x, 10.0f, 1e-6f,
              "after a menu the hip lean is back at once, not riding a stale transition");
}

// ---- The zoom factor -----------------------------------------------------------

// The engine's own numbers: g_fov 80 renders a 50.53 degree fov_y at every
// window shape, and 83.05 x 50.53 at 1863x993. The horizontal pair would read
// 1.054 here.
void TestTheHipFactorIsOne() {
    const float hipFovY = 2.0f * std::atan(std::tan(40.0f * 0.0174532925f) * 9.0f / 16.0f) /
                          0.0174532925f;
    CheckNear(hipFovY, 50.53f, 0.01f, "g_fov 80 is a 50.53 degree fov_y, as measured");
    float factor = 0.0f;
    Check(wolf_ht::ZoomFactor(hipFovY, 80.0f, factor), "the hip fov_y is readable");
    CheckNear(factor, 1.0f, 1e-5f, "the factor is 1 at the hip");
}

void TestAZoomShrinksTheFactor() {
    const float zoomFovY = 2.0f * std::atan(std::tan(20.0f * 0.0174532925f) * 9.0f / 16.0f) /
                           0.0174532925f;
    float factor = 0.0f;
    Check(wolf_ht::ZoomFactor(zoomFovY, 80.0f, factor), "a zoomed fov_y is readable");
    const float expected = std::tan(20.0f * 0.0174532925f) / std::tan(40.0f * 0.0174532925f);
    CheckNear(factor, expected, 1e-5f,
              "a zoom to 40 nominal scales by tan(20) / tan(40), whatever the window shape");
}

void TestAnUnreadableFovIsRefused() {
    float factor = 0.5f;
    Check(!wolf_ht::ZoomFactor(std::nanf(""), 80.0f, factor), "a NaN fov_y is refused");
    Check(!wolf_ht::ZoomFactor(50.0f, 0.0f, factor), "a zero g_fov is refused");
    Check(!wolf_ht::ZoomFactor(180.0f, 80.0f, factor), "a straight-angle fov_y is refused");
    CheckNear(factor, 0.5f, 0.0f, "a refused factor leaves the output alone");
}

void TestYawAndPitchScaleAndRollDoesNot() {
    const HeadPose pose = MakePose(20.0f, -10.0f, 12.0f, 4.0f, -2.0f, 6.0f);
    const float factor = 0.5f;
    const HeadPose out = wolf_ht::ScalePoseForZoom(pose, factor);
    const float expectedYaw =
        std::atan(std::tan(20.0f * 0.0174532925f) * factor) / 0.0174532925f;
    const float expectedPitch =
        std::atan(std::tan(-10.0f * 0.0174532925f) * factor) / 0.0174532925f;
    CheckNear(out.yaw, expectedYaw, 1e-4f, "yaw scales by the zoom, through the tangent");
    CheckNear(out.pitch, expectedPitch, 1e-4f, "pitch scales by the zoom, through the tangent");
    CheckNear(out.roll, 12.0f, 0.0f, "roll does not scale");
    CheckNear(out.x, 2.0f, 1e-6f, "x scales linearly");
    CheckNear(out.y, -1.0f, 1e-6f, "y scales linearly");
    CheckNear(out.z, 3.0f, 1e-6f, "z scales linearly");
}

void TestAFactorOfOneChangesNothing() {
    const HeadPose pose = MakePose(35.0f, -20.0f, 5.0f, 4.0f, -2.0f, 6.0f);
    const HeadPose out = wolf_ht::ScalePoseForZoom(pose, 1.0f);
    CheckNear(out.yaw, 35.0f, 1e-4f, "factor 1 leaves yaw where it was");
    CheckNear(out.pitch, -20.0f, 1e-4f, "factor 1 leaves pitch where it was");
    CheckNear(out.x, 4.0f, 0.0f, "factor 1 leaves the lean where it was");
}

// atan(tan(120 deg)) is -60 deg, so an unguarded round trip turns a head that
// has turned past 90 back the other way even at the hip.
void TestAnglesPastNinetyAreNotFolded() {
    const HeadPose pose = MakePose(120.0f, -100.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    const HeadPose hip = wolf_ht::ScalePoseForZoom(pose, 1.0f);
    CheckNear(hip.yaw, 120.0f, 1e-4f, "a 120 degree yaw stays 120 at the hip");
    CheckNear(hip.pitch, -100.0f, 1e-4f, "a -100 degree pitch stays -100 at the hip");
    const HeadPose zoomed = wolf_ht::ScalePoseForZoom(MakePose(89.9f, 0, 0, 0, 0, 0), 0.5f);
    Check(zoomed.yaw > 89.0f && zoomed.yaw < 90.0f,
          "just short of 90 the scaled angle meets the pass-through side");
}

// ---- The gate ------------------------------------------------------------------

GateInputs Playing(bool aiming) {
    GateInputs in;
    in.have_profile = true;
    in.gameplay = true;
    in.enabled = true;
    in.aiming = aiming;
    return in;
}

void TestAimingNeverClosesTheGate() {
    const wolf_ht::GateVerdict v = EvaluateGate(Playing(true));
    Check(v.reason == GateReason::Gameplay, "the sights up is still gameplay");
    Check(PoseApplies(v.reason), "the pose reaches the camera with the sights up");
    Check(v.aiming, "the sights are reported up, for the lean fade");
}

void TestAMenuOutranksTheSights() {
    GateInputs in = Playing(true);
    in.gameplay = false;
    const wolf_ht::GateVerdict v = EvaluateGate(in);
    Check(v.reason == GateReason::NotGameplay, "a menu reports its own reason");
    Check(!v.aiming, "a menu leaves no stale ADS flag behind");
}

void TestEveryEarlyReturnClearsTheAdsFlag() {
    GateInputs noProfile = Playing(true);
    noProfile.have_profile = false;
    Check(EvaluateGate(noProfile).reason == GateReason::NoBuildProfile, "no profile is reported");
    Check(!EvaluateGate(noProfile).aiming, "no profile clears the ADS flag");

    GateInputs off = Playing(true);
    off.enabled = false;
    Check(EvaluateGate(off).reason == GateReason::Disabled, "the master toggle is reported");
    Check(!EvaluateGate(off).aiming, "the master toggle clears the ADS flag");
}

// The engine takes the sights down on paths that fire no event a mod could hook.
// Polling means the flag heals on the next frame either way.
void TestTheAdsFlagHealsWithoutAnExitEdge() {
    Check(EvaluateGate(Playing(true)).aiming, "the sights going up raise the flag");
    Check(!EvaluateGate(Playing(false)).aiming, "the sights going down clear it, no edge needed");
}

void TestOnlyGameplayAppliesThePose() {
    Check(PoseApplies(GateReason::Gameplay), "gameplay applies the pose");
    Check(!PoseApplies(GateReason::NotGameplay), "a menu does not apply the pose");
    Check(!PoseApplies(GateReason::Disabled), "the master toggle does not apply the pose");
    Check(!PoseApplies(GateReason::NoBuildProfile), "a dormant mod does not apply the pose");
}

}  // namespace

int main() {
    TestHipFirePassesThePoseThrough();
    TestSightsUpKeepRotationAndDropTheLean();
    TestRaisingTheSightsDoesNotStep();
    TestMidTransitionScalesOnlyTheLean();
    TestAReversalContinuesFromWhereItWas();
    TestLoweringTheSightsBringsTheLeanBack();
    TestResetStartsTheNextAimFromTheHip();

    TestTheHipFactorIsOne();
    TestAZoomShrinksTheFactor();
    TestAnUnreadableFovIsRefused();
    TestYawAndPitchScaleAndRollDoesNot();
    TestAFactorOfOneChangesNothing();
    TestAnglesPastNinetyAreNotFolded();

    TestAimingNeverClosesTheGate();
    TestAMenuOutranksTheSights();
    TestEveryEarlyReturnClearsTheAdsFlag();
    TestTheAdsFlagHealsWithoutAnExitEdge();
    TestOnlyGameplayAppliesThePose();

    return checks::Summarize("ads");
}
