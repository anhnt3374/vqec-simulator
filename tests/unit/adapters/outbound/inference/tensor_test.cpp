#include "adapters/outbound/inference/engine/tensor.hpp"

#include <gtest/gtest.h>

namespace vqec::adapters::inference {
namespace {

TEST(Tensor, ByteSizeMultipliesShapeByElementSize) {
    TensorSpec spec;
    spec.data_type = DataType::Float32;
    spec.layout = TensorLayout::Nchw;
    spec.shape = {1, 3, 640, 640};

    EXPECT_EQ(byte_size(spec), 1U * 3U * 640U * 640U * 4U);
}

TEST(Tensor, ElementCountIsZeroForDynamicShape) {
    EXPECT_EQ(element_count({-1, 3, 640, 640}), 0U);
}

}  // namespace
}  // namespace vqec::adapters::inference
