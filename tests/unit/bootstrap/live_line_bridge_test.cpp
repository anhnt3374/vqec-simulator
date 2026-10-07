#include "bootstrap/live_line_bridge.hpp"

#include <gtest/gtest.h>

namespace vqec::test {
TEST(LiveLineBridge, ParsesNormalizedGateAndNullReset) {
    auto parsed =
        bootstrap::parse_live_line_update(R"({"revision":"first","line":[[0.1,0.5],[0.9,0.5]]})");
    ASSERT_TRUE(parsed.ok());
    ASSERT_TRUE(parsed.value().gate);
    EXPECT_EQ(parsed.value().revision, "first");
    EXPECT_EQ(parsed.value().gate->id, "live_line");
    EXPECT_FLOAT_EQ(parsed.value().gate->line.start.x, 0.1F);
    auto clear = bootstrap::parse_live_line_update(R"({"revision":"second","line":null})");
    ASSERT_TRUE(clear.ok());
    EXPECT_FALSE(clear.value().gate);
}
TEST(LiveLineBridge, RejectsMalformedDegenerateAndOutOfBoundsSnapshots) {
    for (const auto* contents : {"{}", "broken[", R"({"revision":"x","line":[[0,0],[0,0]]})",
                                 R"({"revision":"x","line":[[-1,0],[1,1]]})",
                                 R"({"revision":"x","line":[[0,0],[1,1],[0.5,0.5]]})",
                                 R"({"revision":"x","line":[[0,0],[1,.nan]]})"}) {
        EXPECT_FALSE(bootstrap::parse_live_line_update(contents).ok()) << contents;
    }
}
TEST(LiveLineBridge, RejectsMalformedZoneEvenWhenLineIsNull) {
    EXPECT_FALSE(
        bootstrap::parse_live_line_update(R"({"revision":"x","line":null,"zone":[[0,0],[1,1]]})")
            .ok());
    EXPECT_FALSE(bootstrap::parse_live_line_update(
                     R"({"revision":"x","line":null,"zone":[[0,0],[2,0],[0,1]]})")
                     .ok());
}
TEST(LiveLineBridge, ParsesIndependentFeatureRevisionsAndZoneWithoutLine) {
    auto parsed = bootstrap::parse_live_line_update(
        R"({"revision":"all","line_revision":"line","zone_revision":"zone",
            "line":null,"zone":[[0.2,0.2],[0.8,0.2],[0.8,0.8],[0.2,0.8]]})");
    ASSERT_TRUE(parsed.ok());
    EXPECT_FALSE(parsed.value().gate);
    ASSERT_TRUE(parsed.value().zone);
    EXPECT_EQ(parsed.value().line_revision, "line");
    EXPECT_EQ(parsed.value().zone_revision, "zone");
    EXPECT_TRUE(parsed.value().zone->area.contains({0.5F, 0.5F}));
    auto legacy = bootstrap::parse_live_line_update(R"({"revision":"legacy","line":null})");
    ASSERT_TRUE(legacy.ok());
    EXPECT_FALSE(legacy.value().zone);
    EXPECT_EQ(legacy.value().line_revision, "legacy");
    EXPECT_EQ(legacy.value().zone_revision, "legacy");
}
}  // namespace vqec::test
