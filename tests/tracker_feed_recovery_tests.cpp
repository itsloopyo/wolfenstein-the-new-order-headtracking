// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
//
// The tracker port is shared with every other head tracking mod on the machine,
// so a player who launches this game with a previous one still open finds it
// held. What has to happen then is that closing the other game brings tracking
// up here on its own, quickly, with no relaunch.
//
// That behaviour lives in cameraunlock-core's supervisor thread, and reading it
// is not the same as knowing it: the retry period is a constant in one file, the
// supervisor's wake-up granularity is a constant in another, and what a player
// actually waits for is the sum plus a bind, a datagram and a frame. So this
// suite measures the whole path through this mod's own TrackerFeed - real
// sockets, a real sender, the real pipeline - and prints what it measured.
//
// It also locks the bind-failure diagnostic to what the OS said. A line naming a
// cause the error code does not support sends a player hunting an app that is
// not running; a port inside a Hyper-V reserved range refuses the bind with
// WSAEACCES while nothing holds it at all.

#include "check_harness.h"
#include "config.h"
#include "tracker_feed.h"

#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/protocol/socket_types.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/protocol/udp_socket.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace {

using checks::Check;
using checks::CheckNear;

using Clock = std::chrono::steady_clock;

int64_t MsSince(Clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count();
}

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Ports well above the OpenTrack default, so a tracker the developer happens to
// be running - or a sibling mod - cannot be what these tests measure. Offset by
// the process id so two runs of this suite side by side - and a stray
// listener that happens to sit on one - do not collide. A collision here reports
// "the test can hold the port with a plain bind" as a failure, which reads as a
// mod regression and is not one.
const std::uint16_t kPortBase =
    static_cast<std::uint16_t>(14000 + (GetCurrentProcessId() % 200) * 40);
const std::uint16_t kDiagnosticPort = kPortBase;
const std::uint16_t kRecoveryPortBase = static_cast<std::uint16_t>(kPortBase + 1);

const wchar_t* const kLogPathWide = L"tracker_feed_recovery.log";
const char* const kLogPath = "tracker_feed_recovery.log";

