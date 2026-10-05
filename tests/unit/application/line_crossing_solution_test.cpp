#include "application/solutions/line_crossing/line_crossing_solution.hpp"

#include <vector>

#include <gtest/gtest.h>

#include "support/builders.hpp"

namespace vqec::app {
namespace {

using test::make_frame;
using test::make_track;

class LineCrossingSolutionTest : public ::testing::Test {
protected:
    LineCrossingSolutionTest() : solution_(LineCrossingParams{}) {
        solution_.set_scene(domain::Scene{{}, {test::make_middle_gate()}});
    }

    std::vector<domain::Event> step(std::vector<domain::Track> tracks) {
        return solution_.process(FrameContext{make_frame(sequence_++), std::move(tracks)});
    }

    LineCrossingSolution solution_;
    std::uint64_t sequence_ = 0;
};

TEST_F(LineCrossingSolutionTest, RaisesForwardCrossingWhenTrackMovesDown) {
    EXPECT_TRUE(step({make_track(3, 0.5F, 0.4F)}).empty());

    const std::vector<domain::Event> events = step({make_track(3, 0.5F, 0.6F)});

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].type, LineCrossingSolution::kCrossEvent);
    EXPECT_EQ(events[0].gate_id, std::optional<domain::GateId>("gate_a"));
    EXPECT_EQ(events[0].attributes.at(std::string(LineCrossingSolution::kDirectionAttribute)),
              "forward");
}

TEST_F(LineCrossingSolutionTest, RaisesBackwardCrossingWhenTrackMovesUp) {
    EXPECT_TRUE(step({make_track(3, 0.5F, 0.6F)}).empty());

    const std::vector<domain::Event> events = step({make_track(3, 0.5F, 0.4F)});

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].attributes.at(std::string(LineCrossingSolution::kDirectionAttribute)),
              "backward");
}

TEST_F(LineCrossingSolutionTest, ForgetsTracksThatDisappear) {
    EXPECT_TRUE(step({make_track(3, 0.5F, 0.4F)}).empty());
    EXPECT_TRUE(step({}).empty());

    // Same id reappearing on the other side is a new observation, not a crossing.
    EXPECT_TRUE(step({make_track(3, 0.5F, 0.6F)}).empty());
}

}  // namespace
}  // namespace vqec::app
