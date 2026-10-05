#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vqec::adapters::inference {

enum class DataType {
    Float32,
    Float16,
    Int8,
    UInt8,
    Int32,
    Int64,
};

enum class TensorLayout {
    Nchw,
    Nhwc,
    Other,  // Non-image tensors (e.g. detector heads); interpret through the model manifest.
};

// Affine quantization: real_value = scale * (quantized_value - zero_point). NPUs need this.
struct QuantizationParams {
    float scale = 1.0F;
    std::int32_t zero_point = 0;
};

struct TensorSpec {
    std::string name;
    DataType data_type = DataType::Float32;
    TensorLayout layout = TensorLayout::Other;
    std::vector<std::int64_t> shape;                 // -1 marks a dynamic dimension.
    std::optional<QuantizationParams> quantization;  // Set for quantized tensors only.
};

// Tensor in host memory. Backends that keep data on the device do so internally.
struct Tensor {
    TensorSpec spec;
    std::vector<std::uint8_t> data;  // Packed, size == byte_size(spec).
};

constexpr std::size_t element_size(DataType data_type) {
    switch (data_type) {
        case DataType::Float32:
        case DataType::Int32:
            return 4;
        case DataType::Float16:
            return 2;
        case DataType::Int8:
        case DataType::UInt8:
            return 1;
        case DataType::Int64:
            return 8;
    }
    return 0;
}

// Number of elements; 0 when any dimension is dynamic (-1) or the shape is empty.
inline std::size_t element_count(const std::vector<std::int64_t>& shape) {
    if (shape.empty()) {
        return 0;
    }
    std::size_t count = 1;
    for (const std::int64_t dimension : shape) {
        if (dimension < 0) {
            return 0;
        }
        count *= static_cast<std::size_t>(dimension);
    }
    return count;
}

inline std::size_t byte_size(const TensorSpec& spec) {
    return element_count(spec.shape) * element_size(spec.data_type);
}

}  // namespace vqec::adapters::inference
