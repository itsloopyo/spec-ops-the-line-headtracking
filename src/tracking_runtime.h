#pragma once

#include "config.h"

#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

#include <atomic>

namespace SpecOpsTheLineHeadTracking {

// One frame's processed head pose: rotation in degrees (YPR) and position offset
// in metres (tracker basis: x=right, y=up, z=forward). has_* report whether each
// channel produced fresh data this frame.
struct FrameSample {
    bool  has_rotation = false;
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    bool  has_position = false;
    float pos_x = 0.0f, pos_y = 0.0f, pos_z = 0.0f;
};

class TrackingRuntime {
public:
    TrackingRuntime() : m_session(m_receiver) {}

    // No return value: a receiver that cannot bind immediately is NOT a failure. The
    // port is usually held by another game's mod that is still shutting down, and core's
    // supervisor retries until it frees up, so the mod carries on and tracking starts
    // when the port does.
    void Start(const Config& cfg);
    void Stop();

    // Runs the per-frame pipeline once and returns the processed pose. Called
    // from the camera hook on the render thread.
    FrameSample SampleFrame();

    // Reports whether the UDP receiver is currently observing packets, on the core's
    // own fixed liveness window. The heartbeat reports this; the per-frame gate uses
    // the user's DataFreshnessMs instead.
    bool IsReceiving() const { return m_receiver.IsReceiving(); }

    void ToggleEnabled();
    void CycleTrackingMode();
    void ToggleYawMode();

    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(std::memory_order_relaxed); }

private:
    static constexpr float kMaxFrameDtSec = 0.25f;

    // True while the newest packet is younger than Config::data_freshness_ms. The
    // core receiver's own IsReceiving() is fixed at 500ms, which is where the
    // shipped default comes from; this is what makes a user-chosen window mean
    // anything.
    bool IsPoseFresh() const;

    // m_held masked by the tracking mode in force right now.
    FrameSample HeldForCurrentMode() const;

    Config m_cfg{};
    cameraunlock::UdpReceiver m_receiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session;
    cameraunlock::time::FrameClock m_clock{kMaxFrameDtSec};

    // The pose last injected, re-applied while the tracker is quiet so a gap holds
    // the view where it was. Touched only from SampleFrame, i.e. only on the thread
    // the camera detour runs on.
    FrameSample m_held{};

    std::atomic<bool> m_started{false};
    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_worldSpaceYaw{true};
};

}  // namespace SpecOpsTheLineHeadTracking
