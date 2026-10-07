#pragma once

#include <array>
#include <vector>

#include "adapters/outbound/inference/engine/tensor.hpp"
#include "common/result.hpp"
#include "domain/vision/detection.hpp"
#include "domain/vision/frame.hpp"

namespace vqec::adapters::inference {
struct DetectorOptions {
    float confidence = 0.35F;
    float nms_iou = 0.5F;
};

float float_at(const Tensor& tensor, std::size_t index);
bool valid_float_tensor(const Tensor& tensor, const std::vector<std::int64_t>& shape);
Status prepare_bgr_tensor(const domain::Frame& frame, int width, int height,
                          const std::array<float, 3>& mean, const std::array<float, 3>& stddev,
                          Tensor& output);
void clip_and_suppress(std::vector<domain::Detection>& detections, int width, int height,
                       float nms_iou);
}  // namespace vqec::adapters::inference
