#include "bootstrap/app_config.hpp"

#include <filesystem>

#include <gtest/gtest.h>

#include "bootstrap/solution_factory.hpp"

namespace vqec::bootstrap {
namespace {

const std::filesystem::path kExampleConfig =
    std::filesystem::path(VQEC_SOURCE_DIR) / "config" / "simulator.example.yaml";

// The committed example must always load: it is the documentation of the config schema.
TEST(AppConfig, LoadsTheCommittedExampleConfig) {
    const Result<AppConfig> config = load_app_config(kExampleConfig);

    ASSERT_TRUE(config.ok()) << config.error().message;
    ASSERT_FALSE(config.value().streams.empty());
    for (const StreamConfig& stream : config.value().streams) {
        for (const SolutionConfig& solution : stream.solutions) {
            const auto created = create_solution(solution, stream.scene);
            EXPECT_TRUE(created.ok()) << stream.id << ": " << created.error().message;
        }
    }
}

TEST(AppConfig, ReportsMissingFileAsNotFound) {
    const Result<AppConfig> config = load_app_config("/nonexistent/simulator.yaml");

    ASSERT_FALSE(config.ok());
    EXPECT_EQ(config.error().code, ErrorCode::NotFound);
}

}  // namespace
}  // namespace vqec::bootstrap
