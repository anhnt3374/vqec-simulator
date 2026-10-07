#include "domain/vision/face_gallery.hpp"

#include <cmath>
#include <map>
namespace vqec::domain {
namespace {
constexpr std::size_t kDimension = 512;
constexpr double kNormTolerance = 0.002;
}  // namespace
Status validate_face_embedding(const Embedding& embedding) {
    if (embedding.model_id != "edgeface_s_gamma_05_v1" || embedding.values.size() != kDimension)
        return make_error(ErrorCode::InvalidArgument,
                          "face gallery: incompatible model or dimension");
    double norm = 0;
    for (float value : embedding.values) {
        if (!std::isfinite(value))
            return make_error(ErrorCode::InvalidArgument, "face gallery: nonfinite value");
        norm += static_cast<double>(value) * static_cast<double>(value);
    }
    if (std::abs(norm - 1) > kNormTolerance)
        return make_error(ErrorCode::InvalidArgument, "face gallery: vector is not normalized");
    return {};
}
Result<FaceMatch> match_face(const std::vector<FaceGallerySample>& gallery,
                             const Embedding& query) {
    auto status = validate_face_embedding(query);
    if (!status.ok())
        return status.error();
    std::map<std::string, float> ranked;
    for (const auto& sample : gallery) {
        status = validate_face_embedding(sample.embedding);
        if (!status.ok())
            return status.error();
        float score = 0;
        for (std::size_t i = 0; i < kDimension; ++i)
            score += query.values[i] * sample.embedding.values[i];
        auto found = ranked.find(sample.employee_id);
        if (found == ranked.end() || score > found->second)
            ranked[sample.employee_id] = score;
    }
    FaceMatch match;
    float second = -2;
    for (const auto& entry : ranked) {
        if (match.employee_id.empty() || entry.second > match.score) {
            if (!match.employee_id.empty())
                second = match.score;
            match.employee_id = entry.first;
            match.score = entry.second;
        } else if (entry.second > second)
            second = entry.second;
    }
    if (second >= -1)
        match.second_score = second;
    return match;
}
}  // namespace vqec::domain
