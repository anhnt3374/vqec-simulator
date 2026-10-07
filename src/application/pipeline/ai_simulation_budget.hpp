#pragma once
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
namespace vqec::app {
// Pure per-frame service budget. Actual inference/tracking time consumes the budget;
// only unused time is artificial delay. Zero disables simulation.
class AiSimulationBudget final {
public:
    explicit AiSimulationBudget(double fps) {
        assert(std::isfinite(fps) && (fps == 0 || (fps >= 1 && fps <= 1000)));
        if (fps > 0)
            interval_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::duration<double>(1.0 / fps));
    }
    std::chrono::nanoseconds remaining(std::chrono::nanoseconds elapsed) const {
        return std::max(std::chrono::nanoseconds::zero(), interval_ - elapsed);
    }

private:
    std::chrono::nanoseconds interval_{};
};
}  // namespace vqec::app
