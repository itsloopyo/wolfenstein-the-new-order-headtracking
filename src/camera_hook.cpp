// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "camera_hook.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>

#include "cameraunlock/hooks/hook_manager.h"
#include "headtracking_mod.h"
#include "log_throttle.h"
#include "logging.h"

namespace wolf_ht {

namespace {

using SetupFn = void(__fastcall*)(void*, int, int, int, int);
// Atomic, and read on the detour's null branch as well as its live one. MinHook
// writes it inside CreateHook and its EnableHook barrier does order that write
// ahead of the first detour call - but the null branch is now reachable for the
// whole of GameState construction rather than for a handful of instructions, so
// resting a hot-path read on that argument is not worth the paragraph it takes
// to state. A relaxed load costs nothing on x64.
std::atomic<SetupFn> g_original{nullptr};
// Atomic because the detour is live from the moment EnableHook returns, so the
// game's render thread reads this while the bootstrap thread is still writing
// it. The release store in Arm() is also what publishes m_profile, m_mod and
// the caller's game-state resolver, all written earlier on the bootstrap thread
// and dereferenced on the first frame that sees a non-null pointer here.
std::atomic<CameraHook*> g_hook{nullptr};

// Dense at first, because that is where a wrong offset shows, then a thin
// trickle for the rest of the session, which is what a late report ("it drifted
// after an hour", "it stopped when I loaded a save") is read from.
constexpr unsigned kBurstLines = 4;
constexpr unsigned kEarlyLines = 20;
constexpr unsigned kEarlyIntervalFrames = 600;
constexpr unsigned kSteadyIntervalFrames = 2000;

}  // namespace

bool CameraHook::Install(const builds::BuildProfile& profile, HeadTrackingMod& mod) {
    m_profile = &profile;
    m_mod = &mod;

    auto& hooks = cameraunlock::hooks::HookManager::Instance();
    const auto initStatus = hooks.Initialize();
    if (initStatus != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("[camera] MinHook init failed (%s); the mod is dormant and the game is "
                  "unmodified", cameraunlock::hooks::HookStatusToString(initStatus));
        return false;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    void* target = reinterpret_cast<void*>(base + profile.render_view_setup_rva);

    SetupFn original = nullptr;
    const auto createStatus =
        hooks.CreateHook(target, reinterpret_cast<void*>(&CameraHook::Detour),
                         reinterpret_cast<void**>(&original));
    g_original.store(original, std::memory_order_release);
    if (createStatus != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("[camera] could not hook idRenderView::Setup at +0x%X (%s); the mod is "
                  "dormant and the game is unmodified", profile.render_view_setup_rva,
                  cameraunlock::hooks::HookStatusToString(createStatus));
        return false;
    }

    const auto enableStatus = hooks.EnableHook(target);
    if (enableStatus != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("[camera] could not enable the idRenderView::Setup hook (%s); the mod is "
                  "dormant and the game is unmodified",
                  cameraunlock::hooks::HookStatusToString(enableStatus));
        return false;
    }

    Log::Line("[camera] hooked idRenderView::Setup at +0x%X", profile.render_view_setup_rva);
    return true;
}

void CameraHook::Arm() {
    // Deliberately not the tail of Install. Every failure in Install leaves the
    // caller free to destroy this object, and a global still pointing at a
    // destroyed hook is one enabled detour away from a use-after-free - so the
    // store cannot happen until the caller has decided to keep it. It also
    // cannot happen until everything the first frame reads is in place: the
    // detour reaches HeadTrackingMod::UpdateForFrame, which dereferences the
    // game-state resolver the caller builds AFTER a successful Install. A frame
    // landing before this store takes the null branch in Detour, which is the
    // plain pass-through to the game's own Setup.
    g_hook.store(this, std::memory_order_release);
}

void __fastcall CameraHook::Detour(void* renderView, int windowWidth, int windowHeight,
                                   int renderWidth, int renderHeight) {
    const SetupFn original = g_original.load(std::memory_order_acquire);
    CameraHook* const hook = g_hook.load(std::memory_order_acquire);
    if (hook == nullptr || renderView == nullptr) {
        original(renderView, windowWidth, windowHeight, renderWidth, renderHeight);
        return;
    }
    hook->PreSetup(renderView, renderWidth, renderHeight);
    // __finally rather than a call placed after the trampoline: if the game's
    // own Setup unwinds, the tracked view is left sitting in the render view,
    // and the next frame's PreSetup snapshots THAT as its clean view and
    // composes on top of it, so the pose compounds frame after frame until the
    // view is unusable. The restore has to run on every exit, and a C++
    // destructor does not run for an SEH unwind.
    __try {
        original(renderView, windowWidth, windowHeight, renderWidth, renderHeight);
    } __finally {
        hook->PostSetup(renderView);
    }
}

// Read, never written. The projection matrix this frame is built from comes out
// of these two angles - unless the engine is supplying an explicit projection
// matrix instead, in which case they are not what the frame is drawn with and
// the log has to say so rather than print them.
void CameraHook::ReadFieldOfView(std::uintptr_t view) {
    const builds::BuildProfile& p = *m_profile;
    m_fovX = *reinterpret_cast<const float*>(view + p.rv_fov_x_offset);
    m_fovY = *reinterpret_cast<const float*>(view + p.rv_fov_y_offset);
    m_explicitProjection =
        *reinterpret_cast<const unsigned char*>(view + p.rv_explicit_projection_offset) != 0;
}

void CameraHook::PreSetup(void* renderView, int renderWidth, int renderHeight) {
    const builds::BuildProfile& p = *m_profile;
    const auto view = reinterpret_cast<std::uintptr_t>(renderView);
    // Setup derives every matrix from the game copy at offset 0, so that is what
    // gets written, and it is what gets put back in PostSetup.
    auto* org = reinterpret_cast<idtech::Vec3*>(view + p.rv_vieworg_offset);
    auto* axis = reinterpret_cast<idtech::Mat3*>(view + p.rv_viewaxis_offset);

    ReadFieldOfView(view);

    m_cleanOrigin = *org;
    m_cleanAxis = *axis;
    m_renderOrigin = m_cleanOrigin;
    m_renderAxis = m_cleanAxis;
    m_wrote = false;
    m_zoomFactor = 1.0f;

    const bool active = m_mod->UpdateForFrame();
    const bool cleanUsable =
        idtech::IsOrthonormal(m_cleanAxis) && idtech::IsFinite3(&m_cleanOrigin.x);
    // Only while the mod is actually trying to inject. The old short-circuit
    // never evaluated these guards on an inactive frame, and reporting one there
    // would spend the single diagnostic on a frame nothing was going to be
    // written to anyway.
    if (active && !cleanUsable) {
        ReportRefusal(RefusalKind::CleanView,
                      "the view read out of the frame is not a usable camera");
    }
    if (!active || !cleanUsable) {
        LogFrame(active, renderWidth, renderHeight);
        return;
    }

    // An explicit projection matrix means fov_x is not what this frame is drawn
    // with, so there is no zoom to measure and the pose goes on unscaled.
    m_zoomFactor = m_mod->ZoomFactorFor(m_explicitProjection ? std::nanf("") : m_fovY);
    idtech::Vec3 renderOrigin = m_cleanOrigin;
    idtech::Mat3 renderAxis = m_cleanAxis;
    if (m_mod->BuildTrackedView(renderOrigin, renderAxis, m_zoomFactor)) {
        if (idtech::IsOrthonormal(renderAxis) && idtech::IsFinite3(&renderOrigin.x)) {
            *org = renderOrigin;
            *axis = renderAxis;
            m_renderOrigin = renderOrigin;
            m_renderAxis = renderAxis;
            m_wrote = true;
        } else {
            ReportRefusal(RefusalKind::TrackedView, "the tracked view came out malformed");
        }
    }
    LogFrame(active, renderWidth, renderHeight);
}

void CameraHook::ReportRefusal(RefusalKind kind, const char* what) {
    // A latch per reason, not one for both. They are different faults with
    // different next steps, and a single latch means whichever happens first
    // permanently silences the other.
    bool& logged = m_refusalLogged[static_cast<int>(kind)];
    if (logged) return;
    logged = true;
    const builds::BuildProfile& p = *m_profile;
    Log::Line("[camera] refusing to inject: %s. org=(%g %g %g) fwd=(%g %g %g) read at "
              "+0x%X/+0x%X. Head tracking will not appear while this holds.",
              what, m_cleanOrigin.x, m_cleanOrigin.y, m_cleanOrigin.z, m_cleanAxis.m[0],
              m_cleanAxis.m[1], m_cleanAxis.m[2], p.rv_vieworg_offset, p.rv_viewaxis_offset);
}

void CameraHook::PostSetup(void* renderView) {
    if (!m_wrote) return;
    const builds::BuildProfile& p = *m_profile;
    const auto view = reinterpret_cast<std::uintptr_t>(renderView);

    // The game's own view goes straight back. Nothing in game logic reads this
    // copy, so this is not what decouples aim - the write only ever existed
    // between here and the Setup call, and every matrix the frame is drawn and
    // culled with was derived from it inside that window. What the restore buys
    // is that a frame the engine renders WITHOUT refilling the view from the
    // game - a repeated present, a frame during a stall - starts from the game's
    // view again instead of compounding the head rotation frame after frame.
    *reinterpret_cast<idtech::Vec3*>(view + p.rv_vieworg_offset) = m_cleanOrigin;
    *reinterpret_cast<idtech::Mat3*>(view + p.rv_viewaxis_offset) = m_cleanAxis;
    m_wrote = false;
}

void CameraHook::LogFrame(bool active, int renderWidth, int renderHeight) {
    static LogThrottle s_throttle(kBurstLines, kEarlyLines, kEarlyIntervalFrames,
                                  kSteadyIntervalFrames);
    if (!s_throttle.ShouldLog()) return;
    char fov[32];
    if (m_explicitProjection) {
        std::snprintf(fov, sizeof(fov), "explicit");
    } else {
        std::snprintf(fov, sizeof(fov), "%.2fx%.2f", m_fovX, m_fovY);
    }
    Log::Line("[camera] %s org=(%.1f %.1f %.1f) fwd=(%.3f %.3f %.3f) -> "
              "org=(%.1f %.1f %.1f) fwd=(%.3f %.3f %.3f) %dx%d fov=%s zoom=%.4f",
              active ? "active" : "idle",
              m_cleanOrigin.x, m_cleanOrigin.y, m_cleanOrigin.z,
              m_cleanAxis.m[0], m_cleanAxis.m[1], m_cleanAxis.m[2],
              m_renderOrigin.x, m_renderOrigin.y, m_renderOrigin.z,
              m_renderAxis.m[0], m_renderAxis.m[1], m_renderAxis.m[2],
              renderWidth, renderHeight, fov, m_zoomFactor);
}

}  // namespace wolf_ht
