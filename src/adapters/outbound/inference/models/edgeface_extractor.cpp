#include "adapters/outbound/inference/models/edgeface_extractor.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "adapters/outbound/inference/models/detection_utils.hpp"
namespace vqec::adapters::inference {
namespace {
constexpr int kSize = 112;
constexpr std::size_t kDimension = 512, kBatch = 4, kPlane = 112 * 112;
constexpr double kMaxResidual = 12.0, kMinScale = 0.05, kMaxScale = 10.0;
bool valid_frame(const domain::Frame& frame) {
    return frame.image && frame.width > 0 && frame.height > 0 &&
           frame.width <= std::numeric_limits<int>::max() / 3 &&
           frame.image->width == frame.width && frame.image->height == frame.height &&
           frame.image->format == domain::PixelFormat::Bgr24 &&
           frame.image->stride_bytes >= frame.width * 3 &&
           frame.image->data.size() >= static_cast<std::size_t>(frame.image->stride_bytes) *
                                           static_cast<std::size_t>(frame.height);
}

}  // namespace
Result<domain::Embedding> normalize_edgeface(std::vector<float> values) {
    if (values.size() != kDimension)
        return make_error(ErrorCode::InvalidArgument, "edgeface: dimension must be 512");
    double norm = 0;
    for (float value : values) {
        if (!std::isfinite(value))
            return make_error(ErrorCode::InvalidArgument, "edgeface: nonfinite embedding");
        norm += static_cast<double>(value) * static_cast<double>(value);
    }
    if (norm <= 0)
        return make_error(ErrorCode::InvalidArgument, "edgeface: zero embedding");
    const auto divisor = static_cast<float>(std::sqrt(norm));
    for (auto& value : values)
        value /= divisor;
    return domain::Embedding{"edgeface_s_gamma_05_v1", std::move(values)};
}
Result<Tensor> prepare_edgeface_crop(const domain::Frame& frame,
                                     const ports::FeatureRegion& region) {
    if (!region.landmarks || !valid_frame(frame))
        return make_error(ErrorCode::InvalidArgument, "edgeface: invalid landmarks or BGR frame");
    try {
        cv::Mat equations(10, 4, CV_64F), targets(10, 1, CV_64F);
        for (int i = 0; i < 5; ++i) {
            const auto p = region.landmarks->at(static_cast<std::size_t>(i));
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.y < 0 ||
                p.x >= static_cast<float>(frame.width) || p.y >= static_cast<float>(frame.height))
                return make_error(ErrorCode::InvalidArgument, "edgeface: landmarks outside frame");
            const double x = static_cast<double>(p.x), y = static_cast<double>(p.y);
            equations.at<double>(2 * i, 0) = x;
            equations.at<double>(2 * i, 1) = -y;
            equations.at<double>(2 * i, 2) = 1;
            equations.at<double>(2 * i, 3) = 0;
            equations.at<double>(2 * i + 1, 0) = y;
            equations.at<double>(2 * i + 1, 1) = x;
            equations.at<double>(2 * i + 1, 2) = 0;
            equations.at<double>(2 * i + 1, 3) = 1;
            targets.at<double>(2 * i) =
                static_cast<double>(kEdgeFaceLandmarks[static_cast<std::size_t>(i)].x);
            targets.at<double>(2 * i + 1) =
                static_cast<double>(kEdgeFaceLandmarks[static_cast<std::size_t>(i)].y);
        }
        const auto& points = *region.landmarks;
        if (points[0].x >= points[1].x || points[3].x >= points[4].x ||
            points[2].y <= std::min(points[0].y, points[1].y) ||
            points[2].y >= std::max(points[3].y, points[4].y))
            return make_error(ErrorCode::InvalidArgument, "edgeface: malformed landmark geometry");
        cv::Mat coefficients;
        if (!cv::solve(equations, targets, coefficients, cv::DECOMP_SVD))
            return make_error(ErrorCode::InvalidArgument, "edgeface: degenerate alignment");
        const double a = coefficients.at<double>(0), b = coefficients.at<double>(1);
        const double scale = std::hypot(a, b),
                     residual = cv::norm(equations * coefficients - targets) / std::sqrt(5.0);
        if (!std::isfinite(scale) || scale < kMinScale || scale > kMaxScale ||
            residual > kMaxResidual)
            return make_error(ErrorCode::InvalidArgument, "edgeface: alignment rejected");
        cv::Mat transform = (cv::Mat_<double>(2, 3) << a, -b, coefficients.at<double>(2), b, a,
                             coefficients.at<double>(3));
        cv::Mat inverse;
        cv::invertAffineTransform(transform, inverse);
        for (const auto& corner :
             std::array<cv::Point2d, 4>{{{0, 0}, {111, 0}, {0, 111}, {111, 111}}}) {
            const double x = inverse.at<double>(0, 0) * corner.x +
                             inverse.at<double>(0, 1) * corner.y + inverse.at<double>(0, 2);
            const double y = inverse.at<double>(1, 0) * corner.x +
                             inverse.at<double>(1, 1) * corner.y + inverse.at<double>(1, 2);
            if (x < -0.001 || y < -0.001 || x > static_cast<double>(frame.width - 1) + 0.001 ||
                y > static_cast<double>(frame.height - 1) + 0.001)
                return make_error(ErrorCode::InvalidArgument, "edgeface: incomplete crop");
        }
        cv::Mat source(frame.height, frame.width, CV_8UC3,
                       const_cast<std::uint8_t*>(frame.image->data.data()),
                       static_cast<std::size_t>(frame.image->stride_bytes)),
            crop;
        cv::warpAffine(source, crop, transform, cv::Size(kSize, kSize));
        Tensor result;
        result.spec = {"input", DataType::Float32, TensorLayout::Nchw, {1, 3, kSize, kSize}, {}};
        result.data.resize(3 * kPlane * sizeof(float));
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x)
                for (std::size_t c = 0; c < 3; ++c) {
                    const float value = static_cast<float>(crop.ptr<std::uint8_t>(
                                            y)[static_cast<std::size_t>(x) * 3 + (2 - c)]) /
                                            127.5F -
                                        1.0F;
                    const auto index = c * kPlane + static_cast<std::size_t>(y * kSize + x);
                    std::memcpy(result.data.data() + index * sizeof(float), &value, sizeof(value));
                }
        return result;
    } catch (const cv::Exception&) {
        return make_error(ErrorCode::Internal, "edgeface: crop preprocessing failed");
    }
}
EdgeFaceExtractor::EdgeFaceExtractor(std::unique_ptr<IInferenceEngine> engine)
    : engine_(std::move(engine)) {
    assert(engine_);
}
Result<domain::Embedding> EdgeFaceExtractor::extract(const domain::Frame&, const domain::BoxF&) {
    return make_error(ErrorCode::InvalidArgument,
                      "edgeface: five landmarks required; use extract_batch");
}
Result<ports::EmbeddingBatch> EdgeFaceExtractor::extract_batch(
    const domain::Frame& frame, const std::vector<ports::FeatureRegion>& regions) {
    if (regions.empty() || regions.size() > kBatch)
        return make_error(ErrorCode::InvalidArgument, "edgeface: batch must contain 1..4 faces");
    if (!valid_frame(frame))
        return make_error(ErrorCode::InvalidArgument, "edgeface: invalid BGR frame");
    std::lock_guard<std::mutex> lock(mutex_);
    ports::EmbeddingBatch result;
    std::vector<std::size_t> valid;
    Tensor input;
    input.spec = {"input", DataType::Float32, TensorLayout::Nchw, {4, 3, kSize, kSize}, {}};
    input.data.resize(kBatch * 3 * kPlane * sizeof(float), 0);
    for (std::size_t i = 0; i < regions.size(); ++i) {
        auto crop = prepare_edgeface_crop(frame, regions[i]);
        if (!crop.ok()) {
            result.emplace_back(crop.error());
            continue;
        }
        std::memcpy(input.data.data() + valid.size() * crop.value().data.size(),
                    crop.value().data.data(), crop.value().data.size());
        valid.push_back(i);
        result.emplace_back(make_error(ErrorCode::Internal, "edgeface: awaiting inference"));
    }
    if (valid.empty())
        return result;
    std::vector<Tensor> outputs;
    const auto status = engine_->run({std::move(input)}, outputs);
    if (!status.ok())
        return status.error();
    if (outputs.size() != 1 || !valid_float_tensor(outputs[0], {4, 512}))
        return make_error(ErrorCode::Internal, "edgeface: invalid batch output");
    for (std::size_t row = 0; row < valid.size(); ++row) {
        std::vector<float> values(kDimension);
        for (std::size_t c = 0; c < kDimension; ++c)
            values[c] = float_at(outputs[0], row * kDimension + c);
        result[valid[row]] = normalize_edgeface(std::move(values));
    }
    return result;
}
}  // namespace vqec::adapters::inference
