#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "application/solutions/solution.hpp"
#include "bootstrap/app_config.hpp"
#include "common/result.hpp"
#include "domain/scene/scene.hpp"

namespace vqec::bootstrap {

// Solution types this build can create, for logs and the API.
std::vector<std::string_view> available_solution_types();

// Builds a solution from its config block. Validates params and that every referenced zone/gate
// exists in `scene`. Register new solutions here (see docs/recipes.md).
Result<std::unique_ptr<app::ISolution>> create_solution(const SolutionConfig& config,
                                                        const domain::Scene& scene);

}  // namespace vqec::bootstrap
