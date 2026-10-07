#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include "ports/outbound/frame_source.hpp"

namespace vqec::adapters::ffmpeg {
// Used by one capture thread. stop_requested is non-owning and must outlive this source.
class FfmpegFrameSource final : public ports::IFrameSource {
public:
    explicit FfmpegFrameSource(std::string uri, std::chrono::milliseconds io_timeout,
                               const std::atomic<bool>* stop_requested = nullptr);
    ~FfmpegFrameSource() override;
    Status open() override;
    ports::SourceInfo info() const override;
    Result<domain::Frame> read() override;
    void close() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace vqec::adapters::ffmpeg
