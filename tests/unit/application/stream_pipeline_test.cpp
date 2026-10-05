#include "application/pipeline/stream_pipeline.hpp"

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "application/solutions/zone_intrusion/zone_intrusion_solution.hpp"
#include "fakes/fake_frame_source.hpp"
#include "fakes/fake_object_detector.hpp"
#include "fakes/fake_object_tracker.hpp"
#include "fakes/in_memory_event_repository.hpp"
#include "fakes/recording_event_publisher.hpp"
#include "fakes/recording_frame_sink.hpp"
#include "support/builders.hpp"

namespace vqec::app {
namespace {

using test::make_frame;
using test::make_track;

// Wires a pipeline from fakes: `tracks_per_frame[i]` is what the tracker reports for frame i.
class StreamPipelineTest : public ::testing::Test {
protected:
    std::unique_ptr<StreamPipeline> make_pipeline(
        std::deque<std::vector<domain::Track>> tracks_per_frame, domain::Scene scene) {
        std::deque<domain::Frame> frames;
        for (std::uint64_t i = 0; i < tracks_per_frame.size(); ++i) {
            frames.push_back(make_frame(i));
        }
        StreamComponents components;
        components.source = std::make_unique<test::FakeFrameSource>(std::move(frames));
        components.tracker = std::make_unique<test::FakeObjectTracker>(std::move(tracks_per_frame));
        components.sinks.push_back(std::make_unique<test::RecordingFrameSink>(sink_record_));
        components.solutions.push_back(
            std::make_unique<ZoneIntrusionSolution>(ZoneIntrusionParams{}));
        return std::make_unique<StreamPipeline>(test::kStreamId, std::move(components),
                                                SharedServices{detector_, repository_, publisher_},
                                                std::move(scene));
    }

    std::shared_ptr<test::FakeObjectDetector> detector_ =
        std::make_shared<test::FakeObjectDetector>();
    std::shared_ptr<test::InMemoryEventRepository> repository_ =
        std::make_shared<test::InMemoryEventRepository>();
    std::shared_ptr<test::RecordingEventPublisher> publisher_ =
        std::make_shared<test::RecordingEventPublisher>();
    std::shared_ptr<test::RecordingFrameSink::Record> sink_record_ =
        std::make_shared<test::RecordingFrameSink::Record>();
};

TEST_F(StreamPipelineTest, RunsUntilEndOfStreamAndRoutesEventsToEveryOutput) {
    auto pipeline = make_pipeline({{make_track(1, 0.2F, 0.2F)}, {make_track(1, 0.7F, 0.7F)}},
                                  domain::Scene{{test::make_bottom_right_zone()}, {}});
    const std::atomic<bool> stop_requested{false};

    const Status status = pipeline->run(stop_requested);

    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.error().code, ErrorCode::EndOfStream);
    EXPECT_EQ(pipeline->frames_processed(), 2U);
    EXPECT_EQ(detector_->calls(), 2);
    EXPECT_EQ(repository_->events().size(), 1U);
    EXPECT_EQ(publisher_->published().size(), 1U);
    ASSERT_EQ(sink_record_->writes.size(), 2U);
    EXPECT_EQ(sink_record_->writes[1].events.size(), 1U);
    EXPECT_TRUE(sink_record_->opened);
    EXPECT_TRUE(sink_record_->closed);
}

TEST_F(StreamPipelineTest, AppliesSceneUpdateBeforeNextFrame) {
    auto pipeline =
        make_pipeline({{make_track(1, 0.7F, 0.7F)}, {make_track(1, 0.7F, 0.7F)}}, domain::Scene{});
    ASSERT_TRUE(pipeline->open().ok());

    ASSERT_TRUE(pipeline->process_next_frame().ok());
    EXPECT_TRUE(repository_->events().empty());

    pipeline->request_scene_update(domain::Scene{{test::make_bottom_right_zone()}, {}});
    ASSERT_TRUE(pipeline->process_next_frame().ok());
    pipeline->close();

    ASSERT_EQ(repository_->events().size(), 1U);
    EXPECT_EQ(sink_record_->writes[1].scene->zones.size(), 1U);
}

}  // namespace
}  // namespace vqec::app
