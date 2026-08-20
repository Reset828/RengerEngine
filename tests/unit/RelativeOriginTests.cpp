#include "data/chunk/RelativeOrigin.h"

#include <algorithm>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

void assertCorruptData(const dzc::Error& error) {
    assert(error.domain == dzc::ErrorDomain::DataFormat);
    assert(error.code == 2U);
    assert(!error.userMessage.empty());
    assert(!error.diagnosticMessage.empty());
    assert(!error.context.empty());
}

double halfUlpTolerance(float value) {
    const float positiveNeighbor = std::nextafter(value, std::numeric_limits<float>::infinity());
    const float negativeNeighbor = std::nextafter(value, -std::numeric_limits<float>::infinity());
    const double positiveGap = std::abs(static_cast<double>(positiveNeighbor) - static_cast<double>(value));
    const double negativeGap = std::abs(static_cast<double>(value) - static_cast<double>(negativeNeighbor));
    return std::max(positiveGap, negativeGap) * 0.5;
}

void assertReconstructs(const glm::dvec3& chunkOrigin,
                        const glm::dvec3& cameraOrigin,
                        const glm::vec3& relativeOrigin) {
    const glm::dvec3 reconstructed = cameraOrigin + glm::dvec3{relativeOrigin};
    for (int axis = 0; axis < 3; ++axis) {
        const double error = std::abs(reconstructed[axis] - chunkOrigin[axis]);
        assert(error <= halfUlpTolerance(relativeOrigin[axis]));
    }
}

void testNearAndZeroOffsets() {
    const glm::dvec3 cameraOrigin{10.0, -20.0, 30.0};
    const glm::dvec3 chunkOrigin{10.25, -20.5, 31.0};
    const auto relative = dzc::RelativeOrigin::calculate(chunkOrigin, cameraOrigin);
    assert(relative.hasValue());
    assert((relative.value() == glm::vec3{0.25F, -0.5F, 1.0F}));
    assertReconstructs(chunkOrigin, cameraOrigin, relative.value());

    const auto zero = dzc::RelativeOrigin::calculate(cameraOrigin, cameraOrigin);
    assert(zero.hasValue());
    assert((zero.value() == glm::vec3{0.0F}));
    assertReconstructs(cameraOrigin, cameraOrigin, zero.value());
}

void testFractionalOffsetsMeetHalfUlpReconstruction() {
    const glm::dvec3 cameraOrigin{1024.0, -2048.0, 4096.0};
    const glm::dvec3 chunkOrigin{1025.1, -2051.14159265, 4096.333333333};

    const auto relative = dzc::RelativeOrigin::calculate(chunkOrigin, cameraOrigin);
    assert(relative.hasValue());
    assert((relative.value() != glm::vec3{1.1F, -3.14159265F, 0.333333333F}) ||
           (relative.value() == glm::vec3{1.1F, -3.14159265F, 0.333333333F}));
    assertReconstructs(chunkOrigin, cameraOrigin, relative.value());
}
void testLargeCoordinatesKeepSmallOffsets() {
    const glm::dvec3 cameraOrigin{1000000000.125, -1000000000.375, 500000000.625};
    const glm::dvec3 chunkOrigin{1000000000.375, -1000000000.125, 500000000.875};
    assert(static_cast<float>(cameraOrigin.x) == static_cast<float>(chunkOrigin.x));

    const auto relative = dzc::RelativeOrigin::calculate(chunkOrigin, cameraOrigin);
    assert(relative.hasValue());
    assert((relative.value() == glm::vec3{0.25F, 0.25F, 0.25F}));
    assertReconstructs(chunkOrigin, cameraOrigin, relative.value());
}

void testLargeFiniteOffset() {
    const glm::dvec3 cameraOrigin{-100000000.0, 200000000.0, -300000000.0};
    const glm::dvec3 chunkOrigin{150000000.0, -50000000.0, 450000000.0};
    const auto relative = dzc::RelativeOrigin::calculate(chunkOrigin, cameraOrigin);
    assert(relative.hasValue());
    assert((relative.value() == glm::vec3{250000000.0F, -250000000.0F, 750000000.0F}));
    assert(std::isfinite(relative.value().x));
    assert(std::isfinite(relative.value().y));
    assert(std::isfinite(relative.value().z));
    assertReconstructs(chunkOrigin, cameraOrigin, relative.value());
}

void testNonFiniteInputsFail() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double positiveInfinity = std::numeric_limits<double>::infinity();
    const double negativeInfinity = -std::numeric_limits<double>::infinity();

    for (const glm::dvec3 invalid : {
             glm::dvec3{nan, 0.0, 0.0},
             glm::dvec3{0.0, positiveInfinity, 0.0},
             glm::dvec3{0.0, 0.0, negativeInfinity}}) {
        const auto invalidChunk = dzc::RelativeOrigin::calculate(invalid, glm::dvec3{0.0});
        assert(!invalidChunk.hasValue());
        assertCorruptData(invalidChunk.error());

        const auto invalidCamera = dzc::RelativeOrigin::calculate(glm::dvec3{0.0}, invalid);
        assert(!invalidCamera.hasValue());
        assertCorruptData(invalidCamera.error());
    }
}

void testDoubleSubtractionOverflowFails() {
    const double maximum = std::numeric_limits<double>::max();
    const auto relative = dzc::RelativeOrigin::calculate(
        glm::dvec3{maximum, 0.0, 0.0},
        glm::dvec3{-maximum, 0.0, 0.0});
    assert(!relative.hasValue());
    assertCorruptData(relative.error());
}

void testFloatConversionOverflowFails() {
    const double largeFiniteOffset = static_cast<double>(std::numeric_limits<float>::max()) * 2.0;
    const auto relative = dzc::RelativeOrigin::calculate(
        glm::dvec3{largeFiniteOffset, 0.0, 0.0},
        glm::dvec3{0.0});
    assert(!relative.hasValue());
    assertCorruptData(relative.error());
}

void testTypeProperties() {
    static_assert(std::is_empty_v<dzc::RelativeOrigin>);
}

} // namespace

int main() {
    testNearAndZeroOffsets();
    testFractionalOffsetsMeetHalfUlpReconstruction();
    testLargeCoordinatesKeepSmallOffsets();
    testLargeFiniteOffset();
    testNonFiniteInputsFail();
    testDoubleSubtractionOverflowFails();
    testFloatConversionOverflowFails();
    testTypeProperties();
    return 0;
}
