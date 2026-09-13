// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "aim_projection.h"
#include "check_harness.h"
#include <limits>

int main() {
    using checks::Check;
    using checks::CheckNear;
    using wolf_ht::ProjectAim;
    using wolf_ht::idtech::Mat3;
    using wolf_ht::idtech::Vec3;
    const Mat3 level;
    float x = 0, y = 0;
    Check(ProjectAim({100, 0, 0}, level, 90, 90, x, y), "clean aim visible");
    CheckNear(x, 0, 0.00001f, "clean aim centered horizontally");
    CheckNear(y, 0, 0.00001f, "clean aim centered vertically");
    Check(ProjectAim({100, -10, -5}, level, 90, 90, x, y), "near target visible under lean");
    CheckNear(x, 0.1f, 0.00001f, "left lean puts fixed target to the right");
    CheckNear(y, -0.05f, 0.00001f, "up lean puts fixed target below");
    Check(ProjectAim({1000, -10, -5}, level, 90, 90, x, y), "far target visible under lean");
    CheckNear(x, 0.01f, 0.00001f, "parallax decreases with target depth");
    CheckNear(y, -0.005f, 0.00001f, "vertical parallax decreases with target depth");
    Check(ProjectAim({100, -10, 0}, level, 60, 90, x, y), "zoomed target visible");
    CheckNear(x, 0.17320508f, 0.00001f, "live horizontal FOV controls projection");

    constexpr float radians = 0.01745329252f;
    const Mat3 rotated = wolf_ht::idtech::RotateBasisLocal(level, 20*radians, -10*radians, 15*radians);
    const Vec3 target{
        rotated.m[0]*100 + rotated.m[3]*20 + rotated.m[6]*10,
        rotated.m[1]*100 + rotated.m[4]*20 + rotated.m[7]*10,
        rotated.m[2]*100 + rotated.m[5]*20 + rotated.m[8]*10,
    };
    Check(ProjectAim(target, rotated, 90, 90, x, y), "combined yaw pitch roll target visible");
    CheckNear(x, -0.2f, 0.00001f, "combined rotation uses the drawn right basis");
    CheckNear(y, 0.1f, 0.00001f, "combined rotation uses the drawn up basis");
    Check(!ProjectAim({-1, 0, 0}, level, 90, 90, x, y), "behind-camera aim hidden");
    Check(!ProjectAim({0.00001f, 1, 0}, level, 90, 90, x, y), "near-plane singularity hidden");
    Check(!ProjectAim({}, level, 90, 90, x, y), "eye coincident with target hidden");
    Check(!ProjectAim({1, 0, 0}, level, 0, 90, x, y), "zero FOV rejected");
    Check(!ProjectAim({1, 0, 0}, level, 180, 90, x, y), "invalid FOV rejected");
    Check(!ProjectAim({std::numeric_limits<float>::quiet_NaN(), 0, 0}, level, 90, 90, x, y), "invalid target rejected");
    return checks::Summarize("aim projection");
}
