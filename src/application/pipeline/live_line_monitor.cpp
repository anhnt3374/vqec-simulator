#include "application/pipeline/live_line_monitor.hpp"

#include <utility>

#include "application/solutions/line_crossing/line_crossing_solution.hpp"
#include "application/solutions/solution_utils.hpp"

namespace vqec::app {
void LiveLineMonitor::set_gate(std::optional<domain::Gate> gate) {
    gate_ = std::move(gate);
    reset();
}
void LiveLineMonitor::reset() {
    history_.clear();
}

void LiveLineMonitor::annotate(const domain::Frame& frame, ports::FrameAnnotations& annotations) {
    if (!gate_ || frame.width <= 0 || frame.height <= 0)
        return;
    std::map<domain::TrackId, TrackHistory> next;
    for (const auto& track : annotations.tracks) {
        if (track.label != "person" || track.id == 0)
            continue;
        const auto previous = history_.find(track.id);
        if (track.state == domain::TrackState::Lost) {
            if (previous != history_.end())
                next.emplace(*previous);
            continue;
        }
        if (track.state != domain::TrackState::Confirmed)
            continue;
        auto state = previous == history_.end() ? TrackHistory{} : previous->second;
        const auto anchor = normalized_anchor(track, domain::AnchorPoint::BottomCenter, frame);
        if (!domain::is_normalized(anchor)) {
            // A confirmed Kalman box may extend beyond the image without the track ending.
            // Preserve its last valid observation and highlight until the tracker expires it.
            if (state.outward_crossed)
                annotations.outward_crossed_tracks.push_back(track.id);
            next.emplace(track.id, state);
            continue;
        }
        if (state.anchor && domain::detect_crossing(*gate_, *state.anchor, anchor) ==
                                domain::CrossingDirection::Forward) {
            auto event = make_track_event(LineCrossingSolution::kCrossEvent, frame, track);
            event.gate_id = gate_->id;
            event.attributes.emplace(
                std::string(LineCrossingSolution::kDirectionAttribute),
                std::string(domain::to_string(domain::CrossingDirection::Forward)));
            annotations.events.push_back(std::move(event));
            state.outward_crossed = true;
        }
        // Keep the last unambiguous side when an observation lands exactly on the gate.
        if (domain::orientation(gate_->line.start, gate_->line.end, anchor) != 0.0F)
            state.anchor = anchor;
        if (state.outward_crossed)
            annotations.outward_crossed_tracks.push_back(track.id);
        next.emplace(track.id, state);
    }
    history_ = std::move(next);
}
}  // namespace vqec::app
