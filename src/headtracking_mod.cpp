// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "headtracking_mod.h"

#include <windows.h>

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
    m_exeDir = cameraunlock::os::HostExeDirectoryNarrow();
    if (m_exeDir.empty()) {
        Log::Line("[mod] could not resolve the game directory in a form the INI reader can "
                  "use; built-in defaults are in use and HeadTracking.ini will not be read");
        return;
    }
    WriteDefaultConfigIfMissing(m_exeDir);
    LoadConfig(m_exeDir, m_config);
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
    // Straight from the file, never overwritten by anything else at start-up:
    // the ADS mode is the player's choice, and quietly resetting it on launch is
    // the bug this ordering exists to avoid.
    m_ads.Start(m_config.ads_mode);

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

    Log::Line("[mod] ready: tracking %s, yaw about %s, ADS mode %s",
              m_enabled.load() ? "on" : "off",
              m_worldSpaceYaw.load() ? "world up" : "the view axis",
              cameraunlock::ads::AdsModeValue(m_ads.Mode()));
    if (m_ads.Mode() == cameraunlock::ads::AdsMode::Marker) {
        Log::Line("[mod] this build draws no aim marker yet, so ADS mode marker behaves as "
                  "tracked - see the Controls section of README.md");
    }
}

void HeadTrackingMod::LogVerdictChange(const GateVerdict& verdict) {
    // The sights go up and down dozens of times in a firefight, and Log::Line
    // takes a mutex and three WriteFile calls on the render thread. So the ADS
    // suspension is announced once per session and then folded into the gameplay
    // state: one line is enough to answer "tracking stops whenever I aim", which
    // is the whole of what a paused ADS mode does, and a line per aim would bury
    // every other transition in the file.
    if (verdict.reason == GateReason::AdsPaused && !m_adsSuspensionReported) {
        m_adsSuspensionReported = true;
        Log::Line("[state] tracking suspended (%s). Reported once - the sights go up too often "
                  "to log every time.", GateReasonText(GateReason::AdsPaused));
    }
    const GateReason reason =
        verdict.reason == GateReason::AdsPaused ? GateReason::Gameplay : verdict.reason;

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
    inputs.ads_mode = m_ads.Mode();

    const GateVerdict verdict = EvaluateGate(inputs);
    LogVerdictChange(verdict);

    const bool poseApplies = PoseApplies(verdict.reason);
    m_feed.Update(poseApplies);

    if (!poseApplies) {
        // Every suppression EXCEPT the sights being up, which does not land here
        // - that one keeps the pose flowing so the fade can run it down. So this
        // is unconditionally the "start the next aim clean" reset.
        m_ads.Suppress();
        m_rotation.Invalidate();
        m_position.Invalidate();
        return false;
    }

    cameraunlock::ads::AdsEntryPose::Pose absolute;
    const bool rotationValid =
        m_feed.GetRotationDegrees(absolute.yaw, absolute.pitch, absolute.roll);
    const bool positionValid = m_feed.GetPositionOffset(absolute.x, absolute.y, absolute.z);

    PublishPose(m_ads.Apply(verdict.aiming, rotationValid, absolute, GetTickCount64()),
                rotationValid, positionValid);
    m_frameActive = true;
    return true;
}

bool HeadTrackingMod::BuildTrackedView(idtech::Vec3& origin, idtech::Mat3& axis) const {
    const idtech::Mat3 cleanAxis = axis;
    bool modified = false;
    float yaw, pitch, roll;
    if (GetRotationRadians(yaw, pitch, roll)) {
        axis = IsWorldSpaceYaw() ? idtech::RotateBasisWorldYaw(cleanAxis, yaw, pitch, roll)
                                : idtech::RotateBasisLocal(cleanAxis, yaw, pitch, roll);
        modified = true;
    }
    float forward, left, up;
    if (GetPositionOffset(forward, left, up)) {
        origin = idtech::TranslateAlongBasis(origin, cleanAxis, forward, left, up);
        modified = true;
    }
    return modified;
}

void HeadTrackingMod::PublishPose(const cameraunlock::ads::AdsEntryPose::Pose& pose,
                                  bool rotationValid, bool positionValid) {
    m_rotation.Publish(pose.yaw * kDegToRad, pose.pitch * kDegToRad, pose.roll * kDegToRad,
                       rotationValid);
    m_position.Publish(pose.x, pose.y, pose.z, positionValid);
}

bool HeadTrackingMod::GetRotationRadians(float& yaw, float& pitch, float& roll) const {
    return m_rotation.Read(yaw, pitch, roll);
}

bool HeadTrackingMod::GetPositionOffset(float& forward, float& left, float& up) const {
    return m_position.Read(forward, left, up);
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

void HeadTrackingMod::CycleAdsMode() {
    const cameraunlock::ads::AdsMode next = m_ads.Cycle();
    // Written back before it is announced, so what the player is told and what
    // the file holds cannot disagree. This is the one setting the mod persists:
    // the other three toggles are session state, this one is a choice.
    SaveAdsMode(m_exeDir, next);
    // The mod draws nothing on screen, so the log line is the toast. Its text is
    // core's, unaltered, so the same three strings appear in every mod's log.
    Log::Line("[mod] %s", cameraunlock::ads::AdsModeToast(next));
    // The toast says an aim marker is shown. Nothing draws one in this build, so
    // the caveat has to follow the toast every time the player lands on that
    // mode, not only when the INI already read `marker` at start-up.
    if (next == cameraunlock::ads::AdsMode::Marker) {
        Log::Line("[mod] this build draws no aim marker yet, so ADS mode marker behaves as "
                  "tracked - see the Controls section of README.md");
    }
}

}  // namespace wolf_ht
