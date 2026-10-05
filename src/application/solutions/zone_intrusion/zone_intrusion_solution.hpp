#pragma once

#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "application/solutions/solution.hpp"
#include "domain/geometry/box.hpp"
#include "domain/ids.hpp"
#include "domain/scene/zone.hpp"

namespace vqec::app {

struct ZoneIntrusionParams {
    std::vector<domain::ZoneId> zone_ids;  // Empty: every zone of the scene.
    std::vector<std::string> labels;       // Empty: every label.
    domain::AnchorPoint anchor = domain::AnchorPoint::BottomCenter;
};

// Raises "zone_intrusion.enter" when a confirmed track's anchor moves into a zone.
// An object that stays inside raises nothing more; leaving and re-entering raises again.
class ZoneIntrusionSolution final : public ISolution {
public:
    static constexpr std::string_view kType = "zone_intrusion";
    static constexpr std::string_view kEnterEvent = "zone_intrusion.enter";

    explicit ZoneIntrusionSolution(ZoneIntrusionParams params);

    std::string_view type() const override { return kType; }
    void set_scene(const domain::Scene& scene) override;
    std::vector<domain::Event> process(const FrameContext& context) override;

private:
    using Membership = std::pair<domain::TrackId, domain::ZoneId>;

    void keep_memberships_of(domain::TrackId track_id, std::set<Membership>& target) const;

    ZoneIntrusionParams params_;
    std::vector<domain::Zone> zones_;
    std::set<Membership> inside_;  // (track, zone) pairs inside after the previous frame.
};

}  // namespace vqec::app
