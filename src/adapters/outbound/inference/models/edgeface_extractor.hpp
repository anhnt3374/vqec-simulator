#pragma once
#include <array>
#include <memory>
#include <mutex>

#include "adapters/outbound/inference/engine/inference_engine.hpp"
#include "ports/outbound/feature_extractor.hpp"
namespace vqec::adapters::inference {
inline constexpr std::array<domain::Point2f, 5> kEdgeFaceLandmarks{{{38.2946F, 51.6963F},
                                                                    {73.5318F, 51.5014F},
                                                                    {56.0252F, 71.7366F},
                                                                    {41.5493F, 92.3655F},
                                                                    {70.7299F, 92.2041F}}};
Result<domain::Embedding> normalize_edgeface(std::vector<float> values);
Result<Tensor> prepare_edgeface_crop(const domain::Frame& frame,
                                     const ports::FeatureRegion& region);
// Thread-safe; serializes engine access. Input pixels are borrowed synchronously, never mutated.
class EdgeFaceExtractor final : public ports::IFeatureExtractor {
public:
    explicit EdgeFaceExtractor(std::unique_ptr<IInferenceEngine> engine);
    Result<domain::Embedding> extract(const domain::Frame&, const domain::BoxF&) override;
    Result<ports::EmbeddingBatch> extract_batch(const domain::Frame&,
                                                const std::vector<ports::FeatureRegion>&) override;

private:
    std::unique_ptr<IInferenceEngine> engine_;
    std::mutex mutex_;
};
}  // namespace vqec::adapters::inference
