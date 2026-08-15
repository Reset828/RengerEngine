#pragma once

#include "dzc/Error.h"

#include <cassert>
#include <cstdlib>
#include <exception>
#include <type_traits>
#include <utility>
#include <variant>

namespace dzc {

template <typename T>
class Result final {
    static_assert(!std::is_void_v<T>, "Result<void> uses the dedicated specialization");

public:
    // Creates a successful result containing value.
    static Result<T> success(T value) {
        return Result<T>(std::in_place_index<0>, std::move(value));
    }

    // Creates a failed result containing error.
    static Result<T> failure(Error error) {
        return Result<T>(std::in_place_index<1>, std::move(error));
    }

    // Returns true when the result contains a value.
    bool hasValue() const noexcept {
        return mStorage.index() == 0;
    }

    // Returns the successful value; the caller must check hasValue() first.
    T& value() {
        T* storedValue = std::get_if<0>(&mStorage);
        if (storedValue == nullptr) {
            assert(storedValue != nullptr && "Result::value() called on a failure");
            std::terminate();
        }
        return *storedValue;
    }

    // Returns the successful value from a const result; the caller must check
    // hasValue() first.
    const T& value() const {
        const T* storedValue = std::get_if<0>(&mStorage);
        if (storedValue == nullptr) {
            assert(storedValue != nullptr && "Result::value() called on a failure");
            std::terminate();
        }
        return *storedValue;
    }

    // Returns the failure error; only a failed result may call this function.
    const Error& error() const {
        const Error* storedError = std::get_if<1>(&mStorage);
        if (storedError == nullptr) {
            assert(storedError != nullptr && "Result::error() called on a success");
            std::terminate();
        }
        return *storedError;
    }

private:
    template <std::size_t Index, typename Value>
    explicit Result(std::in_place_index_t<Index>, Value&& value)
        : mStorage(std::in_place_index<Index>, std::forward<Value>(value)) {}

    std::variant<T, Error> mStorage;
};

template <>
class Result<void> final {
public:
    // Creates a successful void result.
    static Result<void> success() {
        return Result<void>(std::in_place_index<0>);
    }

    // Creates a failed void result containing error.
    static Result<void> failure(Error error) {
        return Result<void>(std::in_place_index<1>, std::move(error));
    }

    // Returns true when the result is successful.
    bool hasValue() const noexcept {
        return std::holds_alternative<std::monostate>(mStorage);
    }

    // Checks the successful state; the caller must check hasValue() first.
    void value() const {
        const std::monostate* storedValue = std::get_if<std::monostate>(&mStorage);
        if (storedValue == nullptr) {
            assert(storedValue != nullptr && "Result<void>::value() called on a failure");
            std::terminate();
        }
    }

    // Returns the failure error; only a failed result may call this function.
    const Error& error() const {
        const Error* storedError = std::get_if<1>(&mStorage);
        if (storedError == nullptr) {
            assert(storedError != nullptr && "Result<void>::error() called on a success");
            std::terminate();
        }
        return *storedError;
    }

private:
    explicit Result(std::in_place_index_t<0>)
        : mStorage(std::in_place_index<0>) {}

    explicit Result(std::in_place_index_t<1>, Error error)
        : mStorage(std::in_place_index<1>, std::move(error)) {}

    std::variant<std::monostate, Error> mStorage;
};

} // namespace dzc