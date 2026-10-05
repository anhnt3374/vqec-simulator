#pragma once

#include <cstdint>
#include <string>

namespace vqec::domain {

// Identifiers chosen by users in config/WebUI: lowercase snake_case, e.g. "cam_entrance".
using StreamId = std::string;
using ZoneId = std::string;
using GateId = std::string;

// Assigned by the tracker; unique within one stream for the lifetime of a pipeline.
using TrackId = std::uint64_t;

}  // namespace vqec::domain
