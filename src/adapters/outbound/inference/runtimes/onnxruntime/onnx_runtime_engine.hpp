#pragma once

#include <memory>

#include "adapters/outbound/inference/engine/inference_engine.hpp"

namespace vqec::adapters::inference {
// CPU backend. Single-worker ownership; neither load nor run is thread-safe.
class OnnxRuntimeEngine final : public IInferenceEngine {
public:
    OnnxRuntimeEngine();
    ~OnnxRuntimeEngine() override;
    Status load(const EngineOptions& options) override;
    const std::vector<TensorSpec>& input_specs() const override;
    const std::vector<TensorSpec>& output_specs() const override;
    Status run(const std::vector<Tensor>& inputs, std::vector<Tensor>& outputs) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace vqec::adapters::inference
