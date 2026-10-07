#include "adapters/outbound/media/ffmpeg/overlay_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vqec::adapters::ffmpeg {
namespace {
constexpr int kChannels = 3;
constexpr int kLineWidth = 2;
constexpr double kFontScale = 0.5;
const cv::Scalar kZoneColor(0, 165, 255);
constexpr std::int64_t kClockRate = 90000;
}  // namespace

Result<std::shared_ptr<domain::ImageBuffer>> render_overlay(
    const domain::Frame& frame, const ports::FrameAnnotations& annotations, int width, int height) {
    if (!frame.image || frame.width <= 0 || frame.height <= 0 || width <= 0 || height <= 0 ||
        frame.image->format != domain::PixelFormat::Bgr24 ||
        frame.image->stride_bytes < frame.width * kChannels ||
        frame.image->data.size() < static_cast<std::size_t>(frame.image->stride_bytes) *
                                       static_cast<std::size_t>(frame.height))
        return make_error(ErrorCode::InvalidArgument, "overlay: invalid BGR24 frame");
    try {
        auto output = std::make_shared<domain::ImageBuffer>();
        output->width = width;
        output->height = height;
        output->stride_bytes = width * kChannels;
        output->data.resize(static_cast<std::size_t>(output->stride_bytes) *
                            static_cast<std::size_t>(height));
        // Read-only source wrapper; drawing is exclusively on the newly owned output.
        cv::Mat source(frame.height, frame.width, CV_8UC3,
                       const_cast<std::uint8_t*>(frame.image->data.data()),
                       static_cast<std::size_t>(frame.image->stride_bytes));
        cv::Mat canvas(height, width, CV_8UC3, output->data.data());
        cv::resize(source, canvas, canvas.size());
        const float scale_x = static_cast<float>(width) / static_cast<float>(frame.width);
        const float scale_y = static_cast<float>(height) / static_cast<float>(frame.height);
        for (const auto& track : annotations.tracks) {
            if (track.state == domain::TrackState::Lost)
                continue;
            const auto& box = track.box;
            if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.width) ||
                !std::isfinite(box.height))
                continue;
            const auto x1 =
                static_cast<int>(std::clamp(box.x * scale_x, 0.0F, static_cast<float>(width - 1)));
            const auto y1 =
                static_cast<int>(std::clamp(box.y * scale_y, 0.0F, static_cast<float>(height - 1)));
            const auto x2 = static_cast<int>(
                std::clamp((box.x + box.width) * scale_x, 0.0F, static_cast<float>(width - 1)));
            const auto y2 = static_cast<int>(
                std::clamp((box.y + box.height) * scale_y, 0.0F, static_cast<float>(height - 1)));
            if (x2 <= x1 || y2 <= y1)
                continue;
            const bool outward = track.label == "person" &&
                                 std::find(annotations.outward_crossed_tracks.begin(),
                                           annotations.outward_crossed_tracks.end(),
                                           track.id) != annotations.outward_crossed_tracks.end();
            const bool inside_zone = track.label == "person" &&
                                     std::find(annotations.inside_zone_tracks.begin(),
                                               annotations.inside_zone_tracks.end(),
                                               track.id) != annotations.inside_zone_tracks.end();
            const cv::Scalar color = outward                 ? cv::Scalar(0, 0, 255)
                                     : inside_zone           ? kZoneColor
                                     : track.label == "face" ? cv::Scalar(255, 128, 255)
                                                             : cv::Scalar(80, 255, 0);
            cv::rectangle(canvas, cv::Point(x1, y1), cv::Point(x2, y2), color, kLineWidth);
            std::ostringstream label;
            label << track.label;
            if (track.label == "person" && track.id > 0)
                label << " #" << track.id;
            label << ' ' << std::fixed << std::setprecision(2) << track.score;
            cv::putText(canvas, label.str(), cv::Point(x1, std::max(15, y1 - 5)),
                        cv::FONT_HERSHEY_SIMPLEX, kFontScale, color, 1);
        }
        if (annotations.scene) {
            for (const auto& zone : annotations.scene->zones) {
                std::vector<cv::Point> polygon;
                polygon.reserve(zone.area.vertices().size());
                for (const auto& p : zone.area.vertices())
                    polygon.emplace_back(static_cast<int>(p.x * static_cast<float>(width - 1)),
                                         static_cast<int>(p.y * static_cast<float>(height - 1)));
                cv::polylines(canvas, polygon, true, kZoneColor, kLineWidth);
            }
            for (const auto& gate : annotations.scene->gates) {
                const auto point = [&](domain::Point2f p) {
                    return cv::Point(static_cast<int>(p.x * static_cast<float>(width - 1)),
                                     static_cast<int>(p.y * static_cast<float>(height - 1)));
                };
                const auto start = point(gate.line.start);
                const auto end = point(gate.line.end);
                const cv::Scalar gate_color(255, 255, 0);
                cv::line(canvas, start, end, gate_color, kLineWidth);
                const cv::Point middle = (start + end) / 2;
                const cv::Point2d delta(end.x - start.x, end.y - start.y);
                const double length = cv::norm(delta);
                constexpr double kDirectionArrowLength = 35.0;
                if (length > 0.0) {
                    const cv::Point target(
                        middle.x + static_cast<int>(-delta.y / length * kDirectionArrowLength),
                        middle.y + static_cast<int>(delta.x / length * kDirectionArrowLength));
                    cv::arrowedLine(canvas, middle, target, gate_color, kLineWidth);
                    cv::putText(canvas, "OUT", target, cv::FONT_HERSHEY_SIMPLEX, kFontScale,
                                gate_color, 1);
                }
            }
        }
        // Always identify the real AI output, including frames containing no objects.
        cv::putText(canvas, "AI: NanoDet + ByteTrack person | YuNet face", cv::Point(10, 20),
                    cv::FONT_HERSHEY_SIMPLEX, kFontScale, cv::Scalar(255, 255, 255), 1);
        return output;
    } catch (const std::exception&) {
        return make_error(ErrorCode::Internal, "overlay rendering failed");
    }
}

std::int64_t EncoderClock::next(std::chrono::nanoseconds pts) {
    if (!origin_ || pts < previous_) {
        origin_ = pts;
        offset_ = last_ + 1;
    }
    previous_ = pts;
    using Ticks = std::chrono::duration<std::int64_t, std::ratio<1, kClockRate>>;
    const auto candidate = offset_ + std::chrono::duration_cast<Ticks>(pts - *origin_).count();
    last_ = std::max(last_ + 1, candidate);
    return last_;
}
}  // namespace vqec::adapters::ffmpeg
