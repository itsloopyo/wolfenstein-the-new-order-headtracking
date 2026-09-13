// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <atomic>
#include <memory>
#include <string>

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
    bool BuildTrackedView(idtech::Vec3& origin, idtech::Mat3& axis) const;

    bool GetRotationRadians(float& yaw, float& pitch, float& roll) const;
    bool GetPositionOffset(float& forward, float& left, float& up) const;

    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(); }

    void ToggleEnabled();
    void ToggleYawMode();
    void CycleTrackingMode();
    void CycleAdsMode();

    const Config& GetConfig() const { return m_config; }

    // Whether Initialize matched a known build and went on to touch the running
    // process. False leaves the game vanilla, which is what the caller checks
    // before doing anything else a player would see.
    bool IsEngaged() const { return m_engaged; }

private:
    void LoadSettings();
    void InstallEngineHooks(const builds::BuildProfile& profile);
    void PublishPose(const cameraunlock::ads::AdsEntryPose::Pose& pose, bool rotationValid,
                     bool positionValid);
    void LogVerdictChange(const GateVerdict& verdict);

    Config m_config;
    // Where HeadTracking.ini lives, so the ADS mode the player cycled to can be
    // written back. Empty when the game directory could not be resolved.
    std::string m_exeDir;
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_worldSpaceYaw{true};

    TrackerFeed m_feed;
    AdsController m_ads;

    std::unique_ptr<GameState> m_gameState;
    std::unique_ptr<CameraHook> m_cameraHook;
    std::unique_ptr<Hotkeys> m_hotkeys;

    int m_frameNumber = 0;
    bool m_haveFrame = false;
    bool m_frameActive = false;

    GateReason m_lastReason = GateReason::Gameplay;
    bool m_stateKnown = false;
    bool m_adsSuspensionReported = false;
    bool m_engaged = false;

    // This frame's pose after the ADS stage, in the units the camera hook wants:
    // radians for the rotation, id Tech units forward / left / up for the
    // position.
    PublishedTriple m_rotation;
    PublishedTriple m_position;
};

HeadTrackingMod& GetMod();

}  // namespace wolf_ht
