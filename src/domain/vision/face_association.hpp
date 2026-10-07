#pragma once
#include <optional>
#include <vector>

#include "domain/vision/detection.hpp"
#include "domain/vision/track.hpp"
namespace vqec::domain {
std::vector<std::optional<TrackId>> associate_faces(const std::vector<Track>& tracks,
                                                    const std::vector<Detection>& faces);
}
