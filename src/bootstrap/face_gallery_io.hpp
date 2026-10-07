#pragma once
#include <filesystem>

#include "domain/vision/face_gallery.hpp"
namespace vqec::bootstrap {
Result<std::vector<domain::FaceGallerySample>> parse_face_gallery(const std::string& text);
Result<std::string> encode_face_gallery(const std::vector<domain::FaceGallerySample>& samples);
Result<std::vector<domain::FaceGallerySample>> load_face_gallery(const std::filesystem::path& path);
Status write_face_gallery(const std::filesystem::path& path,
                          const std::vector<domain::FaceGallerySample>& samples);
}  // namespace vqec::bootstrap
