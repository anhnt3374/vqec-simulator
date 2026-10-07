#include "application/pipeline/live_zone_monitor.hpp"

#include <cassert>
#include <iterator>
#include <utility>

#include "application/solutions/solution_utils.hpp"

namespace vqec::app {
LiveZoneMonitor::LiveZoneMonitor(std::chrono::seconds dwell_time)
    : solution_(ZoneIntrusionParams{{}, {"person"}, domain::AnchorPoint::BottomCenter}),
      dwell_time_(dwell_time) {
    assert(dwell_time_.count() > 0);
}

void LiveZoneMonitor::set_zone(std::optional<domain::Zone> zone) {
    zone_ = std::move(zone);
    reset();
}

void LiveZoneMonitor::reset() {
    entered_at_.clear();
    solution_ = ZoneIntrusionSolution(
        ZoneIntrusionParams{{}, {"person"}, domain::AnchorPoint::BottomCenter});
    domain::Scene scene;
    if (zone_)
        scene.zones.push_back(*zone_);
    solution_.set_scene(scene);
}

void LiveZoneMonitor::annotate(const domain::Frame& frame, ports::FrameAnnotations& annotations) {
    if (!zone_ || frame.width <= 0 || frame.height <= 0)
        return;
    auto events = solution_.process(FrameContext{frame, annotations.tracks});
    annotations.events.insert(annotations.events.end(), std::make_move_iterator(events.begin()),
                              std::make_move_iterator(events.end()));
    std::unordered_map<domain::TrackId, std::chrono::nanoseconds> next_entered;
    next_entered.reserve(entered_at_.size());
    for (const auto& track : annotations.tracks) {
        const auto previous = entered_at_.find(track.id);
        // Short tracker gaps retain the timer, but Lost tracks are never highlighted.
        if (track.state == domain::TrackState::Lost && previous != entered_at_.end()) {
            next_entered.emplace(track.id, previous->second);
            continue;
        }
        if (track.state != domain::TrackState::Confirmed || track.label != "person" ||
            track.id == 0)
            continue;
        const auto anchor = normalized_anchor(track, domain::AnchorPoint::BottomCenter, frame);
        if (!zone_->area.contains(anchor))
            continue;
        const auto entered = previous != entered_at_.end() && previous->second <= frame.pts
                                 ? previous->second
                                 : frame.pts;
        next_entered.emplace(track.id, entered);
        if (frame.pts - entered >= dwell_time_)
            annotations.inside_zone_tracks.push_back(track.id);
    }
    entered_at_ = std::move(next_entered);
}
}  // namespace vqec::app
