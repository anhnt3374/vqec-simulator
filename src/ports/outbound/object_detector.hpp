#pragma once

#include <string>
#include <vector>

#include "common/result.hpp"
#include "domain/vision/detection.hpp"
#include "domain/vision/frame.hpp"

namespace vqec::ports {

// Runs a detection model on a frame. The runtime (CPU/GPU/NPU) is an adapter detail.
// Thread-safe: several stream pipelines may call detect() concurrently; implementations
// serialize or batch internally.
class IObjectDetector {
public:
    virtual ~IObjectDetector() = default;

    // Boxes are in pixel coordinates of `frame`, already mapped back from the model input.
    virtual Result<std::vector<domain::Detection>> detect(const domain::Frame& frame) = 0;

    // Labels indexed by Detection::class_id, from the model manifest.
    virtual const std::vector<std::string>& labels() const = 0;
};

}  // namespace vqec::ports
