#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace vqec {

// Error categories shared by every layer. Adapters map library-specific failures onto these.
enum class ErrorCode {
    InvalidArgument,  // Caller passed bad input or configuration.
    NotFound,         // Referenced entity does not exist.
    AlreadyExists,    // Entity with the same identity exists.
    EndOfStream,      // A finite source (file) is exhausted. Not a failure for file inputs.
    Unavailable,      // Transient: network down, camera disconnected, service not ready.
    Timeout,          // Operation exceeded its deadline.
    Unsupported,      // Feature/format/backend not available in this build or on this device.
    Internal,         // Bug or unexpected library failure.
};

constexpr std::string_view to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidArgument:
            return "invalid_argument";
        case ErrorCode::NotFound:
            return "not_found";
        case ErrorCode::AlreadyExists:
            return "already_exists";
        case ErrorCode::EndOfStream:
            return "end_of_stream";
        case ErrorCode::Unavailable:
            return "unavailable";
        case ErrorCode::Timeout:
            return "timeout";
        case ErrorCode::Unsupported:
            return "unsupported";
        case ErrorCode::Internal:
            return "internal";
    }
    return "unknown";
}

struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string message;
};

inline Error make_error(ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

// Outcome of an operation that returns no value. Default-constructed means success, so a
// function can `return {};` on success and `return make_error(...);` on failure.
class [[nodiscard]] Status {
public:
    Status() = default;
    Status(Error error) : error_(std::move(error)) {}  // NOLINT(google-explicit-constructor)

    bool ok() const { return !error_.has_value(); }

    // Precondition: !ok().
    const Error& error() const { return error_.value(); }

private:
    std::optional<Error> error_;
};

// Value or error, in the spirit of C++23 std::expected.
template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value)  // NOLINT(google-explicit-constructor)
        : storage_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error)  // NOLINT(google-explicit-constructor)
        : storage_(std::in_place_index<1>, std::move(error)) {}

    bool ok() const { return storage_.index() == 0; }

    // Precondition: ok().
    const T& value() const& { return std::get<0>(storage_); }
    T& value() & { return std::get<0>(storage_); }
    T&& value() && { return std::get<0>(std::move(storage_)); }

    // Precondition: !ok().
    const Error& error() const { return std::get<1>(storage_); }

    Status status() const { return ok() ? Status{} : Status{error()}; }

private:
    std::variant<T, Error> storage_;
};

}  // namespace vqec
