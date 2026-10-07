#include <cmath>
#include <cstring>
#include <limits>

#include <gtest/gtest.h>

#include "adapters/outbound/inference/models/nanodet_detector.hpp"
#include "adapters/outbound/inference/models/yunet_detector.hpp"

namespace vqec::test {
namespace {
using namespace adapters::inference;

Tensor tensor(std::string name, std::vector<std::int64_t> shape, std::vector<float> values) {
    Tensor result{{std::move(name), DataType::Float32, TensorLayout::Other, std::move(shape), {}},
                  {}};
    result.data.resize(values.size() * sizeof(float));
    std::memcpy(result.data.data(), values.data(), result.data.size());
    return result;
}
}  // namespace

TEST(NanoDetDecode, MapsPersonBoxesToNonSquareSourceAndSuppressesDuplicates) {
    std::vector<float> values(2125 * 112, 0.0F);
    for (const std::size_t row : {410U, 411U}) {
        values[row * 112] = row == 410 ? 0.9F : 0.8F;
        for (std::size_t side = 0; side < 4; ++side) {
            for (std::size_t bin = 0; bin < 8; ++bin)
                values[row * 112 + 80 + side * 8 + bin] = bin == 3 ? 20.0F : -20.0F;
        }
    }
    values[0 * 112 + 1] = 0.99F;  // Bicycle must not become person.
    auto result = decode_nanodet(tensor("output", {1, 2125, 112}, values), 640, 320, {});
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.value().size(), 1U);
    EXPECT_EQ(result.value()[0].label, "person");
    EXPECT_NEAR(result.value()[0].box.x, 112.0F, 0.01F);
    EXPECT_NEAR(result.value()[0].box.y, 56.0F, 0.01F);
    EXPECT_NEAR(result.value()[0].box.width, 96.0F, 0.01F);
}

TEST(NanoDetDecode, RejectsMalformedTensorsAndNonfiniteScores) {
    EXPECT_FALSE(decode_nanodet(tensor("output", {1, 1, 112}, {0.5F}), 640, 480, {}).ok());
    std::vector<float> values(2125 * 112, 0.0F);
    values[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(decode_nanodet(tensor("output", {1, 2125, 112}, values), 640, 480, {}).ok());
}

TEST(YuNetDecode, CombinesObjectnessAndClassScoreAndMapsFaceBox) {
    std::vector<Tensor> outputs;
    for (const int stride : {8, 16, 32}) {
        const auto rows = static_cast<std::size_t>((640 / stride) * (640 / stride));
        std::vector<float> cls(rows, 0.0F), obj(rows, 0.0F), boxes(rows * 4, 0.0F),
            kps(rows * 10, 0.0F);
        if (stride == 8) {
            const std::size_t index = 10 * 80 + 10;
            cls[index] = 0.81F;
            obj[index] = 1.0F;
            boxes[index * 4 + 2] = std::log(4.0F);
            boxes[index * 4 + 3] = std::log(4.0F);
        }
        const auto count = static_cast<std::int64_t>(rows);
        outputs.push_back(tensor("cls_" + std::to_string(stride), {1, count, 1}, cls));
        outputs.push_back(tensor("obj_" + std::to_string(stride), {1, count, 1}, obj));
        outputs.push_back(tensor("bbox_" + std::to_string(stride), {1, count, 4}, boxes));
        outputs.push_back(tensor("kps_" + std::to_string(stride), {1, count, 10}, kps));
    }
    auto result = decode_yunet(outputs, 1280, 640, {});
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.value().size(), 1U);
    EXPECT_EQ(result.value()[0].label, "face");
    EXPECT_NEAR(result.value()[0].score, 0.9F, 0.001F);
    EXPECT_NEAR(result.value()[0].box.x, 128.0F, 0.01F);
    EXPECT_NEAR(result.value()[0].box.y, 64.0F, 0.01F);
}

TEST(YuNetDecode, RejectsMissingHeads) {
    EXPECT_FALSE(decode_yunet({}, 640, 480, {}).ok());
}
}  // namespace vqec::test
