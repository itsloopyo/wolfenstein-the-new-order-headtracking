// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <atomic>
#include <memory>

#include "ads.h"
#include "config.h"
#include "idtech/idtech_math.h"
#include "published_triple.h"
#include "tracker_feed.h"
#include "tracking_gate.h"

namespace wolf_ht {

namespace builds { struct BuildProfile; }

class CameraHook;
class GameState;
class Hotkeys;

// Mod-level coordinator: owns the config, the tracker feed, the render-view
// hook and the hotkeys, and is the single object the render hook asks for the
// frame's pose. Constructed once and never destroyed.
class HeadTrackingMod {
public:
    HeadTrackingMod();
    ~HeadTrackingMod();

    HeadTrackingMod(const HeadTrackingMod&) = delete;
    HeadTrackingMod& operator=(const HeadTrackingMod&) = delete;

    void Initialize();

    // HUD and render-view hooks share one pose sample per engine frame.
    bool UpdateForFrame();

    // What the pose is scaled by for a view drawn at `fovYDegrees`, the full
    // vertical angle out of that view's renderView_t. 1 when either FOV is
    // unreadable, which is logged once.
    float ZoomFactorFor(float fovYDegrees);
    bool BuildTrackedView(idtech::Vec3& origin, idtech::Mat3& axis, float zoomFactor) const;

    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(); }

    void ToggleEnabled();
    void ToggleYawMode();
    void CycleTrackingMode();

    const Config& GetConfig() const { return m_config; }

    // Whether Initialize matched a known build and went on to touch the running
    // process. False leaves the game vanilla, which is what the caller checks
    // before doing anything else a player would see.
    bool IsEngaged() const { return m_engaged; }

private:
    void LoadSettings();
    void InstallEngineHooks(const builds::BuildProfile& profile);
    void LogVerdictChange(const GateVerdict& verdict);

    Config m_config;
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_worldSpaceYaw{true};

    TrackerFeed m_feed;
    // Render thread only.
    AdsLeanFade m_leanFade;

    std::unique_ptr<GameState> m_gameState;
    std::unique_ptr<CameraHook> m_cameraHook;
    std::unique_ptr<Hotkeys> m_hotkeys;

    int m_frameNumber = 0;
    bool m_haveFrame = false;
    bool m_frameActive = false;

    GateReason m_lastReason = GateReason::Gameplay;
    bool m_stateKnown = false;
    bool m_engaged = false;

    // The g_fov cvar's float, which the game writes from its FOV slider and the
    // console. Null until the engine hooks are installed.
    const float* m_baseFov = nullptr;
    std::atomic<bool> m_zoomBasisLogged{false};
    std::atomic<bool> m_zoomEngagedLogged{false};
    std::atomic<bool> m_zoomUnreadableLogged{false};

    // This frame's pose after the lean fade, before the zoom factor: degrees for
    // the rotation, id Tech units forward / left / up for the position.
    PublishedTriple m_rotation;
    PublishedTriple m_position;
};

HeadTrackingMod& GetMod();

}  // namespace wolf_ht
