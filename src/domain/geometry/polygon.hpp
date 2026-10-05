#pragma once

#include <cstddef>
#include <vector>

#include "common/result.hpp"
#include "domain/geometry/point.hpp"

namespace vqec::domain {

// Simple polygon (vertices in order, implicitly closed). Invariant: at least kMinVertices.
class Polygon {
public:
    static constexpr std::size_t kMinVertices = 3;

    static Result<Polygon> create(std::vector<Point2f> vertices);

    // Even-odd rule. Points exactly on an edge may be reported either way.
    bool contains(Point2f point) const;

    const std::vector<Point2f>& vertices() const { return vertices_; }

private:
    explicit Polygon(std::vector<Point2f> vertices);

    std::vector<Point2f> vertices_;
};

}  // namespace vqec::domain
