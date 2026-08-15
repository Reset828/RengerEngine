#include <dzc/Error.h>
#include <dzc/Result.h>

#include <cassert>
#include <memory>
#include <string>
#include <type_traits>

namespace {

dzc::Error makeError() {
    return dzc::Error{
        dzc::ErrorDomain::FileIo,
        42,
        "Unable to open file",
        "The file path was not found",
        "PointCloudReader::open"};
}

void testErrorDefaultsAndFields() {
    const dzc::Error defaultError;
    assert(defaultError.domain == dzc::ErrorDomain::General);
    assert(defaultError.code == 0);
    assert(defaultError.userMessage.empty());
    assert(defaultError.diagnosticMessage.empty());
    assert(defaultError.context.empty());

    const dzc::Error error = makeError();
    assert(error.domain == dzc::ErrorDomain::FileIo);
    assert(error.code == 42);
    assert(error.userMessage == "Unable to open file");
    assert(error.diagnosticMessage == "The file path was not found");
    assert(error.context == "PointCloudReader::open");
}

void testValueResult() {
    const auto result = dzc::Result<int>::success(7);
    assert(result.hasValue());
    assert(result.value() == 7);
}

void testErrorResult() {
    const auto result = dzc::Result<int>::failure(makeError());
    assert(!result.hasValue());
    const dzc::Error& error = result.error();
    assert(error.domain == dzc::ErrorDomain::FileIo);
    assert(error.code == 42);
}

void testMoveOnlyValue() {
    auto value = std::make_unique<int>(99);
    auto result = dzc::Result<std::unique_ptr<int>>::success(std::move(value));
    static_assert(!std::is_copy_constructible_v<dzc::Result<std::unique_ptr<int>>>);
    static_assert(std::is_move_constructible_v<dzc::Result<std::unique_ptr<int>>>);
    assert(value == nullptr);
    assert(result.hasValue());
    assert(*result.value() == 99);
}

void testVoidResults() {
    const auto success = dzc::Result<void>::success();
    assert(success.hasValue());
    success.value();

    const auto failure = dzc::Result<void>::failure(makeError());
    assert(!failure.hasValue());
    assert(failure.error().code == 42);
}

void testResultTypesDoNotImplicitlyConvert() {
    static_assert(!std::is_convertible_v<dzc::Result<int>, int>);
    static_assert(!std::is_convertible_v<dzc::Result<int>, dzc::Error>);
    static_assert(!std::is_convertible_v<dzc::Result<void>, bool>);
}

} // namespace

int main() {
    testErrorDefaultsAndFields();
    testValueResult();
    testErrorResult();
    testMoveOnlyValue();
    testVoidResults();
    testResultTypesDoNotImplicitlyConvert();
    return 0;
}