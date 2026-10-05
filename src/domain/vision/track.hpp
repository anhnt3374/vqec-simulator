#pragma once

#include <string>

#include "domain/geometry/box.hpp"
#include "domain/ids.hpp"

namespace vqec::domain {

enum class TrackState {
    Tentative,  // Seen recently, not yet trusted. Solutions ignore it.
    Confirmed,  // Matched over enough frames. Solutions act on it.
    Lost,       // Temporarily not matched; the box is a prediction. Solutions keep its state.
};

// An object followed across frames. Box is in pixel coordinates of the current frame.
struct Track {
    TrackId id = 0;
    TrackState state = TrackState::Tentative;
    BoxF box;
    int class_id = -1;
    std::string label;
    float score = 0.0F;
};

}  // namespace vqec::domain
