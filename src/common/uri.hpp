#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace vqec {

// Removes credentials from a URI so it can be logged or returned by the API:
//   rtsp://admin:secret@10.0.0.5/stream1 -> rtsp://***@10.0.0.5/stream1
// Strings without "scheme://user[:password]@" are returned unchanged.
inline std::string redact_uri(std::string_view uri) {
    constexpr std::string_view kSchemeSeparator = "://";
    constexpr std::string_view kMask = "***";
    const std::size_t scheme_end = uri.find(kSchemeSeparator);
    if (scheme_end == std::string_view::npos) {
        return std::string(uri);
    }
    const std::size_t authority_start = scheme_end + kSchemeSeparator.size();
    const std::size_t authority_end = uri.find('/', authority_start);
    const std::size_t at = uri.rfind('@', authority_end);
    if (at == std::string_view::npos || at < authority_start) {
        return std::string(uri);
    }
    std::string redacted(uri.substr(0, authority_start));
    redacted += kMask;
    redacted += uri.substr(at);
    return redacted;
}

}  // namespace vqec
