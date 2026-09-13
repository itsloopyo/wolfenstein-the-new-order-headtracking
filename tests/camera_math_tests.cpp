// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
//
// Locks the two things that decide whether the view goes the right way: the
// composition order the basis rotation applies, and which axes the boundary
// negates on the way from the tracker's convention to id Tech's.
//
// This is not a substitute for looking at the screen - only a person can say
// that a head turned right turned the view right - but it is what stops a
// refactor from silently reversing an axis that was verified once.

#include "idtech/idtech_math.h"

#include <cmath>
#include <limits>

#include "check_harness.h"

namespace {

using checks::Check;
using checks::CheckNear;

using wolf_ht::idtech::Mat3;
using wolf_ht::idtech::Dot;

constexpr float kDeg = 0.01745329252f;

// A camera looking along +X, level: forward +X, left +Y, up +Z.
Mat3 Level() {
    Mat3 m;
    m.m[0] = 1; m.m[1] = 0; m.m[2] = 0;
    m.m[3] = 0; m.m[4] = 1; m.m[5] = 0;
    m.m[6] = 0; m.m[7] = 0; m.m[8] = 1;
    return m;
}

// Far tighter than idtech::IsOrthonormal, which is the runtime guard's "the
// engine will not choke on this" bar. A rotation that only just clears that bar
// has lost something these tests exist to catch.
bool IsTightlyOrthonormal(const Mat3& a) {
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(Dot(a.Row(i), a.Row(i)) - 1.0f) > 1e-4f) return false;
    }
    return std::fabs(Dot(a.Row(0), a.Row(1))) < 1e-4f &&
           std::fabs(Dot(a.Row(0), a.Row(2))) < 1e-4f &&
           std::fabs(Dot(a.Row(1), a.Row(2))) < 1e-4f;
}

void TestIdentity() {
    const Mat3 out = wolf_ht::idtech::RotateBasisLocal(Level(), 0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 9; ++i) CheckNear(out.m[i], Level().m[i], 1e-6f, "identity pose");
}

// Positive yaw turns the view LEFT: forward gains a +left component. The
// boundary negates the tracker's yaw, so a head turned RIGHT arrives here
// negative and turns the view right.
void TestYawTurnsLeft() {
    const Mat3 out = wolf_ht::idtech::RotateBasisLocal(Level(), 20.0f * kDeg, 0.0f, 0.0f);
    CheckNear(out.Row(0)[0], std::cos(20.0f * kDeg), 1e-4f, "yaw forward.x");
    CheckNear(out.Row(0)[1], std::sin(20.0f * kDeg), 1e-4f, "yaw forward.y is +left");
    CheckNear(out.Row(0)[2], 0.0f, 1e-4f, "yaw does not tilt forward");
    CheckNear(out.Row(2)[2], 1.0f, 1e-4f, "yaw leaves up alone");
    Check(IsTightlyOrthonormal(out), "yaw stays orthonormal");
}

// Positive pitch raises the view: forward gains a +up component.
void TestPitchRaises() {
    const Mat3 out = wolf_ht::idtech::RotateBasisLocal(Level(), 0.0f, 20.0f * kDeg, 0.0f);
    CheckNear(out.Row(0)[2], std::sin(20.0f * kDeg), 1e-4f, "pitch forward.z is +up");
    CheckNear(out.Row(0)[1], 0.0f, 1e-4f, "pitch does not yaw");
    CheckNear(out.Row(1)[1], 1.0f, 1e-4f, "pitch leaves left alone");
    Check(IsTightlyOrthonormal(out), "pitch stays orthonormal");
}

// Positive roll tilts the up row toward the LEFT row. The boundary passes the
// tracker's roll through unnegated, so this is the direction the view tilts for
// a positive tracker roll.
void TestRollTiltsUpTowardLeft() {
    const Mat3 out = wolf_ht::idtech::RotateBasisLocal(Level(), 0.0f, 0.0f, 20.0f * kDeg);
    CheckNear(out.Row(0)[0], 1.0f, 1e-4f, "roll leaves forward alone");
    CheckNear(out.Row(2)[1], std::sin(20.0f * kDeg), 1e-4f, "roll up.y is +left");
    CheckNear(out.Row(2)[2], std::cos(20.0f * kDeg), 1e-4f, "roll up.z");
    Check(IsTightlyOrthonormal(out), "roll stays orthonormal");
}

