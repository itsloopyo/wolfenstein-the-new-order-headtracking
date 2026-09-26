// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

#include <atomic>
#include <cstdint>

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"
#include "cameraunlock/tracking/tracking_mode.h"
#include "config.h"
#include "published_triple.h"

namespace wolf_ht {

// The tracker socket, the pose pipeline and the frame clock behind the rendered
// view. Update() runs on whichever thread the render-view hook runs on, and
// publishes this frame's pose through PublishedTriple; the getters are called
// from that same thread, later in the same frame. See published_triple.h for why
// the triple is not a cross-thread handoff.
class TrackerFeed {
public:
    void Start(const Config& config);

    // Pulls the latest packet, runs the pipeline and caches this frame's
    // rotation (DEGREES, id Tech sign convention) and position offset (id Tech
    // units, camera-local forward/left/up). `active` is the mod's own gate -
    // the toggle, the gameplay check and the ADS branch - passed in rather than
    // mirrored here so the two cannot drift.
    //
    // Degrees rather than radians because what consumes this next is the ADS
    // entry pose, whose seam-aware yaw delta is defined on the -180..180 wrap.
    // The conversion to radians happens where the camera is written.
    void Update(bool active);

    bool GetRotationDegrees(float& yaw, float& pitch, float& roll) const;
    bool GetPositionOffset(float& forward, float& left, float& up) const;

    // Called from the hotkey thread. Only arms a request: the mode change is
    // applied at the top of the next Update(), on the render thread. Core's
    // SetMode does more than store an atomic - stepping into RotationOnly also
    // resets the position processor's smoothing and the position interpolator,
    // which are plain floats the render thread may be inside at that moment.
    // Steps from the last requested mode, not the applied one, because the
    // render view is not built at the shell or in a load, so nothing applies a
    // request there. Returns the mode it requested.
    cameraunlock::TrackingMode CycleMode();
    const char* ModeName() const;

private:
    void Invalidate();
    void LogConnectionChange();

    std::uint16_t m_port = 0;
    bool m_wasConnected = false;
    // CycleMode restates core's cycle rule because it cannot call core's. These
    // pin the enum it restates, so a REORDER in core fails to compile here
    // rather than silently cycling through the wrong modes.
    //
    // They do not catch core appending a fourth mode: the three values below
    // would all still hold and the cycle would quietly skip it. Core's own
    // CycleMode hardcodes the same 3, so that change breaks there too and is not
    // worth a second guard here.
    static constexpr int kTrackingModeCount = 3;
    static_assert(static_cast<int>(cameraunlock::TrackingMode::RotationAndPosition) == 0,
                  "core reordered TrackingMode; CycleMode's restated rule is now wrong");
    static_assert(static_cast<int>(cameraunlock::TrackingMode::RotationOnly) == 1,
                  "core reordered TrackingMode; CycleMode's restated rule is now wrong");
    static_assert(static_cast<int>(cameraunlock::TrackingMode::PositionOnly) == 2,
                  "core reordered TrackingMode; CycleMode's restated rule is now wrong");

    // The mode the player has asked for, written by the hotkey thread; and the
    // one actually pushed into the session, touched only by the render thread.
    // Update() reconciles them.
    std::atomic<cameraunlock::TrackingMode> m_requestedMode{
        cameraunlock::TrackingMode::RotationAndPosition};
    cameraunlock::TrackingMode m_appliedMode = cameraunlock::TrackingMode::RotationAndPosition;

    cameraunlock::UdpReceiver m_receiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_receiver};
    // Without IsRemoteConnection() on the receiver the session silently reports
    // a local connection forever and RemoteSmoothing never applies, with
    // nothing at the call site to show it.
    static_assert(decltype(m_session)::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() or remote smoothing never applies");
    cameraunlock::time::FrameClock m_frameClock;

    bool m_isRemoteConnection = false;
    // Tri-state: false/false cannot be told from a local tracker, so a plain
    // equality check would never report the common local case at all.
    bool m_remoteConnectionKnown = false;

    // Degrees, id Tech sign convention.
    PublishedTriple m_rotation;
    // id Tech units, camera-local forward / left / up.
    PublishedTriple m_position;
};

}  // namespace wolf_ht
