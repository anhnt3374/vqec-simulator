#include "application/solutions/zone_intrusion/zone_intrusion_solution.hpp"

#include <vector>

#include <gtest/gtest.h>

#include "support/builders.hpp"

namespace vqec::app {
namespace {

using domain::TrackState;
using test::make_frame;
using test::make_track;

class ZoneIntrusionSolutionTest : public ::testing::Test {
protected:
    ZoneIntrusionSolutionTest() : solution_(ZoneIntrusionParams{}) {
        solution_.set_scene(domain::Scene{{test::make_bottom_right_zone()}, {}});
    }

    std::vector<domain::Event> step(std::vector<domain::Track> tracks) {
        return solution_.process(FrameContext{make_frame(sequence_++), std::move(tracks)});
    }

    ZoneIntrusionSolution solution_;
    std::uint64_t sequence_ = 0;
};

TEST_F(ZoneIntrusionSolutionTest, RaisesEnterEventWhenTrackMovesIntoZone) {
    EXPECT_TRUE(step({make_track(7, 0.2F, 0.2F)}).empty());

    const std::vector<domain::Event> events = step({make_track(7, 0.7F, 0.7F)});

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].type, ZoneIntrusionSolution::kEnterEvent);
    EXPECT_EQ(events[0].zone_id, std::optional<domain::ZoneId>("zone_a"));
    EXPECT_EQ(events[0].track_id, std::optional<domain::TrackId>(7));
    EXPECT_EQ(events[0].attributes.at("label"), "person");
}

TEST_F(ZoneIntrusionSolutionTest, DoesNotRepeatWhileTrackStaysInside) {
    EXPECT_EQ(step({make_track(7, 0.7F, 0.7F)}).size(), 1U);

    EXPECT_TRUE(step({make_track(7, 0.8F, 0.8F)}).empty());
}

TEST_F(ZoneIntrusionSolutionTest, IgnoresTentativeTracks) {
    EXPECT_TRUE(step({make_track(7, 0.7F, 0.7F, TrackState::Tentative)}).empty());
}

TEST_F(ZoneIntrusionSolutionTest, LostTrackKeepsMembershipSoRecoveryDoesNotRefire) {
    EXPECT_EQ(step({make_track(7, 0.7F, 0.7F)}).size(), 1U);
    EXPECT_TRUE(step({make_track(7, 0.7F, 0.7F, TrackState::Lost)}).empty());

    EXPECT_TRUE(step({make_track(7, 0.7F, 0.7F)}).empty());
}

TEST(ZoneIntrusionSolution, IgnoresLabelsOutsideTheFilter) {
    ZoneIntrusionSolution solution(
        ZoneIntrusionParams{{}, {"car"}, domain::AnchorPoint::BottomCenter});
    solution.set_scene(domain::Scene{{test::make_bottom_right_zone()}, {}});

    const std::vector<domain::Event> events =
        solution.process(FrameContext{make_frame(0), {make_track(1, 0.7F, 0.7F)}});

    EXPECT_TRUE(events.empty());
}

}  // namespace
}  // namespace vqec::app
