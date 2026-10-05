#pragma once

#include <string>
#include <vector>

#include "adapters/outbound/inference/engine/tensor.hpp"
#include "common/result.hpp"

namespace vqec::adapters::inference {

struct EngineOptions {
    std::string model_path;  // Artifact for this backend, resolved from models/manifest.yaml.
    std::string cache_dir;   // Backend caches (TensorRT engines, NPU compiled graphs).
    int device_id = 0;       // GPU/NPU index; ignored on CPU.
    int num_threads = 0;     // CPU intra-op threads; 0 lets the backend decide.
};

// Runtime backend: runs a loaded model on tensors. One implementation per backend under
// runtimes/<backend>/ (onnxruntime, tensorrt, openvino, npu). Model-specific pre/post-processing
// lives in models/ and only talks to this interface, so every model runs on every backend.
// Not thread-safe: a model codec serializes calls or owns one engine per worker.
class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;

    // Logs backend, device, model path and load time at info level.
    virtual Status load(const EngineOptions& options) = 0;

    virtual const std::vector<TensorSpec>& input_specs() const = 0;
    virtual const std::vector<TensorSpec>& output_specs() const = 0;

    // `outputs` is reused across calls to avoid per-frame allocations (hot path).
    virtual Status run(const std::vector<Tensor>& inputs, std::vector<Tensor>& outputs) = 0;
};

}  // namespace vqec::adapters::inference
