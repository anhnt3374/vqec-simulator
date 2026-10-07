#pragma once
#include <optional>

#include "common/result.hpp"
#include "domain/vision/embedding.hpp"
namespace vqec::domain {
struct FaceGallerySample {
    std::string employee_id;
    Embedding embedding;
};
struct FaceMatch {
    std::string employee_id;
    float score = -1;
    std::optional<float> second_score;
};
Status validate_face_embedding(const Embedding& embedding);
Result<FaceMatch> match_face(const std::vector<FaceGallerySample>& gallery, const Embedding& query);
}  // namespace vqec::domain