// Roll is applied innermost and yaw outermost, matching the shared
// yaw * pitch * roll order.
//
// Every entry is pinned against a closed form written out by hand below, NOT
// against a second call into RotateBasisLocal. The version of this test that
// compared the combined pose against a roll-free one could not fail for the
// reorderings that matter: Lr's forward row is {1,0,0}, so with Lr outermost
// roll leaves the forward row alone whichever way Ly and Lp are arranged.
// Building L as Lr*Ly*Lp instead of Lr*Lp*Ly passed the whole suite while moving
// the forward vector 12 degrees at a 40/40/35 pose. That is exactly the
// regression this test exists to catch.
void TestCompositionOrder() {
    const float y = 15.0f * kDeg, p = 25.0f * kDeg, r = 35.0f * kDeg;
    const float cy = std::cos(y), sy = std::sin(y);
    const float cp = std::cos(p), sp = std::sin(p);
    const float cr = std::cos(r), sr = std::sin(r);

    // Lr * Lp * Ly, multiplied out. Level() is the identity, so the returned
    // rows are these coefficients directly.
    const float expected[9] = {
        cp * cy,                  cp * sy,                  sp,
        sr * sp * cy - cr * sy,   sr * sp * sy + cr * cy,   -sr * cp,
        -cr * sp * cy - sr * sy,  -cr * sp * sy + sr * cy,  cr * cp,
    };

    const Mat3 combined = wolf_ht::idtech::RotateBasisLocal(Level(), y, p, r);
    for (int i = 0; i < 9; ++i) {
        CheckNear(combined.m[i], expected[i], 1e-4f,
                  "combined yaw+pitch+roll composes as Lr * Lp * Ly");
    }
    Check(IsTightlyOrthonormal(combined), "combined pose stays orthonormal");

    // The steep pose the reordered composition diverges most on, so a future
    // reordering fails loudly rather than by a fraction of a degree.
    const float sy2 = std::sin(40.0f * kDeg), cy2 = std::cos(40.0f * kDeg);
    const float sp2 = std::sin(40.0f * kDeg), cp2 = std::cos(40.0f * kDeg);
    const Mat3 steep =
        wolf_ht::idtech::RotateBasisLocal(Level(), 40.0f * kDeg, 40.0f * kDeg, 35.0f * kDeg);
    CheckNear(steep.Row(0)[0], cp2 * cy2, 1e-4f, "steep pose forward.x");
    CheckNear(steep.Row(0)[1], cp2 * sy2, 1e-4f, "steep pose forward.y");
    CheckNear(steep.Row(0)[2], sp2, 1e-4f, "steep pose forward.z");
}

// World yaw turns about world +Z rather than the camera's own up row. With the
// camera level the two agree; pitched steeply down they must not.
void TestWorldYawMatchesLocalWhenLevel() {
    const Mat3 local = wolf_ht::idtech::RotateBasisLocal(Level(), 20.0f * kDeg, 0.0f, 0.0f);
    const Mat3 world = wolf_ht::idtech::RotateBasisWorldYaw(Level(), 20.0f * kDeg, 0.0f, 0.0f);
    for (int i = 0; i < 9; ++i) {
        CheckNear(world.m[i], local.m[i], 1e-4f, "world yaw equals local yaw when level");
    }
}

