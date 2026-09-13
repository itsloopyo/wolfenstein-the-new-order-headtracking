// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include "cameraunlock/rendering/aim_ndc_projection.h"
#include "idtech/idtech_math.h"

namespace wolf_ht {

inline bool ProjectAim(const idtech::Vec3& vector, const idtech::Mat3& renderAxis,
                       float fovX, float fovY, float& x, float& y) {
    const float length = std::sqrt(idtech::Dot(&vector.x, &vector.x));
    if (!(length > 0.0f) || !std::isfinite(length) ||
        !(fovX > 0.0f && fovX < 180.0f && fovY > 0.0f && fovY < 180.0f)) return false;
    const float aim[3] = {vector.x / length, vector.y / length, vector.z / length};
    const float right[3] = {-renderAxis.m[3], -renderAxis.m[4], -renderAxis.m[5]};
    constexpr float halfRadians = 0.00872664626f;
    return cameraunlock::rendering::ProjectAimToNdc(
        aim, renderAxis.Row(0), right, renderAxis.Row(2),
        std::tan(fovX * halfRadians), std::tan(fovY * halfRadians), x, y);
}

}
