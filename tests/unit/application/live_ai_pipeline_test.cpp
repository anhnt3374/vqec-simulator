#include "application/pipeline/live_ai_pipeline.hpp"

#include <condition_variable>
#include <mutex>

#include <gtest/gtest.h>

#include "fakes/fake_frame_source.hpp"
#include "fakes/fake_object_detector.hpp"
#include "fakes/fake_object_tracker.hpp"
#include "fakes/recording_frame_sink.hpp"

namespace vqec::test {
namespace {
struct Gate {
    std::mutex mutex;
    std::condition_variable ready;
    int entered = 0;
};
class PairedDetector final : public ports::IObjectDetector {
public:
    PairedDetector(std::shared_ptr<Gate> gate, std::string label, bool fail = false)
        : gate_(std::move(gate)), labels_{std::move(label)}, fail_(fail) {}
    Result<std::vector<domain::Detection>> detect(const domain::Frame& frame) override {
        {
            std::unique_lock<std::mutex> lock(gate_->mutex);
            ++gate_->entered;
            gate_->ready.notify_all();
            if (!gate_->ready.wait_for(lock, std::chrono::seconds(2),
                                       [&] { return gate_->entered >= 2; }))
                return make_error(ErrorCode::Timeout, "detectors did not execute concurrently");
        }
        if (fail_)
            return make_error(ErrorCode::Internal, "scripted detector failure");
        return std::vector<domain::Detection>{
            {{static_cast<float>(frame.sequence), 0, 10, 10}, 0, labels_[0], 0.9F}};
    }
    const std::vector<std::string>& labels() const override { return labels_; }

private:
    std::shared_ptr<Gate> gate_;
    std::vector<std::string> labels_;
    bool fail_;
};
}  // namespace

TEST(FrameAdmission, LimitsProcessingByPtsAndResetsAfterReconnect) {
    app::FrameAdmission rate(10.0);
    EXPECT_TRUE(rate.admit(std::chrono::milliseconds(0)));
    EXPECT_FALSE(rate.admit(std::chrono::milliseconds(50)));
    EXPECT_TRUE(rate.admit(std::chrono::milliseconds(100)));
    EXPECT_TRUE(rate.admit(std::chrono::milliseconds(0)));
    app::FrameAdmission unlimited(0.0);
    EXPECT_TRUE(unlimited.admit(std::chrono::milliseconds(0)));
    EXPECT_TRUE(unlimited.admit(std::chrono::milliseconds(0)));
}

TEST(LiveAiPipeline, DispatchesBothDetectorsConcurrentlyAndMergesSameFrame) {
    auto gate = std::make_shared<Gate>();
    auto record = std::make_shared<RecordingFrameSink::Record>();
    domain::Frame frame;
    frame.sequence = 7;
    frame.width = 100;
    frame.height = 100;
    app::LiveAiPipeline pipeline(
        "test", std::make_unique<FakeFrameSource>(std::deque<domain::Frame>{frame}),
        std::make_shared<PairedDetector>(gate, "person"),
        std::make_shared<PairedDetector>(gate, "face"),
        std::make_unique<RecordingFrameSink>(record));
    const std::atomic<bool> stop{false};
    EXPECT_TRUE(pipeline.run(stop).ok());
    ASSERT_EQ(record->writes.size(), 1U);
    ASSERT_EQ(record->writes[0].tracks.size(), 2U);
    EXPECT_EQ(record->writes[0].tracks[0].label, "person");
    EXPECT_EQ(record->writes[0].tracks[1].label, "face");
    EXPECT_EQ(record->writes[0].tracks[0].box.x, record->writes[0].tracks[1].box.x);
    EXPECT_TRUE(record->closed);
}

TEST(LiveAiPipeline, PropagatesBranchFailureAndJoinsAllWorkers) {
    auto gate = std::make_shared<Gate>();
    auto record = std::make_shared<RecordingFrameSink::Record>();
    domain::Frame frame;
    app::LiveAiPipeline pipeline(
        "test", std::make_unique<FakeFrameSource>(std::deque<domain::Frame>{frame}),
        std::make_shared<PairedDetector>(gate, "person", true),
        std::make_shared<PairedDetector>(gate, "face"),
        std::make_unique<RecordingFrameSink>(record));
    const std::atomic<bool> stop{false};
    EXPECT_FALSE(pipeline.run(stop).ok());
    EXPECT_TRUE(record->writes.empty());
    EXPECT_TRUE(record->closed);
}
TEST(LiveAiPipeline, PublishesFramesWithNoDetectionsAndClosesAtEof) {
    auto record = std::make_shared<RecordingFrameSink::Record>();
    auto person = std::make_shared<FakeObjectDetector>();
    auto face = std::make_shared<FakeObjectDetector>();
    domain::Frame first, second;
    second.pts = std::chrono::milliseconds(40);
    app::LiveAiPipeline pipeline(
        "test", std::make_unique<FakeFrameSource>(std::deque<domain::Frame>{first, second}), person,
        face, std::make_unique<RecordingFrameSink>(record));
    const std::atomic<bool> stop{false};
    EXPECT_TRUE(pipeline.run(stop).ok());
    EXPECT_EQ(person->calls(), 2);
    EXPECT_EQ(face->calls(), 2);
    ASSERT_EQ(record->writes.size(), 2U);
    EXPECT_TRUE(record->writes[0].tracks.empty());
    EXPECT_TRUE(record->writes[1].tracks.empty());
    EXPECT_TRUE(record->closed);
}

TEST(LiveAiPipeline, ShutsDownAtEofWithoutDispatchWhenSourceIsEmpty) {
    auto record = std::make_shared<RecordingFrameSink::Record>();
    auto person = std::make_shared<FakeObjectDetector>();
    auto face = std::make_shared<FakeObjectDetector>();
    app::LiveAiPipeline pipeline("test",
                                 std::make_unique<FakeFrameSource>(std::deque<domain::Frame>{}),
                                 person, face, std::make_unique<RecordingFrameSink>(record));
    const std::atomic<bool> stop{false};
    EXPECT_TRUE(pipeline.run(stop).ok());
    EXPECT_EQ(person->calls(), 0);
    EXPECT_EQ(face->calls(), 0);
    EXPECT_TRUE(record->writes.empty());
    EXPECT_TRUE(record->closed);
}

TEST(LiveAiPipeline, UsesPersonTrackerIdsAndKeepsFacesFrameLocal) {
    auto gate = std::make_shared<Gate>();
    auto record = std::make_shared<RecordingFrameSink::Record>();
    domain::Track tracked{42, domain::TrackState::Confirmed, {10, 10, 20, 20}, 0, "person", 0.9F};
    app::LiveAiPipeline pipeline(
        "test", std::make_unique<FakeFrameSource>(std::deque<domain::Frame>{domain::Frame{}}),
        std::make_shared<PairedDetector>(gate, "person"),
        std::make_shared<PairedDetector>(gate, "face"),
        std::make_unique<RecordingFrameSink>(record), app::LiveAiOptions{},
        std::make_unique<FakeObjectTracker>(std::deque<std::vector<domain::Track>>{{tracked}}));
    const std::atomic<bool> stop{false};
    ASSERT_TRUE(pipeline.run(stop).ok());
    ASSERT_EQ(record->writes.size(), 1U);
    ASSERT_EQ(record->writes[0].tracks.size(), 2U);
    EXPECT_EQ(record->writes[0].tracks[0].id, 42U);
    EXPECT_EQ(record->writes[0].tracks[1].id, 0U);
    EXPECT_EQ(record->writes[0].tracks[1].label, "face");
}

TEST(LiveAiPipeline, ResetsTrackerAndAdmissionWhenSourcePtsRewinds) {
    auto record = std::make_shared<RecordingFrameSink::Record>();
    auto tracker_record = std::make_shared<FakeObjectTracker::Record>();
    domain::Frame first, second, rewound;
    second.pts = std::chrono::milliseconds(200);
    app::LiveAiOptions options;
    options.processing_fps = 5;
    app::LiveAiPipeline pipeline(
        "test",
        std::make_unique<FakeFrameSource>(std::deque<domain::Frame>{first, second, rewound}),
        std::make_shared<FakeObjectDetector>(), std::make_shared<FakeObjectDetector>(),
        std::make_unique<RecordingFrameSink>(record), options,
        std::make_unique<FakeObjectTracker>(std::deque<std::vector<domain::Track>>{},
                                            tracker_record));
    const std::atomic<bool> stop{false};
    ASSERT_TRUE(pipeline.run(stop).ok());
    EXPECT_EQ(tracker_record->resets, 1);
    EXPECT_EQ(record->writes.size(), 3U);
}

}  // namespace vqec::test
