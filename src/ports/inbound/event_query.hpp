#pragma once

#include <vector>

#include "common/result.hpp"
#include "domain/event/event.hpp"
#include "domain/event/event_filter.hpp"

namespace vqec::ports {

// Driving port for reading stored events (REST API, reports). Thread-safe.
class IEventQuery {
public:
    virtual ~IEventQuery() = default;

    virtual Result<std::vector<domain::Event>> find_events(
        const domain::EventFilter& filter) const = 0;
};

}  // namespace vqec::ports
