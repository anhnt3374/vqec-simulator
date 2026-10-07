#include "adapters/outbound/inference/models/detection_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vqec::adapters::inference {
float float_at(const Tensor& tensor, std::size_t index) {
    float value = 0.0F;
    std::memcpy(&value, tensor.data.data() + index * sizeof(float), sizeof(float));
    return value;
}

bool valid_float_tensor(const Tensor& tensor, const std::vector<std::int64_t>& shape) {
    return tensor.spec.data_type == DataType::Float32 && tensor.spec.shape == shape &&
           tensor.data.size() == byte_size(tensor.spec);
}

Status prepare_bgr_tensor(const domain::Frame& frame, int width, int height,
                          const std::array<float, 3>& mean, const std::array<float, 3>& stddev,
                          Tensor& output) {
    if (!frame.image || frame.width <= 0 || frame.height <= 0 ||
        frame.image->width != frame.width || frame.image->height != frame.height ||
        frame.image->format != domain::PixelFormat::Bgr24 ||
        frame.image->stride_bytes < frame.width * 3 ||
        frame.image->data.size() < static_cast<std::size_t>(frame.image->stride_bytes) *
                                       static_cast<std::size_t>(frame.height))
        return make_error(ErrorCode::InvalidArgument, "detector requires a valid BGR24 frame");
    try {
        // cv::resize only reads this wrapper; published pixels remain immutable.
        cv::Mat source(frame.height, frame.width, CV_8UC3,
                       const_cast<std::uint8_t*>(frame.image->data.data()),
                       static_cast<std::size_t>(frame.image->stride_bytes));
        cv::Mat resized;
        cv::resize(source, resized, cv::Size(width, height));
        output.spec.shape = {1, 3, height, width};
        output.spec.data_type = DataType::Float32;
        output.spec.layout = TensorLayout::Nchw;
        output.data.resize(byte_size(output.spec));
        const auto plane = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        for (int y = 0; y < height; ++y) {
            const auto* row = resized.ptr<std::uint8_t>(y);
            for (int x = 0; x < width; ++x) {
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const float value =
                        (static_cast<float>(row[static_cast<std::size_t>(x) * 3 + channel]) -
                         mean[channel]) /
                        stddev[channel];
                    const auto index = channel * plane + static_cast<std::size_t>(y * width + x);
                    std::memcpy(output.data.data() + index * sizeof(float), &value, sizeof(float));
                }
            }
        }
        return {};
    } catch (const cv::Exception&) {
        return make_error(ErrorCode::Internal, "detector image preprocessing failed");
    }
}

void clip_and_suppress(std::vector<domain::Detection>& detections, int width, int height,
                       float nms_iou) {
    const float max_x = static_cast<float>(width);
    const float max_y = static_cast<float>(height);
    for (auto& detection : detections) {
        auto& box = detection.box;
        const float right = std::clamp(box.x + box.width, 0.0F, max_x);
        const float bottom = std::clamp(box.y + box.height, 0.0F, max_y);
        box.x = std::clamp(box.x, 0.0F, max_x);
        box.y = std::clamp(box.y, 0.0F, max_y);
        box.width = right - box.x;
        box.height = bottom - box.y;
    }
    detections.erase(
        std::remove_if(detections.begin(), detections.end(),
                       [](const auto& d) { return d.box.width <= 0.0F || d.box.height <= 0.0F; }),
        detections.end());
    std::sort(detections.begin(), detections.end(),
              [](const auto& a, const auto& b) { return a.score > b.score; });
    std::vector<domain::Detection> kept;
    for (auto& detection : detections) {
        const auto& a = detection.box;
        bool suppressed = false;
        for (const auto& previous : kept) {
            const auto& b = previous.box;
            const float overlap_x =
                std::max(0.0F, std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x));
            const float overlap_y =
                std::max(0.0F, std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y));
            const float intersection = overlap_x * overlap_y;
            const float total = a.width * a.height + b.width * b.height - intersection;
            if (total > 0.0F && intersection / total > nms_iou) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed)
            kept.push_back(std::move(detection));
    }
    detections = std::move(kept);
}
}  // namespace vqec::adapters::inference
