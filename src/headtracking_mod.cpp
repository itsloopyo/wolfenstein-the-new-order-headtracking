// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "headtracking_mod.h"

#include <windows.h>

#include <cmath>

#include "builds/build_registry.h"
#include "camera_hook.h"
#include "cameraunlock/math/angle_utils.h"
#include "cameraunlock/os/module_paths.h"
#include "game_state.h"
#include "hotkeys.h"
#include "logging.h"
#include "reticle_hook.h"

namespace wolf_ht {

namespace {

// Core's, narrowed once here rather than restated as a literal: the mod's
// boundary carries degrees and the basis rotations take radians.
constexpr float kDegToRad = static_cast<float>(cameraunlock::math::kDegToRad);

}  // namespace

// Deliberately leaked, never destroyed. A function-local static would register
// the destructor with atexit, and MSVC runs those from LdrShutdownProcess AFTER
// DllMain(DLL_PROCESS_DETACH) - so the careful "do not tear down on process
// exit" guard in dllmain.cpp would be undone by the destructor doing exactly
// that, under the loader lock, joining threads the OS has already killed.
HeadTrackingMod& GetMod() {
    static HeadTrackingMod* instance = new HeadTrackingMod();
    return *instance;
}

HeadTrackingMod::HeadTrackingMod() = default;
HeadTrackingMod::~HeadTrackingMod() = default;

void HeadTrackingMod::LoadSettings() {
    // Core's narrowing, because WideCharToMultiByte best-fit maps by default: a
    // character the ANSI code page cannot encode becomes a similar-looking one,
    // so a game directory can narrow to the name of a DIFFERENT directory that
    // exists and the INI is then read from and written to that one. Core
    // refuses instead, which lands on the no-INI path below.
    const std::string exeDir = cameraunlock::os::HostExeDirectoryNarrow();
    if (exeDir.empty()) {
        Log::Line("[mod] could not resolve the game directory in a form the INI reader can "
                  "use; built-in defaults are in use and HeadTracking.ini will not be read");
        return;
    }
    WriteDefaultConfigIfMissing(exeDir);
    LoadConfig(exeDir, m_config);
}

// A hook that will not install on a build the mod DOES recognise leaves the
// receiver and the hotkeys running, so a log inspection still shows whether
// tracking data is arriving. An unrecognised build never gets this far - see
// Initialize.
void HeadTrackingMod::InstallEngineHooks(const builds::BuildProfile& profile) {
    m_cameraHook = std::make_unique<CameraHook>();
    if (!m_cameraHook->Install(profile, *this)) {
        m_cameraHook.reset();
        return;
    }
    // Built only once the hook has installed, because building it starts two
    // threads that each read every committed region of the process every three
    // seconds until they find their singleton. At the shell there is no live
    // idGameLocal to find, so they sweep for as long as the player sits there -
    // the most expensive thing the mod does, and with a failed hook it would be
    // running immediately after the line above told the player the mod was
    // dormant and the game unmodified.
    m_gameState = std::make_unique<GameState>(profile);
    m_baseFov = reinterpret_cast<const float*>(
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) + profile.g_fov_value_rva);
    const bool reticleInstalled = InstallReticleHook(profile, *this);
    if (profile.reticle && !reticleInstalled) return;

    // And the detour is armed last, because it reads m_gameState on its very
    // first frame. Arming inside Install would race the assignment above.
    m_cameraHook->Arm();
}

void HeadTrackingMod::Initialize() {
    LoadSettings();

    m_enabled.store(m_config.enable_on_startup);
    m_worldSpaceYaw.store(m_config.world_space_yaw);

    // Everything below this line modifies the running process in some way a
    // player can notice: a bound UDP port, a key poller, a hooked engine
    // function, a repositioned game window. An unrecognised build gets none of
    // it. The build-profile doctrine is that a mismatch leaves the game running
    // vanilla, and a mod that still moved the window while its own log said it
    // was dormant would be the report that costs an evening to place.
    builds::LogBuildResolution();
    const builds::BuildProfile* profile = builds::ResolveRunningBuild();
    if (profile == nullptr) return;
    m_engaged = true;

    m_feed.Start(m_config);
    InstallEngineHooks(*profile);

    m_hotkeys = std::make_unique<Hotkeys>();
    m_hotkeys->Start(*this, m_config);

    Log::Line("[mod] ready: tracking %s, yaw about %s",
              m_enabled.load() ? "on" : "off",
              m_worldSpaceYaw.load() ? "world up" : "the view axis");
}

void HeadTrackingMod::LogVerdictChange(const GateVerdict& verdict) {
    const GateReason reason = verdict.reason;

    // The first evaluation is reported as well as every change. Reporting only
    // changes leaves a session that never reaches gameplay with nothing in the
    // log to say why - which is the one case a "no head tracking" report needs.
    if (m_stateKnown && reason == m_lastReason) return;
    m_stateKnown = true;
    m_lastReason = reason;

    // The gate's own words for everything it decided itself, and idGameLocal's
    // for the one answer it did not: "not in gameplay" is true of the shell, a
    // level load, a video and the pause menu alike, and which of those it is is
    // the whole content of a "no head tracking" report.
    if (reason == GateReason::NotGameplay && m_gameState != nullptr) {
        Log::Line("[state] tracking suspended (%s)", m_gameState->LastReason());
        return;
    }
    Log::Line("[state] %s (%s)", PoseApplies(reason) ? "gameplay" : "tracking suspended",
              GateReasonText(reason));
}

