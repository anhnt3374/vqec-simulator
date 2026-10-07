#include "application/pipeline/live_ai_pipeline.hpp"

#include <condition_variable>
#include <functional>
#include <mutex>

#include <gtest/gtest.h>

#include "fakes/fake_frame_source.hpp"
#include "fakes/fake_object_detector.hpp"
#include "fakes/fake_object_tracker.hpp"
#include "fakes/recording_frame_sink.hpp"
#include "support/builders.hpp"

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
// Executes edits on the coordinator between writes; no timing assumptions or real I/O.
class EditingFrameSink final : public ports::IFrameSink {
public:
    EditingFrameSink(std::shared_ptr<RecordingFrameSink::Record> record,
                     std::function<void(std::size_t)> on_write)
        : record_(std::move(record)), on_write_(std::move(on_write)) {}
    Status open(const ports::SourceInfo&) override { return {}; }
    Status write(const domain::Frame&, const ports::FrameAnnotations& annotations) override {
        record_->writes.push_back(annotations);
        on_write_(record_->writes.size());
        return {};
    }
    void close() override {}

private:
    std::shared_ptr<RecordingFrameSink::Record> record_;
    std::function<void(std::size_t)> on_write_;
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

TEST(LiveAiPipeline, AppliesTemporaryGateAndPublishesCrossingHighlights) {
    auto record = std::make_shared<RecordingFrameSink::Record>();
    auto first = make_frame(0);
    auto second = make_frame(1);
    auto third = make_frame(2);
    second.pts = std::chrono::milliseconds(100);
    third.pts = std::chrono::milliseconds(200);
    app::LiveAiPipeline pipeline(
        "test", std::make_unique<FakeFrameSource>(std::deque<domain::Frame>{first, second, third}),
        std::make_shared<FakeObjectDetector>(), std::make_shared<FakeObjectDetector>(),
        std::make_unique<RecordingFrameSink>(record), app::LiveAiOptions{},
        std::make_unique<FakeObjectTracker>(
            std::deque<std::vector<domain::Track>>{{make_track(7, 0.5F, 0.4F)},
                                                   {make_track(7, 0.5F, 0.6F)},
                                                   {make_track(7, 0.5F, 0.7F)}}));
    pipeline.request_gate_update(make_middle_gate());
    const std::atomic<bool> stop{false};
    ASSERT_TRUE(pipeline.run(stop).ok());
    ASSERT_EQ(record->writes.size(), 3U);
    ASSERT_EQ(record->writes[0].scene->gates.size(), 1U);
    EXPECT_TRUE(record->writes[0].events.empty());
    EXPECT_EQ(record->writes[1].events.size(), 1U);
    EXPECT_EQ(record->writes[1].outward_crossed_tracks, std::vector<domain::TrackId>{7});
    EXPECT_TRUE(record->writes[2].events.empty());
    EXPECT_TRUE(record->writes[2].inside_zone_tracks.empty());
    EXPECT_EQ(record->writes[2].outward_crossed_tracks, std::vector<domain::TrackId>{7});
}

TEST(LiveAiPipeline, LatestGateResetWinsBeforeFrameProcessing) {
    auto record = std::make_shared<RecordingFrameSink::Record>();
    app::LiveAiPipeline pipeline(
        "test", std::make_unique<FakeFrameSource>(std::deque<domain::Frame>{make_frame(0)}),
        std::make_shared<FakeObjectDetector>(), std::make_shared<FakeObjectDetector>(),
        std::make_unique<RecordingFrameSink>(record));
    pipeline.request_gate_update(make_middle_gate());
    pipeline.request_gate_update(std::nullopt);
    const std::atomic<bool> stop{false};
    ASSERT_TRUE(pipeline.run(stop).ok());
    ASSERT_EQ(record->writes.size(), 1U);
    EXPECT_TRUE(record->writes[0].scene->gates.empty());
}

TEST(LiveAiPipeline, ZoneAndGateEditsPreserveEachOthersHistoryAndScene) {
    auto record = std::make_shared<RecordingFrameSink::Record>();
    app::LiveAiPipeline* active = nullptr;  // Non-owning, assigned before the sink is called.
    auto sink = std::make_unique<EditingFrameSink>(record, [&](std::size_t count) {
        if (count == 2)
            active->request_zone_update(make_bottom_right_zone());
        if (count == 3)
            active->request_gate_update(make_middle_gate());
        if (count == 4)
            active->request_zone_update(std::nullopt);
    });
    std::deque<domain::Frame> frames;
    std::deque<std::vector<domain::Track>> tracks;
    for (std::uint64_t i = 0; i < 5; ++i) {
        auto frame = make_frame(i);
        frame.pts = std::chrono::seconds(i * 5);
        frames.push_back(frame);
        tracks.push_back({make_track(7, i == 0 ? 0.4F : 0.7F, i == 0 ? 0.4F : 0.7F)});
    }
    app::LiveAiPipeline pipeline("test", std::make_unique<FakeFrameSource>(std::move(frames)),
                                 std::make_shared<FakeObjectDetector>(),
                                 std::make_shared<FakeObjectDetector>(), std::move(sink),
                                 app::LiveAiOptions{},
                                 std::make_unique<FakeObjectTracker>(std::move(tracks)));
    active = &pipeline;
    pipeline.request_gate_update(make_middle_gate());
    pipeline.request_zone_update(make_bottom_right_zone());
    const std::atomic<bool> stop{false};
    ASSERT_TRUE(pipeline.run(stop).ok());
    ASSERT_EQ(record->writes.size(), 5U);
    EXPECT_EQ(record->writes[1].events.size(), 2U);
    EXPECT_TRUE(record->writes[1].inside_zone_tracks.empty());
    EXPECT_TRUE(record->writes[2].inside_zone_tracks.empty());
    EXPECT_EQ(record->writes[2].outward_crossed_tracks, std::vector<domain::TrackId>{7});
    ASSERT_EQ(record->writes[2].events.size(), 1U);
    EXPECT_EQ(record->writes[2].events[0].type, "zone_intrusion.enter");
    EXPECT_TRUE(record->writes[3].outward_crossed_tracks.empty());
    EXPECT_TRUE(record->writes[3].events.empty());
    EXPECT_EQ(record->writes[3].inside_zone_tracks, std::vector<domain::TrackId>{7});
    EXPECT_TRUE(record->writes[4].inside_zone_tracks.empty());
    EXPECT_TRUE(record->writes[4].scene->zones.empty());
    EXPECT_EQ(record->writes[4].scene->gates.size(), 1U);
}

}  // namespace vqec::test
