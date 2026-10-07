#include "bootstrap/face_gallery_io.hpp"

#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <yaml-cpp/yaml.h>
namespace vqec::bootstrap {
namespace {
constexpr const char* kModel = "edgeface_s_gamma_05_v1";
constexpr const char* kPreprocessing = "rgb_similarity_112_v1";
constexpr const char* kArtifactHash =
    "d01def8073f64e47c215545b5727d5807a9324ad18f9d83e269dbd9068f2140e";
constexpr const char* kSourceHash =
    "dc59abda2e8580399fd115a1eeb07e1f21156196db604b884407bcf0f17efb07";
constexpr std::size_t kMaxGalleryBytes = 16 * 1024 * 1024, kMaxSamples = 49152,
                      kMaxEmployeeSamples = 12;
Status validate_samples(const std::vector<domain::FaceGallerySample>& samples) {
    if (samples.empty() || samples.size() > kMaxSamples)
        return make_error(ErrorCode::InvalidArgument, "gallery: invalid sample count");
    std::map<std::string, std::size_t> counts;
    for (const auto& sample : samples) {
        if (sample.employee_id.empty() || sample.employee_id.size() > 64)
            return make_error(ErrorCode::InvalidArgument, "gallery: invalid employee_id");
        for (char c : sample.employee_id)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
                return make_error(ErrorCode::InvalidArgument, "gallery: invalid employee_id");
        if (++counts[sample.employee_id] > kMaxEmployeeSamples)
            return make_error(ErrorCode::InvalidArgument,
                              "gallery: more than 12 samples per employee");
        auto status = domain::validate_face_embedding(sample.embedding);
        if (!status.ok())
            return status;
    }
    return {};
}
}  // namespace
Result<std::vector<domain::FaceGallerySample>> parse_face_gallery(const std::string& text) {
    if (text.size() > kMaxGalleryBytes)
        return make_error(ErrorCode::InvalidArgument, "gallery: file too large");
    try {
        const auto root = YAML::Load(text);
        std::set<std::string> keys;
        for (const auto& field : root)
            if (!keys.insert(field.first.as<std::string>()).second)
                return make_error(ErrorCode::InvalidArgument, "gallery: duplicate metadata");
        if (!root.IsMap() || root["schema_version"].as<int>() != 1 ||
            root["model_id"].as<std::string>() != kModel ||
            root["preprocessing"].as<std::string>() != kPreprocessing ||
            root["artifact_sha256"].as<std::string>() != kArtifactHash ||
            root["checkpoint_sha256"].as<std::string>() != kSourceHash ||
            !root["samples"].IsSequence() || root["samples"].size() > kMaxSamples)
            return make_error(ErrorCode::InvalidArgument,
                              "gallery: incompatible schema, model or preprocessing");
        std::vector<domain::FaceGallerySample> samples;
        for (const auto& item : root["samples"]) {
            keys.clear();
            for (const auto& field : item)
                if (!keys.insert(field.first.as<std::string>()).second)
                    return make_error(ErrorCode::InvalidArgument, "gallery: duplicate sample key");
            samples.push_back({item["employee_id"].as<std::string>(),
                               {kModel, item["values"].as<std::vector<float>>()}});
        }
        const auto status = validate_samples(samples);
        if (!status.ok())
            return status.error();
        return samples;
    } catch (const std::exception&) {
        return make_error(ErrorCode::InvalidArgument, "gallery: malformed data");
    }
}
Result<std::string> encode_face_gallery(const std::vector<domain::FaceGallerySample>& samples) {
    const auto status = validate_samples(samples);
    if (!status.ok())
        return status.error();
    try {
        YAML::Emitter emitter;
        emitter.SetFloatPrecision(9);
        emitter << YAML::BeginMap << YAML::Key << "schema_version" << YAML::Value << 1 << YAML::Key
                << "model_id" << YAML::Value << kModel << YAML::Key << "preprocessing"
                << YAML::Value << kPreprocessing << YAML::Key << "artifact_sha256" << YAML::Value
                << kArtifactHash << YAML::Key << "checkpoint_sha256" << YAML::Value << kSourceHash
                << YAML::Key << "samples" << YAML::Value << YAML::BeginSeq;
        for (const auto& s : samples)
            emitter << YAML::BeginMap << YAML::Key << "employee_id" << YAML::Value << s.employee_id
                    << YAML::Key << "values" << YAML::Value << YAML::Flow << s.embedding.values
                    << YAML::EndMap;
        emitter << YAML::EndSeq << YAML::EndMap;
        return std::string(emitter.c_str());
    } catch (const std::exception&) {
        return make_error(ErrorCode::Internal, "gallery: serialization failed");
    }
}
Result<std::vector<domain::FaceGallerySample>> load_face_gallery(
    const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input)
        return make_error(ErrorCode::NotFound, "recognition.gallery: cannot open file");
    std::string text;
    char buffer[4096];
    while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
        text.append(buffer, static_cast<std::size_t>(input.gcount()));
        if (text.size() > kMaxGalleryBytes)
            return make_error(ErrorCode::InvalidArgument, "gallery: file too large");
    }
    return parse_face_gallery(text);
}
Status write_face_gallery(const std::filesystem::path& path,
                          const std::vector<domain::FaceGallerySample>& samples) {
    auto encoded = encode_face_gallery(samples);
    if (!encoded.ok())
        return encoded.status();
    try {
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.string() + ".tmp";
        std::ofstream output(temporary);
        output << encoded.value() << '\n';
        output.close();
        if (!output)
            return make_error(ErrorCode::Unavailable, "gallery: write failed");
        std::filesystem::rename(temporary, path);
        return {};
    } catch (const std::exception&) {
        return make_error(ErrorCode::Unavailable, "gallery: atomic save failed");
    }
}
}  // namespace vqec::bootstrap
