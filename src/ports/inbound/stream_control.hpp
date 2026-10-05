#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "common/result.hpp"
#include "domain/ids.hpp"

namespace vqec::ports {

enum class StreamState {
    Stopped,
    Starting,
    Running,
    Reconnecting,
    Failed,
};

struct StreamStatus {
    domain::StreamId id;
    StreamState state = StreamState::Stopped;
    std::uint64_t frames_processed = 0;
    double fps = 0.0;
    std::optional<Error> last_error;
};

// Driving port for starting/stopping streams (used by the HTTP adapter). Thread-safe.
class IStreamControl {
public:
    virtual ~IStreamControl() = default;

    virtual std::vector<StreamStatus> list_streams() const = 0;
    virtual Status start_stream(const domain::StreamId& stream_id) = 0;
    virtual Status stop_stream(const domain::StreamId& stream_id) = 0;
};

}  // namespace vqec::ports
