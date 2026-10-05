#pragma once

#include "domain/event/event.hpp"

namespace vqec::ports {

// Live fan-out of events (WebSocket clients, webhooks). Thread-safe.
class IEventPublisher {
public:
    virtual ~IEventPublisher() = default;

    // Must not block: implementations drop (and count/log) when consumers are slow.
    virtual void publish(const domain::Event& event) = 0;
};

}  // namespace vqec::ports
