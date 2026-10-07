#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>
#include <opencv2/imgcodecs.hpp>
#include <unistd.h>

#include "adapters/outbound/inference/models/nanodet_detector.hpp"
#include "adapters/outbound/inference/models/yunet_detector.hpp"
#include "adapters/outbound/inference/runtimes/onnxruntime/onnx_runtime_engine.hpp"
#include "adapters/outbound/media/ffmpeg/ffmpeg_frame_sink.hpp"
#include "adapters/outbound/media/ffmpeg/ffmpeg_frame_source.hpp"
#include "adapters/outbound/media/ffmpeg/overlay_renderer.hpp"
#include "bootstrap/live_ai_config.hpp"

namespace vqec::test {
namespace {
namespace inference = adapters::inference;
constexpr const char* kModelDirectory = VQEC_SOURCE_DIR "/models/live_ai";
domain::Frame image_frame(const cv::Mat& image) {
    auto pixels = std::make_shared<domain::ImageBuffer>();
    pixels->width = image.cols;
    pixels->height = image.rows;
    pixels->stride_bytes = image.cols * 3;
    pixels->data.resize(static_cast<std::size_t>(pixels->stride_bytes) *
                        static_cast<std::size_t>(image.rows));
    for (int row = 0; row < image.rows; ++row)
        std::memcpy(pixels->data.data() + static_cast<std::size_t>(row * pixels->stride_bytes),
                    image.ptr(row), static_cast<std::size_t>(pixels->stride_bytes));
    domain::Frame frame;
    frame.image = pixels;
    frame.width = image.cols;
    frame.height = image.rows;
    return frame;
}
}  // namespace

TEST(LiveAiModels, LoadsVerifiedPretrainedWeightsAndDetectsPersonAndFace) {
    if (!std::filesystem::exists(std::string(kModelDirectory) + "/nanodet_plus_m_320.onnx"))
        GTEST_SKIP() << "Pretrained artifacts unavailable; run scripts/prepare_live_ai_models.sh";
    ASSERT_TRUE(bootstrap::verify_live_ai_models(kModelDirectory).ok());
    const auto bus = cv::imread(VQEC_SOURCE_DIR "/tests/data/media/bus.jpg");
    const auto face = cv::imread(VQEC_SOURCE_DIR "/tests/data/media/lena.jpg");
    if (bus.empty() || face.empty())
        GTEST_SKIP() << "Integration images unavailable; run scripts/prepare_live_ai_test_data.sh";
    auto person_engine = std::make_unique<inference::OnnxRuntimeEngine>();
    auto face_engine = std::make_unique<inference::OnnxRuntimeEngine>();
    inference::EngineOptions options;
    options.num_threads = 1;
    options.model_path = std::string(kModelDirectory) + "/nanodet_plus_m_320.onnx";
    ASSERT_TRUE(person_engine->load(options).ok());
    options.model_path = std::string(kModelDirectory) + "/yunet_2023mar.onnx";
    ASSERT_TRUE(face_engine->load(options).ok());
    inference::NanoDetDetector person_detector(std::move(person_engine));
    inference::YuNetDetector face_detector(std::move(face_engine));
    const auto persons = person_detector.detect(image_frame(bus));
    const auto faces = face_detector.detect(image_frame(face));
    ASSERT_TRUE(persons.ok());
    ASSERT_TRUE(faces.ok());
    EXPECT_FALSE(persons.value().empty());
    EXPECT_FALSE(faces.value().empty());
    for (const auto& detection : persons.value())
        EXPECT_EQ(detection.label, "person");
    for (const auto& detection : faces.value())
        EXPECT_EQ(detection.label, "face");
}

TEST(LiveAiModels, RejectsWrongInputShapeAndUnloadedEngine) {
    inference::OnnxRuntimeEngine engine;
    std::vector<inference::Tensor> outputs;
    EXPECT_FALSE(engine.run({}, outputs).ok());
    if (!std::filesystem::exists(std::string(kModelDirectory) + "/yunet_2023mar.onnx"))
        GTEST_SKIP() << "YuNet model unavailable";
    inference::EngineOptions options;
    options.model_path = std::string(kModelDirectory) + "/yunet_2023mar.onnx";
    options.num_threads = 1;
    ASSERT_TRUE(engine.load(options).ok());
    EXPECT_FALSE(engine.run({inference::Tensor{}}, outputs).ok());
}

TEST(LiveAiModels, RejectsMissingAndCorruptedArtifactsBeforeLoading) {
    const auto directory = std::filesystem::temp_directory_path() /
                           ("vqec_live_ai_artifact_test_" + std::to_string(getpid()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{directory};
    const auto missing = bootstrap::verify_live_ai_models(directory);
    ASSERT_FALSE(missing.ok());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
    std::filesystem::create_directory(directory);
    {
        std::ofstream corrupt(directory / "nanodet_plus_m_320.onnx");
        corrupt << "invalid model";
    }
    const auto corrupted = bootstrap::verify_live_ai_models(directory);
    ASSERT_FALSE(corrupted.ok());
    EXPECT_EQ(corrupted.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(corrupted.error().message.find("SHA256 mismatch"), std::string::npos);
}

TEST(LiveAiMedia, ReleasesResourcesAfterSourceAndPublisherOpenFailures) {
    adapters::ffmpeg::FfmpegFrameSource source("/nonexistent/vqec_live_ai_test.mp4",
                                               std::chrono::milliseconds(500));
    EXPECT_FALSE(source.open().ok());
    source.close();
    source.close();
    adapters::ffmpeg::FfmpegSinkOptions options;
    options.uri = "rtsp://127.0.0.1:1/ai";
    options.io_timeout = std::chrono::milliseconds(500);
    adapters::ffmpeg::FfmpegFrameSink sink(options);
    const auto opened = sink.open({});
    ASSERT_FALSE(opened.ok());
    EXPECT_EQ(opened.error().code, ErrorCode::Unavailable);
    sink.close();
    sink.close();
}

TEST(LiveAiRtsp, DecodesPublishedAnnotatedVideoWithIncreasingPts) {
    const char* uri = std::getenv("VQEC_TEST_RTSP_URL");
    if (!uri)
        GTEST_SKIP() << "Live RTSP dependency unavailable; set VQEC_TEST_RTSP_URL";
    adapters::ffmpeg::FfmpegFrameSource source(uri, std::chrono::seconds(10));
    const auto opened = source.open();
    ASSERT_TRUE(opened.ok()) << (opened.ok() ? "" : opened.error().message);
    std::chrono::nanoseconds previous{-1};
    for (int i = 0; i < 10; ++i) {
        auto frame = source.read();
        ASSERT_TRUE(frame.ok()) << (frame.ok() ? "" : frame.error().message);
        EXPECT_GT(frame.value().pts, previous);
        previous = frame.value().pts;
        EXPECT_GT(frame.value().width, 0);
        EXPECT_GT(frame.value().height, 0);
        if (i == 9) {
            const char* snapshot = std::getenv("VQEC_TEST_SNAPSHOT_PATH");
            if (snapshot) {
                const auto& pixels = *frame.value().image;
                cv::Mat image(pixels.height, pixels.width, CV_8UC3,
                              const_cast<std::uint8_t*>(pixels.data.data()),
                              static_cast<std::size_t>(pixels.stride_bytes));
                EXPECT_TRUE(cv::imwrite(snapshot, image));
            }
        }
    }
    source.close();
}
}  // namespace vqec::test
