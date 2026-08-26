#include "GlShaderProgram.h"

#include <glad/glad.h>

#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace dzc::opengl {
namespace {

Result<void> failure(
    GlShaderErrorCode code,
    std::string userMessage,
    std::string diagnosticMessage,
    std::string context) {
    return Result<void>::failure(Error{
        ErrorDomain::OpenGL,
        static_cast<std::uint32_t>(code),
        std::move(userMessage),
        std::move(diagnosticMessage),
        std::move(context)});
}

bool tokenCanAccess(const GlContextThreadToken& token,
                    const std::thread::id& ownerThread) noexcept {
    return token.isCurrentThread() && ownerThread == std::this_thread::get_id();
}

std::string sourceLabel(std::string_view name) {
    return name.empty() ? "unnamed shader" : std::string(name);
}

class GladShaderOperations final : public IGlShaderOperations {
public:
    bool createShader(GlShaderStage stage, std::uint32_t& id) const override {
        if (glCreateShader == nullptr) return false;
        const GLenum glStage = stage == GlShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
        id = static_cast<std::uint32_t>(glCreateShader(glStage));
        return id != 0;
    }

    bool setShaderSource(std::uint32_t id, std::string_view source) const override {
        if (id == 0 || glShaderSource == nullptr || source.size() > static_cast<std::size_t>(std::numeric_limits<GLint>::max())) return false;
        const GLchar* text = reinterpret_cast<const GLchar*>(source.data());
        const GLint length = static_cast<GLint>(source.size());
        glShaderSource(static_cast<GLuint>(id), 1, &text, &length);
        return true;
    }

    bool compileShader(std::uint32_t id, std::string& log) const override {
        if (id == 0 || glCompileShader == nullptr || glGetShaderiv == nullptr || glGetShaderInfoLog == nullptr) return false;
        glCompileShader(static_cast<GLuint>(id));
        GLint status = GL_FALSE;
        glGetShaderiv(static_cast<GLuint>(id), GL_COMPILE_STATUS, &status);
        GLint length = 0;
        glGetShaderiv(static_cast<GLuint>(id), GL_INFO_LOG_LENGTH, &length);
        if (length > 0) {
            std::string buffer(static_cast<std::size_t>(length), '\0');
            GLsizei written = 0;
            glGetShaderInfoLog(static_cast<GLuint>(id), length, &written, buffer.data());
            if (written >= 0 && static_cast<std::size_t>(written) <= buffer.size()) buffer.resize(static_cast<std::size_t>(written));
            log = std::move(buffer);
        } else {
            log.clear();
        }
        return status == GL_TRUE;
    }

    bool createProgram(std::uint32_t& id) const override {
        if (glCreateProgram == nullptr) return false;
        id = static_cast<std::uint32_t>(glCreateProgram());
        return id != 0;
    }

    bool attachShader(std::uint32_t programId, std::uint32_t shaderId) const override {
        if (programId == 0 || shaderId == 0 || glAttachShader == nullptr) return false;
        glAttachShader(static_cast<GLuint>(programId), static_cast<GLuint>(shaderId));
        return true;
    }

    bool linkProgram(std::uint32_t programId, std::string& log) const override {
        if (programId == 0 || glLinkProgram == nullptr || glGetProgramiv == nullptr || glGetProgramInfoLog == nullptr) return false;
        glLinkProgram(static_cast<GLuint>(programId));
        GLint status = GL_FALSE;
        glGetProgramiv(static_cast<GLuint>(programId), GL_LINK_STATUS, &status);
        GLint length = 0;
        glGetProgramiv(static_cast<GLuint>(programId), GL_INFO_LOG_LENGTH, &length);
        if (length > 0) {
            std::string buffer(static_cast<std::size_t>(length), '\0');
            GLsizei written = 0;
            glGetProgramInfoLog(static_cast<GLuint>(programId), length, &written, buffer.data());
            if (written >= 0 && static_cast<std::size_t>(written) <= buffer.size()) buffer.resize(static_cast<std::size_t>(written));
            log = std::move(buffer);
        } else {
            log.clear();
        }
        return status == GL_TRUE;
    }

    bool deleteShader(std::uint32_t id) const override {
        if (id == 0) return true;
        if (glDeleteShader == nullptr) return false;
        glDeleteShader(static_cast<GLuint>(id));
        return true;
    }

