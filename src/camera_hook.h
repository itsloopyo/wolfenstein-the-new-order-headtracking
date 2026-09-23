// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <cstdint>

#include "builds/build_profile.h"
#include "idtech/idtech_math.h"

namespace wolf_ht {

class HeadTrackingMod;

// Hooks idRenderView::Setup and injects the head pose into the render view the
// frame is built from.
//
// The injection is a sandwich around the original call: the game's own view is
// read out, the tracked view is written in its place, Setup runs and derives
// every matrix from it, and the game's view is put straight back. Nothing that
// runs later can observe the tracked pose, so aim, projectiles, traces and AI
// vision are identical with tracking on and off - and because the projection
// and MVP matrices are derived AFTER the write, culling and every world-anchored
// element the engine projects agree with what is on screen. There is nothing to
// cull at the edges of a turned view, because the frustum was turned too.
class CameraHook {
public:
    bool Install(const builds::BuildProfile& profile, HeadTrackingMod& mod);

    // Makes the detour start injecting. Separate from Install so the caller can
    // finish wiring up everything the first frame touches before a frame can
    // arrive. Until this is called the detour is a pass-through.
    void Arm();

private:
    static void __fastcall Detour(void* renderView, int windowWidth, int windowHeight,
                                  int renderWidth, int renderHeight);
    void PreSetup(void* renderView, int renderWidth, int renderHeight);
    void PostSetup(void* renderView);
    void ReadFieldOfView(std::uintptr_t view);
    // This frame's tracked view, built from the clean one already snapshotted.
    // The outputs come in holding the clean view; false means the mod published
    // no pose and they were left exactly as they arrived.
    void LogFrame(bool active, int renderWidth, int renderHeight);
    // Which sanity guard refused, and so which latch the report uses.
    enum class RefusalKind { CleanView = 0, TrackedView = 1, Count = 2 };

    // Says once per kind that a guard refused this frame. Refusing silently is
    // the exact symptom a wrong rv_vieworg/rv_viewaxis offset on a new build
    // produces: every frame is dropped, the log still reports the hook as
    // active, and the player reports "no head tracking" with nothing to triage.
    // A latch rather than a throttle, because a repeat of the same refusal says
    // nothing the first line did not.
    void ReportRefusal(RefusalKind kind, const char* what);

    const builds::BuildProfile* m_profile = nullptr;
    HeadTrackingMod* m_mod = nullptr;

    // Carried across the original call so PostSetup can put the game's view
    // back exactly as it was, byte for byte, rather than recomputing an inverse.
    idtech::Vec3 m_cleanOrigin{};
    idtech::Mat3 m_cleanAxis{};
    idtech::Vec3 m_renderOrigin{};
    idtech::Mat3 m_renderAxis{};
    bool m_wrote = false;
    bool m_refusalLogged[static_cast<int>(RefusalKind::Count)] = {};

    // The frame's field of view, read out of the render view. The mod owns no
    // FOV: the game has its own slider. fov_y sets the zoom factor the head
    // pose is scaled by, and the pair is logged because it is half of what a
    // frame was drawn with.
    float m_fovX = 0.0f;
    float m_fovY = 0.0f;
    bool m_explicitProjection = false;
    float m_zoomFactor = 1.0f;
};

}  // namespace wolf_ht