std::string ReadLog() {
    std::ifstream file(kLogPath, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// A 48-byte OpenTrack datagram: three doubles of position, three of rotation.
size_t BuildPacket(unsigned char out[48], double yaw, double pitch, double roll) {
    const double zero = 0.0;
    std::memcpy(out + 0, &zero, sizeof(double));
    std::memcpy(out + 8, &zero, sizeof(double));
    std::memcpy(out + 16, &zero, sizeof(double));
    std::memcpy(out + 24, &yaw, sizeof(double));
    std::memcpy(out + 32, &pitch, sizeof(double));
    std::memcpy(out + 40, &roll, sizeof(double));
    return 48;
}

// A stand-in for the tracker app: sends to the port from before it frees up
// until after tracking is live, so a recovery timing measures the mod
// reclaiming the port rather than the sender starting.
class Sender {
public:
    Sender(std::uint16_t targetPort, std::uint16_t ownPort) : m_targetPort(targetPort) {
        m_ok = m_socket.Open(ownPort);
        if (m_ok) m_thread = std::thread(&Sender::Run, this);
    }

    ~Sender() {
        m_stop.store(true);
        if (m_thread.joinable()) m_thread.join();
        m_socket.Close();
    }

    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;

    bool Ok() const { return m_ok; }

private:
    void Run() {
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(m_targetPort);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        unsigned char packet[48];
        double yaw = 0.0;
        while (!m_stop.load()) {
            // Walked rather than held: a bit-identical repeat is what a tracker
            // that has lost the head sends, and the receiver's dropout gate is
            // entitled to hold one back. Steps this small never trip its
            // large-jump hold either.
            yaw += 0.01;
            if (yaw > 5.0) yaw = 0.0;
            const size_t length = BuildPacket(packet, yaw, 1.0, 0.0);
            sendto(m_socket.GetHandle(), reinterpret_cast<const char*>(packet),
                   static_cast<int>(length), 0, reinterpret_cast<const sockaddr*>(&addr),
                   sizeof(addr));
            // 200Hz, so waiting for the next datagram adds at most 5ms to a
            // measured recovery time.
            SleepMs(5);
        }
    }

    std::uint16_t m_targetPort;
    bool m_ok = false;
    cameraunlock::UdpSocket m_socket;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
};

// Drives TrackerFeed the way the render hook does, and returns the milliseconds
// until the first frame that carries a pose, or -1 on timeout.
int64_t WaitForFirstPose(wolf_ht::TrackerFeed& feed, int timeoutMs) {
    const Clock::time_point start = Clock::now();
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    while (MsSince(start) < timeoutMs) {
        feed.Update(true);
        if (feed.GetRotationDegrees(yaw, pitch, roll)) return MsSince(start);
        SleepMs(1);
    }
    return -1;
}

// ---- The bind failure has to report the reason the OS gave -------------------

void TestBindFailureNamesTheOsError() {
    std::printf("bind failure diagnostic:\n");

    cameraunlock::UdpSocket occupier;
    if (!occupier.Open(kDiagnosticPort)) {
        Check(false, "the test can hold the port with a plain bind");
        return;
    }

    wolf_ht::Config config;
    config.udp_port = kDiagnosticPort;

    cameraunlock::logging::Open(kLogPathWide);
    {
        wolf_ht::TrackerFeed feed;
        feed.Start(config);
    }
    cameraunlock::logging::Close();
    occupier.Close();

    const std::string log = ReadLog();
    Check(log.find("bind failed with error") != std::string::npos,
          "the log names the call that failed");
    Check(log.find(std::to_string(WSAEADDRINUSE)) != std::string::npos,
          "the log carries the error code the OS actually returned");
    // Only the receiver's line knows why the bind failed, and it quotes the OS.
    // Anything the mod adds on top is a guess, and the guess that keeps getting
    // written is a port conflict.
    Check(log.find("busy") == std::string::npos &&
              log.find("another program") == std::string::npos &&
              log.find("another game") == std::string::npos,
          "no line asserts a cause the error code does not support");
    Check(log.find("retrying") != std::string::npos,
          "the log says the receiver keeps retrying");
}

// ---- Reclaiming the port, measured ------------------------------------------

struct Recovery {
    int64_t sinceStartMs = -1;  // when tracking went live, measured from Start()
    int64_t sinceFreedMs = -1;  // what the player waits, measured from the port freeing
    int64_t heldForMs = -1;     // how long the port was ACTUALLY held, sleep overshoot included
};

// Holds the port for holdMs after Start, frees it, and measures how long the
// mod takes to be delivering poses again.
Recovery MeasureRecovery(std::uint16_t port, std::uint16_t senderPort, int holdMs) {
    Recovery result;

    cameraunlock::UdpSocket occupier;
    if (!occupier.Open(port)) {
        Check(false, "the test can hold the port with a plain bind");
        return result;
    }

    Sender sender(port, senderPort);
    if (!sender.Ok()) {
        Check(false, "the test sender binds its own port");
        return result;
    }

    wolf_ht::Config config;
    config.udp_port = port;

    wolf_ht::TrackerFeed feed;
    const Clock::time_point started = Clock::now();
    feed.Start(config);

    const int64_t remaining = holdMs - MsSince(started);
    if (remaining > 0) SleepMs(static_cast<int>(remaining));

    // The other game exits.
    const int64_t heldFor = MsSince(started);
    occupier.Close();

    const int64_t waited = WaitForFirstPose(feed, 5000);
    if (waited < 0) return result;
    result.heldForMs = heldFor;

    result.sinceFreedMs = waited;
    result.sinceStartMs = heldFor + waited;
    return result;
}

void TestPortIsReclaimedPromptly() {
    std::printf("reclaiming a held tracker port:\n");

    // Four different moments to free the port, so the measurement covers the
    // whole retry period rather than one lucky phase of it. The holds either
    // side of a retry boundary are what makes the period itself visible.
    constexpr int kHoldCount = 4;
    const int holds[kHoldCount] = {120, 260, 640, 1100};
    Recovery measured[kHoldCount];

    for (int i = 0; i < kHoldCount; ++i) {
        measured[i] = MeasureRecovery(static_cast<std::uint16_t>(kRecoveryPortBase + i * 2),
                                      static_cast<std::uint16_t>(kRecoveryPortBase + i * 2 + 1),
                                      holds[i]);
        std::printf("    port freed %4dms after start: tracking live %4lldms later "
                    "(%4lldms after start)\n",
                    holds[i], static_cast<long long>(measured[i].sinceFreedMs),
                    static_cast<long long>(measured[i].sinceStartMs));
    }

    bool allRecovered = true;
    for (int i = 0; i < kHoldCount; ++i) {
        if (measured[i].sinceFreedMs < 0) allRecovered = false;
    }
    Check(allRecovered, "tracking comes back every time the port frees up, with no relaunch");
    if (!allRecovered) return;

    // What the player waits. The bound is the retry period plus the supervisor's
    // wake-up granularity plus room for a bind, a datagram and a frame - not a
    // target, a ceiling that a regression in any of those breaks.
    const int64_t bound = cameraunlock::UdpReceiver::kRetryIntervalMs + 100 + 250;
    bool withinBound = true;
    for (int i = 0; i < kHoldCount; ++i) {
        if (measured[i].sinceFreedMs > bound) withinBound = false;
    }
    Check(withinBound, "the wait after the port frees stays under the retry period plus a frame");

    // The cadence itself, read off the measurement rather than off the header:
    // the holds at 120ms and 640ms are one retry period apart, and so are those
    // at 640ms and 1100ms, so tracking going live has to be one period apart too.
    //
    // Checked against the epoch each hold ACTUALLY landed in, not the nominal
    // hold. sinceStartMs is quantised to the retry epochs and the holds sit
    // 360-400ms short of the next one, so a SleepMs that overshoots on a loaded
    // runner pushes one measurement a whole period out. The cadence below would
    // then fail with nothing wrong in src/ - so when the holds did not land where
    // they were aimed, say THAT and skip the cadence reading rather than
    // reporting a measurement of the sleep as a measurement of the receiver.
    const int64_t nominal = cameraunlock::UdpReceiver::kRetryIntervalMs;
    auto epochOf = [nominal](int64_t heldFor) { return (heldFor + nominal - 1) / nominal; };
    const bool sameEpochs = epochOf(measured[0].heldForMs) == epochOf(120) &&
                            epochOf(measured[2].heldForMs) == epochOf(640) &&
                            epochOf(measured[3].heldForMs) == epochOf(1100);
    if (!sameEpochs) {
        std::printf("  [SKIP] the measured retry cadence - the holds overshot their retry "
                    "epochs on this run, so the difference below would measure the sleep "
                    "rather than the receiver\n");
        return;
    }
    const int64_t firstPeriod = measured[2].sinceStartMs - measured[0].sinceStartMs;
    const int64_t secondPeriod = measured[3].sinceStartMs - measured[2].sinceStartMs;
    std::printf("    measured retry period: %lldms and %lldms (the receiver documents %lldms)\n",
                static_cast<long long>(firstPeriod), static_cast<long long>(secondPeriod),
                static_cast<long long>(nominal));

    Check(firstPeriod > nominal / 2 && firstPeriod < nominal * 3 / 2 &&
              secondPeriod > nominal / 2 && secondPeriod < nominal * 3 / 2,
          "the measured retry cadence matches the interval the receiver documents");
}

// ---- The reclaim is visible in the log --------------------------------------

void TestTheReclaimIsInTheLog() {
    std::printf("reclaim is reported:\n");

    const std::uint16_t port = kRecoveryPortBase + 10;
    cameraunlock::UdpSocket occupier;
    if (!occupier.Open(port)) {
        Check(false, "the test can hold the port with a plain bind");
        return;
    }

    Sender sender(port, static_cast<std::uint16_t>(port + 1));
    if (!sender.Ok()) {
        Check(false, "the test sender binds its own port");
        return;
    }

    wolf_ht::Config config;
    config.udp_port = port;

    cameraunlock::logging::Open(kLogPathWide);
    {
        wolf_ht::TrackerFeed feed;
        feed.Start(config);
        SleepMs(150);
        occupier.Close();
        Check(WaitForFirstPose(feed, 5000) >= 0, "tracking comes back");
    }
    cameraunlock::logging::Close();

    const std::string log = ReadLog();
    Check(log.find("Bound UDP port " + std::to_string(port)) != std::string::npos,
          "the log records the port being bound after the wait");
    Check(log.find("source connected on UDP " + std::to_string(port)) != std::string::npos,
          "the log records tracker data arriving");
}

// ---- The engine-boundary sign conversion ------------------------------------
//
// TrackerFeed is the one place the tracker's convention is turned into id
// Tech's, and nothing pinned it. Getting yaw or roll wrong ships a mod where a
// head turned right turns the view left - the commonest first in-game report on
// a new mod - and getting z wrong ships the mirrored-lean bug AGENTS.md names as
// a recurring fleet defect, where leaning in barely moves and pulling back moves
// a lot. Both are silent to every other suite here.
//
// This drives real loopback sockets through the real pipeline, so it measures
// the conversion as shipped rather than restating it.

// Sends one fixed pose, with a hair of movement on yaw so the receiver's
// identical-repeat gate never holds a packet back. The wobble is far below the
// tolerances asserted below.
class PoseSender {
public:
    PoseSender(std::uint16_t targetPort, std::uint16_t ownPort, double yaw, double pitch,
               double roll, double xCm, double yCm, double zCm)
        : m_targetPort(targetPort), m_yaw(yaw), m_pitch(pitch), m_roll(roll), m_x(xCm),
          m_y(yCm), m_z(zCm) {
        m_ok = m_socket.Open(ownPort);
        if (m_ok) m_thread = std::thread(&PoseSender::Run, this);
    }

    ~PoseSender() {
        m_stop.store(true);
        if (m_thread.joinable()) m_thread.join();
        m_socket.Close();
    }

    PoseSender(const PoseSender&) = delete;
    PoseSender& operator=(const PoseSender&) = delete;

    bool Ok() const { return m_ok; }

private:
    void Run() {
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(m_targetPort);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        unsigned char packet[48];
        double wobble = 0.0;
        while (!m_stop.load()) {
            wobble = (wobble == 0.0) ? 0.001 : 0.0;
            std::memcpy(packet + 0, &m_x, sizeof(double));
            std::memcpy(packet + 8, &m_y, sizeof(double));
            std::memcpy(packet + 16, &m_z, sizeof(double));
            const double yaw = m_yaw + wobble;
            std::memcpy(packet + 24, &yaw, sizeof(double));
            std::memcpy(packet + 32, &m_pitch, sizeof(double));
            std::memcpy(packet + 40, &m_roll, sizeof(double));
            sendto(m_socket.GetHandle(), reinterpret_cast<const char*>(packet), 48, 0,
                   reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
            SleepMs(5);
        }
    }

    std::uint16_t m_targetPort;
    double m_yaw, m_pitch, m_roll, m_x, m_y, m_z;
    bool m_ok = false;
    cameraunlock::UdpSocket m_socket;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
};

// Inches per metre, the conversion the boundary applies. Restated here on
// purpose: a test that imported the constant could not catch it changing.
constexpr float kInchesPerMetre = 39.3701f;

// Runs the feed until the pose has settled, then hands back what the boundary
// produced. Smoothing at LocalSmoothing 0.0 has a 20ms time constant, so half a
// second is many time constants of convergence.
struct BoundaryResult {
    bool haveRotation = false;
    bool havePosition = false;
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    float forward = 0.0f, left = 0.0f, up = 0.0f;
};

BoundaryResult SettleBoundary(wolf_ht::TrackerFeed& feed) {
    const Clock::time_point start = Clock::now();
    BoundaryResult result;
    while (MsSince(start) < 1500) {
        feed.Update(true);
        SleepMs(2);
    }
    result.haveRotation = feed.GetRotationDegrees(result.yaw, result.pitch, result.roll);
    result.havePosition = feed.GetPositionOffset(result.forward, result.left, result.up);
    return result;
}

void TestRotationSignsAtTheBoundary() {
    std::printf("rotation signs at the engine boundary:\n");

    const std::uint16_t port = kRecoveryPortBase + 20;
    PoseSender sender(port, static_cast<std::uint16_t>(port + 1), 20.0, 10.0, 15.0, 0, 0, 0);
    if (!sender.Ok()) {
        Check(false, "the test sender binds its own port");
        return;
    }

    wolf_ht::Config config;
    config.udp_port = port;
    wolf_ht::TrackerFeed feed;
    feed.Start(config);

    const BoundaryResult r = SettleBoundary(feed);
    Check(r.haveRotation, "a rotation arrives at all");
    if (!r.haveRotation) return;

    // The tracker calls a head turned right +yaw and the basis rotation calls a
    // view turned left +yaw, so yaw is negated here, once.
    CheckNear(r.yaw, -20.0f, 0.2f, "yaw is negated at the boundary");
    // Pitch and roll are not: both sides call looking up positive, and a head
    // tilted right tilts the rendered horizon the same way.
    CheckNear(r.roll, 15.0f, 0.2f, "roll passes through unnegated");
    CheckNear(r.pitch, 10.0f, 0.2f, "pitch passes through unnegated");
}

void TestForwardLeanIsNegativeProtocolZ() {
    std::printf("a forward lean is negative protocol z:\n");

    const std::uint16_t port = kRecoveryPortBase + 22;
    // -30cm of protocol z. The processor's clamp is [-LimitZ, +LimitZBack] =
    // [-0.40, +0.10], so -0.30m passes through untouched and the boundary turns
    // it into a lean FORWARD.
    PoseSender sender(port, static_cast<std::uint16_t>(port + 1), 0, 0, 0, 0, 0, -30.0);
    if (!sender.Ok()) {
        Check(false, "the test sender binds its own port");
        return;
    }

    wolf_ht::Config config;
    config.udp_port = port;
    wolf_ht::TrackerFeed feed;
    feed.Start(config);

    const BoundaryResult r = SettleBoundary(feed);
    Check(r.havePosition, "a position arrives at all");
    if (!r.havePosition) return;

    CheckNear(r.forward, 0.30f * kInchesPerMetre, 0.5f,
               "negative protocol z leans the eye FORWARD, in inches");
}

void TestBackwardLeanGetsTheTightLimit() {
    std::printf("the asymmetric z limit sits on the right side:\n");

    const std::uint16_t port = kRecoveryPortBase + 24;
    // +30cm of protocol z, which is a lean BACK. LimitZBack is 0.10m, so this
    // saturates at a tenth of a metre - a quarter of the forward range. If the z
    // sign were mirrored anywhere in the chain, this would come back as the
    // generous 0.40 instead, which is the documented symptom: leaning in barely
    // moves, pulling back moves a lot.
    PoseSender sender(port, static_cast<std::uint16_t>(port + 1), 0, 0, 0, 0, 0, 30.0);
    if (!sender.Ok()) {
        Check(false, "the test sender binds its own port");
        return;
    }

    wolf_ht::Config config;
    config.udp_port = port;
    wolf_ht::TrackerFeed feed;
    feed.Start(config);

    const BoundaryResult r = SettleBoundary(feed);
    Check(r.havePosition, "a position arrives at all");
    if (!r.havePosition) return;

    CheckNear(r.forward, -0.10f * kInchesPerMetre, 0.5f,
               "a backward lean clamps at LimitZBack, not at LimitZ");
}

void TestLateralAndVerticalSigns() {
    std::printf("lateral and vertical signs at the boundary:\n");

    const std::uint16_t port = kRecoveryPortBase + 26;
    // +20cm x (the head's right) and +10cm y (up); both inside their limits.
    PoseSender sender(port, static_cast<std::uint16_t>(port + 1), 0, 0, 0, 20.0, 10.0, 0);
    if (!sender.Ok()) {
        Check(false, "the test sender binds its own port");
        return;
    }

    wolf_ht::Config config;
    config.udp_port = port;
    wolf_ht::TrackerFeed feed;
    feed.Start(config);

    const BoundaryResult r = SettleBoundary(feed);
    Check(r.havePosition, "a position arrives at all");
    if (!r.havePosition) return;

    // x passes through unnegated. Its symptom is subtler than z's: leaning looks
    // like it works, it just goes the wrong way, which is what the negation
    // that used to sit here did in game.
    CheckNear(r.left, 0.20f * kInchesPerMetre, 0.5f, "protocol +x passes through unnegated");
    CheckNear(r.up, 0.10f * kInchesPerMetre, 0.5f, "protocol +y maps to id Tech +up");
}

void TestPositionLimitYDownBoundsTheDownwardTravel() {
    std::printf("PositionLimitYDown bounds the downward travel:\n");

    const std::uint16_t port = kRecoveryPortBase + 28;
    // 30cm down, against a PositionLimitYDown the player has tightened to 0.05m
    // while PositionLimitY stays at its default.
    PoseSender sender(port, static_cast<std::uint16_t>(port + 1), 0, 0, 0, 0, -30.0, 0);
    if (!sender.Ok()) {
        Check(false, "the test sender binds its own port");
        return;
    }

    wolf_ht::Config config;
    config.udp_port = port;
    config.position.limit_y_down = 0.05f;
    wolf_ht::TrackerFeed feed;
    feed.Start(config);

    const BoundaryResult r = SettleBoundary(feed);
    Check(r.havePosition, "a position arrives at all");
    if (!r.havePosition) return;

    // MakePositionSettings copies each limit across one by one. Drop the
    // downward one and the default 0.20 survives here: a player who tightened
    // it to 0.05 still gets four times the downward travel they asked for,
    // silently.
    CheckNear(r.up, -0.05f * kInchesPerMetre, 0.5f,
               "downward travel is bounded by PositionLimitYDown, not by the default");
}

void TestLocalSmoothingIsSelectedForLoopback() {
    std::printf("loopback selects LocalSmoothing:\n");

    const std::uint16_t port = kRecoveryPortBase + 30;
    PoseSender sender(port, static_cast<std::uint16_t>(port + 1), 5.0, 0, 0, 0, 0, 0);
    if (!sender.Ok()) {
        Check(false, "the test sender binds its own port");
        return;
    }

    wolf_ht::Config config;
    config.udp_port = port;

    cameraunlock::logging::Open(kLogPathWide);
    {
        wolf_ht::TrackerFeed feed;
        feed.Start(config);
        Check(WaitForFirstPose(feed, 5000) >= 0, "a pose arrives");
    }
    cameraunlock::logging::Close();

    // Swap the two arguments to GetEffectiveSmoothing, or invert the locality
    // flag, and every other check in this file still passes while a local user
    // silently gets RemoteSmoothing.
    const std::string log = ReadLog();
    Check(log.find("source is local, smoothing=0.000") != std::string::npos,
          "a loopback sender is classified LOCAL and gets LocalSmoothing");
}

}  // namespace

int main() {
    // Every check prints, passes included: this suite's output is the
    // measurement report the working notes quote.
    checks::ReportPasses(true);

    TestBindFailureNamesTheOsError();
    TestPortIsReclaimedPromptly();
    TestTheReclaimIsInTheLog();
    TestRotationSignsAtTheBoundary();
    TestForwardLeanIsNegativeProtocolZ();
    TestBackwardLeanGetsTheTightLimit();
    TestLateralAndVerticalSigns();
    TestPositionLimitYDownBoundsTheDownwardTravel();
    TestLocalSmoothingIsSelectedForLoopback();

    std::remove(kLogPath);
    std::remove("tracker_feed_recovery.prev.log");

    return checks::Summarize("tracker feed recovery");
}
