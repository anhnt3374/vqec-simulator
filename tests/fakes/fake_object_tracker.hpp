#pragma once

#include <deque>
#include <utility>
#include <vector>

#include "ports/outbound/object_tracker.hpp"

namespace vqec::test {

// Returns one scripted track list per update() call; empty once the script is exhausted.
class FakeObjectTracker final : public ports::IObjectTracker {
public:
    explicit FakeObjectTracker(std::deque<std::vector<domain::Track>> script)
        : script_(std::move(script)) {}

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

    void reset() override {}

private:
    std::deque<std::vector<domain::Track>> script_;
};

}  // namespace vqec::test
