#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "adapters/outbound/inference/models/nanodet_detector.hpp"
#include "adapters/outbound/inference/models/yunet_detector.hpp"
#include "adapters/outbound/inference/runtimes/onnxruntime/onnx_runtime_engine.hpp"
#include "adapters/outbound/media/ffmpeg/ffmpeg_frame_sink.hpp"
#include "adapters/outbound/media/ffmpeg/ffmpeg_frame_source.hpp"
#include "adapters/outbound/tracking/bytetrack/byte_tracker.hpp"
#include "application/pipeline/live_ai_pipeline.hpp"
#include "bootstrap/live_ai_config.hpp"
#include "bootstrap/live_line_bridge.hpp"
#include "common/log.hpp"
#include "common/uri.hpp"

namespace {
constexpr int kExitConfig = 78;
constexpr int kExitUnavailable = 69;
std::atomic<bool> stop_requested{false};
static_assert(std::atomic<bool>::is_always_lock_free,
              "signal handler requires lock-free stop flag");
void request_stop(int /*signal*/) {
    stop_requested.store(true);
}

int run(int argc, char** argv) {
    std::string config_path;
    bool check_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--config" && i + 1 < argc)
            config_path = argv[++i];
        else if (argument == "--check-config")
            check_only = true;
        else {
            std::cerr << "Usage: vqec_live_ai --config <file> [--check-config]\n";
            return 64;
        }
    }
    if (config_path.empty()) {
        std::cerr << "--config is required\n";
        return 64;
    }
    auto parsed = vqec::bootstrap::load_live_ai_config(config_path);
    if (!parsed.ok()) {
        std::cerr << parsed.error().message << '\n';
        return kExitConfig;
    }
    const auto config = std::move(parsed).value();
    auto status = vqec::log::init(config.logging);
    if (!status.ok()) {
        std::cerr << status.error().message << '\n';
        return kExitConfig;
    }
    VQEC_LOG_INFO(
        "[bootstrap] vqec_live_ai version=0.1.0 runtime=cpu compiled_backends=onnxruntime "
        "config={}",
        config_path);
    VQEC_LOG_INFO(
        "[bootstrap] input={} output={} processing_fps={} simulation_ai_fps={} dimensions={}x{} "
        "queue_capacity={} zone_dwell_seconds={}",
        vqec::redact_uri(config.input_uri), vqec::redact_uri(config.output_uri),
        config.processing_fps, config.simulation_ai_fps, config.output_width, config.output_height,
        config.queue_capacity, config.zone_dwell_time.count());
    status = vqec::bootstrap::verify_live_ai_models(config.models_dir);
    if (!status.ok()) {
        VQEC_LOG_CRITICAL("[bootstrap] model verification failed code={} message={}",
                          vqec::to_string(status.error().code), status.error().message);
        return kExitConfig;
    }
    namespace inference = vqec::adapters::inference;
    const auto load_engine =
        [&](const char* filename,
            int threads) -> vqec::Result<std::unique_ptr<inference::IInferenceEngine>> {
        auto engine = std::make_unique<inference::OnnxRuntimeEngine>();
        inference::EngineOptions options;
        options.model_path = (std::filesystem::path(config.models_dir) / filename).string();
        options.num_threads = threads;
        const auto loaded = engine->load(options);
        if (!loaded.ok())
            return loaded.error();
        return std::unique_ptr<inference::IInferenceEngine>(std::move(engine));
    };
    auto person_engine = load_engine("nanodet_plus_m_320.onnx", config.person.num_threads);
    auto face_engine = load_engine("yunet_2023mar.onnx", config.face.num_threads);
    if (!person_engine.ok() || !face_engine.ok()) {
        const auto& error = !person_engine.ok() ? person_engine.error() : face_engine.error();
        VQEC_LOG_CRITICAL("[bootstrap] model load failed code={} message={}",
                          vqec::to_string(error.code), error.message);
        return kExitUnavailable;
    }
    if (check_only) {
        VQEC_LOG_INFO("[bootstrap] configuration and pretrained model loads verified");
        return 0;
    }
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
    auto person = std::make_shared<inference::NanoDetDetector>(
        std::move(person_engine).value(),
        inference::DetectorOptions{
            std::min(config.person.confidence, config.tracking.low_threshold),
            config.person.nms_iou});
    auto face = std::make_shared<inference::YuNetDetector>(
        std::move(face_engine).value(),
        inference::DetectorOptions{config.face.confidence, config.face.nms_iou});
    auto source = std::make_unique<vqec::adapters::ffmpeg::FfmpegFrameSource>(
        config.input_uri, config.io_timeout, &stop_requested);
    vqec::adapters::ffmpeg::FfmpegSinkOptions sink_options{
        config.output_uri,     config.output_width, config.output_height, config.output_fps,
        config.queue_capacity, config.io_timeout,   config.stats_interval};
    auto sink = std::make_unique<vqec::adapters::ffmpeg::FfmpegFrameSink>(sink_options);
    vqec::app::LiveAiPipeline pipeline(
        "board43", std::move(source), person, face, std::move(sink),
        {config.processing_fps, config.queue_capacity, config.stats_interval,
         config.simulation_ai_fps, config.zone_dwell_time},
        std::make_unique<vqec::adapters::bytetrack::ByteTracker>(config.tracking));
    {
        const char* line_file = std::getenv("LIVE_LINE_FILE");
        vqec::bootstrap::LiveLineBridge bridge(line_file ? line_file : "", pipeline);
        status = pipeline.run(stop_requested);
    }
    if (!status.ok() && !stop_requested.load()) {
        VQEC_LOG_CRITICAL("[bootstrap] AI worker stopped code={} message={}",
                          vqec::to_string(status.error().code), status.error().message);
        return kExitUnavailable;
    }
    VQEC_LOG_INFO("[bootstrap] AI worker shutdown complete");
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    int result = kExitUnavailable;
    try {
        result = run(argc, argv);
    } catch (const std::exception&) {
        VQEC_LOG_CRITICAL("[bootstrap] unhandled startup failure code=internal");
    } catch (...) {
        VQEC_LOG_CRITICAL("[bootstrap] unknown startup failure code=internal");
    }
    vqec::log::shutdown();
    return result;
}
