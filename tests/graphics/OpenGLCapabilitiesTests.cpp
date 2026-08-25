#include "OpenGLCapabilities.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

class FakeCapabilityQueries final : public dzc::opengl::IOpenGLCapabilityQueries {
public:
    dzc::opengl::OpenGLVersion version{4, 5, true};
    dzc::opengl::PointSizeLimits pointSize{1.0F, 64.0F, 1.0F};
    dzc::opengl::OpenGLBufferLimits buffers{256U, 256U, 134217728U};
    dzc::opengl::OpenGLDeviceInfo device{
        "Fake Vendor", "Fake Renderer", "Fake Driver 1.0", "Fake GLSL 4.50"};

    dzc::opengl::OpenGLVersion queryVersion() const override { return version; }
    dzc::opengl::PointSizeLimits queryPointSizeLimits() const override { return pointSize; }
    dzc::opengl::OpenGLBufferLimits queryBufferLimits() const override { return buffers; }
    dzc::opengl::OpenGLDeviceInfo queryDeviceInfo() const override { return device; }
};

void testOpenGl45Core() {
    const FakeCapabilityQueries fake;
    const auto result = dzc::opengl::OpenGLCapabilities::inspect(fake);
    assert(result.hasValue());
    assert(result.value().version.major == 4);
    assert(result.value().version.minor == 5);
    assert(result.value().version.isCoreProfile);
}

void testOpenGl46Core() {
    FakeCapabilityQueries fake;
    fake.version = {4, 6, true};
    const auto result = dzc::opengl::OpenGLCapabilities::inspect(fake);
    assert(result.hasValue());
    assert(result.value().version.major == 4);
    assert(result.value().version.minor == 6);
}

void testOpenGl44IsRejected() {
    FakeCapabilityQueries fake;
    fake.version = {4, 4, true};
    const auto result = dzc::opengl::OpenGLCapabilities::inspect(fake);
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::OpenGL);
    assert(result.error().code == static_cast<std::uint32_t>(
        dzc::opengl::OpenGLCapabilityErrorCode::UnsupportedVersion));
}

void testNonCoreProfileIsRejected() {
    FakeCapabilityQueries fake;
    fake.version = {4, 5, false};
    const auto result = dzc::opengl::OpenGLCapabilities::inspect(fake);
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::OpenGL);
    assert(result.error().code == static_cast<std::uint32_t>(
        dzc::opengl::OpenGLCapabilityErrorCode::UnsupportedProfile));
}

void testCapabilitiesAreCaptured() {
    FakeCapabilityQueries fake;
    fake.pointSize = {0.5F, 128.0F, 0.5F};
    fake.buffers = {256U, 512U, 268435456U};
    fake.device = {"Vendor", "Renderer", "Driver", "GLSL"};
    const auto result = dzc::opengl::OpenGLCapabilities::inspect(fake);
    assert(result.hasValue());
    const auto& snapshot = result.value();
    assert(snapshot.pointSize.min == 0.5F);
    assert(snapshot.pointSize.max == 128.0F);
    assert(snapshot.pointSize.granularity == 0.5F);
    assert(snapshot.buffers.uniformBufferOffsetAlignment == 256U);
    assert(snapshot.buffers.shaderStorageBufferOffsetAlignment == 512U);
    assert(snapshot.buffers.maxBufferSize == 268435456U);
    assert(snapshot.device.vendor == "Vendor");
    assert(snapshot.device.renderer == "Renderer");
    assert(snapshot.device.driverVersion == "Driver");
    assert(snapshot.device.shadingLanguageVersion == "GLSL");
}

void testInvalidCapabilitiesAreRejected() {
    FakeCapabilityQueries fake;
    fake.pointSize.max = 0.0F;
    const auto result = dzc::opengl::OpenGLCapabilities::inspect(fake);
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::OpenGL);
    assert(result.error().code == static_cast<std::uint32_t>(
        dzc::opengl::OpenGLCapabilityErrorCode::InvalidCapabilities));
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--real-context") {
        std::cout << "SKIPPED: real OpenGL Context infrastructure is provided by GL-007.\n";
        return 77;
    }

    testOpenGl45Core();
    testOpenGl46Core();
    testOpenGl44IsRejected();
    testNonCoreProfileIsRejected();
    testCapabilitiesAreCaptured();
    testInvalidCapabilitiesAreRejected();
    return 0;
}
