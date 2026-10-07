#include "adapters/outbound/inference/models/nanodet_detector.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <utility>

namespace vqec::adapters::inference {
namespace {
constexpr int kInputSize = 320;
constexpr std::size_t kClasses = 80;
constexpr std::size_t kBins = 8;
constexpr std::size_t kFeatures = kClasses + 4 * kBins;
constexpr std::array<int, 4> kStrides{8, 16, 32, 64};
constexpr std::array<float, 3> kMean{103.53F, 116.28F, 123.675F};
constexpr std::array<float, 3> kStddev{57.375F, 57.12F, 58.395F};
}  // namespace

Result<std::vector<domain::Detection>> decode_nanodet(const Tensor& output, int source_width,
                                                      int source_height, DetectorOptions options) {
    if (source_width <= 0 || source_height <= 0 || !valid_float_tensor(output, {1, 2125, 112}))
        return make_error(ErrorCode::InvalidArgument, "nanodet: expected float32 [1,2125,112]");
    std::vector<domain::Detection> detections;
    const float scale_x = static_cast<float>(source_width) / static_cast<float>(kInputSize);
    const float scale_y = static_cast<float>(source_height) / static_cast<float>(kInputSize);
    std::size_t row = 0;
    for (const int stride : kStrides) {
        const int grid = (kInputSize + stride - 1) / stride;
        for (int y = 0; y < grid; ++y) {
            for (int x = 0; x < grid; ++x, ++row) {
                const float score = float_at(output, row * kFeatures);
                if (!std::isfinite(score))
                    return make_error(ErrorCode::InvalidArgument,
                                      "nanodet: nonfinite person score");
                if (score < options.confidence)
                    continue;
                std::array<float, 4> distances{};
                for (std::size_t side = 0; side < distances.size(); ++side) {
                    std::array<float, kBins> logits{};
                    for (std::size_t bin = 0; bin < kBins; ++bin) {
                        logits[bin] =
                            float_at(output, row * kFeatures + kClasses + side * kBins + bin);
                        if (!std::isfinite(logits[bin]))
                            return make_error(ErrorCode::InvalidArgument,
                                              "nanodet: nonfinite box distribution");
                    }
                    const float maximum = *std::max_element(logits.begin(), logits.end());
                    float total = 0.0F;
                    float weighted = 0.0F;
                    for (std::size_t bin = 0; bin < kBins; ++bin) {
                        const float probability = std::exp(logits[bin] - maximum);
                        total += probability;
                        weighted += static_cast<float>(bin) * probability;
                    }
                    distances[side] = weighted / total * static_cast<float>(stride);
                }
                const float center_x = static_cast<float>(x * stride);
                const float center_y = static_cast<float>(y * stride);
                detections.push_back(
                    {{(center_x - distances[0]) * scale_x, (center_y - distances[1]) * scale_y,
                      (distances[0] + distances[2]) * scale_x,
                      (distances[1] + distances[3]) * scale_y},
                     0,
                     "person",
                     std::clamp(score, 0.0F, 1.0F)});
            }
        }
    }
    clip_and_suppress(detections, source_width, source_height, options.nms_iou);
    return detections;
}

NanoDetDetector::NanoDetDetector(std::unique_ptr<IInferenceEngine> engine, DetectorOptions options)
    : engine_(std::move(engine)), options_(options) {
    assert(engine_);
}

Result<std::vector<domain::Detection>> NanoDetDetector::detect(const domain::Frame& frame) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto status = prepare_bgr_tensor(frame, kInputSize, kInputSize, kMean, kStddev, inputs_[0]);
    if (!status.ok())
        return status.error();
    status = engine_->run(inputs_, outputs_);
    if (!status.ok())
        return status.error();
    if (outputs_.size() != 1)
        return make_error(ErrorCode::InvalidArgument, "nanodet: expected one output");
    return decode_nanodet(outputs_[0], frame.width, frame.height, options_);
}
}  // namespace vqec::adapters::inference
