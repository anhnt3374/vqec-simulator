#include "domain/scene/gate.hpp"

#include <utility>

namespace vqec::domain {

std::string_view to_string(CrossingDirection direction) {
    switch (direction) {
        case CrossingDirection::None:
            return "none";
        case CrossingDirection::Forward:
            return "forward";
        case CrossingDirection::Backward:
            return "backward";
    }
    return "unknown";
}

Result<Gate> make_gate(GateId id, std::string name, Point2f start, Point2f end) {
    if (id.empty()) {
        return make_error(ErrorCode::InvalidArgument, "gate id must not be empty");
    }
    if (!is_normalized(start) || !is_normalized(end)) {
        return make_error(ErrorCode::InvalidArgument,
                          "gate '" + id + "' has endpoints outside normalized [0, 1] coordinates");
    }
    if (start.x == end.x && start.y == end.y) {
        return make_error(ErrorCode::InvalidArgument,
                          "gate '" + id + "' has identical start and end points");
    }
    return Gate{std::move(id), std::move(name), Segment{start, end}};
}

CrossingDirection detect_crossing(const Gate& gate, Point2f previous, Point2f current) {
    const Segment& line = gate.line;
    const float side_before = orientation(line.start, line.end, previous);
    const float side_after = orientation(line.start, line.end, current);
    if (side_before == 0.0F || side_after == 0.0F || (side_before > 0.0F) == (side_after > 0.0F)) {
        return CrossingDirection::None;
    }
    // The movement changed sides of the infinite line; make sure it passed between the endpoints.
    const float start_side = orientation(previous, current, line.start);
    const float end_side = orientation(previous, current, line.end);
    if ((start_side > 0.0F && end_side > 0.0F) || (start_side < 0.0F && end_side < 0.0F)) {
        return CrossingDirection::None;
    }
    // Screen coordinates have y pointing down, so a positive orientation is the right-hand side.
    return side_after > 0.0F ? CrossingDirection::Forward : CrossingDirection::Backward;
}

}  // namespace vqec::domain
