#pragma once

#include <string>
#include <string_view>

#include "common/result.hpp"
#include "domain/geometry/point.hpp"
#include "domain/geometry/segment.hpp"
#include "domain/ids.hpp"

namespace vqec::domain {

// Direction of a crossing relative to the gate line as drawn on screen (start -> end).
// Forward: the object moves onto the right-hand side of the arrow start -> end.
//   Example: a gate drawn left -> right counts objects moving top -> bottom as Forward.
// Backward: the opposite movement.
enum class CrossingDirection {
    None,
    Forward,
    Backward,
};

std::string_view to_string(CrossingDirection direction);

// A virtual line for counting. Endpoints are normalized [0, 1] frame coordinates.
// Build with make_gate() to get validation.
struct Gate {
    GateId id;
    std::string name;
    Segment line;
};

Result<Gate> make_gate(GateId id, std::string name, Point2f start, Point2f end);

// Reports whether moving from `previous` to `current` crosses the gate segment (not its infinite
// extension). Positions exactly on the line do not count, so an object must fully cross.
CrossingDirection detect_crossing(const Gate& gate, Point2f previous, Point2f current);

}  // namespace vqec::domain
