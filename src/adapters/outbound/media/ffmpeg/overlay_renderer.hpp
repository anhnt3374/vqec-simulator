#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include "ports/outbound/frame_sink.hpp"

namespace vqec::adapters::ffmpeg {
Result<std::shared_ptr<domain::ImageBuffer>> render_overlay(
    const domain::Frame& frame, const ports::FrameAnnotations& annotations, int width, int height);

// Single publisher-thread clock; 90 kHz ticks preserve gaps and handle source resets.
class EncoderClock final {
public:
    std::int64_t next(std::chrono::nanoseconds pts);

private:
    std::optional<std::chrono::nanoseconds> origin_;
    std::chrono::nanoseconds previous_{};
    std::int64_t offset_ = 0;
    std::int64_t last_ = -1;
};
}  // namespace vqec::adapters::ffmpeg
