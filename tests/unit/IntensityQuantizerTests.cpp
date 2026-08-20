#include "data/chunk/IntensityQuantizer.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace {

using dzc::IntensityQuantizationResult;
using dzc::IntensityQuantizationStatus;
using dzc::IntensityQuantizer;
using dzc::IntensitySourceRange;

void assertSuccessfulResult(const dzc::Result<IntensityQuantizationResult>& result) {
    assert(result.hasValue());
}

void assertCorruptData(const dzc::Result<IntensityQuantizationResult>& result) {
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::DataFormat);
    assert(result.error().code == 2U);
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
}

void testIntegerRangeQuantization() {
    const auto quantized = IntensityQuantizer::quantize({0.0, 5.0, 10.0});
    assertSuccessfulResult(quantized);

    const auto& result = quantized.value();
    assert(result.status == IntensityQuantizationStatus::Normal);
    assert(result.invalidCount == 0U);
    const std::vector<std::uint16_t> expectedIntegerValues{0U, 32768U, 65535U};
    assert(result.values == expectedIntegerValues);
    assert(result.metadata.available);
    assert(result.metadata.sourceMinimum == 0.0);
    assert(result.metadata.sourceMaximum == 10.0);
    assert(result.metadata.validMinimum == 0.0);
    assert(result.metadata.validMaximum == 10.0);
}

void testFloatingPointAndNegativeRangeQuantization() {
    const auto quantized = IntensityQuantizer::quantize({-1.5, -0.5, 0.5});
    assertSuccessfulResult(quantized);

    const auto& result = quantized.value();
    assert(result.status == IntensityQuantizationStatus::Normal);
    assert(result.values.size() == 3U);
    assert(result.values[0] == 0U);
    assert(result.values[1] == 32768U);
    assert(result.values[2] == 65535U);
    assert(result.metadata.validMinimum == -1.5);
    assert(result.metadata.validMaximum == 0.5);
}

void testInvalidValuesAreCountedAndQuantizedToZero() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double positiveInfinity = std::numeric_limits<double>::infinity();
    const double negativeInfinity = -std::numeric_limits<double>::infinity();
    const auto quantized = IntensityQuantizer::quantize({nan, 10.0, positiveInfinity, 20.0, negativeInfinity});
    assertSuccessfulResult(quantized);

    const auto& result = quantized.value();
    assert(result.status == IntensityQuantizationStatus::Normal);
    assert(result.invalidCount == 3U);
    const std::vector<std::uint16_t> expectedMixedValues{0U, 0U, 0U, 65535U, 0U};
    assert(result.values == expectedMixedValues);
    assert(result.metadata.available);
    assert(result.metadata.validMinimum == 10.0);
    assert(result.metadata.validMaximum == 20.0);
}

void testDegenerateRangeIsDiagnosed() {
    const auto quantized = IntensityQuantizer::quantize(
        {42.25, 42.25, std::numeric_limits<double>::quiet_NaN()},
        IntensitySourceRange{-10.0, 100.0});
    assertSuccessfulResult(quantized);

    const auto& result = quantized.value();
    assert(result.status == IntensityQuantizationStatus::DegenerateRange);
    assert(result.invalidCount == 1U);
    const std::vector<std::uint16_t> expectedDegenerateValues{0U, 0U, 0U};
    assert(result.values == expectedDegenerateValues);
    assert(result.metadata.available);
    assert(result.metadata.sourceMinimum == -10.0);
    assert(result.metadata.sourceMaximum == 100.0);
    assert(result.metadata.validMinimum == 42.25);
    assert(result.metadata.validMaximum == 42.25);
}

void testNoValidValues() {
    const auto empty = IntensityQuantizer::quantize({});
    assertSuccessfulResult(empty);
    assert(empty.value().status == IntensityQuantizationStatus::NoValidValues);
    assert(empty.value().values.empty());
    assert(empty.value().invalidCount == 0U);
    assert(!empty.value().metadata.available);
    assert(empty.value().metadata.sourceMinimum == 0.0);
    assert(empty.value().metadata.sourceMaximum == 0.0);
    assert(empty.value().metadata.validMinimum == 0.0);
    assert(empty.value().metadata.validMaximum == 0.0);

    const auto allInvalid = IntensityQuantizer::quantize(
        {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()},
        IntensitySourceRange{-2.0, 3.0});
    assertSuccessfulResult(allInvalid);
    assert(allInvalid.value().status == IntensityQuantizationStatus::NoValidValues);
    const std::vector<std::uint16_t> expectedAllInvalidValues{0U, 0U};
    assert(allInvalid.value().values == expectedAllInvalidValues);
    assert(allInvalid.value().invalidCount == 2U);
    assert(!allInvalid.value().metadata.available);
    assert(allInvalid.value().metadata.sourceMinimum == -2.0);
    assert(allInvalid.value().metadata.sourceMaximum == 3.0);
    assert(allInvalid.value().metadata.validMinimum == 0.0);
    assert(allInvalid.value().metadata.validMaximum == 0.0);
}

void testDeclaredSourceRangeDoesNotChangeQuantizationBasis() {
    const auto quantized = IntensityQuantizer::quantize(
        {10.0, 20.0},
        IntensitySourceRange{0.0, 15.0});
    assertSuccessfulResult(quantized);

    const auto& result = quantized.value();
    const std::vector<std::uint16_t> expectedValues{0U, 65535U};
    assert(result.values == expectedValues);
    assert(result.metadata.sourceMinimum == 0.0);
    assert(result.metadata.sourceMaximum == 15.0);
    assert(result.metadata.validMinimum == 10.0);
    assert(result.metadata.validMaximum == 20.0);
}

void testInvalidDeclaredSourceRangesFail() {
    assertCorruptData(IntensityQuantizer::quantize(
        {1.0}, IntensitySourceRange{2.0, 1.0}));
    assertCorruptData(IntensityQuantizer::quantize(
        {1.0}, IntensitySourceRange{std::numeric_limits<double>::quiet_NaN(), 1.0}));
    assertCorruptData(IntensityQuantizer::quantize(
        {1.0}, IntensitySourceRange{0.0, std::numeric_limits<double>::infinity()}));
}

} // namespace

int main() {
    testIntegerRangeQuantization();
    testFloatingPointAndNegativeRangeQuantization();
    testInvalidValuesAreCountedAndQuantizedToZero();
    testDegenerateRangeIsDiagnosed();
    testNoValidValues();
    testDeclaredSourceRangeDoesNotChangeQuantizationBasis();
    testInvalidDeclaredSourceRangesFail();
    return 0;
}