void TestWorldYawDiffersWhenLookingDown() {
    // Looking straight down: forward -Z, left +Y, up +X.
    Mat3 down;
    down.m[0] = 0; down.m[1] = 0; down.m[2] = -1;
    down.m[3] = 0; down.m[4] = 1; down.m[5] = 0;
    down.m[6] = 1; down.m[7] = 0; down.m[8] = 0;

    const Mat3 world = wolf_ht::idtech::RotateBasisWorldYaw(down, 20.0f * kDeg, 0.0f, 0.0f);
    // Yawing about world up while looking straight down is a pure spin about
    // the view axis: the camera still points straight down.
    CheckNear(world.Row(0)[2], -1.0f, 1e-4f, "world yaw looking down keeps pointing down");
    Check(IsTightlyOrthonormal(world), "world yaw stays orthonormal");

    const Mat3 local = wolf_ht::idtech::RotateBasisLocal(down, 20.0f * kDeg, 0.0f, 0.0f);
    Check(std::fabs(local.Row(0)[2] + 1.0f) > 1e-3f,
          "camera-local yaw looking down sweeps the view off vertical");
}

// The metre-to-unit scale, which decides how big every lean is.
void TestUnitScale() {
    CheckNear(wolf_ht::idtech::kUnitsPerMetre, 39.3701f, 1e-3f, "one metre in inches");
}

// A lean is measured in the basis it is applied to, so with the camera level a
// forward lean moves the eye along +X and a left lean along +Y.
void TestTranslationFollowsTheBasisRows() {
    const wolf_ht::idtech::Vec3 origin{10.0f, 20.0f, 30.0f};
    const wolf_ht::idtech::Vec3 out =
        wolf_ht::idtech::TranslateAlongBasis(origin, Level(), 4.0f, 2.0f, 1.0f);
    CheckNear(out.x, 14.0f, 1e-4f, "forward moves along the forward row");
    CheckNear(out.y, 22.0f, 1e-4f, "left moves along the left row");
    CheckNear(out.z, 31.0f, 1e-4f, "up moves along the up row");

    // Yawed 90 degrees left, the same forward lean has to follow the ROW, not
    // world +X: a lean that ignored the basis would move the eye the same way
    // whichever way the body faced.
    const Mat3 turned = wolf_ht::idtech::RotateBasisLocal(Level(), 90.0f * kDeg, 0.0f, 0.0f);
    const wolf_ht::idtech::Vec3 leaned =
        wolf_ht::idtech::TranslateAlongBasis(origin, turned, 4.0f, 0.0f, 0.0f);
    CheckNear(leaned.x, 10.0f, 1e-3f, "a turned body leans along its own forward, not world +X");
    CheckNear(leaned.y, 24.0f, 1e-3f, "which after a 90 degree left turn is world +Y");
}

// The guard the camera hook runs on every frame, in both directions: it has to
// pass a basis the engine built and refuse one that has been scribbled on. A
// guard that accepted anything would write a camera pointing at nothing.
void TestOrthonormalGuardAcceptsAndRefuses() {
    Check(wolf_ht::idtech::IsOrthonormal(Level()), "the guard passes a level basis");
    Check(wolf_ht::idtech::IsOrthonormal(
              wolf_ht::idtech::RotateBasisLocal(Level(), 15.0f * kDeg, 25.0f * kDeg, 35.0f * kDeg)),
          "the guard passes a rotated basis");

    Mat3 scaled = Level();
    scaled.m[0] = 2.0f;
    Check(!wolf_ht::idtech::IsOrthonormal(scaled), "the guard refuses a row that is not unit length");

    Mat3 skewed = Level();
    skewed.m[3] = 0.5f;
    Check(!wolf_ht::idtech::IsOrthonormal(skewed), "the guard refuses rows that are not perpendicular");

    Mat3 broken = Level();
    broken.m[4] = std::numeric_limits<float>::quiet_NaN();
    Check(!wolf_ht::idtech::IsOrthonormal(broken), "the guard refuses a basis carrying NaN");
}

}  // namespace

int main() {
    TestIdentity();
    TestYawTurnsLeft();
    TestPitchRaises();
    TestRollTiltsUpTowardLeft();
    TestCompositionOrder();
    TestWorldYawMatchesLocalWhenLevel();
    TestWorldYawDiffersWhenLookingDown();
    TestUnitScale();
    TestTranslationFollowsTheBasisRows();
    TestOrthonormalGuardAcceptsAndRefuses();

    return checks::Summarize("camera math");
}
