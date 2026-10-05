#pragma once

namespace vqec::domain {

struct Point2f {
    float x = 0.0F;
    float y = 0.0F;
};

// Side test for `point` against the directed line start -> end. The sign tells which side the
// point lies on; zero means collinear. Works in any coordinate space (pixel or normalized).
constexpr float orientation(Point2f start, Point2f end, Point2f point) {
    return (end.x - start.x) * (point.y - start.y) - (end.y - start.y) * (point.x - start.x);
}

// True when both coordinates are in [0, 1], the frame-relative space used by zones and gates.
// NaN is rejected because every comparison with NaN is false.
constexpr bool is_normalized(Point2f point) {
    return point.x >= 0.0F && point.x <= 1.0F && point.y >= 0.0F && point.y <= 1.0F;
}

}  // namespace vqec::domain
