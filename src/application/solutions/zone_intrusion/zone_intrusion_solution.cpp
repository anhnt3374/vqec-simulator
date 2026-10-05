#include "application/solutions/zone_intrusion/zone_intrusion_solution.hpp"

#include "application/solutions/solution_utils.hpp"
#include "common/log.hpp"

namespace vqec::app {

ZoneIntrusionSolution::ZoneIntrusionSolution(ZoneIntrusionParams params)
    : params_(std::move(params)) {}

void ZoneIntrusionSolution::set_scene(const domain::Scene& scene) {
    zones_ = select_zones(scene, params_.zone_ids);
    // Keep state for zones that still exist so an edit does not re-fire for objects inside.
    std::set<Membership> kept;
    for (const Membership& membership : inside_) {
        for (const domain::Zone& zone : zones_) {
            if (zone.id == membership.second) {
                kept.insert(membership);
            }
        }
    }
    inside_ = std::move(kept);
    VQEC_LOG_DEBUG("[solution={}] scene applied: {} monitored zone(s)", kType, zones_.size());
}

std::vector<domain::Event> ZoneIntrusionSolution::process(const FrameContext& context) {
    std::vector<domain::Event> events;
    std::set<Membership> inside_now;
    for (const domain::Track& track : context.tracks) {
        if (track.state == domain::TrackState::Lost) {
            keep_memberships_of(track.id, inside_now);
            continue;
        }
        if (track.state != domain::TrackState::Confirmed ||
            !matches_label(params_.labels, track.label)) {
            continue;
        }
        const domain::Point2f anchor = normalized_anchor(track, params_.anchor, context.frame);
        for (const domain::Zone& zone : zones_) {
            if (!zone.area.contains(anchor)) {
                continue;
            }
            Membership membership{track.id, zone.id};
            if (inside_.count(membership) == 0) {
                domain::Event event = make_track_event(kEnterEvent, context.frame, track);
                event.zone_id = zone.id;
                events.push_back(std::move(event));
            }
            inside_now.insert(std::move(membership));
        }
    }
    inside_ = std::move(inside_now);
    return events;
}

void ZoneIntrusionSolution::keep_memberships_of(domain::TrackId track_id,
                                                std::set<Membership>& target) const {
    for (const Membership& membership : inside_) {
        if (membership.first == track_id) {
            target.insert(membership);
        }
    }
}

}  // namespace vqec::app
