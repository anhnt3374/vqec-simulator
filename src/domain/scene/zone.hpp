#pragma once

#include <string>
#include <vector>

#include "common/result.hpp"
#include "domain/geometry/point.hpp"
#include "domain/geometry/polygon.hpp"
#include "domain/ids.hpp"

namespace vqec::domain {

// A monitored area of one stream. Vertices are normalized [0, 1] frame coordinates so a zone
// stays valid when the stream resolution changes. Build with make_zone() to get validation.
struct Zone {
    ZoneId id;
    std::string name;
    Polygon area;
};

Result<Zone> make_zone(ZoneId id, std::string name, std::vector<Point2f> vertices);

}  // namespace vqec::domain
