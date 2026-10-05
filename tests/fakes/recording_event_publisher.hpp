#pragma once

#include <mutex>
#include <vector>

#include "ports/outbound/event_publisher.hpp"

namespace vqec::test {

class RecordingEventPublisher final : public ports::IEventPublisher {
public:
    void publish(const domain::Event& event) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        published_.push_back(event);
    }

    std::vector<domain::Event> published() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return published_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<domain::Event> published_;
};

}  // namespace vqec::test
