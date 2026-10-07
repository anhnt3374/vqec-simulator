#include "adapters/outbound/media/ffmpeg/overlay_renderer.hpp"

#include <gtest/gtest.h>

namespace vqec::test {
TEST(OverlayRenderer, DrawsBothLabelsWithoutMutatingSharedPixels) {
    auto pixels = std::make_shared<domain::ImageBuffer>();
    pixels->width = 100;
    pixels->height = 100;
    pixels->stride_bytes = 300;
    pixels->data.resize(30000, 0);
    domain::Frame frame;
    frame.width = 100;
    frame.height = 100;
    frame.image = pixels;
    ports::FrameAnnotations annotations;
    annotations.tracks.push_back(
        {0, domain::TrackState::Tentative, {10, 30, 20, 40}, 0, "person", 0.9F});
    annotations.tracks.push_back(
        {0, domain::TrackState::Tentative, {60, 30, 20, 40}, 0, "face", 0.8F});
    auto result = adapters::ffmpeg::render_overlay(frame, annotations, 100, 100);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(pixels->data, std::vector<std::uint8_t>(30000, 0));
    const auto person_pixel = static_cast<std::size_t>(50 * 300 + 10 * 3);
    const auto face_pixel = static_cast<std::size_t>(50 * 300 + 60 * 3);
    EXPECT_NE(result.value()->data[person_pixel + 1], 0);
    EXPECT_NE(result.value()->data[face_pixel + 2], 0);
    EXPECT_NE(result.value()->data[person_pixel + 2], result.value()->data[face_pixel + 2]);
}

TEST(OverlayRenderer, AcceptsNoDetectionsAndResizesOutput) {
    auto pixels = std::make_shared<domain::ImageBuffer>();
    pixels->width = 32;
    pixels->height = 32;
    pixels->stride_bytes = 96;
    pixels->data.resize(3072, 10);
    domain::Frame frame;
    frame.width = 32;
    frame.height = 32;
    frame.image = pixels;
    auto result = adapters::ffmpeg::render_overlay(frame, {}, 64, 32);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value()->data.size(), 6144U);
}

TEST(EncoderPts, PreservesGapsAndMonotonicityAcrossResets) {
    adapters::ffmpeg::EncoderClock clock;
    EXPECT_EQ(clock.next(std::chrono::milliseconds(0)), 0);
    EXPECT_EQ(clock.next(std::chrono::milliseconds(100)), 9000);
    EXPECT_EQ(clock.next(std::chrono::milliseconds(300)), 27000);
    EXPECT_GT(clock.next(std::chrono::milliseconds(0)), 27000);
}
TEST(OverlayRenderer, DisplaysPersonIdAndSuppressesLostPredictionBoxes) {
    auto pixels = std::make_shared<domain::ImageBuffer>();
    pixels->width = 200;
    pixels->height = 100;
    pixels->stride_bytes = 600;
    pixels->data.resize(60000, 0);
    domain::Frame frame;
    frame.width = 200;
    frame.height = 100;
    frame.image = pixels;
    ports::FrameAnnotations annotations;
    annotations.tracks.push_back(
        {42, domain::TrackState::Confirmed, {10, 50, 30, 30}, 0, "person", 0.9F});
    auto tracked = adapters::ffmpeg::render_overlay(frame, annotations, 200, 100);
    ASSERT_TRUE(tracked.ok());
    annotations.tracks[0].id = 0;
    auto plain = adapters::ffmpeg::render_overlay(frame, annotations, 200, 100);
    ASSERT_TRUE(plain.ok());
    EXPECT_NE(tracked.value()->data, plain.value()->data);
    annotations.tracks[0].state = domain::TrackState::Lost;
    auto lost = adapters::ffmpeg::render_overlay(frame, annotations, 200, 100);
    auto empty = adapters::ffmpeg::render_overlay(frame, {}, 200, 100);
    ASSERT_TRUE(lost.ok());
    ASSERT_TRUE(empty.ok());
    EXPECT_EQ(lost.value()->data, empty.value()->data);
}
}  // namespace vqec::test