    bool deleteProgram(std::uint32_t id) const override {
        if (id == 0) return true;
        if (glDeleteProgram == nullptr) return false;
        glDeleteProgram(static_cast<GLuint>(id));
        return true;
    }
};

std::string readSourceFile(const std::filesystem::path& path, bool& opened) {
    opened = false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    opened = true;
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

void appendCleanupFailure(std::string& diagnostic, const char* resource) {
    diagnostic += "; cleanup failed for ";
    diagnostic += resource;
}

} // namespace

std::shared_ptr<const IGlShaderOperations> makeDefaultGlShaderOperations() {
    static const std::shared_ptr<const IGlShaderOperations> operations = std::make_shared<const GladShaderOperations>();
    return operations;
}

GlShaderProgram::~GlShaderProgram() noexcept {
    releaseNoexcept();
}

GlShaderProgram::GlShaderProgram(GlShaderProgram&& other) noexcept
    : mVertexShaderId(other.mVertexShaderId),
      mFragmentShaderId(other.mFragmentShaderId),
      mProgramId(other.mProgramId),
      mOperations(std::move(other.mOperations)),
      mOwnerThread(other.mOwnerThread),
      mReleasePending(other.mReleasePending) {
    other.mVertexShaderId = 0;
    other.mFragmentShaderId = 0;
    other.mProgramId = 0;
    other.mOwnerThread = std::thread::id{};
    other.mReleasePending = false;
}

GlShaderProgram& GlShaderProgram::operator=(GlShaderProgram&& other) noexcept {
    if (this != &other) {
        releaseNoexcept();
        mVertexShaderId = other.mVertexShaderId;
        mFragmentShaderId = other.mFragmentShaderId;
        mProgramId = other.mProgramId;
        mOperations = std::move(other.mOperations);
        mOwnerThread = other.mOwnerThread;
        mReleasePending = other.mReleasePending;
        other.mVertexShaderId = 0;
        other.mFragmentShaderId = 0;
        other.mProgramId = 0;
        other.mOwnerThread = std::thread::id{};
        other.mReleasePending = false;
    }
    return *this;
}

dzc::Result<void> GlShaderProgram::create(
    const GlContextThreadToken& token,
    std::string_view vertexSource,
    std::string_view fragmentSource,
    std::string_view vertexSourceName,
    std::string_view fragmentSourceName,
    std::shared_ptr<const IGlShaderOperations> operations) {
    if (!token.isCurrentThread()) {
        return failure(GlShaderErrorCode::InvalidThreadToken,
            "The OpenGL shader token belongs to another thread",
            "GlShaderProgram::create requires a token bound to the calling thread",
            "shader create: thread token");
    }
    if (isValid() || mVertexShaderId != 0 || mFragmentShaderId != 0) {
        return failure(GlShaderErrorCode::OperationFailed,
            "The shader program already owns resources",
            "GlShaderProgram::create must be called on an empty object",
            "shader create");
    }
    if (vertexSource.empty()) {
        return failure(GlShaderErrorCode::EmptySource,
            "The Vertex shader source is empty",
            "No GLSL source bytes were supplied",
            "Vertex compilation: " + sourceLabel(vertexSourceName));
    }
    if (fragmentSource.empty()) {
        return failure(GlShaderErrorCode::EmptySource,
            "The Fragment shader source is empty",
            "No GLSL source bytes were supplied",
            "Fragment compilation: " + sourceLabel(fragmentSourceName));
    }
    if (!operations) operations = makeDefaultGlShaderOperations();
    if (!operations) {
        return failure(GlShaderErrorCode::OperationFailed,
            "OpenGL shader operations are unavailable",
            "No shader operation table was provided",
            "shader create");
    }

    std::uint32_t vertexId = 0;
    std::uint32_t fragmentId = 0;
    std::uint32_t programId = 0;
    std::string log;
    std::string cleanupDiagnostic;
    auto cleanup = [&]() {
        if (programId != 0) {
            try { if (!operations->deleteProgram(programId)) appendCleanupFailure(cleanupDiagnostic, "program"); }
            catch (...) { appendCleanupFailure(cleanupDiagnostic, "program"); }
            programId = 0;
        }
        if (fragmentId != 0) {
            try { if (!operations->deleteShader(fragmentId)) appendCleanupFailure(cleanupDiagnostic, "fragment shader"); }
            catch (...) { appendCleanupFailure(cleanupDiagnostic, "fragment shader"); }
            fragmentId = 0;
        }
        if (vertexId != 0) {
            try { if (!operations->deleteShader(vertexId)) appendCleanupFailure(cleanupDiagnostic, "vertex shader"); }
            catch (...) { appendCleanupFailure(cleanupDiagnostic, "vertex shader"); }
            vertexId = 0;
        }
    };
    auto failed = [&](GlShaderErrorCode code, std::string user, std::string diagnostic, std::string context) {
        cleanup();
        if (!cleanupDiagnostic.empty()) diagnostic += " (" + cleanupDiagnostic + ")";
        return failure(code, std::move(user), std::move(diagnostic), std::move(context));
    };

    try {
        if (!operations->createShader(GlShaderStage::Vertex, vertexId) || vertexId == 0) {
            return failed(GlShaderErrorCode::OperationFailed, "Vertex shader creation failed", "Shader operation returned failure", "Vertex creation: " + sourceLabel(vertexSourceName));
        }
        if (!operations->setShaderSource(vertexId, vertexSource)) {
            return failed(GlShaderErrorCode::OperationFailed, "Vertex shader source setup failed", "Shader operation returned failure", "Vertex source setup: " + sourceLabel(vertexSourceName));
        }
        log.clear();
        if (!operations->compileShader(vertexId, log)) {
            const std::string diagnostic = "Vertex compilation log: " + (log.empty() ? std::string("<empty>") : log);
            return failed(GlShaderErrorCode::VertexCompilationFailed, "Vertex shader compilation failed", diagnostic, "Vertex compilation: " + sourceLabel(vertexSourceName));
        }
        if (!operations->createShader(GlShaderStage::Fragment, fragmentId) || fragmentId == 0) {
            return failed(GlShaderErrorCode::OperationFailed, "Fragment shader creation failed", "Shader operation returned failure", "Fragment creation: " + sourceLabel(fragmentSourceName));
        }
        if (!operations->setShaderSource(fragmentId, fragmentSource)) {
            return failed(GlShaderErrorCode::OperationFailed, "Fragment shader source setup failed", "Shader operation returned failure", "Fragment source setup: " + sourceLabel(fragmentSourceName));
        }
        log.clear();
        if (!operations->compileShader(fragmentId, log)) {
            const std::string diagnostic = "Fragment compilation log: " + (log.empty() ? std::string("<empty>") : log);
            return failed(GlShaderErrorCode::FragmentCompilationFailed, "Fragment shader compilation failed", diagnostic, "Fragment compilation: " + sourceLabel(fragmentSourceName));
        }
        if (!operations->createProgram(programId) || programId == 0) {
            return failed(GlShaderErrorCode::OperationFailed, "Shader program creation failed", "Program operation returned failure", "Link setup: " + sourceLabel(vertexSourceName) + ", " + sourceLabel(fragmentSourceName));
        }
        if (!operations->attachShader(programId, vertexId)) {
            return failed(GlShaderErrorCode::OperationFailed, "Vertex shader attachment failed", "Program operation returned failure", "Link setup: " + sourceLabel(vertexSourceName));
        }
        if (!operations->attachShader(programId, fragmentId)) {
            return failed(GlShaderErrorCode::OperationFailed, "Fragment shader attachment failed", "Program operation returned failure", "Link setup: " + sourceLabel(fragmentSourceName));
        }
        log.clear();
        if (!operations->linkProgram(programId, log)) {
            const std::string diagnostic = "Link log: " + (log.empty() ? std::string("<empty>") : log);
            return failed(GlShaderErrorCode::LinkFailed, "Shader program link failed", diagnostic, "Link: " + sourceLabel(vertexSourceName) + ", " + sourceLabel(fragmentSourceName));
        }
    } catch (const std::exception& exception) {
        return failed(GlShaderErrorCode::OperationFailed, "OpenGL shader operation threw an exception", exception.what(), "shader create");
    } catch (...) {
        return failed(GlShaderErrorCode::OperationFailed, "OpenGL shader operation threw an unknown exception", "Unknown exception from shader operation table", "shader create");
    }

    mVertexShaderId = vertexId;
    mFragmentShaderId = fragmentId;
    mProgramId = programId;
    mOperations = std::move(operations);
    mOwnerThread = std::this_thread::get_id();
    mReleasePending = false;
    return Result<void>::success();
}

dzc::Result<void> GlShaderProgram::createFromFiles(
    const GlContextThreadToken& token,
    const std::filesystem::path& vertexPath,
    const std::filesystem::path& fragmentPath,
    std::shared_ptr<const IGlShaderOperations> operations) {
    if (!token.isCurrentThread()) {
        return failure(GlShaderErrorCode::InvalidThreadToken,
            "The OpenGL shader token belongs to another thread",
            "GlShaderProgram::createFromFiles requires a token bound to the calling thread",
            "shader source read: thread token");
    }

    bool vertexOpened = false;
    bool fragmentOpened = false;
    const std::string vertexSource = readSourceFile(vertexPath, vertexOpened);
    if (!vertexOpened) {
        return failure(GlShaderErrorCode::SourceReadFailed, "Vertex shader source could not be read", "Unable to open shader file", "Vertex source read: " + vertexPath.string());
    }
    const std::string fragmentSource = readSourceFile(fragmentPath, fragmentOpened);
    if (!fragmentOpened) {
        return failure(GlShaderErrorCode::SourceReadFailed, "Fragment shader source could not be read", "Unable to open shader file", "Fragment source read: " + fragmentPath.string());
    }
    if (vertexSource.empty()) {
        return failure(GlShaderErrorCode::EmptySource, "The Vertex shader file is empty", "The file contains no GLSL source bytes", "Vertex source read: " + vertexPath.string());
    }
    if (fragmentSource.empty()) {
        return failure(GlShaderErrorCode::EmptySource, "The Fragment shader file is empty", "The file contains no GLSL source bytes", "Fragment source read: " + fragmentPath.string());
    }
    return create(token, vertexSource, fragmentSource, vertexPath.string(), fragmentPath.string(), std::move(operations));
}

dzc::Result<void> GlShaderProgram::reset(const GlContextThreadToken& token) {
    if (!isValid() && mVertexShaderId == 0 && mFragmentShaderId == 0) {
        mOperations.reset();
        mOwnerThread = std::thread::id{};
        mReleasePending = false;
        return Result<void>::success();
    }
    if (!tokenCanAccess(token, mOwnerThread)) {
        mReleasePending = true;
        return failure(GlShaderErrorCode::InvalidThreadToken, "The OpenGL shader must be released on its creation thread", "GlShaderProgram::reset received a token for another thread", "shader reset: thread token");
    }
    if (!mOperations) {
        mReleasePending = true;
        return failure(GlShaderErrorCode::OperationFailed, "OpenGL shader operations are unavailable", "The resource has no operation table for deletion", "shader reset");
    }

    bool failedOperation = false;
    try {
        if (mProgramId != 0) {
            if (mOperations->deleteProgram(mProgramId)) mProgramId = 0;
            else failedOperation = true;
        }
        if (mFragmentShaderId != 0) {
            if (mOperations->deleteShader(mFragmentShaderId)) mFragmentShaderId = 0;
            else failedOperation = true;
        }
        if (mVertexShaderId != 0) {
            if (mOperations->deleteShader(mVertexShaderId)) mVertexShaderId = 0;
            else failedOperation = true;
        }
    } catch (...) {
        failedOperation = true;
    }
    if (failedOperation) {
        mReleasePending = true;
        return failure(GlShaderErrorCode::OperationFailed, "One or more OpenGL shader resources could not be deleted", "Program/shader deletion failed; remaining resources stay owned by this object", "shader reset: delete resources");
    }
    mOperations.reset();
    mOwnerThread = std::thread::id{};
    mReleasePending = false;
    return Result<void>::success();
}

void GlShaderProgram::releaseNoexcept() noexcept {
    if (mProgramId == 0 && mFragmentShaderId == 0 && mVertexShaderId == 0) return;
    if (!mOperations || mOwnerThread != std::this_thread::get_id()) {
        mReleasePending = true;
        return;
    }
    try {
        if (mProgramId != 0 && mOperations->deleteProgram(mProgramId)) mProgramId = 0;
        if (mFragmentShaderId != 0 && mOperations->deleteShader(mFragmentShaderId)) mFragmentShaderId = 0;
        if (mVertexShaderId != 0 && mOperations->deleteShader(mVertexShaderId)) mVertexShaderId = 0;
        if (mProgramId == 0 && mFragmentShaderId == 0 && mVertexShaderId == 0) {
            mOperations.reset();
            mOwnerThread = std::thread::id{};
            mReleasePending = false;
        } else {
            mReleasePending = true;
        }
    } catch (...) {
        mReleasePending = true;
    }
}

} // namespace dzc::opengl
