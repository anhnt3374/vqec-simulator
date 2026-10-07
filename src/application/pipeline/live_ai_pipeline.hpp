#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>

#include "ports/outbound/frame_sink.hpp"
#include "ports/outbound/object_detector.hpp"
#include "ports/outbound/object_tracker.hpp"

namespace vqec::app {
struct LiveAiOptions {
    double processing_fps = 0.0;
    std::size_t queue_capacity = 2;
    std::chrono::seconds stats_interval{10};
    double simulation_ai_fps = 0.0;
    std::chrono::seconds zone_dwell_time{5};
};

// Single coordinator-thread admission. PTS resets are treated as a fresh source session.
class FrameAdmission final {
public:
    explicit FrameAdmission(double fps);
    bool admit(std::chrono::nanoseconds pts);

private:
    std::chrono::nanoseconds interval_{};
    std::optional<std::chrono::nanoseconds> last_admitted_;
    std::optional<std::chrono::nanoseconds> last_seen_;
};

// Single-use run on one coordinator thread; joined capture/model/output workers.
// Input pixels remain immutable and each active frame is dispatched to both models.
class LiveAiPipeline final {
public:
    LiveAiPipeline(domain::StreamId stream_id, std::unique_ptr<ports::IFrameSource> source,
                   std::shared_ptr<ports::IObjectDetector> person,
                   std::shared_ptr<ports::IObjectDetector> face,
                   std::unique_ptr<ports::IFrameSink> sink, LiveAiOptions options = {},
                   std::unique_ptr<ports::IObjectTracker> person_tracker = {});
    // Thread-safe latest-update mailbox. Non-blocking except for the short mailbox lock;
    // validated gates are applied by the coordinator between frames. nullopt clears the gate.
    void request_gate_update(std::optional<domain::Gate> gate);
    // Same latest-update contract as gate updates; preserves the independent gate state.
    void request_zone_update(std::optional<domain::Zone> zone);
    Status run(const std::atomic<bool>& stop_requested);

private:
    std::mutex gate_mutex_;
    bool gate_pending_ = false;
    std::optional<domain::Gate> pending_gate_;
    bool zone_pending_ = false;
    std::optional<domain::Zone> pending_zone_;
    domain::StreamId stream_id_;
    std::unique_ptr<ports::IFrameSource> source_;
    std::shared_ptr<ports::IObjectDetector> person_;
    std::shared_ptr<ports::IObjectDetector> face_;
    std::unique_ptr<ports::IFrameSink> sink_;
    LiveAiOptions options_;
    std::unique_ptr<ports::IObjectTracker> person_tracker_;
};
}  // namespace vqec::app
