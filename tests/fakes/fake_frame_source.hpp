#pragma once

#include <deque>
#include <utility>

#include "ports/outbound/frame_source.hpp"

namespace vqec::test {

// Plays back a scripted list of frames, then reports EndOfStream.
class FakeFrameSource final : public ports::IFrameSource {
public:
    explicit FakeFrameSource(std::deque<domain::Frame> frames) : frames_(std::move(frames)) {}

    Status open() override {
        is_open_ = true;
        return {};
    }

    ports::SourceInfo info() const override {
        return {"file:///fake.mp4", 1000, 1000, 25.0, false};
    }

    Result<domain::Frame> read() override {
        if (frames_.empty()) {
            return make_error(ErrorCode::EndOfStream, "no more frames");
        }
        domain::Frame frame = std::move(frames_.front());
        frames_.pop_front();
        return frame;
    }

    void close() override { is_open_ = false; }

    bool is_open() const { return is_open_; }

private:
    std::deque<domain::Frame> frames_;
    bool is_open_ = false;
};

}  // namespace vqec::test
