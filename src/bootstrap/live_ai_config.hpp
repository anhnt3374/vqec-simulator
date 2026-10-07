#pragma once

#include <chrono>
#include <filesystem>
#include <string>

#include "adapters/outbound/tracking/bytetrack/byte_tracker.hpp"
#include "common/log.hpp"
#include "common/result.hpp"

namespace vqec::bootstrap {

struct LiveDetectorConfig {
    float confidence = 0.35F;
    float nms_iou = 0.5F;
    int num_threads = 1;
};

struct LiveAiConfig {
    std::string input_uri;
    std::string output_uri;
    std::string models_dir;
    double processing_fps = 0.0;
    double simulation_ai_fps = 0.0;
    int output_width = 1280;
    int output_height = 720;
    int output_fps = 15;
    std::size_t queue_capacity = 2;
    std::chrono::milliseconds io_timeout{10000};
    std::chrono::seconds stats_interval{10};
    std::chrono::seconds zone_dwell_time{5};
    LiveDetectorConfig person;
    LiveDetectorConfig face;
    adapters::bytetrack::ByteTrackerOptions tracking;
    log::LogConfig logging;
};

// Bootstrap only: env expansion and YAML validation. Errors identify keys, never values.
Result<LiveAiConfig> parse_live_ai_config(const std::string& yaml);
Result<LiveAiConfig> load_live_ai_config(const std::filesystem::path& path);
// Verifies both pinned artifacts using SHA256 from the tracked preparation metadata.
Status verify_live_ai_models(const std::filesystem::path& models_dir);

}  // namespace vqec::bootstrap
