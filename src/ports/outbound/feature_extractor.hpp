#pragma once

#include "common/result.hpp"
#include "domain/geometry/box.hpp"
#include "domain/vision/embedding.hpp"
#include "domain/vision/frame.hpp"

namespace vqec::ports {

// Computes an embedding (face, person re-id) for a region of a frame.
// Thread-safe, same contract as IObjectDetector.
class IFeatureExtractor {
public:
    virtual ~IFeatureExtractor() = default;

    // `region` is in pixel coordinates of `frame`.
    virtual Result<domain::Embedding> extract(const domain::Frame& frame,
                                              const domain::BoxF& region) = 0;
};

}  // namespace vqec::ports
