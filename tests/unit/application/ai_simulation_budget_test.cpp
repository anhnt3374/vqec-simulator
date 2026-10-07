#include "application/pipeline/ai_simulation_budget.hpp"

#include <gtest/gtest.h>
namespace vqec::test {
TEST(AiSimulationBudget, WaitsOnlyForUnusedTimeAtEighteenFps) {
    app::AiSimulationBudget budget(18);
    const auto target = std::chrono::nanoseconds(55555556);
    EXPECT_NEAR(static_cast<double>(budget.remaining(std::chrono::nanoseconds(0)).count()),
                static_cast<double>(target.count()), 1.0);
    EXPECT_NEAR(static_cast<double>(budget.remaining(std::chrono::milliseconds(30)).count()),
                25555556.0, 1.0);
    EXPECT_EQ(budget.remaining(std::chrono::milliseconds(60)), std::chrono::nanoseconds(0));
}
TEST(AiSimulationBudget, DisabledAddsNoArtificialDelay) {
    app::AiSimulationBudget budget(0);
    EXPECT_EQ(budget.remaining(std::chrono::nanoseconds(0)), std::chrono::nanoseconds(0));
    EXPECT_EQ(budget.remaining(std::chrono::milliseconds(30)), std::chrono::nanoseconds(0));
}
}  // namespace vqec::test
