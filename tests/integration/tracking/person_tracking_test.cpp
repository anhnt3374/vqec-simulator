#include <cstring>
#include <filesystem>

#include <gtest/gtest.h>
#include <opencv2/imgcodecs.hpp>

#include "adapters/outbound/inference/models/nanodet_detector.hpp"
#include "adapters/outbound/inference/runtimes/onnxruntime/onnx_runtime_engine.hpp"
#include "adapters/outbound/tracking/bytetrack/byte_tracker.hpp"
namespace vqec::test {
TEST(PersonTracking, MaintainsIdsFromRealNanoDetPredictionsAcrossFrames) {
    const std::string model = VQEC_SOURCE_DIR "/models/live_ai/nanodet_plus_m_320.onnx";
    if (!std::filesystem::exists(model))
        GTEST_SKIP() << "NanoDet artifact unavailable";
    const auto image = cv::imread(VQEC_SOURCE_DIR "/tests/data/media/bus.jpg");
    if (image.empty())
        GTEST_SKIP() << "Downloaded bus fixture unavailable";
    auto engine = std::make_unique<adapters::inference::OnnxRuntimeEngine>();
    adapters::inference::EngineOptions options;
    options.model_path = model;
    options.num_threads = 1;
    ASSERT_TRUE(engine->load(options).ok());
    adapters::inference::NanoDetDetector detector(std::move(engine), {0.1F, 0.5F});
    auto buffer = std::make_shared<domain::ImageBuffer>();
    buffer->width = image.cols;
    buffer->height = image.rows;
    buffer->stride_bytes = image.cols * 3;
    buffer->data.resize(static_cast<std::size_t>(buffer->stride_bytes) *
                        static_cast<std::size_t>(image.rows));
    for (int y = 0; y < image.rows; ++y)
        std::memcpy(buffer->data.data() + static_cast<std::size_t>(y) *
                                              static_cast<std::size_t>(buffer->stride_bytes),
                    image.ptr(y), static_cast<std::size_t>(buffer->stride_bytes));
    domain::Frame frame;
    frame.width = image.cols;
    frame.height = image.rows;
    frame.image = buffer;
    adapters::bytetrack::ByteTracker tracker;
    const auto detections = detector.detect(frame);
    ASSERT_TRUE(detections.ok());
    const auto first = tracker.update(frame, detections.value());
    ASSERT_FALSE(first.empty());
    frame.pts = std::chrono::milliseconds(40);
    const auto next_detections = detector.detect(frame);
    ASSERT_TRUE(next_detections.ok());
    const auto second = tracker.update(frame, next_detections.value());
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].id, second[i].id);
        EXPECT_EQ(second[i].state, domain::TrackState::Confirmed);
    }
}
}  // namespace vqec::test
