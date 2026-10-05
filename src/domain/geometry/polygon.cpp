#include "domain/geometry/polygon.hpp"

#include <string>
#include <utility>

namespace vqec::domain {

Result<Polygon> Polygon::create(std::vector<Point2f> vertices) {
    if (vertices.size() < kMinVertices) {
        return make_error(ErrorCode::InvalidArgument,
                          "polygon needs at least " + std::to_string(kMinVertices) +
                              " vertices, got " + std::to_string(vertices.size()));
    }
    return Polygon(std::move(vertices));
}

Polygon::Polygon(std::vector<Point2f> vertices) : vertices_(std::move(vertices)) {}

bool Polygon::contains(Point2f point) const {
    bool inside = false;
    const std::size_t count = vertices_.size();
    for (std::size_t current = 0, previous = count - 1; current < count; previous = current++) {
        const Point2f& a = vertices_[current];
        const Point2f& b = vertices_[previous];
        // Cast a horizontal ray to +x and count the edges it crosses.
        const bool edge_spans_ray = (a.y > point.y) != (b.y > point.y);
        if (edge_spans_ray) {
            const float crossing_x = a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (point.x < crossing_x) {
                inside = !inside;
            }
        }
    }
    return inside;
}

}  // namespace vqec::domain
