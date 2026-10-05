#pragma once

#include <vector>

#include "domain/scene/gate.hpp"
#include "domain/scene/zone.hpp"

namespace vqec::domain {

// Zones and gates configured for one stream. Ids are unique within a scene.
struct Scene {
    std::vector<Zone> zones;
    std::vector<Gate> gates;
};

}  // namespace vqec::domain
