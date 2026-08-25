#include "OpenGLCapabilities.h"

#include <glad/glad.h>

#include <cmath>
#include <limits>
#include <string>

namespace dzc::opengl {
namespace {

constexpr std::uint32_t errorCode(OpenGLCapabilityErrorCode code) noexcept {
    return static_cast<std::uint32_t>(code);
}

Result<OpenGLCapabilitySnapshot> failure(
    OpenGLCapabilityErrorCode code,
    const char* userMessage,
    const std::string& diagnosticMessage) {
    return Result<OpenGLCapabilitySnapshot>::failure(Error{
        ErrorDomain::OpenGL,
        errorCode(code),
        userMessage,
        diagnosticMessage,
        "OpenGLCapabilities::inspect"});
}

class GladCapabilityQueries final : public IOpenGLCapabilityQueries {
public:
    OpenGLVersion queryVersion() const override {
        GLint major = 0;
        GLint minor = 0;
        GLint profileMask = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
        return OpenGLVersion{
            major,
            minor,
            (profileMask & GL_CONTEXT_CORE_PROFILE_BIT) != 0};
    }

    PointSizeLimits queryPointSizeLimits() const override {
        GLfloat range[2]{0.0F, 0.0F};
        GLfloat granularity = 0.0F;
        glGetFloatv(GL_POINT_SIZE_RANGE, range);
        glGetFloatv(GL_POINT_SIZE_GRANULARITY, &granularity);
        return PointSizeLimits{range[0], range[1], granularity};
    }

    OpenGLBufferLimits queryBufferLimits() const override {
        GLint uniformAlignment = 0;
        GLint storageAlignment = 0;
        GLint maxBuffer = 0;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniformAlignment);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &storageAlignment);
        glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxBuffer);
        return OpenGLBufferLimits{
            toUnsigned(uniformAlignment),
            toUnsigned(storageAlignment),
            toUnsigned(maxBuffer)};
    }

    OpenGLDeviceInfo queryDeviceInfo() const override {
        return OpenGLDeviceInfo{
            stringValue(glGetString(GL_VENDOR)),
            stringValue(glGetString(GL_RENDERER)),
            stringValue(glGetString(GL_VERSION)),
            stringValue(glGetString(GL_SHADING_LANGUAGE_VERSION))};
    }

private:
    static std::uint32_t toUnsigned(GLint value) noexcept {
        return value > 0 ? static_cast<std::uint32_t>(value) : 0U;
    }

    static std::string stringValue(const GLubyte* value) {
        return value == nullptr
            ? std::string{}
            : std::string(reinterpret_cast<const char*>(value));
    }
};

bool isFinite(float value) noexcept {
    return std::isfinite(value) != 0;
}

bool hasValidVersion(const OpenGLVersion& version) noexcept {
    return version.major >= 0 && version.minor >= 0;
}

bool hasValidPointSizeLimits(const PointSizeLimits& limits) noexcept {
    return isFinite(limits.min) && isFinite(limits.max)
        && isFinite(limits.granularity)
        && limits.min >= 0.0F
        && limits.max >= limits.min
        && limits.granularity > 0.0F;
}

bool hasValidBufferLimits(const OpenGLBufferLimits& limits) noexcept {
    return limits.uniformBufferOffsetAlignment > 0
        && limits.shaderStorageBufferOffsetAlignment > 0
        && limits.maxBufferSize > 0;
}

bool hasValidDeviceInfo(const OpenGLDeviceInfo& device) noexcept {
    return !device.vendor.empty()
        && !device.renderer.empty()
        && !device.driverVersion.empty()
        && !device.shadingLanguageVersion.empty();
}

} // namespace

Result<OpenGLCapabilitySnapshot> OpenGLCapabilities::inspect(
    const IOpenGLCapabilityQueries& queries) {
    const OpenGLVersion version = queries.queryVersion();
    if (!hasValidVersion(version)) {
        return failure(
            OpenGLCapabilityErrorCode::InvalidCapabilities,
            "OpenGL capability values are invalid",
            "The reported OpenGL version contains a negative component");
    }
    if (version.major < 4 || (version.major == 4 && version.minor < 5)) {
        return failure(
            OpenGLCapabilityErrorCode::UnsupportedVersion,
            "OpenGL 4.5 Core or newer is required",
            "The current context reports OpenGL "
                + std::to_string(version.major) + "."
                + std::to_string(version.minor));
    }
    if (!version.isCoreProfile) {
        return failure(
            OpenGLCapabilityErrorCode::UnsupportedProfile,
            "An OpenGL Core Profile context is required",
            "The current context is not a Core Profile context");
    }

    const PointSizeLimits pointSize = queries.queryPointSizeLimits();
    const OpenGLBufferLimits buffers = queries.queryBufferLimits();
    const OpenGLDeviceInfo device = queries.queryDeviceInfo();
    if (!hasValidPointSizeLimits(pointSize)
        || !hasValidBufferLimits(buffers)
        || !hasValidDeviceInfo(device)) {
        return failure(
            OpenGLCapabilityErrorCode::InvalidCapabilities,
            "OpenGL capability values are invalid",
            "The current context returned an invalid point-size, buffer-limit, or device value");
    }

    return Result<OpenGLCapabilitySnapshot>::success(
        OpenGLCapabilitySnapshot{version, pointSize, buffers, device});
}

Result<OpenGLCapabilitySnapshot> OpenGLCapabilities::queryCurrentContext() {
    const GladCapabilityQueries queries;
    return inspect(queries);
}

} // namespace dzc::opengl
