// Composition root: parses arguments, loads config, initializes logging, validates the
// requested runtime against what this build contains, and wires adapters into pipelines.

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bootstrap/app_config.hpp"
#include "bootstrap/build_info.hpp"
#include "bootstrap/solution_factory.hpp"
#include "common/log.hpp"
#include "common/result.hpp"
#include "common/uri.hpp"

namespace {

namespace bootstrap = vqec::bootstrap;
namespace build_info = vqec::bootstrap::build_info;

// sysexits.h conventions, so scripts and supervisors can tell failures apart.
constexpr int kExitOk = 0;
constexpr int kExitUsage = 64;
constexpr int kExitUnavailable = 69;
constexpr int kExitSoftware = 70;
constexpr int kExitConfig = 78;

constexpr std::string_view kUsage =
    "Usage: vqec_simulator --config <file> [--check-config]\n"
    "       vqec_simulator --version\n";

struct CommandLine {
    std::filesystem::path config_path;
    bool check_only = false;
    bool show_version = false;
};

vqec::Result<CommandLine> parse_command_line(int argc, char** argv) {
    CommandLine command_line;
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--version") {
            command_line.show_version = true;
        } else if (args[i] == "--check-config") {
            command_line.check_only = true;
        } else if (args[i] == "--config" && i + 1 < args.size()) {
            command_line.config_path = std::string(args[++i]);
        } else {
            return vqec::make_error(
                vqec::ErrorCode::InvalidArgument,
                "unknown or incomplete argument '" + std::string(args[i]) + "'");
        }
    }
    if (!command_line.show_version && command_line.config_path.empty()) {
        return vqec::make_error(vqec::ErrorCode::InvalidArgument, "--config is required");
    }
    return command_line;
}

bool is_backend_compiled(std::string_view backend) {
    return (backend == "onnxruntime" && build_info::kWithOnnxRuntime) ||
           (backend == "tensorrt" && build_info::kWithTensorRt) ||
           (backend == "openvino" && build_info::kWithOpenVino);
}

std::string compiled_backends() {
    std::string list;
    const auto append = [&list](bool enabled, std::string_view name) {
        if (enabled) {
            list += (list.empty() ? "" : ",") + std::string(name);
        }
    };
    append(build_info::kWithOnnxRuntime, "onnxruntime");
    append(build_info::kWithTensorRt, "tensorrt");
    append(build_info::kWithOpenVino, "openvino");
    return list.empty() ? "none" : list;
}

void log_config_summary(const bootstrap::AppConfig& config) {
    VQEC_LOG_INFO("[bootstrap] inference: runtime={} backend={} device={} models_dir={}",
                  config.inference.runtime, config.inference.backend, config.inference.device_id,
                  config.inference.models_dir);
    VQEC_LOG_INFO("[bootstrap] storage: sql={} ({}) vector={} ({}) media_dir={}",
                  config.storage.sql_driver, vqec::redact_uri(config.storage.sql_uri),
                  config.storage.vector_driver, vqec::redact_uri(config.storage.vector_uri),
                  config.storage.media_dir);
    VQEC_LOG_INFO("[bootstrap] http: {}:{} web_root={}", config.http.listen_address,
                  config.http.port, config.http.web_root);
    for (const bootstrap::StreamConfig& stream : config.streams) {
        VQEC_LOG_INFO(
            "[bootstrap] stream {}: source={} outputs={} detector={} zones={} gates={} "
            "solutions={}",
            stream.id, vqec::redact_uri(stream.source_uri), stream.output_uris.size(),
            stream.detector_model, stream.scene.zones.size(), stream.scene.gates.size(),
            stream.solutions.size());
    }
}

// Builds every configured solution once so config mistakes surface at startup.
vqec::Status validate_solutions(const bootstrap::AppConfig& config) {
    for (const bootstrap::StreamConfig& stream : config.streams) {
        for (const bootstrap::SolutionConfig& solution : stream.solutions) {
            auto created = bootstrap::create_solution(solution, stream.scene);
            if (!created.ok()) {
                return vqec::make_error(created.error().code,
                                        "stream " + stream.id + ": " + created.error().message);
            }
        }
    }
    return {};
}

int run(const CommandLine& command_line) {
    vqec::Result<bootstrap::AppConfig> loaded =
        bootstrap::load_app_config(command_line.config_path);
    if (!loaded.ok()) {
        std::cerr << "config error: " << loaded.error().message << '\n';
        return kExitConfig;
    }
    const bootstrap::AppConfig config = std::move(loaded).value();
    if (vqec::Status status = vqec::log::init(config.logging.log); !status.ok()) {
        std::cerr << "logging error: " << status.error().message << '\n';
        return kExitConfig;
    }

    VQEC_LOG_INFO("[bootstrap] vqec_simulator {} ({}, runtime={}) starting, config={}",
                  build_info::kVersion, build_info::kBuildType, build_info::kRuntime,
                  command_line.config_path.string());
    VQEC_LOG_INFO("[bootstrap] compiled inference backends: {}", compiled_backends());
    log_config_summary(config);

    if (vqec::Status status = validate_solutions(config); !status.ok()) {
        VQEC_LOG_CRITICAL("[bootstrap] invalid solution config: {}", status.error().message);
        return kExitConfig;
    }
    if (config.streams.empty()) {
        VQEC_LOG_WARN("[bootstrap] no streams configured; only the API would be served");
    }
    if (command_line.check_only) {
        VQEC_LOG_INFO("[bootstrap] configuration OK");
        return kExitOk;
    }

    if (!is_backend_compiled(config.inference.backend)) {
        VQEC_LOG_CRITICAL(
            "[bootstrap] inference backend '{}' is not compiled into this build (compiled: {}); "
            "rebuild with the matching VQEC_WITH_* option or preset",
            config.inference.backend, compiled_backends());
        return kExitUnavailable;
    }

    // Adapter wiring goes here once adapters exist (docs/recipes.md, "Wire a new adapter"):
    // create shared services, then one StreamPipeline per stream on its own thread, start the
    // HTTP server, wait for SIGINT/SIGTERM, stop pipelines and join threads in reverse order.
    VQEC_LOG_CRITICAL("[bootstrap] no adapters are wired yet; nothing to run");
    return kExitUnavailable;
}

}  // namespace

int main(int argc, char** argv) {
    // Last-resort guard: an escaped exception is a bug, but it must still be logged.
    try {
        const vqec::Result<CommandLine> command_line = parse_command_line(argc, argv);
        if (!command_line.ok()) {
            std::cerr << command_line.error().message << '\n' << kUsage;
            return kExitUsage;
        }
        if (command_line.value().show_version) {
            std::cout << "vqec_simulator " << build_info::kVersion << " (runtime "
                      << build_info::kRuntime << ", backends " << compiled_backends() << ")\n";
            return kExitOk;
        }
        const int exit_code = run(command_line.value());
        vqec::log::shutdown();
        return exit_code;
    } catch (const std::exception& error) {
        VQEC_LOG_CRITICAL("[bootstrap] unhandled exception: {}", error.what());
        std::cerr << "fatal: unhandled exception: " << error.what() << '\n';
    } catch (...) {
        VQEC_LOG_CRITICAL("[bootstrap] unhandled non-standard exception");
        std::cerr << "fatal: unhandled non-standard exception\n";
    }
    vqec::log::shutdown();
    return kExitSoftware;
}
