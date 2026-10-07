#include "bootstrap/face_gallery_io.hpp"

#include <gtest/gtest.h>
namespace vqec::test {
TEST(FaceGalleryIo, RejectsWrongModelAndMalformedVectors) {
    EXPECT_FALSE(
        bootstrap::parse_face_gallery("{schema_version: 1, model_id: arcface, samples: []}").ok());
    EXPECT_FALSE(bootstrap::parse_face_gallery(
                     "{schema_version: 1, model_id: edgeface_s_gamma_05_v1, preprocessing: "
                     "rgb_similarity_112_v1, samples: [{employee_id: TECH1, values: [1]}]}")
                     .ok());
}
TEST(FaceGalleryIo, RoundTripsAndRejectsInvalidEmployeeIds) {
    std::vector<float> values(512, 0);
    values[0] = 1;
    std::vector<domain::FaceGallerySample> samples{
        {"TECH0000", {"edgeface_s_gamma_05_v1", values}}};
    auto encoded = bootstrap::encode_face_gallery(samples);
    ASSERT_TRUE(encoded.ok());
    EXPECT_FALSE(
        bootstrap::parse_face_gallery(encoded.value() + "\nmodel_id: edgeface_s_gamma_05_v1\n")
            .ok());
    auto parsed = bootstrap::parse_face_gallery(encoded.value());
    ASSERT_TRUE(parsed.ok());
    EXPECT_EQ(parsed.value()[0].employee_id, "TECH0000");
    samples[0].employee_id = "../escape";
    EXPECT_FALSE(bootstrap::encode_face_gallery(samples).ok());
}
}  // namespace vqec::test

namespace vqec::test {
TEST(FaceGalleryIo, RejectsCorruptionEmptyAndOverfullEmployeeSamples) {
    EXPECT_FALSE(bootstrap::parse_face_gallery("[").ok());
    EXPECT_FALSE(bootstrap::parse_face_gallery("").ok());
    EXPECT_FALSE(bootstrap::encode_face_gallery({}).ok());
    std::vector<float> values(512, 0);
    values[0] = 1;
    std::vector<domain::FaceGallerySample> samples(13,
                                                   {"TECH1", {"edgeface_s_gamma_05_v1", values}});
    EXPECT_FALSE(bootstrap::encode_face_gallery(samples).ok());
    samples.resize(1);
    samples[0].embedding.values[0] = 2;
    EXPECT_FALSE(bootstrap::encode_face_gallery(samples).ok());
}
}  // namespace vqec::test
