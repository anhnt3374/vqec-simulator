#pragma once

#include <algorithm>
#include <mutex>
#include <vector>

#include "ports/outbound/event_repository.hpp"

namespace vqec::test {

class InMemoryEventRepository final : public ports::IEventRepository {
public:
    Status save(const domain::Event& event) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(event);
        return {};
    }

    Result<std::vector<domain::Event>> find(const domain::EventFilter& filter) const override {
        const std::lock_guard<std::mutex> lock(mutex_);
        std::vector<domain::Event> matches;
        for (auto it = events_.rbegin(); it != events_.rend() && matches.size() < filter.limit;
             ++it) {
            if ((!filter.stream_id || it->stream_id == *filter.stream_id) &&
                (!filter.type || it->type == *filter.type)) {
                matches.push_back(*it);
            }
        }
        return matches;
    }

    std::vector<domain::Event> events() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<domain::Event> events_;
};

}  // namespace vqec::test
