#pragma once

#include <chrono>
#include <memory>

#include "ports/outbound/frame_sink.hpp"

namespace vqec::adapters::ffmpeg {
struct FfmpegSinkOptions {
    std::string uri;
    int width = 1280;
    int height = 720;
    int fps = 15;
    std::size_t queue_capacity = 2;
    std::chrono::milliseconds io_timeout{10000};
    std::chrono::seconds stats_interval{10};
};

// Caller-thread open/write/close. Encoding/publishing happens on an owned joined thread.
// Frames and annotations are copied cheaply into a bounded queue; pixels stay immutable.
class FfmpegFrameSink final : public ports::IFrameSink {
public:
    explicit FfmpegFrameSink(FfmpegSinkOptions options);
    ~FfmpegFrameSink() override;
    Status open(const ports::SourceInfo& source_info) override;
    Status write(const domain::Frame& frame, const ports::FrameAnnotations& annotations) override;
    void close() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace vqec::adapters::ffmpeg
