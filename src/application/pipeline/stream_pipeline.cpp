#include "application/pipeline/stream_pipeline.hpp"

#include <cassert>
#include <iterator>
#include <utility>

#include "common/log.hpp"
#include "common/uri.hpp"

namespace vqec::app {
namespace {

using Milliseconds = std::chrono::duration<double, std::milli>;

double average_ms(std::chrono::steady_clock::duration total, std::uint64_t count) {
    return count == 0 ? 0.0 : Milliseconds(total).count() / static_cast<double>(count);
}

}  // namespace

StreamPipeline::StreamPipeline(domain::StreamId stream_id, StreamComponents components,
                               SharedServices services, domain::Scene initial_scene,
                               PipelineOptions options)
    : stream_id_(std::move(stream_id)),
      components_(std::move(components)),
      services_(std::move(services)),
      options_(options) {
    assert(components_.source && components_.tracker);
    assert(services_.detector && services_.event_repository && services_.event_publisher);
    apply_scene(std::move(initial_scene));
}

void StreamPipeline::request_scene_update(domain::Scene scene) {
    const std::lock_guard<std::mutex> lock(pending_scene_mutex_);
    pending_scene_ = std::move(scene);
}

Status StreamPipeline::open() {
    if (Status status = components_.source->open(); !status.ok()) {
        VQEC_LOG_ERROR("[stream={}] cannot open source: {} ({})", stream_id_,
                       status.error().message, to_string(status.error().code));
        return status;
    }
    const ports::SourceInfo info = components_.source->info();
    VQEC_LOG_INFO("[stream={}] source opened: {} {}x{} @ {:.1f} fps ({})", stream_id_,
                  redact_uri(info.uri), info.width, info.height, info.fps,
                  info.is_live ? "live" : "file");
    for (const auto& sink : components_.sinks) {
        if (Status status = sink->open(info); !status.ok()) {
            VQEC_LOG_ERROR("[stream={}] cannot open sink: {} ({})", stream_id_,
                           status.error().message, to_string(status.error().code));
            close();
            return status;
        }
    }
    components_.tracker->reset();
    window_start_ = Clock::now();
    VQEC_LOG_INFO("[stream={}] pipeline opened: {} sink(s), {} solution(s)", stream_id_,
                  components_.sinks.size(), components_.solutions.size());
    return {};
}

Status StreamPipeline::process_next_frame() {
    apply_pending_scene();
    StageTimes times;

    Clock::time_point stage_start = Clock::now();
    Result<domain::Frame> frame = components_.source->read();
    times.read = Clock::now() - stage_start;
    if (!frame.ok()) {
        return frame.status();
    }

    stage_start = Clock::now();
    Result<std::vector<domain::Detection>> detections = services_.detector->detect(frame.value());
    times.detect = Clock::now() - stage_start;
    if (!detections.ok()) {
        VQEC_LOG_ERROR("[stream={}] detection failed on frame {}: {} ({})", stream_id_,
                       frame.value().sequence, detections.error().message,
                       to_string(detections.error().code));
        return detections.status();
    }

    stage_start = Clock::now();
    FrameContext context;
    context.frame = std::move(frame).value();
    context.tracks = components_.tracker->update(context.frame, detections.value());
    times.track = Clock::now() - stage_start;
    VQEC_LOG_TRACE("[stream={}] frame {}: {} detection(s), {} track(s)", stream_id_,
                   context.frame.sequence, detections.value().size(), context.tracks.size());

    stage_start = Clock::now();
    std::vector<domain::Event> events;
    for (const auto& solution : components_.solutions) {
        std::vector<domain::Event> produced = solution->process(context);
        events.insert(events.end(), std::make_move_iterator(produced.begin()),
                      std::make_move_iterator(produced.end()));
    }
    times.solutions = Clock::now() - stage_start;

    stage_start = Clock::now();
    record_events(events);
    const std::size_t event_count = events.size();
    const ports::FrameAnnotations annotations{std::move(context.tracks), scene_, std::move(events)};
    for (const auto& sink : components_.sinks) {
        if (Status status = sink->write(context.frame, annotations); !status.ok()) {
            VQEC_LOG_ERROR("[stream={}] sink failed on frame {}: {} ({})", stream_id_,
                           context.frame.sequence, status.error().message,
                           to_string(status.error().code));
            return status;
        }
    }
    times.output = Clock::now() - stage_start;

    frames_processed_.fetch_add(1);
    accumulate_stats(times, event_count);
    log_stats_if_due();
    return {};
}

void StreamPipeline::close() {
    for (const auto& sink : components_.sinks) {
        sink->close();
    }
    components_.source->close();
    VQEC_LOG_INFO("[stream={}] pipeline closed after {} frame(s)", stream_id_,
                  frames_processed_.load());
}

Status StreamPipeline::run(const std::atomic<bool>& stop_requested) {
    if (Status status = open(); !status.ok()) {
        return status;
    }
    Status result;
    while (!stop_requested.load()) {
        result = process_next_frame();
        if (!result.ok()) {
            break;
        }
    }
    if (result.ok()) {
        VQEC_LOG_INFO("[stream={}] stop requested", stream_id_);
    } else if (result.error().code == ErrorCode::EndOfStream) {
        VQEC_LOG_INFO("[stream={}] end of stream", stream_id_);
    } else {
        VQEC_LOG_ERROR("[stream={}] pipeline stopped: {} ({})", stream_id_, result.error().message,
                       to_string(result.error().code));
    }
    close();
    return result;
}

void StreamPipeline::apply_pending_scene() {
    std::optional<domain::Scene> pending;
    {
        const std::lock_guard<std::mutex> lock(pending_scene_mutex_);
        pending.swap(pending_scene_);
    }
    if (pending) {
        apply_scene(std::move(*pending));
    }
}

void StreamPipeline::apply_scene(domain::Scene scene) {
    scene_ = std::make_shared<const domain::Scene>(std::move(scene));
    for (const auto& solution : components_.solutions) {
        solution->set_scene(*scene_);
    }
    VQEC_LOG_INFO("[stream={}] scene applied: {} zone(s), {} gate(s)", stream_id_,
                  scene_->zones.size(), scene_->gates.size());
}

void StreamPipeline::record_events(const std::vector<domain::Event>& events) {
    for (const domain::Event& event : events) {
        VQEC_LOG_INFO("[stream={}] event {} frame={} track={} zone={} gate={}", stream_id_,
                      event.type, event.frame_sequence,
                      event.track_id ? std::to_string(*event.track_id) : "-",
                      event.zone_id.value_or("-"), event.gate_id.value_or("-"));
        if (Status status = services_.event_repository->save(event); !status.ok()) {
            // Video keeps flowing; the failure is counted and reported in the stats line.
            ++window_event_save_failures_;
            VQEC_LOG_DEBUG("[stream={}] event {} not persisted: {}", stream_id_, event.type,
                           status.error().message);
        }
        services_.event_publisher->publish(event);
    }
}

void StreamPipeline::accumulate_stats(const StageTimes& times, std::size_t event_count) {
    ++window_frames_;
    window_events_ += event_count;
    window_times_.read += times.read;
    window_times_.detect += times.detect;
    window_times_.track += times.track;
    window_times_.solutions += times.solutions;
    window_times_.output += times.output;
}

void StreamPipeline::log_stats_if_due() {
    const Clock::time_point now = Clock::now();
    const Clock::duration elapsed = now - window_start_;
    if (elapsed < options_.stats_interval) {
        return;
    }
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double fps = static_cast<double>(window_frames_) / seconds;
    VQEC_LOG_INFO(
        "[stream={}] stats: {:.1f} fps, {} event(s), avg ms read={:.2f} detect={:.2f} "
        "track={:.2f} solutions={:.2f} output={:.2f}",
        stream_id_, fps, window_events_, average_ms(window_times_.read, window_frames_),
        average_ms(window_times_.detect, window_frames_),
        average_ms(window_times_.track, window_frames_),
        average_ms(window_times_.solutions, window_frames_),
        average_ms(window_times_.output, window_frames_));
    if (window_event_save_failures_ > 0) {
        VQEC_LOG_WARN("[stream={}] {} event(s) could not be persisted in the last {:.0f} s",
                      stream_id_, window_event_save_failures_, seconds);
    }
    window_start_ = now;
    window_frames_ = 0;
    window_events_ = 0;
    window_event_save_failures_ = 0;
    window_times_ = StageTimes{};
}

}  // namespace vqec::app
