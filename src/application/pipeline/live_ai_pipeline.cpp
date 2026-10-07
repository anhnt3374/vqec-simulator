#include "application/pipeline/live_ai_pipeline.hpp"

#include <cassert>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

#include "application/pipeline/ai_simulation_budget.hpp"
#include "application/pipeline/live_line_monitor.hpp"
#include "application/pipeline/live_zone_monitor.hpp"
#include "common/bounded_queue.hpp"
#include "common/log.hpp"

namespace vqec::app {
namespace {
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::duration<double, std::milli>;
constexpr std::chrono::milliseconds kInitialReconnectDelay{250};
constexpr std::chrono::seconds kMaxReconnectDelay{5};
constexpr std::chrono::milliseconds kSimulationStopPoll{20};
struct CapturedFrame {
    domain::Frame frame;
    std::uint64_t session = 0;
};
struct EmbeddingJob {
    domain::Frame frame;
    std::uint64_t session;
    std::vector<FaceRecognitionRequest> requests;
};
struct EmbeddingResult {
    std::uint64_t sequence, session;
    Result<ports::EmbeddingBatch> embeddings;
};
constexpr std::size_t kEmbeddingQueueCapacity = 2;
constexpr std::uint64_t kEmbeddingWarningInterval = 100;
void embed_worker(ports::IFeatureExtractor& extractor, BoundedQueue<EmbeddingJob>& jobs,
                  BoundedQueue<EmbeddingResult>& results) {
    try {
        while (auto job = jobs.pop()) {
            auto embedded = [&]() -> Result<ports::EmbeddingBatch> {
                try {
                    std::vector<ports::FeatureRegion> regions;
                    for (const auto& request : job->requests)
                        regions.push_back(request.region);
                    return extractor.extract_batch(job->frame, regions);
                } catch (...) {
                    return make_error(ErrorCode::Internal, "embedding worker exception");
                }
            }();
            if (!results.push({job->frame.sequence, job->session, std::move(embedded)}))
                break;
        }
    } catch (...) {
        jobs.close();
    }
    results.close();
}
struct DetectorResult {
    std::uint64_t sequence;
    Result<std::vector<domain::Detection>> detections;
    Clock::duration elapsed;
};

void detect_worker(ports::IObjectDetector& detector, BoundedQueue<domain::Frame>& jobs,
                   BoundedQueue<DetectorResult>& results) {
    try {
        while (auto frame = jobs.pop()) {
            const auto started = Clock::now();
            auto detections = [&]() -> Result<std::vector<domain::Detection>> {
                try {
                    return detector.detect(*frame);
                } catch (const std::exception&) {
                    return make_error(ErrorCode::Internal, "detector worker exception");
                } catch (...) {
                    return make_error(ErrorCode::Internal, "unknown detector worker exception");
                }
            }();
            if (!results.push({frame->sequence, std::move(detections), Clock::now() - started}))
                break;
        }
    } catch (...) {
        // Closing both queues wakes the coordinator and translates thread failures to Status.
        jobs.close();
    }
    results.close();
}
}  // namespace

FrameAdmission::FrameAdmission(double fps) {
    assert(fps >= 0.0);
    if (fps > 0.0)
        interval_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(1.0 / fps));
}

bool FrameAdmission::admit(std::chrono::nanoseconds pts) {
    if (last_seen_ && pts < *last_seen_)
        last_admitted_.reset();
    last_seen_ = pts;
    if (last_admitted_ && interval_.count() > 0 && pts - *last_admitted_ < interval_)
        return false;
    last_admitted_ = pts;
    return true;
}

LiveAiPipeline::LiveAiPipeline(domain::StreamId stream_id,
                               std::unique_ptr<ports::IFrameSource> source,
                               std::shared_ptr<ports::IObjectDetector> person,
                               std::shared_ptr<ports::IObjectDetector> face,
                               std::unique_ptr<ports::IFrameSink> sink, LiveAiOptions options,
                               std::unique_ptr<ports::IObjectTracker> person_tracker,
                               std::shared_ptr<ports::IFeatureExtractor> extractor,
                               std::vector<domain::FaceGallerySample> gallery)
    : stream_id_(std::move(stream_id)),
      source_(std::move(source)),
      person_(std::move(person)),
      face_(std::move(face)),
      sink_(std::move(sink)),
      options_(options),
      person_tracker_(std::move(person_tracker)),
      extractor_(std::move(extractor)),
      gallery_(std::move(gallery)) {
    assert(source_ && person_ && face_ && sink_ && options_.queue_capacity > 0);
}

