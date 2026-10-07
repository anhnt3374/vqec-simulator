#pragma once

#include <memory>
#include <vector>

#include "common/result.hpp"
#include "domain/event/event.hpp"
#include "domain/scene/scene.hpp"
#include "domain/vision/frame.hpp"
#include "domain/vision/track.hpp"
#include "ports/outbound/frame_source.hpp"

namespace vqec::ports {

// What the pipeline knows about a frame, for overlays and recording metadata.
struct FrameAnnotations {
    std::vector<domain::Track> tracks;
    std::shared_ptr<const domain::Scene> scene;  // Never null when produced by the pipeline.
    std::vector<domain::Event> events;           // Events raised on this frame.
    // Persistent outward-crossing highlights for visible tracks in this frame; empty by default.
    // Owned snapshot, safe for a sink to copy into its bounded publication queue.
    std::vector<domain::TrackId> outward_crossed_tracks = {};
    // Visible confirmed people inside for the configured dwell time. Red takes precedence.
    // Owned snapshot, safe for queued sinks.
    std::vector<domain::TrackId> inside_zone_tracks = {};
};

// Video output: annotated RTSP push, MP4 recording, preview. One instance per stream output,
// used by the stream's pipeline thread only.
class IFrameSink {
public:
    virtual ~IFrameSink() = default;

    virtual Status open(const SourceInfo& source_info) = 0;

    // Must not block longer than one frame interval: implementations handle transient network
    // problems internally (reconnect, drop frames, log). An error means the sink is unusable.
    virtual Status write(const domain::Frame& frame, const FrameAnnotations& annotations) = 0;

    // Flushes and releases resources. Idempotent; safe to call when open() failed.
    virtual void close() = 0;
};

}  // namespace vqec::ports
