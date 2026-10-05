#pragma once

#include <string>

#include "domain/geometry/box.hpp"

namespace vqec::domain {

// One object found by a detector in one frame.
struct Detection {
    BoxF box;            // Pixel coordinates of the source frame (letterbox already undone).
    int class_id = -1;   // Index into the model's label list.
    std::string label;   // Model label from the manifest, e.g. "person".
    float score = 0.0F;  // Confidence in [0, 1].
};

}  // namespace vqec::domain
