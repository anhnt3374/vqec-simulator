#include "application/solutions/line_crossing/line_crossing_solution.hpp"

#include <utility>

#include "application/solutions/solution_utils.hpp"
#include "common/log.hpp"

namespace vqec::app {

LineCrossingSolution::LineCrossingSolution(LineCrossingParams params)
    : params_(std::move(params)) {}

void LineCrossingSolution::set_scene(const domain::Scene& scene) {
    gates_ = select_gates(scene, params_.gate_ids);
    VQEC_LOG_DEBUG("[solution={}] scene applied: {} monitored gate(s)", kType, gates_.size());
}

std::vector<domain::Event> LineCrossingSolution::process(const FrameContext& context) {
    std::vector<domain::Event> events;
    std::map<domain::TrackId, domain::Point2f> anchors_now;
    for (const domain::Track& track : context.tracks) {
        const auto previous = last_anchors_.find(track.id);
        if (track.state == domain::TrackState::Lost) {
            if (previous != last_anchors_.end()) {
                anchors_now.insert(*previous);
            }
            continue;
        }
        if (track.state != domain::TrackState::Confirmed ||
            !matches_label(params_.labels, track.label)) {
            continue;
        }
        const domain::Point2f anchor = normalized_anchor(track, params_.anchor, context.frame);
        if (previous != last_anchors_.end()) {
            for (const domain::Gate& gate : gates_) {
                const domain::CrossingDirection direction =
                    domain::detect_crossing(gate, previous->second, anchor);
                if (direction == domain::CrossingDirection::None) {
                    continue;
                }
                domain::Event event = make_track_event(kCrossEvent, context.frame, track);
                event.gate_id = gate.id;
                event.attributes.emplace(std::string(kDirectionAttribute),
                                         std::string(domain::to_string(direction)));
                events.push_back(std::move(event));
            }
        }
        anchors_now.emplace(track.id, anchor);
    }
    last_anchors_ = std::move(anchors_now);
    return events;
}

}  // namespace vqec::app
