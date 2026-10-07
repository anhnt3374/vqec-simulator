#include "adapters/outbound/inference/models/yunet_detector.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <utility>

namespace vqec::adapters::inference {
namespace {
constexpr int kInputSize = 640;
constexpr std::array<int, 3> kStrides{8, 16, 32};
constexpr std::array<float, 3> kMean{0.0F, 0.0F, 0.0F};
constexpr std::array<float, 3> kStddev{1.0F, 1.0F, 1.0F};
const Tensor* find_head(const std::vector<Tensor>& outputs, const std::string& name) {
    for (const auto& tensor : outputs)
        if (tensor.spec.name == name)
            return &tensor;
    return nullptr;
}
}  // namespace

Result<std::vector<domain::Detection>> decode_yunet(const std::vector<Tensor>& outputs,
                                                    int source_width, int source_height,
                                                    DetectorOptions options) {
    if (source_width <= 0 || source_height <= 0)
        return make_error(ErrorCode::InvalidArgument, "yunet: invalid source dimensions");
    std::vector<domain::Detection> detections;
    const float scale_x = static_cast<float>(source_width) / static_cast<float>(kInputSize);
    const float scale_y = static_cast<float>(source_height) / static_cast<float>(kInputSize);
    for (const int stride : kStrides) {
        const int grid = kInputSize / stride;
        const auto rows = static_cast<std::int64_t>(grid * grid);
        const auto suffix = std::to_string(stride);
        const auto* cls = find_head(outputs, "cls_" + suffix);
        const auto* obj = find_head(outputs, "obj_" + suffix);
        const auto* box = find_head(outputs, "bbox_" + suffix);
        const auto* kps = find_head(outputs, "kps_" + suffix);
        if (!cls || !obj || !box || !kps || !valid_float_tensor(*cls, {1, rows, 1}) ||
            !valid_float_tensor(*obj, {1, rows, 1}) || !valid_float_tensor(*box, {1, rows, 4}) ||
            !valid_float_tensor(*kps, {1, rows, 10}))
            return make_error(ErrorCode::InvalidArgument,
                              "yunet: invalid or missing output head at stride " + suffix);
        for (int y = 0; y < grid; ++y) {
            for (int x = 0; x < grid; ++x) {
                const auto index = static_cast<std::size_t>(y * grid + x);
                const float class_score = float_at(*cls, index);
                const float object_score = float_at(*obj, index);
                if (!std::isfinite(class_score) || !std::isfinite(object_score))
                    return make_error(ErrorCode::InvalidArgument, "yunet: nonfinite score");
                const float score = std::sqrt(std::clamp(class_score, 0.0F, 1.0F) *
                                              std::clamp(object_score, 0.0F, 1.0F));
                if (score < options.confidence)
                    continue;
                std::array<float, 4> values{};
                for (std::size_t side = 0; side < values.size(); ++side) {
                    values[side] = float_at(*box, index * 4 + side);
                    if (!std::isfinite(values[side]))
                        return make_error(ErrorCode::InvalidArgument, "yunet: nonfinite box");
                }
                const float center_x =
                    (static_cast<float>(x) + values[0]) * static_cast<float>(stride);
                const float center_y =
                    (static_cast<float>(y) + values[1]) * static_cast<float>(stride);
                const float width = std::exp(values[2]) * static_cast<float>(stride);
                const float height = std::exp(values[3]) * static_cast<float>(stride);
                if (!std::isfinite(width) || !std::isfinite(height))
                    return make_error(ErrorCode::InvalidArgument, "yunet: overflowing box");
                // Decode landmarks according to the upstream graph; not exposed without an
                // embedding consumer.
                for (std::size_t point = 0; point < 5; ++point) {
                    const float landmark_x =
                        (float_at(*kps, index * 10 + point * 2) + static_cast<float>(x)) *
                        static_cast<float>(stride) * scale_x;
                    const float landmark_y =
                        (float_at(*kps, index * 10 + point * 2 + 1) + static_cast<float>(y)) *
                        static_cast<float>(stride) * scale_y;
                    if (!std::isfinite(landmark_x) || !std::isfinite(landmark_y))
                        return make_error(ErrorCode::InvalidArgument, "yunet: nonfinite landmark");
                }
                detections.push_back(
                    {{(center_x - width / 2.0F) * scale_x, (center_y - height / 2.0F) * scale_y,
                      width * scale_x, height * scale_y},
                     0,
                     "face",
                     score});
            }
        }
    }
    clip_and_suppress(detections, source_width, source_height, options.nms_iou);
    return detections;
}

YuNetDetector::YuNetDetector(std::unique_ptr<IInferenceEngine> engine, DetectorOptions options)
    : engine_(std::move(engine)), options_(options) {
    assert(engine_);
}

Result<std::vector<domain::Detection>> YuNetDetector::detect(const domain::Frame& frame) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto status = prepare_bgr_tensor(frame, kInputSize, kInputSize, kMean, kStddev, inputs_[0]);
    if (!status.ok())
        return status.error();
    status = engine_->run(inputs_, outputs_);
    if (!status.ok())
        return status.error();
    return decode_yunet(outputs_, frame.width, frame.height, options_);
}
}  // namespace vqec::adapters::inference
