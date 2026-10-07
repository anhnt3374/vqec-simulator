#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "application/pipeline/live_ai_pipeline.hpp"
#include "common/log.hpp"
#include "fakes/fake_frame_source.hpp"
#include "fakes/fake_object_detector.hpp"
#include "fakes/recording_frame_sink.hpp"
namespace vqec::test {
namespace {
using Clock = std::chrono::steady_clock;
constexpr int kFrameCount = 75;
constexpr auto kSourcePeriod = std::chrono::milliseconds(40);
class TimedLiveSource final : public ports::IFrameSource {
public:
    Status open() override {
        start_ = Clock::now();
        return {};
    }
    ports::SourceInfo info() const override { return {"test://25fps", 100, 100, 25, true}; }
    Result<domain::Frame> read() override {
        if (index_ == kFrameCount)
            return make_error(ErrorCode::EndOfStream, "test complete");
        {
            std::unique_lock<std::mutex> lock(mutex_);
            timer_.wait_until(lock, start_ + kSourcePeriod * index_, [] { return false; });
        }
        domain::Frame frame;
        frame.pts = kSourcePeriod * index_++;
        return frame;
    }
    void close() override {}

private:
    Clock::time_point start_{};
    int index_ = 0;
    std::mutex mutex_;
    std::condition_variable timer_;
};
}  // namespace
TEST(BoardSimulation, ProcessesAboutEighteenFpsAndDropsExcessLiveFrames) {
    auto record = std::make_shared<RecordingFrameSink::Record>();
    app::LiveAiOptions options;
    options.simulation_ai_fps = 18;
    app::LiveAiPipeline pipeline("simulation_test", std::make_unique<TimedLiveSource>(),
                                 std::make_shared<FakeObjectDetector>(),
                                 std::make_shared<FakeObjectDetector>(),
                                 std::make_unique<RecordingFrameSink>(record), options);
    const std::atomic<bool> stop{false};
    const auto start = Clock::now();
    ASSERT_TRUE(pipeline.run(stop).ok());
    const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    const double fps = static_cast<double>(record->writes.size()) / elapsed;
    EXPECT_GT(fps, 16.5);
    EXPECT_LT(fps, 18.2);
    EXPECT_LT(record->writes.size(), static_cast<std::size_t>(kFrameCount));
    EXPECT_TRUE(record->closed);
    VQEC_LOG_INFO(
        "[simulation_measurement] source_fps=25 simulated_ai_fps=18 processed={} "
        "frames_not_output={} measured_fps={:.3f}",
        record->writes.size(), static_cast<std::size_t>(kFrameCount) - record->writes.size(), fps);
}
TEST(BoardSimulation, StopInterruptsArtificialDelayBeforeOutput) {
    auto record = std::make_shared<RecordingFrameSink::Record>();
    app::LiveAiOptions options;
    options.simulation_ai_fps = 1;
    app::LiveAiPipeline pipeline(
        "stop_test", std::make_unique<FakeFrameSource>(std::deque<domain::Frame>{domain::Frame{}}),
        std::make_shared<FakeObjectDetector>(), std::make_shared<FakeObjectDetector>(),
        std::make_unique<RecordingFrameSink>(record), options);
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::condition_variable timer;
    const auto start = Clock::now();
    std::thread stopper([&] {
        std::unique_lock<std::mutex> lock(mutex);
        timer.wait_until(lock, start + std::chrono::milliseconds(100), [] { return false; });
        stop.store(true);
    });
    const auto status = pipeline.run(stop);
    stopper.join();
    EXPECT_TRUE(status.ok());
    EXPECT_LT(Clock::now() - start, std::chrono::milliseconds(500));
    EXPECT_TRUE(record->writes.empty());
    EXPECT_TRUE(record->closed);
}
}  // namespace vqec::test
