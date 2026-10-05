#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "domain/event/event.hpp"
#include "domain/geometry/box.hpp"
#include "domain/geometry/point.hpp"
#include "domain/ids.hpp"
#include "domain/scene/scene.hpp"
#include "domain/vision/frame.hpp"
#include "domain/vision/track.hpp"

// Helpers shared by solutions. Keep this file small: logic specific to one solution stays in
// that solution's directory.
namespace vqec::app {

// Event attribute keys shared by all solutions.
inline constexpr std::string_view kLabelAttribute = "label";

// True when `allowed` is empty (no filter) or contains `label`.
bool matches_label(const std::vector<std::string>& allowed, const std::string& label);

// Anchor of the track box in normalized [0, 1] coordinates of `frame`.
domain::Point2f normalized_anchor(const domain::Track& track, domain::AnchorPoint anchor,
                                  const domain::Frame& frame);

// Zones/gates of `scene` whose id is listed; all of them when `ids` is empty.
std::vector<domain::Zone> select_zones(const domain::Scene& scene,
                                       const std::vector<domain::ZoneId>& ids);
std::vector<domain::Gate> select_gates(const domain::Scene& scene,
                                       const std::vector<domain::GateId>& ids);

// Event pre-filled with the frame and track fields every track-based event carries.
domain::Event make_track_event(std::string_view type, const domain::Frame& frame,
                               const domain::Track& track);

}  // namespace vqec::app
