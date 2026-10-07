#include "bootstrap/live_ai_config.hpp"

#include <gtest/gtest.h>

namespace vqec::test {
namespace {
const std::string kConfig = R"(
input_uri: rtsp://mediamtx:8554/relay
output_uri: rtsp://mediamtx:8554/ai
models_dir: /models
processing_fps: 0
output: {width: 1280, height: 720, fps: 15}
person: {confidence: 0.35, nms_iou: 0.5, num_threads: 1}
face: {confidence: 0.35, nms_iou: 0.5, num_threads: 1}
)";
}

TEST(LiveAiConfig, ParsesUnlimitedProcessingAndModelDefaults) {
    auto result = bootstrap::parse_live_ai_config(kConfig);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().processing_fps, 0.0);
    EXPECT_EQ(result.value().person.num_threads, 1);
    EXPECT_EQ(result.value().output_width, 1280);
    EXPECT_EQ(result.value().queue_capacity, 2U);
}

TEST(LiveAiConfig, RejectsNegativeAndNonfiniteRatesWithKey) {
    for (const auto* value : {"-1", ".nan", ".inf", "1e-20", "1e20"}) {
        auto text = kConfig;
        text.replace(text.find("processing_fps: 0"), 17, std::string("processing_fps: ") + value);
        auto result = bootstrap::parse_live_ai_config(text);
        ASSERT_FALSE(result.ok());
        EXPECT_NE(result.error().message.find("processing_fps"), std::string::npos);
    }
}

TEST(LiveAiConfig, RejectsInvalidThresholdDimensionsAndCapacity) {
    for (const auto* extra :
         {"queue_capacity: 0", "person: {confidence: 1.5}",
          "output: {width: 1279, height: 720, fps: 15}", "face: {num_threads: 0}"}) {
        auto result = bootstrap::parse_live_ai_config(kConfig + extra + "\n");
        EXPECT_FALSE(result.ok()) << extra;
    }
}

TEST(LiveAiConfig, RejectsMissingUriWithoutEchoingCredentials) {
    auto result = bootstrap::parse_live_ai_config("input_uri: rtsp://user:secret@host/path\n");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().message.find("secret"), std::string::npos);
}
TEST(LiveAiConfig, ParsesTrackingPolicyAndRejectsInvalidThresholdOrder) {
    auto parsed = bootstrap::parse_live_ai_config(
        kConfig +
        "tracking: {low_threshold: 0.1, high_threshold: 0.4, new_threshold: 0.5, lost_timeout_ms: "
        "1500, max_tracks: 64}\n");
    ASSERT_TRUE(parsed.ok());
    EXPECT_FLOAT_EQ(parsed.value().tracking.high_threshold, 0.4F);
    EXPECT_EQ(parsed.value().tracking.lost_timeout.count(), 1500);
    for (const auto* policy : {"low_threshold: 0.8", "new_threshold: 0.2", "lost_timeout_ms: 0",
                               "max_tracks: 0", "match_cost: .nan"}) {
        auto bad = bootstrap::parse_live_ai_config(kConfig + "tracking: {" + policy + "}\n");
        EXPECT_FALSE(bad.ok()) << policy;
    }
}
TEST(LiveAiConfig, ParsesSimulationRateAndDefaultsToDisabled) {
    auto defaults = bootstrap::parse_live_ai_config(kConfig);
    ASSERT_TRUE(defaults.ok());
    EXPECT_EQ(defaults.value().simulation_ai_fps, 0);
    auto enabled = bootstrap::parse_live_ai_config(kConfig + "simulation: {ai_fps: 18}\n");
    ASSERT_TRUE(enabled.ok());
    EXPECT_EQ(enabled.value().simulation_ai_fps, 18);
    for (const auto* value : {"-1", ".nan", ".inf", "0.001", "1001"}) {
        auto bad =
            bootstrap::parse_live_ai_config(kConfig + "simulation: {ai_fps: " + value + "}\n");
        EXPECT_FALSE(bad.ok()) << value;
        if (!bad.ok()) {
            EXPECT_NE(bad.error().message.find("simulation.ai_fps"), std::string::npos);
        }
    }
    EXPECT_FALSE(bootstrap::parse_live_ai_config(kConfig + "simulation: invalid\n").ok());
}
TEST(LiveAiConfig, DefaultsZoneDwellToFiveSecondsAndValidatesOverrides) {
    auto defaults = bootstrap::parse_live_ai_config(kConfig);
    ASSERT_TRUE(defaults.ok());
    EXPECT_EQ(defaults.value().zone_dwell_time, std::chrono::seconds(5));
    auto custom = bootstrap::parse_live_ai_config(kConfig + "zone_dwell_seconds: 7\n");
    ASSERT_TRUE(custom.ok());
    EXPECT_EQ(custom.value().zone_dwell_time, std::chrono::seconds(7));
    for (const auto* value : {"0", "-1", ".nan", ".inf", "3601"}) {
        auto bad = bootstrap::parse_live_ai_config(kConfig + "zone_dwell_seconds: " + value + "\n");
        ASSERT_FALSE(bad.ok()) << value;
        EXPECT_NE(bad.error().message.find("zone_dwell_seconds"), std::string::npos);
    }
}
}  // namespace vqec::test
