#pragma once

#include <map>
#include <optional>

#include "domain/scene/gate.hpp"
#include "ports/outbound/frame_sink.hpp"

namespace vqec::app {
// Coordinator-thread only. Keeps the last off-line anchor through Lost/on-line observations.
// Track state is bounded by the tracker's live/lost set and cleared when a gate changes.
class LiveLineMonitor final {
public:
    void set_gate(std::optional<domain::Gate> gate);
    void reset();
    void annotate(const domain::Frame& frame, ports::FrameAnnotations& annotations);

private:
    struct TrackHistory {
        std::optional<domain::Point2f> anchor;
        bool outward_crossed = false;
    };
    std::optional<domain::Gate> gate_;
    std::map<domain::TrackId, TrackHistory> history_;
};
}  // namespace vqec::app
