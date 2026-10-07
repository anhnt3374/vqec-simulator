#include "application/pipeline/live_line_monitor.hpp"

#include <gtest/gtest.h>

#include "support/builders.hpp"

namespace vqec::test {
namespace {
class LiveLineMonitorTest : public ::testing::Test {
protected:
    app::LiveLineMonitor monitor;
    LiveLineMonitorTest() { monitor.set_gate(make_middle_gate()); }
    ports::FrameAnnotations step(std::vector<domain::Track> tracks) {
        ports::FrameAnnotations result;
        result.tracks = std::move(tracks);
        monitor.annotate(make_frame(1), result);
        return result;
    }
};
TEST_F(LiveLineMonitorTest, LogsEveryOutwardCrossingAndKeepsTrackRedUntilItEnds) {
    EXPECT_TRUE(step({make_track(7, 0.5F, 0.4F)}).events.empty());
    auto crossed = step({make_track(7, 0.5F, 0.6F)});
    ASSERT_EQ(crossed.events.size(), 1U);
    EXPECT_EQ(crossed.events[0].type, "line_crossing.cross");
    EXPECT_EQ(crossed.events[0].attributes.at("direction"), "forward");
    EXPECT_EQ(crossed.events[0].track_id.value(), 7U);
    EXPECT_EQ(crossed.outward_crossed_tracks, std::vector<domain::TrackId>{7});
    EXPECT_EQ(step({make_track(7, 0.5F, 0.7F)}).outward_crossed_tracks,
              std::vector<domain::TrackId>{7});
    EXPECT_TRUE(step({make_track(7, 0.5F, 0.4F)}).events.empty());
    EXPECT_EQ(step({make_track(7, 0.5F, 0.6F)}).events.size(), 1U);
    step({});
    EXPECT_TRUE(step({make_track(7, 0.5F, 0.6F)}).outward_crossed_tracks.empty());
}
TEST_F(LiveLineMonitorTest, IgnoresInwardCrossingsAndInfiniteLineExtension) {
    step({make_track(7, 0.5F, 0.6F), make_track(8, 0.95F, 0.4F)});
    const auto result = step({make_track(7, 0.5F, 0.4F), make_track(8, 0.95F, 0.6F)});
    EXPECT_TRUE(result.events.empty());
    EXPECT_TRUE(result.outward_crossed_tracks.empty());
}
TEST_F(LiveLineMonitorTest, PreservesLastSideWhileAnchorIsExactlyOnLine) {
    step({make_track(7, 0.5F, 0.4F)});
    EXPECT_TRUE(step({make_track(7, 0.5F, 0.5F)}).events.empty());
    EXPECT_EQ(step({make_track(7, 0.5F, 0.6F)}).events.size(), 1U);
}
TEST_F(LiveLineMonitorTest, KeepsRedHighlightWhileConfirmedAnchorTemporarilyLeavesFrame) {
    step({make_track(7, 0.5F, 0.4F)});
    step({make_track(7, 0.5F, 0.6F)});
    const auto outside = step({make_track(7, 0.5F, 1.02F)});
    EXPECT_TRUE(outside.events.empty());
    EXPECT_EQ(outside.outward_crossed_tracks, std::vector<domain::TrackId>{7});
    const auto returned = step({make_track(7, 0.5F, 0.7F)});
    EXPECT_TRUE(returned.events.empty());
    EXPECT_EQ(returned.outward_crossed_tracks, std::vector<domain::TrackId>{7});
}
TEST_F(LiveLineMonitorTest, KeepsStateWhileLostAndIgnoresFacesAndTentativeTracks) {
    step({make_track(7, 0.5F, 0.4F)});
    step({make_track(7, 0.5F, 0.9F, domain::TrackState::Lost)});
    auto result =
        step({make_track(7, 0.5F, 0.6F), make_track(8, 0.5F, 0.4F, domain::TrackState::Tentative),
              make_track(9, 0.5F, 0.4F, domain::TrackState::Confirmed, "face")});
    EXPECT_EQ(result.events.size(), 1U);
    result = step({make_track(8, 0.5F, 0.6F, domain::TrackState::Tentative),
                   make_track(9, 0.5F, 0.6F, domain::TrackState::Confirmed, "face")});
    EXPECT_TRUE(result.events.empty());
}
TEST_F(LiveLineMonitorTest, EditingClearingAndReconnectResetCrossingState) {
    step({make_track(7, 0.5F, 0.4F)});
    step({make_track(7, 0.5F, 0.6F)});
    monitor.reset();
    EXPECT_TRUE(step({make_track(7, 0.5F, 0.6F)}).outward_crossed_tracks.empty());
    monitor.set_gate(std::nullopt);
    EXPECT_TRUE(step({make_track(7, 0.5F, 0.4F)}).events.empty());
    auto reversed = make_middle_gate();
    std::swap(reversed.line.start, reversed.line.end);
    monitor.set_gate(reversed);
    step({make_track(7, 0.5F, 0.6F)});
    EXPECT_EQ(step({make_track(7, 0.5F, 0.4F)}).events.size(), 1U);
    monitor.set_gate(make_middle_gate());
    EXPECT_TRUE(step({make_track(7, 0.5F, 0.6F)}).events.empty());
}
}  // namespace
}  // namespace vqec::test
