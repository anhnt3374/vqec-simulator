#include "bootstrap/app_config.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace vqec::bootstrap {
namespace {

constexpr std::array<std::string_view, 3> kRuntimes = {"cpu", "gpu", "npu"};
constexpr std::array<std::string_view, 3> kBackends = {"onnxruntime", "tensorrt", "openvino"};
constexpr std::array<std::string_view, 2> kSqlDrivers = {"sqlite", "postgres"};
constexpr std::array<std::string_view, 2> kVectorDrivers = {"qdrant", "memory"};
constexpr int kMinPort = 1;
constexpr int kMaxPort = 65535;
constexpr std::size_t kBytesPerMegabyte = std::size_t{1024} * 1024;
constexpr std::string_view kEnvOpen = "${";
constexpr char kEnvClose = '}';

// Thrown and caught inside this file only; load_app_config() converts it to an Error.
class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::string child_path(const std::string& path, const std::string& key) {
    return path.empty() ? key : path + "." + key;
}

std::string index_path(const std::string& path, std::size_t index) {
    return path + "[" + std::to_string(index) + "]";
}

// Replaces ${NAME} with the environment variable NAME so secrets stay out of config files.
std::string expand_env(const std::string& text, const std::string& path) {
    std::string expanded;
    std::size_t position = 0;
    while (true) {
        const std::size_t open = text.find(kEnvOpen, position);
        if (open == std::string::npos) {
            expanded.append(text, position);
            return expanded;
        }
        const std::size_t name_start = open + kEnvOpen.size();
        const std::size_t close = text.find(kEnvClose, name_start);
        if (close == std::string::npos) {
            throw ConfigError(path + ": unterminated ${");
        }
        const auto name = text.substr(name_start, close - name_start);
        // Single-threaded: config is loaded before any thread starts.
        const char* value = std::getenv(name.c_str());  // NOLINT(concurrency-mt-unsafe)
        if (value == nullptr) {
            throw ConfigError(path + ": environment variable " + name + " is not set");
        }
        expanded.append(text, position, open - position);
        expanded.append(value);
        position = close + 1;
    }
}

YAML::Node require(const YAML::Node& parent, const std::string& key, const std::string& path) {
    YAML::Node child = parent[key];
    if (!child) {
        throw ConfigError(child_path(path, key) + " is required");
    }
    return child;
}

template <typename T>
T read(const YAML::Node& parent, const std::string& key, const std::string& path) {
    const YAML::Node node = require(parent, key, path);
    try {
        return node.as<T>();
    } catch (const YAML::Exception&) {
        throw ConfigError(child_path(path, key) + " has an invalid value");
    }
}

template <typename T>
T read_or(const YAML::Node& parent, const std::string& key, const std::string& path, T fallback) {
    return parent[key] ? read<T>(parent, key, path) : fallback;
}

std::string read_string(const YAML::Node& parent, const std::string& key, const std::string& path) {
    return expand_env(read<std::string>(parent, key, path), child_path(path, key));
}

std::string read_string_or(const YAML::Node& parent, const std::string& key,
                           const std::string& path, const std::string& fallback) {
    return parent[key] ? read_string(parent, key, path) : fallback;
}

template <std::size_t N>
void require_one_of(const std::string& value, const std::array<std::string_view, N>& allowed,
                    const std::string& path) {
    if (std::find(allowed.begin(), allowed.end(), value) == allowed.end()) {
        std::string list;
        for (const std::string_view item : allowed) {
            list += (list.empty() ? "" : ", ") + std::string(item);
        }
        throw ConfigError(path + ": '" + value + "' is not one of: " + list);
    }
}

void require_identifier(const std::string& id, const std::string& path) {
    const bool valid = !id.empty() && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
    if (!valid) {
        throw ConfigError(path + ": '" + id + "' must be lowercase snake_case [a-z0-9_]");
    }
}

domain::Point2f parse_point(const YAML::Node& node, const std::string& path) {
    std::vector<float> xy;
    try {
        xy = node.as<std::vector<float>>();
    } catch (const YAML::Exception&) {
        throw ConfigError(path + " must be a [x, y] pair");
    }
    if (xy.size() != 2) {
        throw ConfigError(path + " must be a [x, y] pair");
    }
    return {xy[0], xy[1]};
}

template <typename T>
T unwrap(Result<T> result, const std::string& path) {
    if (!result.ok()) {
        throw ConfigError(path + ": " + result.error().message);
    }
    return std::move(result).value();
}

domain::Scene parse_scene(const YAML::Node& stream, const std::string& path) {
    domain::Scene scene;
    std::set<std::string> ids;
    const YAML::Node zones = stream["zones"];
    for (std::size_t i = 0; zones && i < zones.size(); ++i) {
        const std::string zone_path = index_path(child_path(path, "zones"), i);
        const YAML::Node zone = zones[i];
        const auto id = read<std::string>(zone, "id", zone_path);
        require_identifier(id, child_path(zone_path, "id"));
        if (!ids.insert(id).second) {
            throw ConfigError(zone_path + ": duplicate id '" + id + "'");
        }
        std::vector<domain::Point2f> vertices;
        const YAML::Node polygon = require(zone, "polygon", zone_path);
        for (std::size_t v = 0; v < polygon.size(); ++v) {
            vertices.push_back(parse_point(polygon[v], index_path(zone_path + ".polygon", v)));
        }
        scene.zones.push_back(
            unwrap(domain::make_zone(id, read_or<std::string>(zone, "name", zone_path, id),
                                     std::move(vertices)),
                   zone_path));
    }
    const YAML::Node gates = stream["gates"];
    for (std::size_t i = 0; gates && i < gates.size(); ++i) {
        const std::string gate_path = index_path(child_path(path, "gates"), i);
        const YAML::Node gate = gates[i];
        const auto id = read<std::string>(gate, "id", gate_path);
        require_identifier(id, child_path(gate_path, "id"));
        if (!ids.insert(id).second) {
            throw ConfigError(gate_path + ": duplicate id '" + id + "'");
        }
        const YAML::Node line = require(gate, "line", gate_path);
        if (line.size() != 2) {
            throw ConfigError(gate_path + ".line must have exactly 2 points");
        }
        scene.gates.push_back(
            unwrap(domain::make_gate(id, read_or<std::string>(gate, "name", gate_path, id),
                                     parse_point(line[0], gate_path + ".line[0]"),
                                     parse_point(line[1], gate_path + ".line[1]")),
                   gate_path));
    }
    return scene;
}

StreamConfig parse_stream(const YAML::Node& node, const std::string& path) {
    StreamConfig stream;
    stream.id = read<std::string>(node, "id", path);
    require_identifier(stream.id, child_path(path, "id"));
    stream.source_uri = read_string(node, "source", path);
    const YAML::Node outputs = node["outputs"];
    for (std::size_t i = 0; outputs && i < outputs.size(); ++i) {
        const std::string output_path = index_path(child_path(path, "outputs"), i);
        stream.output_uris.push_back(expand_env(outputs[i].as<std::string>(), output_path));
    }
    stream.detector_model = read<std::string>(node, "detector", path);
    stream.scene = parse_scene(node, path);
    const YAML::Node solutions = node["solutions"];
    for (std::size_t i = 0; solutions && i < solutions.size(); ++i) {
        const std::string solution_path = index_path(child_path(path, "solutions"), i);
        stream.solutions.push_back(
            {read<std::string>(solutions[i], "type", solution_path), solutions[i]});
    }
    return stream;
}

AppConfig parse_app_config(const YAML::Node& root) {
    AppConfig config;

    const YAML::Node logging = root["logging"];
    if (logging) {
        log::LogConfig& log = config.logging.log;
        log.level = read_or<std::string>(logging, "level", "logging", log.level);
        log.directory = read_string_or(logging, "directory", "logging", log.directory);
        log.max_file_size_bytes =
            read_or<std::size_t>(logging, "max_file_size_mb", "logging",
                                 log.max_file_size_bytes / kBytesPerMegabyte) *
            kBytesPerMegabyte;
        log.max_files = read_or<std::size_t>(logging, "max_files", "logging", log.max_files);
        config.logging.stats_interval = std::chrono::seconds(read_or<std::int64_t>(
            logging, "stats_interval_s", "logging", config.logging.stats_interval.count()));
    }

    const YAML::Node http = root["http"];
    if (http) {
        config.http.listen_address =
            read_or<std::string>(http, "listen_address", "http", config.http.listen_address);
        config.http.port = read_or<int>(http, "port", "http", config.http.port);
        config.http.web_root = read_string_or(http, "web_root", "http", config.http.web_root);
        if (config.http.port < kMinPort || config.http.port > kMaxPort) {
            throw ConfigError("http.port must be in [1, 65535]");
        }
    }

    const YAML::Node inference = require(root, "inference", "");
    config.inference.runtime = read<std::string>(inference, "runtime", "inference");
    require_one_of(config.inference.runtime, kRuntimes, "inference.runtime");
    config.inference.backend = read<std::string>(inference, "backend", "inference");
    require_one_of(config.inference.backend, kBackends, "inference.backend");
    config.inference.device_id = read_or<int>(inference, "device_id", "inference", 0);
    config.inference.models_dir = read_string(inference, "models_dir", "inference");

    const YAML::Node storage = require(root, "storage", "");
    config.storage.sql_driver = read<std::string>(storage, "sql_driver", "storage");
    require_one_of(config.storage.sql_driver, kSqlDrivers, "storage.sql_driver");
    config.storage.sql_uri = read_string(storage, "sql_uri", "storage");
    config.storage.vector_driver = read<std::string>(storage, "vector_driver", "storage");
    require_one_of(config.storage.vector_driver, kVectorDrivers, "storage.vector_driver");
    config.storage.vector_uri = read_string_or(storage, "vector_uri", "storage", "");
    config.storage.media_dir = read_string(storage, "media_dir", "storage");

    std::set<std::string> stream_ids;
    const YAML::Node streams = root["streams"];
    for (std::size_t i = 0; streams && i < streams.size(); ++i) {
        const std::string path = index_path("streams", i);
        StreamConfig stream = parse_stream(streams[i], path);
        if (!stream_ids.insert(stream.id).second) {
            throw ConfigError(path + ": duplicate stream id '" + stream.id + "'");
        }
        config.streams.push_back(std::move(stream));
    }
    return config;
}

}  // namespace

Result<AppConfig> load_app_config(const std::filesystem::path& path) {
    try {
        return parse_app_config(YAML::LoadFile(path.string()));
    } catch (const ConfigError& error) {
        return make_error(ErrorCode::InvalidArgument, path.string() + ": " + error.what());
    } catch (const YAML::BadFile&) {
        return make_error(ErrorCode::NotFound, "cannot read config file " + path.string());
    } catch (const YAML::Exception& error) {
        return make_error(ErrorCode::InvalidArgument, path.string() + ": " + error.what());
    }
}

}  // namespace vqec::bootstrap
