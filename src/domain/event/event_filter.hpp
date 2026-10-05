#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

#include "domain/ids.hpp"

namespace vqec::domain {

// Criteria for querying stored events. Unset fields do not filter.
struct EventFilter {
    static constexpr std::size_t kDefaultLimit = 100;

    std::optional<StreamId> stream_id;
    std::optional<std::string> type;
    std::optional<std::chrono::system_clock::time_point> from;  // Inclusive.
    std::optional<std::chrono::system_clock::time_point> to;    // Exclusive.
    std::size_t limit = kDefaultLimit;
};

}  // namespace vqec::domain
