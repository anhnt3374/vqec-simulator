#pragma once

#include <string>
#include <vector>

namespace vqec::domain {

// Feature vector produced by a re-identification / face model. Values are L2-normalized.
// Embeddings from different `model_id`s are not comparable and never share a collection.
struct Embedding {
    std::string model_id;  // Manifest id including version, e.g. "arcface_r100_v1".
    std::vector<float> values;
};

}  // namespace vqec::domain
