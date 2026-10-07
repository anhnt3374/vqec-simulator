#pragma once

#include <memory>
#include <mutex>

#include "adapters/outbound/inference/engine/inference_engine.hpp"
#include "adapters/outbound/inference/models/detection_utils.hpp"
#include "ports/outbound/object_detector.hpp"

namespace vqec::adapters::inference {
Result<std::vector<domain::Detection>> decode_nanodet(const Tensor& output, int source_width,
                                                      int source_height, DetectorOptions options);

// Thread-safe; owns its engine, serializes calls. Pretrained COCO person class only.
class NanoDetDetector final : public ports::IObjectDetector {
public:
    explicit NanoDetDetector(std::unique_ptr<IInferenceEngine> engine,
                             DetectorOptions options = {});
    Result<std::vector<domain::Detection>> detect(const domain::Frame& frame) override;
    const std::vector<std::string>& labels() const override { return labels_; }

private:
    std::unique_ptr<IInferenceEngine> engine_;
    DetectorOptions options_;
    std::mutex mutex_;
    std::vector<Tensor> inputs_{1};
    std::vector<Tensor> outputs_;
    const std::vector<std::string> labels_{"person"};
};
}  // namespace vqec::adapters::inference
