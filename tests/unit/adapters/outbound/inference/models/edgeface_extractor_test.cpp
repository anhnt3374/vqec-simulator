#include "adapters/outbound/inference/models/edgeface_extractor.hpp"

#include <cmath>
#include <cstring>

#include <gtest/gtest.h>

#include "adapters/outbound/inference/models/detection_utils.hpp"
#include "support/builders.hpp"
namespace vqec::test {
TEST(EdgeFace, NormalizesAndRejectsInvalidVectors) {
    std::vector<float> values(512, 1.0F);
    auto result = adapters::inference::normalize_edgeface(values);
    ASSERT_TRUE(result.ok());
    EXPECT_NEAR(result.value().values[0], 1.0F / std::sqrt(512.0F), 1e-6F);
    EXPECT_EQ(result.value().model_id, "edgeface_s_gamma_05_v1");
    EXPECT_FALSE(adapters::inference::normalize_edgeface(std::vector<float>(512, 0.0F)).ok());
    EXPECT_FALSE(adapters::inference::normalize_edgeface({1.0F}).ok());
    values[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(adapters::inference::normalize_edgeface(values).ok());
}
TEST(EdgeFace, AlignsCanonicalLandmarksAndRejectsDegenerateCrops) {
    auto frame = make_frame(1);
    auto image = std::make_shared<domain::ImageBuffer>();
    image->width = frame.width = 200;
    image->height = frame.height = 200;
    image->stride_bytes = 600;
    image->data.resize(120000, 255);
    frame.image = image;
    ports::FeatureRegion region{{20, 30, 90, 110}, adapters::inference::kEdgeFaceLandmarks};
    auto tensor = adapters::inference::prepare_edgeface_crop(frame, region);
    ASSERT_TRUE(tensor.ok());
    EXPECT_NEAR(adapters::inference::float_at(tensor.value(), 0), 1.0F, 1e-6F);
    region.landmarks.reset();
    EXPECT_FALSE(adapters::inference::prepare_edgeface_crop(frame, region).ok());
    region.landmarks = std::array<domain::Point2f, 5>{};
    EXPECT_FALSE(adapters::inference::prepare_edgeface_crop(frame, region).ok());
}
}  // namespace vqec::test
namespace vqec::test {
namespace {
class BatchEngine final : public adapters::inference::IInferenceEngine {
public:
    Status load(const adapters::inference::EngineOptions&) override { return {}; }
    const std::vector<adapters::inference::TensorSpec>& input_specs() const override {
        return specs;
    }
    const std::vector<adapters::inference::TensorSpec>& output_specs() const override {
        return specs;
    }
    Status run(const std::vector<adapters::inference::Tensor>& inputs,
               std::vector<adapters::inference::Tensor>& outputs) override {
        EXPECT_EQ(inputs[0].spec.shape, (std::vector<std::int64_t>{4, 3, 112, 112}));
        adapters::inference::Tensor output;
        output.spec = {"embedding",
                       adapters::inference::DataType::Float32,
                       adapters::inference::TensorLayout::Other,
                       {4, 512},
                       {}};
        output.data.resize(4 * 512 * sizeof(float), 0);
        for (std::size_t row = 0; row < 4; ++row) {
            const float one = 1;
            std::memcpy(output.data.data() + (row * 512 + row) * sizeof(float), &one, sizeof(one));
        }
        outputs = {output};
        return {};
    }
    std::vector<adapters::inference::TensorSpec> specs;
};
}  // namespace
TEST(EdgeFace, PreservesResultOrderWhenMiddleCropIsInvalid) {
    auto frame = make_frame(1);
    auto image = std::make_shared<domain::ImageBuffer>();
    image->width = frame.width = 200;
    image->height = frame.height = 200;
    image->stride_bytes = 600;
    image->data.resize(120000, 128);
    frame.image = image;
    ports::FeatureRegion valid{{20, 30, 90, 110}, adapters::inference::kEdgeFaceLandmarks},
        invalid = valid;
    invalid.landmarks.reset();
    adapters::inference::EdgeFaceExtractor codec(std::make_unique<BatchEngine>());
    auto batch = codec.extract_batch(frame, {valid, invalid, valid});
    ASSERT_TRUE(batch.ok());
    ASSERT_EQ(batch.value().size(), 3U);
    ASSERT_TRUE(batch.value()[0].ok());
    EXPECT_FALSE(batch.value()[1].ok());
    ASSERT_TRUE(batch.value()[2].ok());
    EXPECT_EQ(batch.value()[0].value().values[0], 1);
    EXPECT_EQ(batch.value()[2].value().values[1], 1);
    EXPECT_FALSE(codec.extract_batch(frame, {}).ok());
    EXPECT_FALSE(codec.extract_batch(frame, {valid, valid, valid, valid, valid}).ok());
}
}  // namespace vqec::test

namespace vqec::test {
TEST(EdgeFace, UsesRgbNchwWithoutChangingPixelsAndRejectsCrossedOrIncompleteLandmarks) {
    auto frame = make_frame(1);
    auto image = std::make_shared<domain::ImageBuffer>();
    image->width = frame.width = 200;
    image->height = frame.height = 200;
    image->stride_bytes = 600;
    image->data.resize(120000);
    for (std::size_t i = 0; i < image->data.size(); i += 3) {
        image->data[i] = 0;
        image->data[i + 1] = 128;
        image->data[i + 2] = 255;
    }
    frame.image = image;
    const auto original = image->data;
    ports::FeatureRegion region{{20, 30, 90, 110}, adapters::inference::kEdgeFaceLandmarks};
    auto tensor = adapters::inference::prepare_edgeface_crop(frame, region);
    ASSERT_TRUE(tensor.ok());
    constexpr std::size_t plane = 112 * 112;
    EXPECT_FLOAT_EQ(adapters::inference::float_at(tensor.value(), 56 * 112 + 56), 1);
    EXPECT_NEAR(adapters::inference::float_at(tensor.value(), plane + 56 * 112 + 56),
                128.F / 127.5F - 1, 1e-6F);
    EXPECT_FLOAT_EQ(adapters::inference::float_at(tensor.value(), 2 * plane + 56 * 112 + 56), -1);
    EXPECT_EQ(image->data, original);
    std::swap((*region.landmarks)[0], (*region.landmarks)[1]);
    EXPECT_FALSE(adapters::inference::prepare_edgeface_crop(frame, region).ok());
    region.landmarks = adapters::inference::kEdgeFaceLandmarks;
    for (auto& point : *region.landmarks)
        point.x -= 20;
    EXPECT_FALSE(adapters::inference::prepare_edgeface_crop(frame, region).ok());
}
}  // namespace vqec::test

namespace vqec::test {
TEST(EdgeFace, RejectsInvalidFrameAsBatchFailure) {
    adapters::inference::EdgeFaceExtractor codec(std::make_unique<BatchEngine>());
    const auto result = codec.extract_batch(
        make_frame(1), {{{20, 30, 90, 110}, adapters::inference::kEdgeFaceLandmarks}});
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}
}  // namespace vqec::test
