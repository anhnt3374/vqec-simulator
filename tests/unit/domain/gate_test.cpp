#include "domain/scene/gate.hpp"

#include <gtest/gtest.h>

namespace vqec::domain {
namespace {

// Horizontal gate drawn left -> right across the middle of the frame.
Gate make_horizontal_gate() {
    return make_gate("gate_a", "A", {0.2F, 0.5F}, {0.8F, 0.5F}).value();
}

TEST(GateCrossing, ReportsForwardWhenMovingDownAcrossLeftToRightGate) {
    EXPECT_EQ(detect_crossing(make_horizontal_gate(), {0.5F, 0.4F}, {0.5F, 0.6F}),
              CrossingDirection::Forward);
}

TEST(GateCrossing, ReportsBackwardWhenMovingUp) {
    EXPECT_EQ(detect_crossing(make_horizontal_gate(), {0.5F, 0.6F}, {0.5F, 0.4F}),
              CrossingDirection::Backward);
}

TEST(GateCrossing, ReportsForwardWhenMovingLeftAcrossTopToBottomGate) {
    const Gate vertical = make_gate("gate_v", "V", {0.5F, 0.2F}, {0.5F, 0.8F}).value();

    EXPECT_EQ(detect_crossing(vertical, {0.6F, 0.5F}, {0.4F, 0.5F}), CrossingDirection::Forward);
}

TEST(GateCrossing, IgnoresMovementOnOneSide) {
    EXPECT_EQ(detect_crossing(make_horizontal_gate(), {0.5F, 0.1F}, {0.6F, 0.4F}),
              CrossingDirection::None);
}

TEST(GateCrossing, IgnoresCrossingOfTheLineExtensionBeyondTheEndpoints) {
    EXPECT_EQ(detect_crossing(make_horizontal_gate(), {0.9F, 0.4F}, {0.9F, 0.6F}),
              CrossingDirection::None);
}

TEST(GateCrossing, RequiresLeavingTheLineCompletely) {
    EXPECT_EQ(detect_crossing(make_horizontal_gate(), {0.5F, 0.4F}, {0.5F, 0.5F}),
              CrossingDirection::None);
}

TEST(Gate, RejectsIdenticalEndpoints) {
    const Result<Gate> gate = make_gate("gate_a", "A", {0.5F, 0.5F}, {0.5F, 0.5F});

    ASSERT_FALSE(gate.ok());
    EXPECT_EQ(gate.error().code, ErrorCode::InvalidArgument);
}

}  // namespace
}  // namespace vqec::domain
