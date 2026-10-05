#pragma once

#include <cstddef>
#include <string>

#include <spdlog/spdlog.h>

#include "common/result.hpp"

// Logging facade. Use these macros instead of spdlog directly so the backend can change in one
// place. Format strings use fmt syntax and every message starts with its context:
//   VQEC_LOG_INFO("[stream={}] source opened: {}x{} @ {:.1f} fps", id, width, height, fps);
// Logging rules (what to log at which level) are in AGENTS.md section 6.
#define VQEC_LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define VQEC_LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define VQEC_LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define VQEC_LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define VQEC_LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define VQEC_LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)

namespace vqec::log {

struct LogConfig {
    static constexpr std::size_t kDefaultMaxFileSizeBytes = std::size_t{50} * 1024 * 1024;
    static constexpr std::size_t kDefaultMaxFiles = 10;

    std::string level = "info";  // trace, debug, info, warning, error, critical, off.
    std::string directory;       // Empty: console only. Otherwise also <directory>/vqec.log.
    std::size_t max_file_size_bytes = kDefaultMaxFileSizeBytes;
    std::size_t max_files = kDefaultMaxFiles;  // Rotated files kept.
};

// Installs the default logger: console + optional rotating file, written asynchronously so the
// pipeline threads never block on log I/O. Call once from bootstrap before any thread starts.
Status init(const LogConfig& config);

// Flushes and stops the async logger. Call at the end of main().
void shutdown();

}  // namespace vqec::log
