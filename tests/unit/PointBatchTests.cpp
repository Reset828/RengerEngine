#include "data/chunk/PointBatch.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

constexpr std::uint32_t attributeMask(dzc::PointAttribute attribute) noexcept {
    return static_cast<std::uint32_t>(attribute);
}

constexpr std::uint32_t kPositionMask = attributeMask(dzc::PointAttribute::Position);
constexpr std::uint32_t kColorMask = attributeMask(dzc::PointAttribute::Color);
constexpr std::uint32_t kIntensityMask = attributeMask(dzc::PointAttribute::Intensity);

std::vector<glm::dvec3> samplePositions() {
    return {
        glm::dvec3{1.0, 2.0, 3.0},
        glm::dvec3{4.0, 5.0, 6.0}};
}

void assertValid(const dzc::PointBatch& batch) {
    const dzc::Result<void> result = batch.validate();
    assert(result.hasValue());
}

void assertCorruptData(const dzc::PointBatch& batch) {
    const dzc::Result<void> result = batch.validate();
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::DataFormat);
    assert(result.error().code == 2U);
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
}

void testValidSchemaCombinations() {
    dzc::PointBatch positionsOnly{};
    positionsOnly.schema.mask = kPositionMask;
    positionsOnly.positions = samplePositions();
    assertValid(positionsOnly);

    dzc::PointBatch positionsAndColor{};
    positionsAndColor.schema.mask = kPositionMask | kColorMask;
    positionsAndColor.positions = samplePositions();
    positionsAndColor.colorsRgba8 = {0x11223344U, 0x55667788U};
    assertValid(positionsAndColor);

    dzc::PointBatch positionsAndIntensity{};
    positionsAndIntensity.schema.mask = kPositionMask | kIntensityMask;
    positionsAndIntensity.positions = samplePositions();
    positionsAndIntensity.intensities = {7U, 19U};
    assertValid(positionsAndIntensity);

    dzc::PointBatch allAttributes{};
    allAttributes.schema.mask = kPositionMask | kColorMask | kIntensityMask;
    allAttributes.positions = samplePositions();
    allAttributes.colorsRgba8 = {0x01020304U, 0xA0B0C0D0U};
    allAttributes.intensities = {0U, 65535U};
    assertValid(allAttributes);
}

void testEmptyBatchIsValid() {
    dzc::PointBatch empty{};
    empty.schema.mask = kPositionMask | kColorMask | kIntensityMask;
    assertValid(empty);
}

void testPositionDeclarationIsRequired() {
    dzc::PointBatch missingPosition{};
    missingPosition.positions = samplePositions();
    assertCorruptData(missingPosition);
}

void testDeclaredColorLengthMustMatchPointCount() {
    dzc::PointBatch tooShort{};
    tooShort.schema.mask = kPositionMask | kColorMask;
    tooShort.positions = samplePositions();
    tooShort.colorsRgba8 = {0x11223344U};
    assertCorruptData(tooShort);

    dzc::PointBatch tooLong{};
    tooLong.schema.mask = kPositionMask | kColorMask;
    tooLong.positions = samplePositions();
    tooLong.colorsRgba8 = {0x11223344U, 0x55667788U, 0x99AABBCCU};
    assertCorruptData(tooLong);
}

void testDeclaredIntensityLengthMustMatchPointCount() {
    dzc::PointBatch tooShort{};
    tooShort.schema.mask = kPositionMask | kIntensityMask;
    tooShort.positions = samplePositions();
    tooShort.intensities = {1U};
    assertCorruptData(tooShort);

    dzc::PointBatch tooLong{};
    tooLong.schema.mask = kPositionMask | kIntensityMask;
    tooLong.positions = samplePositions();
    tooLong.intensities = {1U, 2U, 3U};
    assertCorruptData(tooLong);
}

void testUndeclaredStreamsMustBeEmpty() {
    dzc::PointBatch undeclaredColor{};
    undeclaredColor.schema.mask = kPositionMask;
    undeclaredColor.positions = samplePositions();
    undeclaredColor.colorsRgba8 = {0x11223344U, 0x55667788U};
    assertCorruptData(undeclaredColor);

    dzc::PointBatch undeclaredIntensity{};
    undeclaredIntensity.schema.mask = kPositionMask;
    undeclaredIntensity.positions = samplePositions();
    undeclaredIntensity.intensities = {1U, 2U};
    assertCorruptData(undeclaredIntensity);
}

void testUnknownSchemaBitsAreAllowed() {
    dzc::PointBatch batch{};
    batch.schema.mask = kPositionMask | (1U << 31U);
    batch.positions = samplePositions();
    assertValid(batch);
}

void testNonFinitePositionsDoNotAffectStructuralValidation() {
    dzc::PointBatch batch{};
    batch.schema.mask = kPositionMask;
    batch.positions = {
        glm::dvec3{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0},
        glm::dvec3{std::numeric_limits<double>::infinity(), 1.0, 1.0}};
    assert(std::isnan(batch.positions[0].x));
    assert(std::isinf(batch.positions[1].x));
    assertValid(batch);
}

} // namespace

int main() {
    testValidSchemaCombinations();
    testEmptyBatchIsValid();
    testPositionDeclarationIsRequired();
    testDeclaredColorLengthMustMatchPointCount();
    testDeclaredIntensityLengthMustMatchPointCount();
    testUndeclaredStreamsMustBeEmpty();
    testUnknownSchemaBitsAreAllowed();
    testNonFinitePositionsDoNotAffectStructuralValidation();
    return 0;
}
