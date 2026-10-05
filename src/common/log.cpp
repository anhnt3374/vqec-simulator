#include "common/log.hpp"

#include <chrono>
#include <exception>
#include <memory>
#include <string_view>
#include <vector>

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace vqec::log {
namespace {

// ISO-8601 time with milliseconds, level, thread id, message.
constexpr const char* kPattern = "%Y-%m-%dT%H:%M:%S.%e%z [%^%l%$] [%t] %v";
constexpr const char* kLoggerName = "vqec";
constexpr const char* kLogFileName = "vqec.log";
constexpr std::string_view kOffLevelName = "off";
// Async queue: when full the oldest messages are overwritten instead of blocking producers.
constexpr std::size_t kAsyncQueueSize = 8192;
constexpr std::size_t kAsyncThreadCount = 1;
constexpr std::chrono::seconds kFlushInterval{2};

}  // namespace

Status init(const LogConfig& config) {
    const spdlog::level::level_enum level = spdlog::level::from_str(config.level);
    // from_str() maps unknown names to `off`, so only accept `off` when it was asked for.
    if (level == spdlog::level::off && config.level != kOffLevelName) {
        return make_error(ErrorCode::InvalidArgument, "unknown log level '" + config.level + "'");
    }
    try {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        if (!config.directory.empty()) {
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                config.directory + "/" + kLogFileName, config.max_file_size_bytes,
                config.max_files));
        }
        spdlog::init_thread_pool(kAsyncQueueSize, kAsyncThreadCount);
        auto logger = std::make_shared<spdlog::async_logger>(
            kLoggerName, sinks.begin(), sinks.end(), spdlog::thread_pool(),
            spdlog::async_overflow_policy::overrun_oldest);
        logger->set_level(level);
        logger->set_pattern(kPattern);
        logger->flush_on(spdlog::level::warn);
        spdlog::set_default_logger(logger);
        spdlog::flush_every(kFlushInterval);
    } catch (const std::exception& error) {  // spdlog reports sink failures by throwing.
        return make_error(ErrorCode::Unavailable,
                          std::string("cannot initialize logging: ") + error.what());
    }
    return {};
}

void shutdown() {
    spdlog::shutdown();
}

}  // namespace vqec::log
