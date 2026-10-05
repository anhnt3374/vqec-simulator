#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "domain/ids.hpp"

namespace vqec::domain {

enum class PixelFormat {
    Bgr24,
    Rgb24,
    Nv12,
    Gray8,
};

// Decoded image in host memory. Immutable once published to the pipeline, which shares it as
// std::shared_ptr<const ImageBuffer>. Device-resident (GPU/NPU) images are introduced through an
// opaque handle in a port, never through vendor types here.
struct ImageBuffer {
    int width = 0;
    int height = 0;
    int stride_bytes = 0;
    PixelFormat format = PixelFormat::Bgr24;
    std::vector<std::uint8_t> data;
};

// One video frame plus the metadata every stage needs. Cheap to copy: pixels are shared.
struct Frame {
    StreamId stream_id;
    std::uint64_t sequence = 0;                         // Per stream, restarts at 0 on open().
    std::chrono::nanoseconds pts{0};                    // Presentation time, stream clock.
    std::chrono::system_clock::time_point captured_at;  // Wall clock when decoded.
    int width = 0;                                      // Pixels; always > 0 for real frames.
    int height = 0;
    std::shared_ptr<const ImageBuffer> image;  // May be null in tests and metadata-only runs.
};

}  // namespace vqec::domain