bool HeadTrackingMod::UpdateForFrame() {
    // HUD geometry is submitted before Setup, which can run twice for one view.
    int frame = 0;
    const bool haveFrame = m_gameState && m_gameState->GetFrameNumber(frame);
    if (haveFrame && m_haveFrame && frame == m_frameNumber) return m_frameActive;
    m_haveFrame = haveFrame;
    m_frameNumber = frame;
    m_frameActive = false;
    GateInputs inputs;
    inputs.have_profile = m_gameState != nullptr;
    inputs.gameplay = m_gameState != nullptr && m_gameState->IsGameplay();
    inputs.enabled = m_enabled.load();
    // Polled from the engine's own aim state on every frame, and only once the
    // frame is otherwise a gameplay frame - reading it through a level load
    // would be a pointer chase for an answer the walk is about to discard.
    inputs.aiming = inputs.gameplay && m_gameState->IsAiming();

    const GateVerdict verdict = EvaluateGate(inputs);
    LogVerdictChange(verdict);

    const bool poseApplies = PoseApplies(verdict.reason);
    m_feed.Update(poseApplies);

    if (!poseApplies) {
        m_leanFade.Reset();
        m_rotation.Invalidate();
        m_position.Invalidate();
        return false;
    }

    HeadPose pose;
    const bool rotationValid = m_feed.GetRotationDegrees(pose.yaw, pose.pitch, pose.roll);
    const bool positionValid = m_feed.GetPositionOffset(pose.x, pose.y, pose.z);
    pose = m_leanFade.Apply(verdict.aiming, pose, GetTickCount64());

    m_rotation.Publish(pose.yaw, pose.pitch, pose.roll, rotationValid);
    m_position.Publish(pose.x, pose.y, pose.z, positionValid);
    m_frameActive = true;
    return true;
}

float HeadTrackingMod::ZoomFactorFor(float fovYDegrees) {
    const float gFov = m_baseFov != nullptr ? *static_cast<const volatile float*>(m_baseFov)
                                            : std::nanf("");
    float factor = 1.0f;
    if (!ZoomFactor(fovYDegrees, gFov, factor)) {
        if (!m_zoomUnreadableLogged.exchange(true)) {
            Log::Line("[zoom] no zoom compensation on a view with fov_y=%g, g_fov=%g; head "
                      "movement is applied unscaled there. Reported once.", fovYDegrees, gFov);
        }
        return 1.0f;
    }
    // Every term, once, on the first hip view - which reads 1.0000, and anything
    // else there means fov_y and g_fov are not related the way idView::CalcFOV
    // relates them on this build. Then once more the first time a zoom engages.
    // The band is tight because the opening sequence eases its FOV through the
    // hip value: at 1% the basis line caught a 1.0094 frame of that ease.
    const bool zoomed = std::fabs(factor - 1.0f) > 0.001f;
    std::atomic<bool>& latch = zoomed ? m_zoomEngagedLogged : m_zoomBasisLogged;
    if (!latch.exchange(true)) {
        Log::Line("[zoom] %s: fov_y=%.3f deg (this view, full vertical), g_fov=%.3f deg "
                  "(nominal, horizontal at %.4f:1), factor tan(fov_y/2)/(tan(g_fov/2)/%.4f)"
                  "=%.4f", zoomed ? "first zoomed view" : "basis", fovYDegrees, gFov,
                  kFovReferenceAspect, kFovReferenceAspect, factor);
    }
    return factor;
}

bool HeadTrackingMod::BuildTrackedView(idtech::Vec3& origin, idtech::Mat3& axis,
                                       float zoomFactor) const {
    HeadPose pose;
    const bool haveRotation = m_rotation.Read(pose.yaw, pose.pitch, pose.roll);
    const bool havePosition = m_position.Read(pose.x, pose.y, pose.z);
    if (!haveRotation && !havePosition) return false;
    pose = ScalePoseForZoom(pose, zoomFactor);

    const idtech::Mat3 cleanAxis = axis;
    if (haveRotation) {
        const float yaw = pose.yaw * kDegToRad;
        const float pitch = pose.pitch * kDegToRad;
        const float roll = pose.roll * kDegToRad;
        axis = IsWorldSpaceYaw() ? idtech::RotateBasisWorldYaw(cleanAxis, yaw, pitch, roll)
                                : idtech::RotateBasisLocal(cleanAxis, yaw, pitch, roll);
    }
    if (havePosition) {
        origin = idtech::TranslateAlongBasis(origin, cleanAxis, pose.x, pose.y, pose.z);
    }
    return true;
}

// The toggles log from here rather than from the hotkey handler, so a nav key
// and its Ctrl+Shift chord produce the same single line - and so the log names
// the state that was actually reached, not the one the caller asked for.
void HeadTrackingMod::ToggleEnabled() {
    const bool next = !m_enabled.load();
    m_enabled.store(next);
    Log::Line("[mod] tracking -> %s", next ? "on" : "off");
}

void HeadTrackingMod::ToggleYawMode() {
    const bool next = !m_worldSpaceYaw.load();
    m_worldSpaceYaw.store(next);
    Log::Line("[mod] yaw about -> %s", next ? "world up" : "the view axis");
}

void HeadTrackingMod::CycleTrackingMode() {
    m_feed.CycleMode();
    Log::Line("[mod] tracking mode -> %s", m_feed.ModeName());
}

}  // namespace wolf_ht
