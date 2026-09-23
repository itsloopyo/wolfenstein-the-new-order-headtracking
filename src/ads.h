// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cmath>

#include "cameraunlock/ads/ads_fade.h"
#include "cameraunlock/camera/zoom_compensation.h"

namespace wolf_ht {

// One frame's head pose at the engine boundary: id Tech degrees for the
// rotation, id Tech units forward / left / up for the position.
struct HeadPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Head tracking carries straight on through the sights. Turning the camera
// about the eye leaves the weapon's sight line through the eye, so rotation is
// never faded, made relative or suspended. A lean translates the eye OFF that
// line, and this mod does not own the pass the weapon is drawn in, so the lean
// eases out while the sights are up and back in when they come down.
//
// `aiming` is idPresentablePlayer::wantZoom polled this frame, false on every
// frame the gate closed early. No file, no clock and no logging, so the whole
// transition is driven frame by frame in tests/ads_tests.cpp.
class AdsLeanFade {
public:
    HeadPose Apply(bool aiming, const HeadPose& pose, unsigned long long nowMs) {
        const float scale = m_fade.Update(aiming, nowMs);
        HeadPose out = pose;
        out.x *= scale;
        out.y *= scale;
        out.z *= scale;
        return out;
    }

    // A menu, a video, a level load or the master toggle. The next aim starts
    // from the hip rather than from a transition left half run.
    void Reset() { m_fade.Reset(); }

private:
    cameraunlock::ads::AdsFade m_fade;
};

// g_fov and the zoom FOVs are nominal angles, horizontal at a 16:9 reference.
// idView::CalcFOV turns one into renderView_t::fov_y as
// 2 * atan(tan(nominal / 2) / (16 / 9)), whatever the window's shape, and only
// then widens fov_x by the real aspect. So fov_y is the axis the zoom lives in:
// at 1863x993 with g_fov 80 the hip view reads 83.05 x 50.53, and fov_x would
// put the hip factor at 1.054.
inline constexpr float kFovReferenceAspect = 16.0f / 9.0f;

// The factor that keeps a head movement the same size on screen whatever the
// game has zoomed to: tan(fov_y / 2) over the tangent g_fov resolves to on the
// same vertical axis. 1 at the hip. False for a value no projection could have
// been built from, and the caller then applies no compensation.
inline bool ZoomFactor(float fovYDegrees, float gFovDegrees, float& factor) {
    const auto usable = [](float fov) { return std::isfinite(fov) && fov > 0.0f && fov < 180.0f; };
    if (!usable(fovYDegrees) || !usable(gFovDegrees)) return false;
    constexpr float kHalfDegToRad = 0.00872664626f;
    factor = cameraunlock::camera::FovZoomFactor(
        std::tan(fovYDegrees * kHalfDegToRad),
        std::tan(gFovDegrees * kHalfDegToRad) / kFovReferenceAspect);
    return true;
}

// Core's tangent round trip only holds inside +/-90 degrees: past it tan()
// changes sign and atan() folds the angle back to the other side of the view,
// even at a factor of 1. The scaled angle reaches exactly 90 as the input does,
// so passing anything beyond straight through is continuous.
inline float ScaleHeadAngleForZoom(float degrees, float factor) {
    if (!(std::fabs(degrees) < 90.0f)) return degrees;
    return cameraunlock::camera::ScaleAngleForZoom(degrees, factor);
}

// Yaw, pitch and the lean translate the picture, so they scale. Roll rotates it
// about the view axis by the same angle at every field of view, so it does not.
inline HeadPose ScalePoseForZoom(const HeadPose& pose, float factor) {
    HeadPose out = pose;
    out.yaw = ScaleHeadAngleForZoom(pose.yaw, factor);
    out.pitch = ScaleHeadAngleForZoom(pose.pitch, factor);
    out.x *= factor;
    out.y *= factor;
    out.z *= factor;
    return out;
}

}  // namespace wolf_ht
