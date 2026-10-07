#pragma once

#include <deque>
#include <memory>
#include <utility>
#include <vector>

#include "ports/outbound/object_tracker.hpp"

namespace vqec::test {

// Returns one scripted track list per update() call; empty once the script is exhausted.
class FakeObjectTracker final : public ports::IObjectTracker {
public:
    struct Record {
        int resets = 0;
    };
    explicit FakeObjectTracker(std::deque<std::vector<domain::Track>> script,
                               std::shared_ptr<Record> record = {})
        : script_(std::move(script)), record_(std::move(record)) {}

    std::vector<domain::Track> update(
        const domain::Frame& /*frame*/,
        const std::vector<domain::Detection>& /*detections*/) override {
        if (script_.empty()) {
            return {};
        }
        std::vector<domain::Track> tracks = std::move(script_.front());
        script_.pop_front();
        return tracks;
    }

    void reset() override {
        if (record_)
            ++record_->resets;
    }

private:
    std::deque<std::vector<domain::Track>> script_;
    std::shared_ptr<Record> record_;
};

}  // namespace vqec::test
