#pragma once
#include <chrono>
#include <memory>

#include "ports/outbound/object_tracker.hpp"
namespace vqec::adapters::bytetrack {
struct ByteTrackerOptions {
    float low_threshold = 0.1F;
    float high_threshold = 0.35F;
    float new_threshold = 0.45F;
    float match_cost = 0.8F;
    float low_match_cost = 0.5F;
    float tentative_match_cost = 0.7F;
    std::chrono::milliseconds lost_timeout{1000};
    std::size_t max_tracks = 128;
};
// One instance per stream, called exclusively on the coordinator thread.
// ByteTrack XYAH Kalman prediction + global IoU assignment in two confidence passes.
// Timeouts/prediction use source PTS. reset preserves monotonically increasing IDs.
class ByteTracker final : public ports::IObjectTracker {
public:
    explicit ByteTracker(ByteTrackerOptions options = {});
    ~ByteTracker() override;
    std::vector<domain::Track> update(const domain::Frame& frame,
                                      const std::vector<domain::Detection>& detections) override;
    void reset() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace vqec::adapters::bytetrack
