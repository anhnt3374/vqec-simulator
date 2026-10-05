#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "application/solutions/solution.hpp"
#include "domain/geometry/box.hpp"
#include "domain/geometry/point.hpp"
#include "domain/ids.hpp"
#include "domain/scene/gate.hpp"

namespace vqec::app {

struct LineCrossingParams {
    std::vector<domain::GateId> gate_ids;  // Empty: every gate of the scene.
    std::vector<std::string> labels;       // Empty: every label.
    domain::AnchorPoint anchor = domain::AnchorPoint::BottomCenter;
};

// Raises "line_crossing.cross" each time a confirmed track crosses a gate, with attribute
// "direction" = "forward" | "backward". Counting is done by querying these events.
class LineCrossingSolution final : public ISolution {
public:
    static constexpr std::string_view kType = "line_crossing";
    static constexpr std::string_view kCrossEvent = "line_crossing.cross";
    static constexpr std::string_view kDirectionAttribute = "direction";

    explicit LineCrossingSolution(LineCrossingParams params);

    std::string_view type() const override { return kType; }
    void set_scene(const domain::Scene& scene) override;
    std::vector<domain::Event> process(const FrameContext& context) override;

private:
    LineCrossingParams params_;
    std::vector<domain::Gate> gates_;
    std::map<domain::TrackId, domain::Point2f> last_anchors_;  // Normalized, previous frame.
};

}  // namespace vqec::app
