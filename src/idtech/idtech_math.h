// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cmath>

namespace wolf_ht::idtech {

// id Tech's world is right-handed with +X forward, +Y left, +Z up, and lengths
// in inches. An idMat3 is nine floats, row-major, and a view axis holds its
// rows in the order forward / left / up - confirmed against the engine's own
// view-matrix builder, which reads rows 1 and 2 as the view's right (negated)
// and up axes and row 0 as its backward axis.
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Mat3 {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    float* Row(int i) { return m + i * 3; }
    const float* Row(int i) const { return m + i * 3; }
};

inline float Dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline bool IsFinite3(const float* v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// How far from unit length, and from perpendicular, a basis may be and still be
// something the engine will not choke on. Loose enough to pass a float basis the
// engine built itself, tight enough to refuse a garbage read.
inline constexpr float kOrthonormalTolerance = 0.01f;

// Three finite, unit-length, mutually perpendicular rows. The camera hook checks
// this on the way IN as well as on the way out, so a frame where the engine
// handed it something unexpected is passed through untouched rather than turned
// into a camera pointing at nothing.
inline bool IsOrthonormal(const Mat3& axis) {
    for (int i = 0; i < 3; ++i) {
        if (!IsFinite3(axis.Row(i))) return false;
        if (std::fabs(Dot(axis.Row(i), axis.Row(i)) - 1.0f) > kOrthonormalTolerance) return false;
    }
    return std::fabs(Dot(axis.Row(0), axis.Row(1))) < kOrthonormalTolerance &&
           std::fabs(Dot(axis.Row(0), axis.Row(2))) < kOrthonormalTolerance &&
           std::fabs(Dot(axis.Row(1), axis.Row(2))) < kOrthonormalTolerance;
}

// One metre in id Tech units, which are inches.
//
// Read out of the running game rather than assumed. Its movement and physics
// constants are Doom 3's, unchanged: g_gravity 1066, pm_bboxwidth 32,
// pm_walkspeed 140, pm_stepsize 16. Those are the numbers that only make sense
// against Doom 3's inch-per-unit world, so this game measures in inches too.
// The player box is bigger than Doom 3's (pm_normalheight 90 against 74, view
// height 87) because B.J. is, not because the scale changed.
//
// A wrong scale here breaks nothing visibly - it just makes every lean the
// wrong size, which is the kind of fault that never gets reported. Re-read
// those four cvars if a future build ever looks off.
inline constexpr float kUnitsPerMetre = 39.3701f;

// Rotates an orthonormal basis whose rows are forward / left / up, by angles
// expressed in that same basis. Radians.
//
//   yaw   > 0 turns the view LEFT   (right-hand rule about the up row)
//   pitch > 0 raises the view       (about the negated left row, because a
//                                    right-handed turn about +left lowers it)
//   roll  > 0 tilts the up row toward the LEFT row, which is what a head
//             tilted LEFT does - so the boundary negates the tracker's roll,
//             which calls a head tilted RIGHT positive
//
// Composition is yaw outermost then pitch then roll, matching the shared
// yaw * pitch * roll order every mod in the fleet applies, so the reticle
// projection derived from it can reuse the same decomposition.
//
// The rotation is built in the basis's own coordinates and then mapped back
// through the basis, which is what makes it camera-local: the resulting rows
// are combinations of the incoming rows, so an axis that was already banked
// stays banked.
inline Mat3 RotateBasisLocal(const Mat3& axis, float yaw, float pitch, float roll) {
    const float cy = std::cos(yaw),   sy = std::sin(yaw);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cr = std::cos(roll),  sr = std::sin(roll);

    // L[i][j] is how much of the old row j the new row i is made of.
    // Yaw about up (row 2):    f -> c f + s l,  l -> -s f + c l
    // Pitch about left (row 1): f -> c f + s u,  u -> -s f + c u   (positive = up)
    // Roll about forward (0):   l -> c l - s u,  u ->  s l + c u   (up tilts toward +left)
    const float Ly[3][3] = {{cy, sy, 0}, {-sy, cy, 0}, {0, 0, 1}};
    const float Lp[3][3] = {{cp, 0, sp}, {0, 1, 0}, {-sp, 0, cp}};
    const float Lr[3][3] = {{1, 0, 0}, {0, cr, -sr}, {0, sr, cr}};

    // L maps OLD-basis coordinates to NEW-basis coordinates, so it is the
    // INVERSE of the frame rotation - which reverses the order the three
    // factors go in. For the frame to turn by yaw * pitch * roll with roll
    // innermost, L has to be Lr * Lp * Ly, not Ly * Lp * Lr. Written the
    // other way round every single-axis case still looks right and only a
    // combined pose is wrong, with roll swinging where the camera points.
    float Lrp[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            Lrp[i][j] = Lr[i][0] * Lp[0][j] + Lr[i][1] * Lp[1][j] + Lr[i][2] * Lp[2][j];

    float L[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            L[i][j] = Lrp[i][0] * Ly[0][j] + Lrp[i][1] * Ly[1][j] + Lrp[i][2] * Ly[2][j];

    Mat3 out;
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 3; ++c)
            out.m[i * 3 + c] = L[i][0] * axis.m[0 * 3 + c] + L[i][1] * axis.m[1 * 3 + c] +
                               L[i][2] * axis.m[2 * 3 + c];
    return out;
}

// Yaw applied about WORLD up (+Z) instead of about the basis's own up row, with
// pitch and roll still camera-local. Keeps a glance left level while the player
// is looking steeply up or down, which is the whole point of the world-yaw mode:
// with a camera-local yaw, looking down a stairwell and turning the head spins
// the world about the view axis instead of sweeping it sideways.
inline Mat3 RotateBasisWorldYaw(const Mat3& axis, float yaw, float pitch, float roll) {
    const Mat3 local = RotateBasisLocal(axis, 0.0f, pitch, roll);

    const float c = std::cos(yaw), s = std::sin(yaw);
    // Right-hand rule about world +Z: x -> c x - s y, y -> s x + c y. That turns
    // +X (forward) toward +Y (left), so positive yaw looks left here too.
    Mat3 out;
    for (int i = 0; i < 3; ++i) {
        const float* r = local.Row(i);
        out.m[i * 3 + 0] = c * r[0] - s * r[1];
        out.m[i * 3 + 1] = s * r[0] + c * r[1];
        out.m[i * 3 + 2] = r[2];
    }
    return out;
}

// Moves an eye along the rows of a basis. `forward`, `left` and `up` are in id
// Tech units and are measured in `axis`, so passing the CLEAN basis is what
// makes a lean follow where the body faces rather than where the head is
// looking: turn your head and lean in, and you move along your shoulders'
// forward, not along the new line of sight.
inline Vec3 TranslateAlongBasis(const Vec3& origin, const Mat3& axis, float forward, float left,
                                float up) {
    Vec3 out = origin;
    for (int c = 0; c < 3; ++c) {
        (&out.x)[c] += axis.Row(0)[c] * forward + axis.Row(1)[c] * left + axis.Row(2)[c] * up;
    }
    return out;
}

}  // namespace wolf_ht::idtech
