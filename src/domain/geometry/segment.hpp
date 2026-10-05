#pragma once

#include "domain/geometry/point.hpp"

namespace vqec::domain {

// Directed line segment from `start` to `end`.
struct Segment {
    Point2f start;
    Point2f end;
};

}  // namespace vqec::domain