void LiveAiPipeline::request_gate_update(std::optional<domain::Gate> gate) {
    std::lock_guard<std::mutex> lock(gate_mutex_);
    pending_gate_ = std::move(gate);
    gate_pending_ = true;
}

void LiveAiPipeline::request_zone_update(std::optional<domain::Zone> zone) {
    std::lock_guard<std::mutex> lock(gate_mutex_);
    pending_zone_ = std::move(zone);
    zone_pending_ = true;
}

Status LiveAiPipeline::run(const std::atomic<bool>& stop_requested) {
    if (stop_requested.load())
        return {};
    auto opened = source_->open();
    if (!opened.ok()) {
        source_->close();
        return opened;
    }
    opened = sink_->open(source_->info());
    if (!opened.ok()) {
        sink_->close();
        source_->close();
        return opened;
    }
    const bool live = source_->info().is_live;
    BoundedQueue<CapturedFrame> captured(options_.queue_capacity,
                                         live ? OverflowPolicy::DropOldest : OverflowPolicy::Block);
    BoundedQueue<domain::Frame> person_jobs(1, OverflowPolicy::Block),
        face_jobs(1, OverflowPolicy::Block);
    BoundedQueue<DetectorResult> person_results(1, OverflowPolicy::Block),
        face_results(1, OverflowPolicy::Block);
    BoundedQueue<EmbeddingJob> embedding_jobs(kEmbeddingQueueCapacity, OverflowPolicy::Block);
    BoundedQueue<EmbeddingResult> embedding_results(kEmbeddingQueueCapacity, OverflowPolicy::Block);
    FaceRecognition recognition(options_.recognition, gallery_);
    std::uint64_t embedding_count = 0, embedding_failures = 0, recognized_count = 0;
    std::uint64_t batch_failures = 0;
    std::map<ErrorCode, std::uint64_t> region_failures;
    std::atomic<bool> cancelled{false};
    std::atomic<std::uint64_t> capture_count{0};
    std::atomic<std::uint64_t> source_session{0};
    std::optional<Error> capture_error;  // Read only after capture thread joins.
    std::mutex reconnect_mutex;
    std::condition_variable reconnect_ready;
    std::thread person_thread, face_thread, embedding_thread, capture_thread;
    auto shutdown = [&] {
        cancelled.store(true);
        reconnect_ready.notify_all();
        captured.close();
        person_jobs.close();
        face_jobs.close();
        person_results.close();
        face_results.close();
        embedding_jobs.close();
        embedding_results.close();
        if (capture_thread.joinable())
            capture_thread.join();
        if (embedding_thread.joinable())
            embedding_thread.join();
        if (face_thread.joinable())
            face_thread.join();
        if (person_thread.joinable())
            person_thread.join();
        sink_->close();
        source_->close();
    };
    Status status;
    try {
        person_thread = std::thread([&] { detect_worker(*person_, person_jobs, person_results); });
        face_thread = std::thread([&] { detect_worker(*face_, face_jobs, face_results); });
        if (extractor_)
            embedding_thread =
                std::thread([&] { embed_worker(*extractor_, embedding_jobs, embedding_results); });
        capture_thread = std::thread([&] {
            try {
                std::uint64_t sequence = 0, session = 0;
                while (!cancelled.load() && !stop_requested.load()) {
                    auto frame = source_->read();
                    if (!frame.ok()) {
                        if (frame.error().code == ErrorCode::EndOfStream || stop_requested.load() ||
                            cancelled.load())
                            break;
                        if (!live) {
                            capture_error = frame.error();
                            break;
                        }
                        // Invalidate queued and in-flight work before releasing the source.
                        source_session.store(++session);
                        source_->close();
                        VQEC_LOG_WARN("[stream={}] source reconnect code={} message={}", stream_id_,
                                      to_string(frame.error().code), frame.error().message);
                        auto delay = kInitialReconnectDelay;
                        std::uint64_t attempts = 0;
                        while (!cancelled.load() && !stop_requested.load()) {
                            {
                                std::unique_lock<std::mutex> lock(reconnect_mutex);
                                reconnect_ready.wait_for(lock, delay, [&] {
                                    return cancelled.load() || stop_requested.load();
                                });
                            }
                            if (cancelled.load() || stop_requested.load())
                                break;
                            const auto reconnected = source_->open();
                            ++attempts;
                            if (reconnected.ok()) {
                                VQEC_LOG_INFO("[stream={}] source reconnected attempts={}",
                                              stream_id_, attempts);
                                break;
                            }
                            if (attempts == 1 || attempts % 10 == 0)
                                VQEC_LOG_WARN(
                                    "[stream={}] reconnect failed attempts={} backoff_ms={} "
                                    "code={}",
                                    stream_id_, attempts, delay.count(),
                                    to_string(reconnected.error().code));
                            delay = std::min(delay * 2,
                                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 kMaxReconnectDelay));
                        }
                        continue;
                    }
                    frame.value().sequence = sequence++;
                    frame.value().stream_id = stream_id_;
                    capture_count.fetch_add(1);
                    if (!captured.push({std::move(frame).value(), session}))
                        break;
                }
            } catch (const std::exception&) {
                capture_error = make_error(ErrorCode::Internal, "capture worker exception");
            } catch (...) {
                capture_error = make_error(ErrorCode::Internal, "unknown capture worker exception");
            }
            captured.close();
        });
        VQEC_LOG_INFO(
            "[stream={}] AI pipeline opened person=nanodet_plus_m_320 face=yunet_2023mar "
            "processing_fps={} simulation_ai_fps={} parallel_workers={}",
            stream_id_, options_.processing_fps, options_.simulation_ai_fps, extractor_ ? 3 : 2);
        FrameAdmission admission(options_.processing_fps);
        std::uint64_t active_session = 0;
        std::optional<std::chrono::nanoseconds> previous_pts;
        Clock::duration tracking_time{}, simulation_wait_time{};
        const AiSimulationBudget simulation_budget(options_.simulation_ai_fps);
        auto window_start = Clock::now();
        std::uint64_t processed = 0, skipped = 0, previous_capture = 0, previous_drops = 0;
        Clock::duration person_time{}, face_time{}, pair_time{};
        auto scene = std::make_shared<const domain::Scene>();
        LiveLineMonitor line_monitor;
        LiveZoneMonitor zone_monitor(options_.zone_dwell_time);
        while (!stop_requested.load()) {
            auto captured_frame = captured.pop();
            if (!captured_frame)
                break;
            if (captured_frame->session != source_session.load())
                continue;
            auto* frame = &captured_frame->frame;
            if (captured_frame->session != active_session ||
                (previous_pts && frame->pts < *previous_pts)) {
                if (person_tracker_)
                    person_tracker_->reset();
                line_monitor.reset();
                zone_monitor.reset();
                recognition.reset();
                admission = FrameAdmission(options_.processing_fps);
                active_session = captured_frame->session;
            }
            std::optional<domain::Gate> updated_gate;
            bool gate_changed = false;
            std::optional<domain::Zone> updated_zone;
            bool zone_changed = false;
            {
                std::lock_guard<std::mutex> lock(gate_mutex_);
                if (gate_pending_) {
                    updated_gate = std::move(pending_gate_);
                    gate_pending_ = false;
                    gate_changed = true;
                }
                if (zone_pending_) {
                    updated_zone = std::move(pending_zone_);
                    zone_pending_ = false;
                    zone_changed = true;
                }
            }
            if (gate_changed) {
                auto updated_scene = std::make_shared<domain::Scene>(*scene);
                updated_scene->gates.clear();
                if (updated_gate)
                    updated_scene->gates.push_back(*updated_gate);
                scene = std::move(updated_scene);
                line_monitor.set_gate(std::move(updated_gate));
                VQEC_LOG_INFO("[stream={}] temporary line applied gates={}", stream_id_,
                              scene->gates.size());
            }
            if (zone_changed) {
                auto updated_scene = std::make_shared<domain::Scene>(*scene);
                updated_scene->zones.clear();
                if (updated_zone)
                    updated_scene->zones.push_back(*updated_zone);
                scene = std::move(updated_scene);
                zone_monitor.set_zone(std::move(updated_zone));
                VQEC_LOG_INFO("[stream={}] temporary zone applied zones={}", stream_id_,
                              scene->zones.size());
            }
            previous_pts = frame->pts;
            if (!admission.admit(frame->pts)) {
                ++skipped;
                continue;
            }
            const auto pair_start = Clock::now();
            if (!person_jobs.push(*frame) || !face_jobs.push(*frame)) {
                status = make_error(ErrorCode::Internal, "detector worker queue closed");
                break;
            }
            auto person = person_results.pop();
            auto face = face_results.pop();
            if (!person || !face || person->sequence != frame->sequence ||
                face->sequence != frame->sequence) {
                status =
                    make_error(ErrorCode::Internal, "detector results do not match admitted frame");
                break;
            }
            if (!person->detections.ok()) {
                status = person->detections.status();
                break;
            }
            if (!face->detections.ok()) {
                status = face->detections.status();
                break;
            }
            ports::FrameAnnotations annotations;
            annotations.scene = scene;
            const auto tracking_start = Clock::now();
            if (person_tracker_)
                annotations.tracks = person_tracker_->update(*frame, person->detections.value());
            else
                for (const auto& detection : person->detections.value())
                    annotations.tracks.push_back({0, domain::TrackState::Tentative, detection.box,
                                                  detection.class_id, detection.label,
                                                  detection.score});
            tracking_time += Clock::now() - tracking_start;
            if (extractor_) {
                auto requests =
                    recognition.select(*frame, annotations.tracks, face->detections.value());
                if (!requests.empty()) {
                    embedding_count += requests.size();
                    if (!embedding_jobs.push({*frame, active_session, requests})) {
                        status = make_error(ErrorCode::Internal, "embedding worker stopped");
                        break;
                    }
                    auto embedded = embedding_results.pop();
                    if (!embedded || embedded->sequence != frame->sequence ||
                        embedded->session != active_session) {
                        status = make_error(ErrorCode::Internal,
                                            "embedding result frame/session mismatch");
                        break;
                    }
                    if (stop_requested.load())
                        break;
                    if (source_session.load() != active_session)
                        continue;
                    if (embedded->embeddings.ok()) {
                        std::size_t region_index = 0;
                        for (const auto& output : embedded->embeddings.value()) {
                            if (!output.ok()) {
                                ++embedding_failures;
                                const auto count = ++region_failures[output.error().code];
                                if (count == 1 || count % kEmbeddingWarningInterval == 0)
                                    VQEC_LOG_WARN(
                                        "[stream={}] embedding region rejected "
                                        "failures_for_code={} "
                                        "person_track_id={} code={} message={}",
                                        stream_id_, count,
                                        region_index < requests.size()
                                            ? requests[region_index].track_id
                                            : 0,
                                        to_string(output.error().code), output.error().message);
                            }
                            ++region_index;
                        }
                        auto events =
                            recognition.apply(*frame, requests, embedded->embeddings.value());
                        recognized_count += events.size();
                        for (auto& event : events)
                            annotations.events.push_back(std::move(event));
                    } else {
                        ++embedding_failures;
                        ++batch_failures;
                        ports::EmbeddingBatch failed;
                        for (std::size_t i = 0; i < requests.size(); ++i)
                            failed.emplace_back(embedded->embeddings.error());
                        recognition.apply(*frame, requests, failed);
                        if (batch_failures == 1 || batch_failures % kEmbeddingWarningInterval == 0)
                            VQEC_LOG_WARN(
                                "[stream={}] embedding failed failures={} code={} message={}",
                                stream_id_, batch_failures,
                                to_string(embedded->embeddings.error().code),
                                embedded->embeddings.error().message);
                    }
                }
            }
            if (stop_requested.load())
                break;
            if (source_session.load() != active_session)
                continue;
            line_monitor.annotate(*frame, annotations);
            zone_monitor.annotate(*frame, annotations);
            recognition.annotate(*frame, annotations);
            for (const auto& event : annotations.events) {
                const auto utc_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        event.occurred_at.time_since_epoch())
                                        .count();
                if (event.type == FaceRecognition::kAttendanceEvent) {
                    VQEC_LOG_INFO(
                        "[stream={}] event={} employee_id={} person_track_id={} line_id={} "
                        "direction={} frame_sequence={} pts_ms={} occurred_at_utc_ms={}",
                        stream_id_, event.type,
                        event.attributes.at(FaceRecognition::kEmployeeIdAttribute),
                        event.track_id.value_or(0), event.gate_id.value_or(""),
                        event.attributes.at("direction"), event.frame_sequence,
                        event.attributes.at("pts_ms"), utc_ms);
                    continue;
                }
                if (event.type == FaceRecognition::kRecognizedEvent) {
                    VQEC_LOG_INFO(
                        "[stream={}] event={} employee_id={} person_track_id={} score={} "
                        "second_score={} gap={} confirmation={} frame_sequence={} pts_ms={} "
                        "occurred_at_utc_ms={}",
                        stream_id_, event.type, event.attributes.at("employee_id"),
                        event.track_id.value_or(0), event.attributes.at("score"),
                        event.attributes.count("second_score") ? event.attributes.at("second_score")
                                                               : "none",
                        event.attributes.count("gap") ? event.attributes.at("gap") : "none",
                        event.attributes.at("confirmation"), event.frame_sequence,
                        event.attributes.at("pts_ms"), utc_ms);
                    continue;
                }
                VQEC_LOG_INFO(
                    "[stream={}] event={} gate={} zone={} track={} direction={} "
                    "frame_sequence={} occurred_at_utc_ms={}",
                    stream_id_, event.type, event.gate_id.value_or(""), event.zone_id.value_or(""),
                    event.track_id.value_or(0),
                    event.attributes.count("direction") ? event.attributes.at("direction") : "none",
                    event.frame_sequence, utc_ms);
            }
            const auto simulation_start = Clock::now();
            const auto unused =
                simulation_budget.remaining(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    simulation_start - pair_start));
            if (unused.count() > 0) {
                const auto deadline = simulation_start + unused;
                std::unique_lock<std::mutex> lock(reconnect_mutex);
                // Timed wait models occupied AI service time. Poll the atomic signal flag
                // so SIGTERM can interrupt a long simulation budget without a helper thread.
                while (!stop_requested.load() && !cancelled.load() && Clock::now() < deadline)
                    reconnect_ready.wait_until(
                        lock, std::min(deadline, Clock::now() + kSimulationStopPoll),
                        [&] { return stop_requested.load() || cancelled.load(); });
            }
            simulation_wait_time += Clock::now() - simulation_start;
            if (stop_requested.load() || cancelled.load())
                break;
            status = sink_->write(*frame, annotations);
            if (!status.ok())
                break;
            person_time += person->elapsed;
            face_time += face->elapsed;
            pair_time += Clock::now() - pair_start;
            ++processed;
            const auto now = Clock::now();
            if (now - window_start >= options_.stats_interval) {
                if (extractor_)
                    VQEC_LOG_INFO(
                        "[stream={}] recognition_stats embedding_faces={} embedding_failures={} "
                        "recognized={} source_rejected={} unowned={} deferred={}",
                        stream_id_, embedding_count, embedding_failures, recognized_count,
                        recognition.stats().source_rejected, recognition.stats().unowned,
                        recognition.stats().deferred);
                const double seconds = std::chrono::duration<double>(now - window_start).count();
                const auto count = capture_count.load();
                const auto drops = captured.dropped_count();
                VQEC_LOG_INFO(
                    "[stream={}] input_fps={:.2f} processed_fps={:.2f} processed={} fps_skipped={} "
                    "person_ms={:.2f} face_ms={:.2f} pair_ms={:.2f} tracking_ms={:.2f} "
                    "simulation_wait_ms={:.2f} queue_depth={} dropped={}",
                    stream_id_, static_cast<double>(count - previous_capture) / seconds,
                    static_cast<double>(processed) / seconds, processed, skipped,
                    Milliseconds(person_time).count() / static_cast<double>(processed),
                    Milliseconds(face_time).count() / static_cast<double>(processed),
                    Milliseconds(pair_time).count() / static_cast<double>(processed),
                    Milliseconds(tracking_time).count() / static_cast<double>(processed),
                    Milliseconds(simulation_wait_time).count() / static_cast<double>(processed),
                    captured.size(), drops);
                if (drops > previous_drops)
                    VQEC_LOG_WARN("[stream={}] dropped_input_frames={} total={}", stream_id_,
                                  drops - previous_drops, drops);
                previous_capture = count;
                previous_drops = drops;
                window_start = now;
                processed = 0;
                skipped = 0;
                person_time = {};
                face_time = {};
                pair_time = {};
                tracking_time = {};
                simulation_wait_time = {};
            }
        }
    } catch (const std::exception&) {
        status = make_error(ErrorCode::Internal, "AI pipeline orchestration failed");
    } catch (...) {
        status = make_error(ErrorCode::Internal, "unknown AI pipeline orchestration failure");
    }
    shutdown();
    if (status.ok() && capture_error)
        status = *capture_error;
    VQEC_LOG_INFO("[stream={}] AI pipeline closed captured={} dropped={}", stream_id_,
                  capture_count.load(), captured.dropped_count());
    return status;
}
}  // namespace vqec::app
