#pragma once

#include <array>
#include <optional>
#include <vector>

#include "common/result.hpp"
#include "domain/geometry/box.hpp"
#include "domain/geometry/point.hpp"
#include "domain/vision/embedding.hpp"
#include "domain/vision/frame.hpp"

namespace vqec::ports {

// Computes an embedding (face, person re-id) for a region of a frame.
// Thread-safe, same contract as IObjectDetector.
struct FeatureRegion {
    domain::BoxF box;
    std::optional<std::array<domain::Point2f, 5>> landmarks = {};
};
using EmbeddingBatch = std::vector<Result<domain::Embedding>>;
class IFeatureExtractor {
public:
    // Synchronous, thread-safe, borrows immutable pixels until return. 1..4 source-pixel ROIs.
    // Results preserve ROI order, including per-ROI InvalidArgument errors (bad landmarks/crop).
    // Outer InvalidArgument denotes invalid batch/frame; Internal denotes inference failure.
    // No partial success is lost for an invalid crop. Technology types do not cross this port.
    virtual Result<EmbeddingBatch> extract_batch(const domain::Frame& frame,
                                                 const std::vector<FeatureRegion>& regions) = 0;
    virtual ~IFeatureExtractor() = default;

    // `region` is in pixel coordinates of `frame`.
    virtual Result<domain::Embedding> extract(const domain::Frame& frame,
                                              const domain::BoxF& region) = 0;
};

}  // namespace vqec::ports
