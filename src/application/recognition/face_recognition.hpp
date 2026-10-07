#pragma once
#include <chrono>
#include <unordered_map>
#include <unordered_set>

#include "domain/event/event.hpp"
#include "domain/vision/detection.hpp"
#include "domain/vision/face_gallery.hpp"
#include "domain/vision/track.hpp"
#include "ports/outbound/feature_extractor.hpp"
#include "ports/outbound/frame_sink.hpp"
namespace vqec::app {
struct FaceRecognitionOptions {
    float minimum_width = 20, minimum_height = 25, minimum_area = 500, match_threshold = 0.41F,
          minimum_gap = 0.08F;
    std::size_t max_batch = 4, max_tracks = 512;
    std::chrono::milliseconds refresh_interval{2000};
};
struct FaceRecognitionStats {
    std::uint64_t source_rejected = 0, unowned = 0, deferred = 0;
};
struct FaceRecognitionRequest {
    domain::TrackId track_id;
    ports::FeatureRegion region;
};
// Coordinator-thread only. Immutable startup gallery, bounded per-person state; no I/O.
class FaceRecognition final {
public:
    static constexpr const char* kRecognizedEvent = "face_recognition.recognized";
    static constexpr const char* kAttendanceEvent = "attendance.line_crossed";
    static constexpr const char* kEmployeeIdAttribute = "employee_id";
    // Call once per frame after the line monitor; snapshots cached identities and attributes
    // forward crossings.
    void annotate(const domain::Frame&, ports::FrameAnnotations&) const;
    FaceRecognition(FaceRecognitionOptions options, std::vector<domain::FaceGallerySample> gallery);
    std::vector<FaceRecognitionRequest> select(const domain::Frame&,
                                               const std::vector<domain::Track>&,
                                               const std::vector<domain::Detection>&);
    std::vector<domain::Event> apply(const domain::Frame&,
                                     const std::vector<FaceRecognitionRequest>&,
                                     const ports::EmbeddingBatch&);
    void reset();
    const FaceRecognitionStats& stats() const { return stats_; }

private:
    struct State {
        std::string confirmed, pending;
        unsigned hits = 0;
        std::chrono::nanoseconds last_embedding{};
        bool has_embedding = false;
    };
    FaceRecognitionStats stats_;
    FaceRecognitionOptions options_;
    std::vector<domain::FaceGallerySample> gallery_;
    std::unordered_map<domain::TrackId, State> states_;
    std::unordered_set<domain::TrackId> deferred_;
};
}  // namespace vqec::app
