#pragma once

#include "domain/geometry/point.hpp"

namespace vqec::domain {

// Axis-aligned box in pixel coordinates of the source frame: top-left corner plus size.
struct BoxF {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

// The point of a box that stands for the object's position when testing zones and gates.
// BottomCenter approximates the ground contact point of people and vehicles.
enum class AnchorPoint {
    Center,
    BottomCenter,
};

Point2f anchor_of(const BoxF& box, AnchorPoint anchor);

// Converts a pixel position into normalized [0, 1] frame coordinates.
// Precondition: frame_width > 0 and frame_height > 0.
Point2f normalize(Point2f pixel, int frame_width, int frame_height);

}  // namespace vqec::domain
