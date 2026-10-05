#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "application/solutions/solution.hpp"
#include "common/result.hpp"
#include "domain/event/event.hpp"
#include "domain/ids.hpp"
#include "domain/scene/scene.hpp"
#include "ports/outbound/event_publisher.hpp"
#include "ports/outbound/event_repository.hpp"
#include "ports/outbound/frame_sink.hpp"
#include "ports/outbound/frame_source.hpp"
#include "ports/outbound/object_detector.hpp"
#include "ports/outbound/object_tracker.hpp"

namespace vqec::app {

// Adapters owned by exactly one stream.
struct StreamComponents {
    std::unique_ptr<ports::IFrameSource> source;
    std::unique_ptr<ports::IObjectTracker> tracker;
    std::vector<std::unique_ptr<ports::IFrameSink>> sinks;
    std::vector<std::unique_ptr<ISolution>> solutions;
};

// Thread-safe services shared by all streams; created by bootstrap. None may be null.
struct SharedServices {
    std::shared_ptr<ports::IObjectDetector> detector;
    std::shared_ptr<ports::IEventRepository> event_repository;
    std::shared_ptr<ports::IEventPublisher> event_publisher;
};

struct PipelineOptions {
    static constexpr std::chrono::seconds kDefaultStatsInterval{10};

    std::chrono::seconds stats_interval = kDefaultStatsInterval;  // Periodic stats log line.
};

// Runs one stream: source -> detector -> tracker -> solutions -> events + sinks.
// Version 1 runs all stages sequentially on the caller's thread; stages are split onto threads
// connected by BoundedQueue only when profiling shows the need.
class StreamPipeline {
public:
    StreamPipeline(domain::StreamId stream_id, StreamComponents components, SharedServices services,
                   domain::Scene initial_scene, PipelineOptions options = {});

    StreamPipeline(const StreamPipeline&) = delete;
    StreamPipeline& operator=(const StreamPipeline&) = delete;
    StreamPipeline(StreamPipeline&&) = delete;
    StreamPipeline& operator=(StreamPipeline&&) = delete;
    ~StreamPipeline() = default;

    // Thread-safe. The scene is applied before the next frame is processed.
    void request_scene_update(domain::Scene scene);

    // The methods below must be called from one thread (the stream thread).
    Status open();
    // Pulls one frame through every stage. Returns EndOfStream when a file source is exhausted.
    Status process_next_frame();
    void close();
    // open() + process_next_frame() until stop is requested or an error occurs + close().
    Status run(const std::atomic<bool>& stop_requested);

    const domain::StreamId& stream_id() const { return stream_id_; }
    std::uint64_t frames_processed() const { return frames_processed_.load(); }

private:
    using Clock = std::chrono::steady_clock;

    struct StageTimes {
        Clock::duration read{};
        Clock::duration detect{};
        Clock::duration track{};
        Clock::duration solutions{};
        Clock::duration output{};
    };

    void apply_pending_scene();
    void apply_scene(domain::Scene scene);
    void record_events(const std::vector<domain::Event>& events);
    void accumulate_stats(const StageTimes& times, std::size_t event_count);
    void log_stats_if_due();

    const domain::StreamId stream_id_;
    StreamComponents components_;
    SharedServices services_;
    const PipelineOptions options_;
    std::shared_ptr<const domain::Scene> scene_;

    std::mutex pending_scene_mutex_;
    std::optional<domain::Scene> pending_scene_;

    std::atomic<std::uint64_t> frames_processed_{0};

    // Stats window, touched only by the stream thread.
    Clock::time_point window_start_{};
    std::uint64_t window_frames_ = 0;
    std::uint64_t window_events_ = 0;
    std::uint64_t window_event_save_failures_ = 0;
    StageTimes window_times_;
};

}  // namespace vqec::app
