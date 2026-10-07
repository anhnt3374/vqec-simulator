#include "application/pipeline/live_zone_monitor.hpp"

#include <gtest/gtest.h>

#include "support/builders.hpp"

namespace vqec::test {
namespace {
class LiveZoneMonitorTest : public ::testing::Test {
protected:
    app::LiveZoneMonitor monitor;
    LiveZoneMonitorTest() { monitor.set_zone(make_bottom_right_zone()); }
    ports::FrameAnnotations step(std::vector<domain::Track> tracks,
                                 std::chrono::nanoseconds pts = std::chrono::nanoseconds{0}) {
        ports::FrameAnnotations result;
        result.tracks = std::move(tracks);
        auto frame = make_frame(1);
        frame.pts = pts;
        monitor.annotate(frame, result);
        return result;
    }
};
TEST_F(LiveZoneMonitorTest, HighlightsOnlyInsideAndLogsEveryEntryOnce) {
    EXPECT_TRUE(step({make_track(7, 0.2F, 0.2F)}).events.empty());
    const auto entered = step({make_track(7, 0.7F, 0.7F)});
    ASSERT_EQ(entered.events.size(), 1U);
    EXPECT_EQ(entered.events[0].type, "zone_intrusion.enter");
    EXPECT_EQ(entered.events[0].zone_id.value(), "zone_a");
    EXPECT_TRUE(entered.inside_zone_tracks.empty());
    EXPECT_TRUE(step({make_track(7, 0.8F, 0.8F)}).events.empty());
    const auto left = step({make_track(7, 0.2F, 0.2F)});
    EXPECT_TRUE(left.inside_zone_tracks.empty());
    EXPECT_TRUE(left.events.empty());
    EXPECT_EQ(step({make_track(7, 0.7F, 0.7F)}).events.size(), 1U);
}
TEST_F(LiveZoneMonitorTest, WaitsFiveSecondsOfStreamTimeBeforeHighlighting) {
    const auto track = make_track(7, 0.7F, 0.7F);
    EXPECT_TRUE(step({track}, std::chrono::seconds(10)).inside_zone_tracks.empty());
    EXPECT_TRUE(step({track}, std::chrono::milliseconds(14999)).inside_zone_tracks.empty());
    const auto mature = step({track}, std::chrono::seconds(15));
    EXPECT_EQ(mature.inside_zone_tracks, std::vector<domain::TrackId>{7});
    EXPECT_TRUE(mature.events.empty());
    EXPECT_EQ(step({track}, std::chrono::seconds(16)).inside_zone_tracks,
              std::vector<domain::TrackId>{7});
}
TEST_F(LiveZoneMonitorTest, LeavingAndReentryRestartTheFiveSecondTimer) {
    const auto inside = make_track(7, 0.7F, 0.7F);
    step({inside}, std::chrono::seconds(0));
    EXPECT_EQ(step({inside}, std::chrono::seconds(5)).inside_zone_tracks.size(), 1U);
    EXPECT_TRUE(
        step({make_track(7, 0.2F, 0.2F)}, std::chrono::seconds(6)).inside_zone_tracks.empty());
    EXPECT_TRUE(step({inside}, std::chrono::seconds(7)).inside_zone_tracks.empty());
    EXPECT_TRUE(step({inside}, std::chrono::seconds(11)).inside_zone_tracks.empty());
    EXPECT_EQ(step({inside}, std::chrono::seconds(12)).inside_zone_tracks.size(), 1U);
}
TEST_F(LiveZoneMonitorTest, PreservesTimerThroughShortLostRecoveryAndResetsExpiredTracks) {
    const auto inside = make_track(7, 0.7F, 0.7F);
    step({inside}, std::chrono::seconds(0));
    EXPECT_TRUE(
        step({make_track(7, 0.7F, 0.7F, domain::TrackState::Lost)}, std::chrono::milliseconds(4500))
            .inside_zone_tracks.empty());
    EXPECT_EQ(step({inside}, std::chrono::seconds(5)).inside_zone_tracks.size(), 1U);
    step({}, std::chrono::seconds(6));
    EXPECT_TRUE(step({inside}, std::chrono::seconds(7)).inside_zone_tracks.empty());
}
TEST_F(LiveZoneMonitorTest, ZoneEditAndReconnectResetDwellTimer) {
    const auto inside = make_track(7, 0.7F, 0.7F);
    step({inside}, std::chrono::seconds(0));
    step({inside}, std::chrono::seconds(5));
    monitor.set_zone(make_bottom_right_zone());
    EXPECT_TRUE(step({inside}, std::chrono::seconds(6)).inside_zone_tracks.empty());
    EXPECT_EQ(step({inside}, std::chrono::seconds(11)).inside_zone_tracks.size(), 1U);
    monitor.reset();
    EXPECT_TRUE(step({inside}, std::chrono::seconds(0)).inside_zone_tracks.empty());
    EXPECT_EQ(step({inside}, std::chrono::seconds(5)).inside_zone_tracks.size(), 1U);
}
TEST_F(LiveZoneMonitorTest, LostRecoveryDoesNotRefireAndFacesAndTentativeTracksAreIgnored) {
    step({make_track(7, 0.7F, 0.7F)});
    const auto lost = step({make_track(7, 0.7F, 0.7F, domain::TrackState::Lost)});
    EXPECT_TRUE(lost.inside_zone_tracks.empty());
    EXPECT_TRUE(step({make_track(7, 0.7F, 0.7F)}).events.empty());
    const auto ignored = step({make_track(8, 0.7F, 0.7F, domain::TrackState::Tentative),
                               make_track(9, 0.7F, 0.7F, domain::TrackState::Confirmed, "face")});
    EXPECT_TRUE(ignored.inside_zone_tracks.empty());
    EXPECT_TRUE(ignored.events.empty());
}
TEST_F(LiveZoneMonitorTest, ResetDeletionAndTrackExpiryClearZoneMembership) {
    step({make_track(7, 0.7F, 0.7F)});
    step({});
    EXPECT_EQ(step({make_track(7, 0.7F, 0.7F)}).events.size(), 1U);
    monitor.reset();
    EXPECT_EQ(step({make_track(7, 0.7F, 0.7F)}).events.size(), 1U);
    monitor.set_zone(std::nullopt);
    const auto cleared = step({make_track(7, 0.7F, 0.7F)});
    EXPECT_TRUE(cleared.events.empty());
    EXPECT_TRUE(cleared.inside_zone_tracks.empty());
}
}  // namespace
}  // namespace vqec::test
