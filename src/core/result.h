#pragma once

#include <optional>
#include <string>
#include <utility>

namespace chunkmap {

struct Error {
    std::string code;
    std::string message;
};

template <typename T>
class Result {
public:
    static Result success(T value) {
        Result result;
        result.value_ = std::move(value);
        return result;
    }

    static Result failure(std::string code, std::string message) {
        Result result;
        result.error_ = Error{std::move(code), std::move(message)};
        return result;
    }

    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }

    T& value() { return *value_; }
    const T& value() const { return *value_; }
    T take_value() { return std::move(*value_); }

    const Error& error() const { return *error_; }

private:
    std::optional<T> value_;
    std::optional<Error> error_;
};

template <>
class Result<void> {
public:
    static Result success() { return Result(true, std::nullopt); }

    static Result failure(std::string code, std::string message) {
        return Result(false, Error{std::move(code), std::move(message)});
    }

    bool ok() const { return ok_; }
    explicit operator bool() const { return ok(); }
    const Error& error() const { return *error_; }

private:
    Result(bool ok, std::optional<Error> error)
        : ok_(ok), error_(std::move(error)) {}

    bool ok_ = false;
    std::optional<Error> error_;
};

}  // namespace chunkmap

