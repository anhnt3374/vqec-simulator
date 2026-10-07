#include "adapters/outbound/inference/runtimes/onnxruntime/onnx_runtime_engine.hpp"

#include <chrono>
#include <cstring>

#include <onnxruntime_cxx_api.h>

#include "common/log.hpp"

namespace vqec::adapters::inference {
struct OnnxRuntimeEngine::Impl {
    Ort::Env environment{ORT_LOGGING_LEVEL_ERROR, "vqec"};
    std::unique_ptr<Ort::Session> session;
    std::vector<TensorSpec> inputs;
    std::vector<TensorSpec> outputs;
};

OnnxRuntimeEngine::OnnxRuntimeEngine() : impl_(std::make_unique<Impl>()) {
    impl_->environment.DisableTelemetryEvents();
}
OnnxRuntimeEngine::~OnnxRuntimeEngine() = default;
const std::vector<TensorSpec>& OnnxRuntimeEngine::input_specs() const {
    return impl_->inputs;
}
const std::vector<TensorSpec>& OnnxRuntimeEngine::output_specs() const {
    return impl_->outputs;
}

Status OnnxRuntimeEngine::load(const EngineOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    try {
        Ort::SessionOptions settings;
        settings.SetIntraOpNumThreads(options.num_threads);
        settings.SetInterOpNumThreads(1);
        settings.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        auto session = std::make_unique<Ort::Session>(impl_->environment,
                                                      options.model_path.c_str(), settings);
        Ort::AllocatorWithDefaultOptions allocator;
        std::vector<TensorSpec> inputs, outputs;
        for (std::size_t i = 0; i < session->GetInputCount(); ++i) {
            const auto info = session->GetInputTypeInfo(i);
            const auto tensor = info.GetTensorTypeAndShapeInfo();
            if (tensor.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
                return make_error(ErrorCode::Unsupported,
                                  "onnxruntime: live models require float32 inputs");
            inputs.push_back({session->GetInputNameAllocated(i, allocator).get(),
                              DataType::Float32,
                              TensorLayout::Nchw,
                              tensor.GetShape(),
                              {}});
        }
        for (std::size_t i = 0; i < session->GetOutputCount(); ++i) {
            const auto info = session->GetOutputTypeInfo(i);
            const auto tensor = info.GetTensorTypeAndShapeInfo();
            if (tensor.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
                return make_error(ErrorCode::Unsupported,
                                  "onnxruntime: live models require float32 outputs");
            outputs.push_back({session->GetOutputNameAllocated(i, allocator).get(),
                               DataType::Float32,
                               TensorLayout::Other,
                               tensor.GetShape(),
                               {}});
        }
        impl_->session = std::move(session);
        impl_->inputs = std::move(inputs);
        impl_->outputs = std::move(outputs);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        VQEC_LOG_INFO(
            "[engine=onnxruntime] loaded model={} backend=onnxruntime device=cpu threads={} "
            "load_ms={}",
            options.model_path, options.num_threads, elapsed.count());
        return {};
    } catch (const Ort::Exception&) {
        return make_error(ErrorCode::Unavailable, "onnxruntime: cannot load model artifact");
    } catch (const std::exception&) {
        return make_error(ErrorCode::Internal, "onnxruntime: model loading failed");
    }
}

Status OnnxRuntimeEngine::run(const std::vector<Tensor>& inputs, std::vector<Tensor>& outputs) {
    if (!impl_->session || inputs.size() != impl_->inputs.size())
        return make_error(ErrorCode::InvalidArgument,
                          "onnxruntime: unloaded engine or wrong input count");
    try {
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Ort::Value> values;
        std::vector<const char*> input_names, output_names;
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const auto& input = inputs[i];
            if (input.spec.data_type != DataType::Float32 ||
                input.spec.shape != impl_->inputs[i].shape ||
                input.data.size() != byte_size(input.spec))
                return make_error(ErrorCode::InvalidArgument,
                                  "onnxruntime: wrong input shape or byte count");
            input_names.push_back(impl_->inputs[i].name.c_str());
            // ORT reads input buffers synchronously and does not mutate them.
            values.push_back(Ort::Value::CreateTensor(
                memory, const_cast<std::uint8_t*>(input.data.data()), input.data.size(),
                input.spec.shape.data(), input.spec.shape.size(),
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT));
        }
        for (const auto& spec : impl_->outputs)
            output_names.push_back(spec.name.c_str());
        auto results =
            impl_->session->Run(Ort::RunOptions{nullptr}, input_names.data(), values.data(),
                                values.size(), output_names.data(), output_names.size());
        outputs.resize(results.size());
        for (std::size_t i = 0; i < results.size(); ++i) {
            auto& output = outputs[i];
            output.spec = impl_->outputs[i];
            output.spec.shape = results[i].GetTensorTypeAndShapeInfo().GetShape();
            output.data.resize(byte_size(output.spec));
            std::memcpy(output.data.data(), results[i].GetTensorRawData(), output.data.size());
        }
        return {};
    } catch (const Ort::Exception&) {
        return make_error(ErrorCode::Internal, "onnxruntime: inference failed");
    } catch (const std::exception&) {
        return make_error(ErrorCode::Internal, "onnxruntime: tensor conversion failed");
    }
}
}  // namespace vqec::adapters::inference
