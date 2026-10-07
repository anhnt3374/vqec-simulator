#pragma once

#include <memory>
#include <mutex>

#include "adapters/outbound/inference/engine/inference_engine.hpp"
#include "adapters/outbound/inference/models/detection_utils.hpp"
#include "ports/outbound/object_detector.hpp"

namespace vqec::adapters::inference {
Result<std::vector<domain::Detection>> decode_yunet(const std::vector<Tensor>& outputs,
                                                    int source_width, int source_height,
                                                    DetectorOptions options);

// Thread-safe; owns a serialized engine for the pinned fixed-640 YuNet artifact.
class YuNetDetector final : public ports::IObjectDetector {
public:
    explicit YuNetDetector(std::unique_ptr<IInferenceEngine> engine, DetectorOptions options = {});
    Result<std::vector<domain::Detection>> detect(const domain::Frame& frame) override;
    const std::vector<std::string>& labels() const override { return labels_; }

private:
    std::unique_ptr<IInferenceEngine> engine_;
    DetectorOptions options_;
    std::mutex mutex_;
    std::vector<Tensor> inputs_{1};
    std::vector<Tensor> outputs_;
    const std::vector<std::string> labels_{"face"};
};
}  // namespace vqec::adapters::inference
