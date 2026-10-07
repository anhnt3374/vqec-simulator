#include "adapters/outbound/tracking/bytetrack/byte_tracker.hpp"

#include <gtest/gtest.h>
namespace vqec::test {
namespace {
domain::Frame frame(int ms) {
    domain::Frame f;
    f.pts = std::chrono::milliseconds(ms);
    return f;
}
domain::Detection person(float x = 10, float score = 0.9F) {
    return {{x, 10, 40, 80}, 0, "person", score};
}
}  // namespace
TEST(ByteTracker, KeepsIdForMovingPersonAndUsesLowConfidenceAssociation) {
    adapters::bytetrack::ByteTracker tracker;
    auto first = tracker.update(frame(0), {person()});
    ASSERT_EQ(first.size(), 1U);
    EXPECT_GT(first[0].id, 0U);
    auto second = tracker.update(frame(40), {person(12)});
    ASSERT_EQ(second.size(), 1U);
    EXPECT_EQ(second[0].id, first[0].id);
    auto weak = tracker.update(frame(80), {person(14, 0.2F)});
    ASSERT_EQ(weak.size(), 1U);
    EXPECT_EQ(weak[0].id, first[0].id);
    EXPECT_EQ(weak[0].state, domain::TrackState::Confirmed);
}
TEST(ByteTracker, ReacquiresShortLossButExpiresByPtsWhenProcessingIsSlow) {
    adapters::bytetrack::ByteTracker tracker;
    auto first = tracker.update(frame(0), {person()});
    auto lost = tracker.update(frame(200), {});
    ASSERT_EQ(lost.size(), 1U);
    EXPECT_EQ(lost[0].state, domain::TrackState::Lost);
    auto found = tracker.update(frame(400), {person()});
    ASSERT_EQ(found.size(), 1U);
    EXPECT_EQ(found[0].id, first[0].id);
    auto expired = tracker.update(frame(1600), {person()});
    ASSERT_EQ(expired.size(), 1U);
    EXPECT_NE(expired[0].id, first[0].id);
}
TEST(ByteTracker, KeepsDistinctIdsWhenDetectionOrderChanges) {
    adapters::bytetrack::ByteTracker tracker;
    auto first = tracker.update(frame(0), {person(10), person(200)});
    ASSERT_EQ(first.size(), 2U);
    auto second = tracker.update(frame(40), {person(202), person(12)});
    ASSERT_EQ(second.size(), 2U);
    EXPECT_EQ(second[0].id, first[0].id);
    EXPECT_EQ(second[1].id, first[1].id);
    EXPECT_LT(second[0].box.x, second[1].box.x);
}
TEST(ByteTracker, DoesNotStartTracksFromWeakDetectionsOrFaces) {
    adapters::bytetrack::ByteTracker tracker;
    auto face = person();
    face.label = "face";
    EXPECT_TRUE(tracker.update(frame(0), {person(10, 0.2F), face}).empty());
}
TEST(ByteTracker, ResetAndPtsRewindForgetAssociationsWithoutReusingIds) {
    adapters::bytetrack::ByteTracker tracker;
    auto first = tracker.update(frame(100), {person()});
    tracker.reset();
    auto second = tracker.update(frame(200), {person()});
    ASSERT_EQ(second.size(), 1U);
    EXPECT_GT(second[0].id, first[0].id);
    auto rewind = tracker.update(frame(0), {person()});
    ASSERT_EQ(rewind.size(), 1U);
    EXPECT_GT(rewind[0].id, second[0].id);
}
TEST(ByteTracker, RequiresAnotherHighDetectionToConfirmNewTracksAfterFirstFrame) {
    adapters::bytetrack::ByteTracker tracker;
    EXPECT_TRUE(tracker.update(frame(0), {}).empty());
    auto tentative = tracker.update(frame(40), {person()});
    ASSERT_EQ(tentative.size(), 1U);
    EXPECT_EQ(tentative[0].state, domain::TrackState::Tentative);
    auto confirmed = tracker.update(frame(80), {person()});
    ASSERT_EQ(confirmed.size(), 1U);
    EXPECT_EQ(confirmed[0].state, domain::TrackState::Confirmed);
}
}  // namespace vqec::test
