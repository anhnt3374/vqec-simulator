#include "bootstrap/face_gallery_io.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>
namespace vqec::test {
namespace {
struct GalleryDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("vqec-gallery-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~GalleryDirectory() { std::filesystem::remove_all(path); }
};
}  // namespace
TEST(FaceGalleryFiles, PreservesExistingGalleryWhenReplacementFailsAndRejectsCorruptInput) {
    GalleryDirectory directory;
    const auto path = directory.path / "gallery.yaml";
    EXPECT_FALSE(bootstrap::load_face_gallery(path).ok());
    std::vector<float> values(512, 0);
    values[0] = 1;
    const std::vector<domain::FaceGallerySample> samples{
        {"TECH1", {"edgeface_s_gamma_05_v1", values}}};
    ASSERT_TRUE(bootstrap::write_face_gallery(path, samples).ok());
    ASSERT_TRUE(bootstrap::load_face_gallery(path).ok());
    EXPECT_FALSE(bootstrap::write_face_gallery(path, {}).ok());
    auto retained = bootstrap::load_face_gallery(path);
    ASSERT_TRUE(retained.ok());
    EXPECT_EQ(retained.value()[0].employee_id, "TECH1");
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp"));
    std::ofstream corrupt(path);
    corrupt << "invalid: [";
    corrupt.close();
    EXPECT_FALSE(bootstrap::load_face_gallery(path).ok());
}
TEST(FaceGalleryFiles, LoadsActualEnrollmentAndSelfMatchesWithoutClaimingLiveAccuracy) {
    const auto path = std::filesystem::path(VQEC_SOURCE_DIR) / "data/face_gallery.edgeface.yaml";
    if (!std::filesystem::exists(path))
        GTEST_SKIP() << "Local employee gallery unavailable";
    auto gallery = bootstrap::load_face_gallery(path);
    ASSERT_TRUE(gallery.ok());
    ASSERT_FALSE(gallery.value().empty());
    const auto match = domain::match_face(gallery.value(), gallery.value()[0].embedding);
    ASSERT_TRUE(match.ok());
    EXPECT_EQ(match.value().employee_id, gallery.value()[0].employee_id);
    EXPECT_NEAR(match.value().score, 1.0F, 0.002F);
}
}  // namespace vqec::test
