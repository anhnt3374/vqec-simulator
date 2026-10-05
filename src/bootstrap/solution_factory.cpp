#include "bootstrap/solution_factory.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "application/solutions/line_crossing/line_crossing_solution.hpp"
#include "application/solutions/zone_intrusion/zone_intrusion_solution.hpp"

namespace vqec::bootstrap {
namespace {

constexpr std::string_view kAnchorCenter = "center";
constexpr std::string_view kAnchorBottomCenter = "bottom_center";

std::vector<std::string> read_list(const YAML::Node& params, const std::string& key) {
    const YAML::Node node = params[key];
    return node ? node.as<std::vector<std::string>>() : std::vector<std::string>{};
}

Result<domain::AnchorPoint> read_anchor(const YAML::Node& params) {
    const YAML::Node node = params["anchor"];
    if (!node) {
        return domain::AnchorPoint::BottomCenter;
    }
    const auto value = node.as<std::string>();
    if (value == kAnchorCenter) {
        return domain::AnchorPoint::Center;
    }
    if (value == kAnchorBottomCenter) {
        return domain::AnchorPoint::BottomCenter;
    }
    return make_error(ErrorCode::InvalidArgument,
                      "anchor must be 'center' or 'bottom_center', got '" + value + "'");
}

template <typename Item>
Status require_ids_exist(const std::vector<std::string>& ids, const std::vector<Item>& items,
                         std::string_view kind) {
    for (const std::string& id : ids) {
        const bool found = std::any_of(items.begin(), items.end(),
                                       [&id](const Item& item) { return item.id == id; });
        if (!found) {
            return make_error(ErrorCode::NotFound,
                              std::string(kind) + " '" + id + "' is not defined in the stream");
        }
    }
    return {};
}

Result<std::unique_ptr<app::ISolution>> create_zone_intrusion(const YAML::Node& params,
                                                              const domain::Scene& scene) {
    app::ZoneIntrusionParams parsed;
    parsed.zone_ids = read_list(params, "zones");
    parsed.labels = read_list(params, "labels");
    Result<domain::AnchorPoint> anchor = read_anchor(params);
    if (!anchor.ok()) {
        return anchor.error();
    }
    parsed.anchor = anchor.value();
    if (Status status = require_ids_exist(parsed.zone_ids, scene.zones, "zone"); !status.ok()) {
        return status.error();
    }
    return std::unique_ptr<app::ISolution>(
        std::make_unique<app::ZoneIntrusionSolution>(std::move(parsed)));
}

Result<std::unique_ptr<app::ISolution>> create_line_crossing(const YAML::Node& params,
                                                             const domain::Scene& scene) {
    app::LineCrossingParams parsed;
    parsed.gate_ids = read_list(params, "gates");
    parsed.labels = read_list(params, "labels");
    Result<domain::AnchorPoint> anchor = read_anchor(params);
    if (!anchor.ok()) {
        return anchor.error();
    }
    parsed.anchor = anchor.value();
    if (Status status = require_ids_exist(parsed.gate_ids, scene.gates, "gate"); !status.ok()) {
        return status.error();
    }
    return std::unique_ptr<app::ISolution>(
        std::make_unique<app::LineCrossingSolution>(std::move(parsed)));
}

}  // namespace

std::vector<std::string_view> available_solution_types() {
    return {app::ZoneIntrusionSolution::kType, app::LineCrossingSolution::kType};
}

Result<std::unique_ptr<app::ISolution>> create_solution(const SolutionConfig& config,
                                                        const domain::Scene& scene) {
    Result<std::unique_ptr<app::ISolution>> solution =
        make_error(ErrorCode::NotFound, "unknown solution type '" + config.type + "'");
    try {
        if (config.type == app::ZoneIntrusionSolution::kType) {
            solution = create_zone_intrusion(config.params, scene);
        } else if (config.type == app::LineCrossingSolution::kType) {
            solution = create_line_crossing(config.params, scene);
        }
    } catch (const YAML::Exception& error) {
        return make_error(ErrorCode::InvalidArgument,
                          "solution '" + config.type + "': " + error.what());
    }
    if (!solution.ok()) {
        return make_error(solution.error().code,
                          "solution '" + config.type + "': " + solution.error().message);
    }
    return solution;
}

}  // namespace vqec::bootstrap
