// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "tracker_feed.h"

#include <string>

#include "cameraunlock/math/smoothing_utils.h"
#include "idtech/idtech_math.h"
#include "logging.h"

namespace wolf_ht {

namespace {

cameraunlock::PositionSettings MakePositionSettings(const Config& c) {
    cameraunlock::PositionSettings s = cameraunlock::PositionSettings::Default();
    s.limit_x = c.limit_x;
    s.limit_y = c.limit_y;
    s.limit_y_down = c.limit_y;
    s.limit_z = c.limit_z;
    s.limit_z_back = c.limit_z_back;
    return s;
}

}  // namespace

void TrackerFeed::Start(const Config& config) {
    m_port = config.udp_port;
    const auto startMode = config.position_enabled
                               ? cameraunlock::TrackingMode::RotationAndPosition
                               : cameraunlock::TrackingMode::RotationOnly;
    m_session.SetMode(startMode);
    m_requestedMode.store(startMode);
    m_appliedMode = startMode;
    m_session.SetLocalSmoothing(config.local_smoothing);
    m_session.SetRemoteSmoothing(config.remote_smoothing);
    // Through the session rather than straight onto the processor: the session
    // owns the smoothing pair and recomposes it onto the incoming struct, so the
    // two calls compose in either order.
    m_session.SetPositionSettings(MakePositionSettings(config));
    // Our trackers report head position directly, so the core's synthetic pivot
    // term - which exists to cancel a webcam pivot - would only inject
    // rotation-coupled movement that is not there.
    m_session.GetPositionProcessor().SetTrackerPivotForward(0.0f);

    m_receiver.SetLog([](const std::string& msg) { Log::Line("[receiver] %s", msg.c_str()); });
    if (m_receiver.Start(m_port)) {
        Log::Line("[tracker] listening on UDP %u", m_port);
    } else {
        // Not fatal, and deliberately not a reason to abort setup. The receiver's
        // supervisor keeps re-attempting the bind, so a player who launches this
        // game while another one still holds the port gets head tracking about
        // half a second after that other game exits, with no relaunch.
        //
        // No cause is named here, because none is known here. The [receiver]
        // line immediately above carries the OS's own words for it, and a bind
        // fails for reasons that are not another program holding the port - a
        // port inside a Hyper-V reserved range refuses it with WSAEACCES while
        // nothing holds it at all, and a line that says otherwise sends the
        // player hunting an app that is not running.
        Log::Line("[tracker] UDP %u could not be bound; see the [receiver] line above for what "
                  "the OS said. The receiver retries every %dms in the background and starts "
                  "listening as soon as the port frees up, with no relaunch",
                  m_port, cameraunlock::UdpReceiver::kRetryIntervalMs);
    }
}

void TrackerFeed::LogConnectionChange() {
    const bool isRemote = m_session.IsRemoteConnection();
    if (m_remoteConnectionKnown && isRemote == m_isRemoteConnection) return;
    m_isRemoteConnection = isRemote;
    m_remoteConnectionKnown = true;

    const double effective = cameraunlock::math::GetEffectiveSmoothing(
        m_session.GetLocalSmoothing(), m_session.GetRemoteSmoothing(), isRemote);
    Log::Line("[tracker] source is %s, smoothing=%.3f", isRemote ? "remote" : "local", effective);
}

void TrackerFeed::Invalidate() {
    m_rotation.Invalidate();
    m_position.Invalidate();
}

void TrackerFeed::CycleMode() {
    // Core's HeadTrackingSession::CycleMode owns this rule, and cannot be called
    // from here: it goes through SetMode, which resets non-atomic processor
    // state under the render thread. That is the whole reason this is deferred.
    // The rule is therefore restated, and the static_asserts in tracker_feed.h
    // are what stop core REORDERING the enum under it silently. A
    // NextTrackingMode free function in core would remove the restatement
    // altogether; it does not exist yet.
    const auto current = m_requestedMode.load();
    m_requestedMode.store(static_cast<cameraunlock::TrackingMode>(
        (static_cast<int>(current) + 1) % kTrackingModeCount));
}

const char* TrackerFeed::ModeName() const {
    // The requested mode, not the session's, so the log line the hotkey writes
    // names the mode the player just asked for rather than the one still in
    // force until the next frame.
    switch (m_requestedMode.load()) {
        case cameraunlock::TrackingMode::RotationAndPosition: return "6DOF (rotation + position)";
        case cameraunlock::TrackingMode::RotationOnly:        return "rotation only";
        case cameraunlock::TrackingMode::PositionOnly:        return "position only";
    }
    return "?";
}

void TrackerFeed::Update(bool active) {
    // The hotkey thread only ever stores an enum here. Applying it is the render
    // thread's job, because SetMode also resets the position processor's
    // smoothing and the position interpolator - plain floats this thread may be
    // in the middle of reading.
    const auto requested = m_requestedMode.load();
    if (requested != m_appliedMode) {
        m_session.SetMode(requested);
        m_appliedMode = requested;
    }

    // Connection state is tracked whether or not tracking is active, so a player
    // who opens the pause menu or alt-tabs with a tracker running still gets the
    // connect and disconnect lines. Nothing calls this at the shell or during a
    // level load - the render view is not built then - so the log is silent
    // there either way.
    const bool receiving = m_receiver.IsReceiving();
    if (receiving != m_wasConnected) {
        m_wasConnected = receiving;
        if (receiving) {
            Log::Line("[tracker] source connected on UDP %u", m_port);
        } else {
            Log::Line("[tracker] source disconnected (no packets within the freshness window)");
        }
    }

    // Ticked every frame, before any early return. FrameClock reports the time
    // since the LAST tick and clamps it to 0.1s, so skipping it through a gap
    // hands the first resumed frame a dt two orders of magnitude too large - and
    // at that dt the smoothing factor is 0.99, which converges the whole gap's
    // worth of head movement in a single frame. That is the snap the hold below
    // exists to remove, arriving one frame later.
    const float dt = m_frameClock.Tick();

    // A closed gate is a deliberate suppression - a menu, a video, the master
    // toggle - so the pose goes away and the frame renders from the game's own
    // view.
    if (!active) {
        Invalidate();
        return;
    }

    // A tracker gap is NOT. Dropping the pose because the last packet is older
    // than the freshness window snaps the view to the untracked orientation and
    // then whips it back when packets resume, twice per dropout, and a webcam
    // tracker losing the face for half a second is the commonest thing that
    // happens to one. Doctrine is to hold the last known pose and let smoothing
    // blend back in, so the published pose is simply left standing.
    if (!receiving) return;

    if (!m_session.Update(dt)) return;
    LogConnectionChange();

    float yawDeg = 0.0f, pitchDeg = 0.0f, rollDeg = 0.0f;
    m_session.GetRotation(yawDeg, pitchDeg, rollDeg);

    // The protocol-to-engine sign conversion, done once, here, at the boundary
    // where the tracker convention meets id Tech's - never as a user setting.
    //
    // The tracker calls a head turned RIGHT positive yaw, and RotateBasisLocal
    // calls a view turned LEFT positive yaw, so yaw is negated. Pitch and roll
    // pass through. Roll was negated here until 2026-09-06, when the view was
    // reported tilting against the head in game and the negation was dropped.
    m_rotation.Publish(-yawDeg, pitchDeg, rollDeg, true);

    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (m_session.GetPositionOffset(x, y, z)) {
        // The same boundary, for position. The processor reports negative z for
        // a head leaning IN, while id Tech's basis rows are forward / left / up
        // and measured in inches, so forward is -z, negated exactly once, here,
        // after the processor has applied its asymmetric forward/back clamp.
        // x passes through. It was negated here until 2026-09-06, when the
        // view was reported leaning the wrong way in game.
        m_position.Publish(-z * idtech::kUnitsPerMetre, x * idtech::kUnitsPerMetre,
                           y * idtech::kUnitsPerMetre, true);
    } else {
        m_position.Invalidate();
    }
}

bool TrackerFeed::GetRotationDegrees(float& yaw, float& pitch, float& roll) const {
    return m_rotation.Read(yaw, pitch, roll);
}

bool TrackerFeed::GetPositionOffset(float& forward, float& left, float& up) const {
    return m_position.Read(forward, left, up);
}

}  // namespace wolf_ht
