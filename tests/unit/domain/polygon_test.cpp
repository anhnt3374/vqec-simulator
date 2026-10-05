#include "domain/geometry/polygon.hpp"

#include <gtest/gtest.h>

#include "domain/scene/zone.hpp"

namespace vqec::domain {
namespace {

Polygon make_square() {
    return Polygon::create({{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}).value();
}

TEST(Polygon, RejectsFewerThanThreeVertices) {
    const Result<Polygon> polygon = Polygon::create({{0.0F, 0.0F}, {1.0F, 1.0F}});

    ASSERT_FALSE(polygon.ok());
    EXPECT_EQ(polygon.error().code, ErrorCode::InvalidArgument);
}

TEST(Polygon, ContainsPointInside) {
    EXPECT_TRUE(make_square().contains({0.5F, 0.5F}));
}

TEST(Polygon, ExcludesPointOutside) {
    EXPECT_FALSE(make_square().contains({1.5F, 0.5F}));
}

TEST(Polygon, HandlesConcaveShape) {
    // U shape: the notch between the arms is outside.
    const Polygon u_shape = Polygon::create({{0.0F, 0.0F},
                                             {0.3F, 0.0F},
                                             {0.3F, 0.7F},
                                             {0.7F, 0.7F},
                                             {0.7F, 0.0F},
                                             {1.0F, 0.0F},
                                             {1.0F, 1.0F},
                                             {0.0F, 1.0F}})
                                .value();

    EXPECT_FALSE(u_shape.contains({0.5F, 0.3F}));
    EXPECT_TRUE(u_shape.contains({0.1F, 0.3F}));
    EXPECT_TRUE(u_shape.contains({0.5F, 0.9F}));
}

TEST(Zone, RejectsVerticesOutsideNormalizedRange) {
    const Result<Zone> zone = make_zone("zone_a", "A", {{0.0F, 0.0F}, {1.5F, 0.0F}, {1.0F, 1.0F}});

    ASSERT_FALSE(zone.ok());
    EXPECT_EQ(zone.error().code, ErrorCode::InvalidArgument);
}

}  // namespace
}  // namespace vqec::domain
