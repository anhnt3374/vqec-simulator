#pragma once

#include <vector>

#include "common/result.hpp"
#include "domain/event/event.hpp"
#include "domain/event/event_filter.hpp"

namespace vqec::ports {

// SQL persistence of events. Thread-safe.
class IEventRepository {
public:
    virtual ~IEventRepository() = default;

    // Called from pipeline threads: must not block on I/O. Implementations enqueue into a
    // bounded queue and write in the background; a full queue returns Unavailable (and logs).
    virtual Status save(const domain::Event& event) = 0;

    // Newest first, at most filter.limit results. May block on I/O (called from API threads).
    virtual Result<std::vector<domain::Event>> find(const domain::EventFilter& filter) const = 0;
};

}  // namespace vqec::ports
