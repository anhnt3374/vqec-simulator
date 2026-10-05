#pragma once

#include <string>

#include "common/result.hpp"
#include "domain/vision/frame.hpp"

namespace vqec::ports {

struct SourceInfo {
    std::string uri;  // May contain credentials: log only through redact_uri().
    int width = 0;
    int height = 0;
    double fps = 0.0;
    bool is_live = false;  // RTSP/camera (drop frames when slow) vs file (process every frame).
};

// Video input: RTSP stream, MP4 file, camera. One instance per stream, used by one thread.
class IFrameSource {
public:
    virtual ~IFrameSource() = default;

    virtual Status open() = 0;

    // Valid after open() succeeded.
    virtual SourceInfo info() const = 0;

    // Blocks until the next decoded frame. Errors:
    //   EndOfStream  a finite source (file) is exhausted;
    //   Unavailable  a live source dropped; the caller decides whether to reopen.
    virtual Result<domain::Frame> read() = 0;

    // Idempotent; safe to call when open() failed.
    virtual void close() = 0;
};

}  // namespace vqec::ports
