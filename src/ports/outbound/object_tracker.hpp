#pragma once

#include <vector>

#include "domain/vision/detection.hpp"
#include "domain/vision/frame.hpp"
#include "domain/vision/track.hpp"

namespace vqec::ports {

// Associates detections across frames. One instance per stream; not thread-safe.
class IObjectTracker {
public:
    virtual ~IObjectTracker() = default;

    // Returns all tracks known after this frame, including Tentative and Lost ones.
    virtual std::vector<domain::Track> update(const domain::Frame& frame,
                                              const std::vector<domain::Detection>& detections) = 0;

    // Forgets all tracks, e.g. after the source reconnects.
    virtual void reset() = 0;
};

}  // namespace vqec::ports
