#include "bootstrap/live_ai_config.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <openssl/evp.h>
#include <yaml-cpp/yaml.h>

namespace vqec::bootstrap {
namespace {
constexpr int kMaxDimension = 8192;
constexpr int kMaxThreads = 64;
constexpr int kMaxQueueCapacity = 128;
constexpr double kMinProcessingFps = 0.001;
constexpr double kMaxProcessingFps = 1000.0;
constexpr double kMinSimulationFps = 1.0;
constexpr double kMaxSimulationFps = 1000.0;

void require(bool condition, const std::string& key) {
    if (!condition)
        throw std::runtime_error(key + ": invalid or missing value");
}

std::string expand_environment(const std::string& value, const std::string& key) {
    std::string result;
    std::size_t begin = 0;
    while (true) {
        const auto marker = value.find("${", begin);
        if (marker == std::string::npos)
            return result + value.substr(begin);
        result += value.substr(begin, marker - begin);
        const auto end = value.find('}', marker);
        require(end != std::string::npos, key);
        const auto name = value.substr(marker + 2, end - marker - 2);
        const char* expanded = std::getenv(name.c_str());
        require(expanded != nullptr, key + ": missing environment variable " + name);
        result += expanded;
        begin = end + 1;
    }
}

void validate_mapping(const YAML::Node& node, const std::string& path) {
    if (!node.IsMap())
        return;
    std::unordered_set<std::string> keys;
    for (const auto& entry : node) {
        const auto key = entry.first.as<std::string>();
        require(keys.insert(key).second, path + key + ": duplicate key");
        validate_mapping(entry.second, path + key + '.');
    }
}

LiveDetectorConfig detector_config(const YAML::Node& node, const std::string& key) {
    LiveDetectorConfig options;
    if (node) {
        require(node.IsMap(), key);
        if (node["confidence"])
            options.confidence = node["confidence"].as<float>();
        if (node["nms_iou"])
            options.nms_iou = node["nms_iou"].as<float>();
        if (node["num_threads"])
            options.num_threads = node["num_threads"].as<int>();
    }
    require(std::isfinite(options.confidence) && options.confidence > 0.0F &&
                options.confidence <= 1.0F,
            key + ".confidence");
    require(std::isfinite(options.nms_iou) && options.nms_iou > 0.0F && options.nms_iou <= 1.0F,
            key + ".nms_iou");
    require(options.num_threads > 0 && options.num_threads <= kMaxThreads, key + ".num_threads");
    return options;
}

Status verify_artifact(const std::filesystem::path& path, const std::string& expected) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return make_error(ErrorCode::NotFound,
                          "model missing: " + path.filename().string() +
                              "; run scripts/prepare_live_ai_models.sh inside Docker");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
        return make_error(ErrorCode::Internal, "model SHA256 initialization failed");
    std::array<char, 65536> buffer{};
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (EVP_DigestUpdate(ctx.get(), buffer.data(), static_cast<std::size_t>(file.gcount())) !=
            1)
            return make_error(ErrorCode::Internal, "model SHA256 update failed");
    }
    if (!file.eof())
        return make_error(ErrorCode::Unavailable, "model artifact read failed");
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest.data(), &length) != 1)
        return make_error(ErrorCode::Internal, "model SHA256 finalization failed");
    std::ostringstream hex;
    for (unsigned int i = 0; i < length; ++i)
        hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    if (hex.str() != expected)
        return make_error(ErrorCode::InvalidArgument,
                          "model SHA256 mismatch: " + path.filename().string());
    return {};
}
}  // namespace

