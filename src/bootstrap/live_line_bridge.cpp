#include "bootstrap/live_line_bridge.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "common/log.hpp"

namespace vqec::bootstrap {
namespace {
constexpr std::chrono::milliseconds kConfigPollInterval{100};
constexpr std::size_t kMaxSnapshotBytes = 4096;
constexpr std::size_t kMaxRevisionLength = 128;
constexpr std::size_t kMaxZoneVertices = 32;
}  // namespace

Result<LiveLineUpdate> parse_live_line_update(const std::string& contents) {
    if (contents.size() > kMaxSnapshotBytes)
        return make_error(ErrorCode::InvalidArgument, "live_line: snapshot exceeds size limit");
    try {
        const auto root = YAML::Load(contents);
        if (!root.IsMap() || !root["revision"].IsScalar() || !root["line"])
            return make_error(ErrorCode::InvalidArgument, "live_line: revision and line required");
        LiveLineUpdate result;
        result.revision = root["revision"].as<std::string>();
        if (result.revision.empty() || result.revision.size() > kMaxRevisionLength)
            return make_error(ErrorCode::InvalidArgument, "live_line.revision: invalid length");
        result.line_revision =
            root["line_revision"] ? root["line_revision"].as<std::string>() : result.revision;
        result.zone_revision =
            root["zone_revision"] ? root["zone_revision"].as<std::string>() : result.revision;
        if (result.line_revision.empty() || result.zone_revision.empty() ||
            result.line_revision.size() > kMaxRevisionLength ||
            result.zone_revision.size() > kMaxRevisionLength)
            return make_error(ErrorCode::InvalidArgument, "live_scene: invalid feature revision");
        const auto line = root["line"];
        if (!line.IsNull()) {
            if (!line.IsSequence() || line.size() != 2 || !line[0].IsSequence() ||
                !line[1].IsSequence() || line[0].size() != 2 || line[1].size() != 2)
                return make_error(ErrorCode::InvalidArgument,
                                  "live_line.line: expected two endpoints");
            auto gate = domain::make_gate("live_line", "Live outward line",
                                          {line[0][0].as<float>(), line[0][1].as<float>()},
                                          {line[1][0].as<float>(), line[1][1].as<float>()});
            if (!gate.ok())
                return gate.error();
            result.gate = std::move(gate).value();
        }
        const auto polygon = root["zone"];
        if (polygon && !polygon.IsNull()) {
            if (!polygon.IsSequence() || polygon.size() < domain::Polygon::kMinVertices ||
                polygon.size() > kMaxZoneVertices)
                return make_error(ErrorCode::InvalidArgument, "live_zone: expected 3..32 vertices");
            std::vector<domain::Point2f> vertices;
            vertices.reserve(polygon.size());
            for (const auto& point : polygon) {
                if (!point.IsSequence() || point.size() != 2)
                    return make_error(ErrorCode::InvalidArgument,
                                      "live_zone: expected [x,y] vertices");
                vertices.push_back({point[0].as<float>(), point[1].as<float>()});
            }
            auto zone = domain::make_zone("live_zone", "Live zone", std::move(vertices));
            if (!zone.ok())
                return zone.error();
            result.zone = std::move(zone).value();
        }
        return result;
    } catch (const std::exception&) {
        return make_error(ErrorCode::InvalidArgument, "live_line: invalid snapshot");
    }
}

LiveLineBridge::LiveLineBridge(std::filesystem::path file, app::LiveAiPipeline& pipeline)
    : file_(std::move(file)), pipeline_(pipeline) {
    if (!file_.empty())
        thread_ = std::thread([this] { watch(); });
}
LiveLineBridge::~LiveLineBridge() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    ready_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void LiveLineBridge::watch() {
    try {
        VQEC_LOG_INFO("[bootstrap] temporary line/zone bridge opened");
        std::string previous_contents;
        std::string revision;
        std::string line_revision, zone_revision;
        bool failed = false;
        while (true) {
            std::ifstream input(file_);
            std::string contents;
            if (input) {
                // Bounded read also protects against an accidentally oversized IPC file.
                contents.resize(kMaxSnapshotBytes + 1);
                input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
                contents.resize(static_cast<std::size_t>(input.gcount()));
            }
            if (!contents.empty() && contents != previous_contents) {
                previous_contents = contents;
                auto parsed = parse_live_line_update(contents);
                if (parsed.ok()) {
                    if (parsed.value().revision != revision) {
                        revision = parsed.value().revision;
                        if (parsed.value().line_revision != line_revision) {
                            line_revision = parsed.value().line_revision;
                            pipeline_.request_gate_update(parsed.value().gate);
                        }
                        if (parsed.value().zone_revision != zone_revision) {
                            zone_revision = parsed.value().zone_revision;
                            pipeline_.request_zone_update(parsed.value().zone);
                        }
                        VQEC_LOG_DEBUG("[bootstrap] temporary line/zone revisions queued");
                    }
                    failed = false;
                } else if (!failed) {
                    pipeline_.request_gate_update(std::nullopt);
                    pipeline_.request_zone_update(std::nullopt);
                    VQEC_LOG_WARN("[bootstrap] temporary line and zone disabled code={} message={}",
                                  to_string(parsed.error().code), parsed.error().message);
                    revision.clear();
                    line_revision.clear();
                    zone_revision.clear();
                    failed = true;
                }
            } else if (contents.empty() && !revision.empty()) {
                pipeline_.request_gate_update(std::nullopt);
                pipeline_.request_zone_update(std::nullopt);
                revision.clear();
                line_revision.clear();
                zone_revision.clear();
                previous_contents.clear();
                VQEC_LOG_WARN(
                    "[bootstrap] temporary line and zone disabled code=unavailable snapshot "
                    "missing");
            }
            std::unique_lock<std::mutex> lock(mutex_);
            if (ready_.wait_for(lock, kConfigPollInterval, [this] { return stopped_; }))
                break;
        }
        VQEC_LOG_INFO("[bootstrap] temporary line/zone bridge closed");
    } catch (...) {
        pipeline_.request_gate_update(std::nullopt);
        pipeline_.request_zone_update(std::nullopt);
        VQEC_LOG_ERROR("[bootstrap] temporary line/zone bridge stopped code=internal");
    }
}
}  // namespace vqec::bootstrap
