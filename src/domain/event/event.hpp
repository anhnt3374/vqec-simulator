#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "domain/ids.hpp"

namespace vqec::domain {

// A business fact produced by a solution, e.g. "object entered zone".
struct Event {
    std::string type;  // "<solution_type>.<event_name>", constants owned by the solution.
    StreamId stream_id;
    std::uint64_t frame_sequence = 0;
    std::chrono::system_clock::time_point occurred_at;  // Wall clock of the source frame.
    std::optional<TrackId> track_id;
    std::optional<ZoneId> zone_id;
    std::optional<GateId> gate_id;
    std::map<std::string, std::string> attributes;  // Solution-specific details, snake_case keys.
};

}  // namespace vqec::domain