Result<LiveAiConfig> parse_live_ai_config(const std::string& yaml) {
    std::string key = "config";
    try {
        const auto root = YAML::Load(yaml);
        require(root.IsMap(), key);
        validate_mapping(root, "");
        LiveAiConfig config;
        for (const auto* field : {"input_uri", "output_uri", "models_dir"}) {
            key = field;
            require(root[field] && root[field].IsScalar(), key);
            const auto value = expand_environment(root[field].as<std::string>(), key);
            require(!value.empty(), key);
            if (key == "input_uri")
                config.input_uri = value;
            else if (key == "output_uri")
                config.output_uri = value;
            else
                config.models_dir = value;
        }
        key = "input_uri";
        require(
            config.input_uri.rfind("rtsp://", 0) == 0 || config.input_uri.rfind("rtsps://", 0) == 0,
            key);
        key = "output_uri";
        require(config.output_uri.rfind("rtsp://", 0) == 0, key);
        key = "processing_fps";
        if (root[key])
            config.processing_fps = root[key].as<double>();
        require(std::isfinite(config.processing_fps) &&
                    (config.processing_fps == 0.0 || (config.processing_fps >= kMinProcessingFps &&
                                                      config.processing_fps <= kMaxProcessingFps)),
                key);
        key = "simulation";
        if (root[key]) {
            require(root[key].IsMap(), key);
            const auto simulation = root[key];
            key = "simulation.ai_fps";
            if (simulation["ai_fps"])
                config.simulation_ai_fps = simulation["ai_fps"].as<double>();
            require(std::isfinite(config.simulation_ai_fps) &&
                        (config.simulation_ai_fps == 0 ||
                         (config.simulation_ai_fps >= kMinSimulationFps &&
                          config.simulation_ai_fps <= kMaxSimulationFps)),
                    key);
        }
        key = "output";
        if (root[key]) {
            const auto output = root[key];
            require(output.IsMap(), key);
            if (output["width"])
                config.output_width = output["width"].as<int>();
            if (output["height"])
                config.output_height = output["height"].as<int>();
            if (output["fps"])
                config.output_fps = output["fps"].as<int>();
        }
        require(config.output_width > 0 && config.output_width <= kMaxDimension &&
                    config.output_width % 2 == 0,
                "output.width");
        require(config.output_height > 0 && config.output_height <= kMaxDimension &&
                    config.output_height % 2 == 0,
                "output.height");
        require(config.output_fps > 0 && config.output_fps <= 240, "output.fps");
        key = "queue_capacity";
        if (root[key]) {
            const auto capacity = root[key].as<int>();
            require(capacity > 0 && capacity <= kMaxQueueCapacity, key);
            config.queue_capacity = static_cast<std::size_t>(capacity);
        }
        key = "io_timeout_ms";
        if (root[key])
            config.io_timeout = std::chrono::milliseconds(root[key].as<int>());
        require(config.io_timeout >= std::chrono::milliseconds(100) &&
                    config.io_timeout <= std::chrono::minutes(1),
                key);
        key = "stats_interval_seconds";
        if (root[key])
            config.stats_interval = std::chrono::seconds(root[key].as<int>());
        require(config.stats_interval.count() > 0, key);
        key = "zone_dwell_seconds";
        if (root[key])
            config.zone_dwell_time = std::chrono::seconds(root[key].as<int>());
        require(config.zone_dwell_time >= std::chrono::seconds(1) &&
                    config.zone_dwell_time <= std::chrono::hours(1),
                key);
        key = "person";
        config.person = detector_config(root[key], key);
        key = "face";
        config.face = detector_config(root[key], key);
        key = "tracking";
        if (root[key]) {
            const auto tracking = root[key];
            require(tracking.IsMap(), key);
            auto& options = config.tracking;
            for (const auto& field : std::initializer_list<std::pair<const char*, float*>>{
                     {"low_threshold", &options.low_threshold},
                     {"high_threshold", &options.high_threshold},
                     {"new_threshold", &options.new_threshold},
                     {"match_cost", &options.match_cost},
                     {"low_match_cost", &options.low_match_cost},
                     {"tentative_match_cost", &options.tentative_match_cost}}) {
                key = std::string("tracking.") + field.first;
                if (tracking[field.first])
                    *field.second = tracking[field.first].as<float>();
                require(std::isfinite(*field.second) && *field.second > 0 && *field.second <= 1,
                        key);
            }
            require(options.low_threshold < options.high_threshold, "tracking.low_threshold");
            require(options.high_threshold <= options.new_threshold, "tracking.new_threshold");
            key = "tracking.lost_timeout_ms";
            if (tracking["lost_timeout_ms"])
                options.lost_timeout =
                    std::chrono::milliseconds(tracking["lost_timeout_ms"].as<int>());
            require(
                options.lost_timeout.count() > 0 && options.lost_timeout <= std::chrono::minutes(1),
                key);
            key = "tracking.max_tracks";
            if (tracking["max_tracks"]) {
                const int capacity = tracking["max_tracks"].as<int>();
                require(capacity > 0 && capacity <= 512, key);
                options.max_tracks = static_cast<std::size_t>(capacity);
            }
        }
        key = "recognition";
        if (root[key]) {
            const auto node = root[key];
            require(node.IsMap(), key);
            if (node["enabled"])
                config.recognition_enabled = node["enabled"].as<bool>();
            if (node["gallery"])
                config.gallery_path =
                    expand_environment(node["gallery"].as<std::string>(), "recognition.gallery");
            if (node["model"])
                config.recognition_model = node["model"].as<std::string>();
            auto& policy = config.recognition;
            for (const auto& field : std::initializer_list<std::pair<const char*, float*>>{
                     {"minimum_width", &policy.minimum_width},
                     {"minimum_height", &policy.minimum_height},
                     {"minimum_area", &policy.minimum_area},
                     {"match_threshold", &policy.match_threshold},
                     {"minimum_gap", &policy.minimum_gap}}) {
                key = std::string("recognition.") + field.first;
                if (node[field.first])
                    *field.second = node[field.first].as<float>();
                require(std::isfinite(*field.second) && *field.second >= 0, key);
            }
            require(policy.minimum_width > 0 && policy.minimum_width <= 8192,
                    "recognition.minimum_width");
            require(policy.minimum_height > 0 && policy.minimum_height <= 8192,
                    "recognition.minimum_height");
            require(policy.minimum_area > 0 && policy.minimum_area <= 8192.0F * 8192.0F,
                    "recognition.minimum_area");
            require(policy.match_threshold <= 1, "recognition.match_threshold");
            require(policy.minimum_gap <= 1, "recognition.minimum_gap");
            key = "recognition.max_batch";
            if (node["max_batch"]) {
                int v = node["max_batch"].as<int>();
                require(v >= 1 && v <= 4, key);
                policy.max_batch = static_cast<std::size_t>(v);
            }
            key = "recognition.max_tracks";
            if (node["max_tracks"]) {
                int v = node["max_tracks"].as<int>();
                require(v >= 1 && v <= 4096, key);
                policy.max_tracks = static_cast<std::size_t>(v);
            }
            key = "recognition.refresh_interval_ms";
            if (node["refresh_interval_ms"])
                policy.refresh_interval =
                    std::chrono::milliseconds(node["refresh_interval_ms"].as<int>());
            require(policy.refresh_interval >= std::chrono::milliseconds(100) &&
                        policy.refresh_interval <= std::chrono::seconds(60),
                    key);
            if (config.recognition_enabled) {
                require(!config.gallery_path.empty(), "recognition.gallery");
                require(config.recognition_model == "edgeface_s_gamma_05_batch4.onnx",
                        "recognition.model");
            }
        }
        key = "logging";
        if (root[key]) {
            if (root[key]["level"])
                config.logging.level = root[key]["level"].as<std::string>();
            if (root[key]["directory"])
                config.logging.directory = expand_environment(
                    root[key]["directory"].as<std::string>(), key + ".directory");
        }
        return config;
    } catch (const YAML::Exception&) {
        return make_error(ErrorCode::InvalidArgument, key + ": invalid YAML value");
    } catch (const std::exception& error) {
        return make_error(ErrorCode::InvalidArgument, error.what());
    }
}

Result<LiveAiConfig> load_live_ai_config(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file)
        return make_error(ErrorCode::NotFound, "config: cannot open file");
    std::ostringstream contents;
    contents << file.rdbuf();
    return parse_live_ai_config(contents.str());
}

Status verify_edgeface_model(const std::filesystem::path& path) {
    return verify_artifact(path,
                           "d01def8073f64e47c215545b5727d5807a9324ad18f9d83e269dbd9068f2140e");
}
Status verify_live_ai_models(const std::filesystem::path& models_dir) {
    const auto person =
        verify_artifact(models_dir / "nanodet_plus_m_320.onnx",
                        "4f12723cce3d48e47ca92cb925ba74d97a965c069208edca660bbb9f7ce2c610");
    if (!person.ok())
        return person;
    return verify_artifact(models_dir / "yunet_2023mar.onnx",
                           "8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4");
}
}  // namespace vqec::bootstrap
