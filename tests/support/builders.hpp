#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "domain/geometry/box.hpp"
#include "domain/ids.hpp"
#include "domain/scene/scene.hpp"
#include "domain/vision/frame.hpp"
#include "domain/vision/track.hpp"

// Small factories that keep tests focused on behavior instead of setup.
namespace vqec::test {

inline constexpr int kFrameWidth = 1000;
inline constexpr int kFrameHeight = 1000;
inline constexpr const char* kStreamId = "cam_test";

inline domain::Frame make_frame(std::uint64_t sequence) {
    domain::Frame frame;
    frame.stream_id = kStreamId;
    frame.sequence = sequence;
    frame.width = kFrameWidth;
    frame.height = kFrameHeight;
    return frame;
}

// Track whose bottom-center anchor is at normalized (x, y) in a kFrameWidth x kFrameHeight frame.
inline domain::Track make_track(domain::TrackId id, float x, float y,
                                domain::TrackState state = domain::TrackState::Confirmed,
                                const std::string& label = "person") {
    constexpr float kBoxSize = 50.0F;
    domain::Track track;
    track.id = id;
    track.state = state;
    track.label = label;
    track.score = 1.0F;
    track.box = domain::BoxF{x * static_cast<float>(kFrameWidth) - kBoxSize / 2.0F,
                             y * static_cast<float>(kFrameHeight) - kBoxSize, kBoxSize, kBoxSize};
    return track;
}

// Zone covering the normalized square [0.5, 1] x [0.5, 1].
inline domain::Zone make_bottom_right_zone(const domain::ZoneId& id = "zone_a") {
    return domain::make_zone(id, id, {{0.5F, 0.5F}, {1.0F, 0.5F}, {1.0F, 1.0F}, {0.5F, 1.0F}})
        .value();
}

// Horizontal gate across the middle of the frame, drawn left -> right.
inline domain::Gate make_middle_gate(const domain::GateId& id = "gate_a") {
    return domain::make_gate(id, id, {0.1F, 0.5F}, {0.9F, 0.5F}).value();
}

}  // namespace vqec::test
