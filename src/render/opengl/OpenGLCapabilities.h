#pragma once

#include <dzc/Error.h>
#include <dzc/Result.h>

#include <cstdint>
#include <string>

namespace dzc::opengl {

enum class OpenGLCapabilityErrorCode : std::uint32_t {
    UnsupportedVersion = 1,
    UnsupportedProfile = 2,
    InvalidCapabilities = 3
};

struct OpenGLVersion final {
    std::int32_t major{0};
    std::int32_t minor{0};
    bool isCoreProfile{false};
};

struct PointSizeLimits final {
    float min{0.0F};
    float max{0.0F};
    float granularity{0.0F};
};

struct OpenGLBufferLimits final {
    std::uint32_t uniformBufferOffsetAlignment{0};
    std::uint32_t shaderStorageBufferOffsetAlignment{0};
    std::uint32_t maxBufferSize{0};
};

struct OpenGLDeviceInfo final {
    std::string vendor;
    std::string renderer;
    std::string driverVersion;
    std::string shadingLanguageVersion;
};

struct OpenGLCapabilitySnapshot final {
    OpenGLVersion version;
    PointSizeLimits pointSize;
    OpenGLBufferLimits buffers;
    OpenGLDeviceInfo device;
};

// Supplies values queried from an already-current OpenGL context. The
// interface deliberately uses project-owned types and contains no GL handles.
class IOpenGLCapabilityQueries {
public:
    virtual ~IOpenGLCapabilityQueries() = default;

    virtual OpenGLVersion queryVersion() const = 0;
    virtual PointSizeLimits queryPointSizeLimits() const = 0;
    virtual OpenGLBufferLimits queryBufferLimits() const = 0;
    virtual OpenGLDeviceInfo queryDeviceInfo() const = 0;
};

class OpenGLCapabilities final {
public:
    // Validates and captures capabilities from an already-current context.
    static Result<OpenGLCapabilitySnapshot> inspect(
        const IOpenGLCapabilityQueries& queries);

    // Queries the already-current context after GLAD has been loaded by the
    // caller. This function does not create a context or load platform symbols.
    static Result<OpenGLCapabilitySnapshot> queryCurrentContext();
};

} // namespace dzc::opengl
