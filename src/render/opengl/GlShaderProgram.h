#pragma once

#include "GlResource.h"

#include <dzc/Result.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace dzc::opengl {

enum class GlShaderStage : std::uint8_t {
    Vertex,
    Fragment
};

enum class GlShaderErrorCode : std::uint32_t {
    SourceReadFailed = 1,
    EmptySource = 2,
    VertexCompilationFailed = 3,
    FragmentCompilationFailed = 4,
    LinkFailed = 5,
    InvalidThreadToken = 6,
    OperationFailed = 7
};

class IGlShaderOperations {
public:
    virtual ~IGlShaderOperations() = default;

    virtual bool createShader(GlShaderStage stage, std::uint32_t& id) const = 0;
    virtual bool setShaderSource(std::uint32_t id, std::string_view source) const = 0;
    virtual bool compileShader(std::uint32_t id, std::string& log) const = 0;
    virtual bool createProgram(std::uint32_t& id) const = 0;
    virtual bool attachShader(std::uint32_t programId, std::uint32_t shaderId) const = 0;
    virtual bool linkProgram(std::uint32_t programId, std::string& log) const = 0;
    virtual bool deleteShader(std::uint32_t id) const = 0;
    virtual bool deleteProgram(std::uint32_t id) const = 0;
};

std::shared_ptr<const IGlShaderOperations> makeDefaultGlShaderOperations();

class GlShaderProgram final {
public:
    GlShaderProgram() noexcept = default;
    ~GlShaderProgram() noexcept;

    GlShaderProgram(const GlShaderProgram&) = delete;
    GlShaderProgram& operator=(const GlShaderProgram&) = delete;
    GlShaderProgram(GlShaderProgram&& other) noexcept;
    GlShaderProgram& operator=(GlShaderProgram&& other) noexcept;

    dzc::Result<void> create(
        const GlContextThreadToken& token,
        std::string_view vertexSource,
        std::string_view fragmentSource,
        std::string_view vertexSourceName = "vertex shader",
        std::string_view fragmentSourceName = "fragment shader",
        std::shared_ptr<const IGlShaderOperations> operations = {});

    dzc::Result<void> createFromFiles(
        const GlContextThreadToken& token,
        const std::filesystem::path& vertexPath,
        const std::filesystem::path& fragmentPath,
        std::shared_ptr<const IGlShaderOperations> operations = {});

    dzc::Result<void> reset(const GlContextThreadToken& token);

    bool isValid() const noexcept { return mProgramId != 0; }
    std::uint32_t id() const noexcept { return mProgramId; }
    bool releasePending() const noexcept { return mReleasePending; }

private:
    void releaseNoexcept() noexcept;

    std::uint32_t mVertexShaderId{0};
    std::uint32_t mFragmentShaderId{0};
    std::uint32_t mProgramId{0};
    std::shared_ptr<const IGlShaderOperations> mOperations;
    std::thread::id mOwnerThread;
    bool mReleasePending{false};
};

} // namespace dzc::opengl
