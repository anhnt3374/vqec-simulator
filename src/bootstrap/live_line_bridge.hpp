#pragma once

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "application/pipeline/live_ai_pipeline.hpp"
#include "common/result.hpp"

namespace vqec::bootstrap {
struct LiveLineUpdate {
    std::string revision;
    std::optional<domain::Gate> gate;
    std::optional<domain::Zone> zone;
    std::string line_revision;
    std::string zone_revision;
};
Result<LiveLineUpdate> parse_live_line_update(const std::string& contents);

// Owns a joined config-watcher thread. pipeline is non-owning and must outlive this bridge.
// File I/O never runs on the coordinator. Destruction interrupts the polling wait and joins.
class LiveLineBridge final {
public:
    LiveLineBridge(std::filesystem::path file, app::LiveAiPipeline& pipeline);
    ~LiveLineBridge();
    LiveLineBridge(const LiveLineBridge&) = delete;
    LiveLineBridge& operator=(const LiveLineBridge&) = delete;

private:
    void watch();
    std::filesystem::path file_;
    app::LiveAiPipeline& pipeline_;
    std::mutex mutex_;
    std::condition_variable ready_;
    bool stopped_ = false;
    std::thread thread_;
};
}  // namespace vqec::bootstrap
