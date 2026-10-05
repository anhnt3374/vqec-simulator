#include "common/uri.hpp"

#include <gtest/gtest.h>

namespace vqec {
namespace {

TEST(RedactUri, MasksUserAndPassword) {
    EXPECT_EQ(redact_uri("rtsp://admin:secret@10.0.0.5:554/stream1"),
              "rtsp://***@10.0.0.5:554/stream1");
}

TEST(RedactUri, KeepsUriWithoutCredentials) {
    EXPECT_EQ(redact_uri("rtsp://10.0.0.5/stream1"), "rtsp://10.0.0.5/stream1");
}

TEST(RedactUri, IgnoresAtSignInPath) {
    EXPECT_EQ(redact_uri("http://host/path@x"), "http://host/path@x");
}

TEST(RedactUri, KeepsPlainFilePath) {
    EXPECT_EQ(redact_uri("/data/videos/demo.mp4"), "/data/videos/demo.mp4");
}

}  // namespace
}  // namespace vqec
