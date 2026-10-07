#pragma once

#include <chrono>
#include <optional>
#include <unordered_map>

#include "application/solutions/zone_intrusion/zone_intrusion_solution.hpp"
#include "ports/outbound/frame_sink.hpp"

namespace vqec::app {
// Coordinator-thread only. Reuses the zone-intrusion solution for entry events; highlights
// start after the configured stream-time dwell and clear when a person leaves the polygon.
class LiveZoneMonitor final {
public:
    explicit LiveZoneMonitor(std::chrono::seconds dwell_time = std::chrono::seconds{5});
    void set_zone(std::optional<domain::Zone> zone);
    void reset();
    void annotate(const domain::Frame& frame, ports::FrameAnnotations& annotations);

private:
    std::optional<domain::Zone> zone_;
    ZoneIntrusionSolution solution_;
    std::chrono::seconds dwell_time_;
    std::unordered_map<domain::TrackId, std::chrono::nanoseconds> entered_at_;
};
}  // namespace vqec::app
