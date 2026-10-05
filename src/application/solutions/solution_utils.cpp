#include "application/solutions/solution_utils.hpp"

#include <algorithm>

namespace vqec::app {
namespace {

template <typename Item, typename Id>
std::vector<Item> select_by_id(const std::vector<Item>& items, const std::vector<Id>& ids) {
    if (ids.empty()) {
        return items;
    }
    std::vector<Item> selected;
    std::copy_if(items.begin(), items.end(), std::back_inserter(selected),
                 [&ids](const Item& item) {
                     return std::find(ids.begin(), ids.end(), item.id) != ids.end();
                 });
    return selected;
}

}  // namespace

bool matches_label(const std::vector<std::string>& allowed, const std::string& label) {
    return allowed.empty() || std::find(allowed.begin(), allowed.end(), label) != allowed.end();
}

domain::Point2f normalized_anchor(const domain::Track& track, domain::AnchorPoint anchor,
                                  const domain::Frame& frame) {
    return domain::normalize(domain::anchor_of(track.box, anchor), frame.width, frame.height);
}

std::vector<domain::Zone> select_zones(const domain::Scene& scene,
                                       const std::vector<domain::ZoneId>& ids) {
    return select_by_id(scene.zones, ids);
}

std::vector<domain::Gate> select_gates(const domain::Scene& scene,
                                       const std::vector<domain::GateId>& ids) {
    return select_by_id(scene.gates, ids);
}

domain::Event make_track_event(std::string_view type, const domain::Frame& frame,
                               const domain::Track& track) {
    domain::Event event;
    event.type = std::string(type);
    event.stream_id = frame.stream_id;
    event.frame_sequence = frame.sequence;
    event.occurred_at = frame.captured_at;
    event.track_id = track.id;
    event.attributes.emplace(std::string(kLabelAttribute), track.label);
    return event;
}

}  // namespace vqec::app
