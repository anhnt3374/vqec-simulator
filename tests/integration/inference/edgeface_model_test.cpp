#include <filesystem>

#include <gtest/gtest.h>

#include "adapters/outbound/inference/models/edgeface_extractor.hpp"
#include "adapters/outbound/inference/runtimes/onnxruntime/onnx_runtime_engine.hpp"
namespace vqec::test {
TEST(EdgeFaceModel, MatchesOrderedPaddedBatchesOnTargetRuntime) {
    namespace inference = adapters::inference;
    const auto model =
        std::filesystem::path(VQEC_SOURCE_DIR) / "models/live_ai/edgeface_s_gamma_05_batch4.onnx";
    if (!std::filesystem::exists(model))
        GTEST_SKIP() << "EdgeFace artifact unavailable; run tools/prepare_edgeface.py";
    auto engine = std::make_unique<inference::OnnxRuntimeEngine>();
    ASSERT_TRUE(engine->load({model.string(), {}, 0, 1}).ok());
    inference::EdgeFaceExtractor codec(std::move(engine));
    domain::Frame frame;
    frame.width = frame.height = 320;
    auto image = std::make_shared<domain::ImageBuffer>();
    image->width = image->height = 320;
    image->stride_bytes = 960;
    image->data.resize(307200);
    for (int y = 0; y < 320; ++y)
        for (int x = 0; x < 320; ++x)
            for (int c = 0; c < 3; ++c)
                image->data[static_cast<std::size_t>(y * 960 + x * 3 + c)] =
                    static_cast<std::uint8_t>((x + y * (c + 1)) % 256);
    frame.image = image;
    std::vector<ports::FeatureRegion> regions;
    std::vector<domain::Embedding> reference;
    for (int i = 0; i < 4; ++i) {
        auto landmarks = inference::kEdgeFaceLandmarks;
        for (auto& point : landmarks) {
            point.x += static_cast<float>(i * 25);
            point.y += static_cast<float>(i * 20);
        }
        regions.push_back({{0, 0, 112, 112}, landmarks});
        auto output = codec.extract_batch(frame, {regions.back()});
        ASSERT_TRUE(output.ok());
        ASSERT_TRUE(output.value()[0].ok());
        reference.push_back(output.value()[0].value());
    }
    for (std::size_t count = 1; count <= 4; ++count) {
        auto output = codec.extract_batch(
            frame, std::vector<ports::FeatureRegion>(
                       regions.begin(), regions.begin() + static_cast<std::ptrdiff_t>(count)));
        ASSERT_TRUE(output.ok());
        ASSERT_EQ(output.value().size(), count);
        for (std::size_t row = 0; row < count; ++row) {
            ASSERT_TRUE(output.value()[row].ok());
            ASSERT_EQ(output.value()[row].value().values.size(), 512U);
            for (std::size_t c = 0; c < 512; ++c)
                EXPECT_NEAR(output.value()[row].value().values[c], reference[row].values[c], 2e-4F);
        }
    }
}
}  // namespace vqec::test
