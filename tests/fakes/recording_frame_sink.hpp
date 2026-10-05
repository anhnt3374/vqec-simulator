#pragma once

#include <memory>
#include <vector>

#include "ports/outbound/frame_sink.hpp"

namespace vqec::test {

// Records what the pipeline writes. State is shared so tests can inspect it after the sink has
// been moved into the pipeline.
class RecordingFrameSink final : public ports::IFrameSink {
public:
    struct Record {
        bool opened = false;
        bool closed = false;
        std::vector<ports::FrameAnnotations> writes;
    };

    explicit RecordingFrameSink(std::shared_ptr<Record> record) : record_(std::move(record)) {}

    Status open(const ports::SourceInfo& /*source_info*/) override {
        record_->opened = true;
        return {};
    }

    Status write(const domain::Frame& /*frame*/,
                 const ports::FrameAnnotations& annotations) override {
        record_->writes.push_back(annotations);
        return {};
    }

    void close() override { record_->closed = true; }

private:
    std::shared_ptr<Record> record_;
};

}  // namespace vqec::test
