#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "common/log.hpp"
#include "common/result.hpp"
#include "domain/ids.hpp"
#include "domain/scene/scene.hpp"

// Typed, validated view of config/simulator.yaml. Schema documented in
// config/simulator.example.yaml; keep both in sync.
namespace vqec::bootstrap {

struct LoggingConfig {
    static constexpr std::chrono::seconds kDefaultStatsInterval{10};

    log::LogConfig log;
    std::chrono::seconds stats_interval = kDefaultStatsInterval;
};

struct HttpConfig {
    static constexpr int kDefaultPort = 8080;

    std::string listen_address = "0.0.0.0";
    int port = kDefaultPort;
    std::string web_root;
};

struct InferenceConfig {
    std::string runtime;  // cpu | gpu | npu
    std::string backend;  // onnxruntime | tensorrt | openvino | <npu sdk>
    int device_id = 0;
    std::string models_dir;
};

struct StorageConfig {
    std::string sql_driver;  // sqlite | postgres
    std::string sql_uri;
    std::string vector_driver;  // qdrant | memory
    std::string vector_uri;
    std::string media_dir;  // Snapshots and recordings.
};

struct SolutionConfig {
    std::string type;
    YAML::Node params;  // Parsed by the solution factory for that type.
};

struct StreamConfig {
    domain::StreamId id;
    std::string source_uri;  // May contain credentials: log with redact_uri().
    std::vector<std::string> output_uris;
    std::string detector_model;  // Model id from models/manifest.yaml.
    domain::Scene scene;
    std::vector<SolutionConfig> solutions;
};

struct AppConfig {
    LoggingConfig logging;
    HttpConfig http;
    InferenceConfig inference;
    StorageConfig storage;
    std::vector<StreamConfig> streams;
};

// Reads, expands ${ENV_VAR} references and validates the file. The error message names the
// offending key, e.g. "streams[0].zones[1].polygon: ...".
Result<AppConfig> load_app_config(const std::filesystem::path& path);

}  // namespace vqec::bootstrap
