#include "OpenGLBackend.h"

#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace dzc::opengl {
namespace {

std::uint32_t code(OpenGLBackendErrorCode value) noexcept {
    return static_cast<std::uint32_t>(value);
}

Error makeError(
    OpenGLBackendErrorCode errorCode,
    const char* userMessage,
    std::string diagnostic,
    const char* context) {
    return Error{
        ErrorDomain::OpenGL,
        code(errorCode),
        userMessage,
        std::move(diagnostic),
        context};
}

bool finitePositive(float value) noexcept {
    return std::isfinite(value) != 0 && value > 0.0F;
}

bool validSize(const RenderSize& size) noexcept {
    return size.width > 0U && size.height > 0U && finitePositive(size.devicePixelRatio);
}

std::string chunkContext(const char* operation, ChunkId id) {
    return std::string(operation) + " (ChunkId=" + std::to_string(id.value) + ")";
}

} // namespace

OpenGLBackend::OpenGLBackend() noexcept = default;

OpenGLBackend::OpenGLBackend(
    std::shared_ptr<const dzc::IRenderContextOperations> contextOperations,
    std::shared_ptr<const IOpenGLCapabilityQueries> capabilityQueries,
    std::shared_ptr<const IGlChunkUploadOperations> chunkOperations)
    : m_contextOperations(std::move(contextOperations)),
      m_capabilityQueries(std::move(capabilityQueries)),
      m_chunkOperations(std::move(chunkOperations)) {}

OpenGLBackend::~OpenGLBackend() noexcept {
    shutdownNoexcept();
}

OpenGLBackend::OpenGLBackend(OpenGLBackend&& other) noexcept
    : m_state(other.m_state),
      m_contextOperations(std::move(other.m_contextOperations)),
      m_capabilityQueries(std::move(other.m_capabilityQueries)),
      m_chunkOperations(std::move(other.m_chunkOperations)),
      m_chunks(std::move(other.m_chunks)),
      m_currentFrame(std::move(other.m_currentFrame)),
      m_renderSize(other.m_renderSize),
      m_capabilities(std::move(other.m_capabilities)),
      m_lastError(std::move(other.m_lastError)),
      m_ownerThread(other.m_ownerThread) {
    other.resetMovedFrom();
}

OpenGLBackend& OpenGLBackend::operator=(OpenGLBackend&& other) noexcept {
    if (this != &other) {
        shutdownNoexcept();
        m_state = other.m_state;
        m_contextOperations = std::move(other.m_contextOperations);
        m_capabilityQueries = std::move(other.m_capabilityQueries);
        m_chunkOperations = std::move(other.m_chunkOperations);
        m_chunks = std::move(other.m_chunks);
        m_currentFrame = std::move(other.m_currentFrame);
        m_renderSize = other.m_renderSize;
        m_capabilities = std::move(other.m_capabilities);
        m_lastError = std::move(other.m_lastError);
        m_ownerThread = other.m_ownerThread;
        other.resetMovedFrom();
    }
    return *this;
}

bool OpenGLBackend::hasChunk(dzc::ChunkId chunkId) const noexcept {
    return m_chunks.find(chunkId) != m_chunks.end();
}

Result<void> OpenGLBackend::makeFailure(
    OpenGLBackendErrorCode errorCode,
    const char* userMessage,
    std::string diagnostic,
    const char* context) const {
    return Result<void>::failure(makeError(errorCode, userMessage, std::move(diagnostic), context));
}

void OpenGLBackend::recordError(Error error) noexcept {
    try {
        m_lastError = std::move(error);
    } catch (...) {
        // A noexcept lifecycle method must not propagate allocation failures.
    }
}

void OpenGLBackend::clearLastError() noexcept {
    m_lastError.reset();
}

Result<void> OpenGLBackend::checkStateForInit() const {
    if (m_state == OpenGLBackendState::Initialized) {
        return makeFailure(
            OpenGLBackendErrorCode::InvalidState,
            "OpenGL backend is already initialized",
            "init cannot be called while the backend is Initialized",
            "OpenGLBackend::init");
    }
    return Result<void>::success();
}

Result<void> OpenGLBackend::checkThreadAndContext(const char* operation) const {
    const GlContextThreadToken token = GlContextThreadToken::current();
    if (m_ownerThread != std::this_thread::get_id() || !token.isCurrentThread()) {
        return makeFailure(
            OpenGLBackendErrorCode::InvalidThreadToken,
            "OpenGL backend called from the wrong thread",
            "The operation must run on the thread that initialized the backend",
            operation);
    }
    if (m_contextOperations == nullptr || !m_contextOperations->isCurrent()) {
        return makeFailure(
            OpenGLBackendErrorCode::ContextUnavailable,
            "The OpenGL context is not current",
            "The backend operation requires an active context on the calling thread",
            operation);
    }
    if (!m_contextOperations->functionsLoaded()) {
        return makeFailure(
            OpenGLBackendErrorCode::FunctionsUnavailable,
            "OpenGL functions are not loaded",
            "The external GLAD loader has not reported loaded functions",
            operation);
    }
    return Result<void>::success();
}

Result<void> OpenGLBackend::checkReady(const char* operation) const {
    if (m_state != OpenGLBackendState::Initialized) {
        return makeFailure(
            OpenGLBackendErrorCode::InvalidState,
            "OpenGL backend is not initialized",
            "The requested operation requires the Initialized backend state",
            operation);
    }
    return checkThreadAndContext(operation);
}

Result<void> OpenGLBackend::init(const dzc::RenderBackendConfig& config) {
    clearLastError();
    const Result<void> stateResult = checkStateForInit();
    if (!stateResult.hasValue()) return stateResult;
    if (!validSize(config.initialSize)) {
        return makeFailure(
            OpenGLBackendErrorCode::OperationFailed,
            "The initial render size is invalid",
            "Width, height, and devicePixelRatio must be positive finite values",
            "OpenGLBackend::init");
    }
    if (m_contextOperations == nullptr) {
        return makeFailure(
            OpenGLBackendErrorCode::ContextUnavailable,
            "An OpenGL context operation table is required",
            "OpenGLBackend was constructed without external context operations",
            "OpenGLBackend::init");
    }
    if (!m_contextOperations->isCurrent()) {
        return makeFailure(
            OpenGLBackendErrorCode::ContextUnavailable,
            "The OpenGL context is not current",
            "The caller must activate the context before initializing OpenGLBackend",
            "OpenGLBackend::init");
    }
    if (!m_contextOperations->loadFunctions() || !m_contextOperations->functionsLoaded()) {
        return makeFailure(
            OpenGLBackendErrorCode::FunctionsUnavailable,
            "OpenGL functions could not be loaded",
            "The externally supplied GLAD loader did not report success",
            "OpenGLBackend::init");
    }

    Result<OpenGLCapabilitySnapshot> capabilityResult =
        m_capabilityQueries != nullptr
            ? OpenGLCapabilities::inspect(*m_capabilityQueries)
            : OpenGLCapabilities::queryCurrentContext();
    if (!capabilityResult.hasValue()) {
        const Error& capabilityError = capabilityResult.error();
        return makeFailure(
            OpenGLBackendErrorCode::CapabilityCheckFailed,
            "The OpenGL context does not satisfy backend capabilities",
            capabilityError.diagnosticMessage,
            "OpenGLBackend::init capability check");
    }

    try {
        m_capabilities = capabilityResult.value();
        m_renderSize = config.initialSize;
        m_currentFrame.reset();
        m_ownerThread = std::this_thread::get_id();
        m_state = OpenGLBackendState::Initialized;
        return Result<void>::success();
    } catch (const std::exception& exception) {
        m_capabilities.reset();
        m_ownerThread = std::thread::id{};
        return makeFailure(
            OpenGLBackendErrorCode::OperationFailed,
            "OpenGL backend initialization failed",
            exception.what(),
            "OpenGLBackend::init");
    } catch (...) {
        m_capabilities.reset();
        m_ownerThread = std::thread::id{};
        return makeFailure(
            OpenGLBackendErrorCode::OperationFailed,
            "OpenGL backend initialization failed",
            "An unknown initialization exception occurred",
            "OpenGLBackend::init");
    }
}

Result<void> OpenGLBackend::upload(const dzc::UploadBatch& batch) {
    clearLastError();
    const Result<void> ready = checkReady("OpenGLBackend::upload");
    if (!ready.hasValue()) return ready;

    for (const dzc::ChunkUpload& item : batch.chunks) {
        try {
            GlChunkResource resource;
            Result<void> result = resource.upload(
                GlContextThreadToken::current(),
                item.metadata,
                item.cpuData,
                m_chunkOperations);
            if (!result.hasValue()) {
                std::string diagnostic = result.error().diagnosticMessage;
                diagnostic += "; " + chunkContext("upload failed", item.metadata.id);
                return makeFailure(
                    OpenGLBackendErrorCode::UploadFailed,
                    "OpenGL Chunk upload failed",
                    std::move(diagnostic),
                    chunkContext("OpenGLBackend::upload", item.metadata.id).c_str());
            }

            auto existing = m_chunks.find(item.metadata.id);
            if (existing != m_chunks.end()) {
                Result<void> oldReset = existing->second.reset(GlContextThreadToken::current());
                if (!oldReset.hasValue()) {
                    return makeFailure(
                        OpenGLBackendErrorCode::OperationFailed,
                        "The existing OpenGL Chunk could not be replaced",
                        oldReset.error().diagnosticMessage,
                        chunkContext("OpenGLBackend::upload replacement", item.metadata.id).c_str());
                }
                existing->second = std::move(resource);
            } else {
                m_chunks.emplace(item.metadata.id, std::move(resource));
            }
        } catch (const std::exception& exception) {
            return makeFailure(
                OpenGLBackendErrorCode::OperationFailed,
                "OpenGL Chunk upload failed",
                exception.what(),
                chunkContext("OpenGLBackend::upload", item.metadata.id).c_str());
        } catch (...) {
            return makeFailure(
                OpenGLBackendErrorCode::OperationFailed,
                "OpenGL Chunk upload failed",
                "An unknown exception occurred while storing the Chunk resource",
                chunkContext("OpenGLBackend::upload", item.metadata.id).c_str());
        }
    }
    return Result<void>::success();
}

Result<void> OpenGLBackend::update(const dzc::RenderFrame& frame) {
    clearLastError();
    const Result<void> ready = checkReady("OpenGLBackend::update");
    if (!ready.hasValue()) return ready;
    if (!validSize(frame.size) || !finitePositive(frame.pointSize)) {
        return makeFailure(
            OpenGLBackendErrorCode::UpdateFailed,
            "The OpenGL frame input is invalid",
            "Render size and pointSize must be positive finite values",
            "OpenGLBackend::update");
    }
    try {
        m_currentFrame = frame;
        m_renderSize = frame.size;
        return Result<void>::success();
    } catch (const std::exception& exception) {
        return makeFailure(
            OpenGLBackendErrorCode::UpdateFailed,
            "The OpenGL frame could not be stored",
            exception.what(),
            "OpenGLBackend::update");
    } catch (...) {
        return makeFailure(
            OpenGLBackendErrorCode::UpdateFailed,
            "The OpenGL frame could not be stored",
            "An unknown exception occurred while storing the frame",
            "OpenGLBackend::update");
    }
}

Result<void> OpenGLBackend::render() {
    clearLastError();
    const Result<void> ready = checkReady("OpenGLBackend::render");
    if (!ready.hasValue()) return ready;
    if (!m_currentFrame.has_value()) {
        return makeFailure(
            OpenGLBackendErrorCode::InvalidState,
            "No frame has been submitted to the OpenGL backend",
            "render requires a successful update call first",
            "OpenGLBackend::render");
    }
    return Result<void>::success();
}

Result<void> OpenGLBackend::resize(const dzc::RenderSize& size) {
    clearLastError();
    const Result<void> ready = checkReady("OpenGLBackend::resize");
    if (!ready.hasValue()) return ready;
    if (!validSize(size)) {
        return makeFailure(
            OpenGLBackendErrorCode::ResizeFailed,
            "The OpenGL render size is invalid",
            "Width, height, and devicePixelRatio must be positive finite values",
            "OpenGLBackend::resize");
    }
    m_renderSize = size;
    return Result<void>::success();
}

void OpenGLBackend::release(dzc::ChunkId chunkId) noexcept {
    clearLastError();
    try {
        if (m_state != OpenGLBackendState::Initialized) {
            recordError(makeError(
                OpenGLBackendErrorCode::InvalidState,
                "The OpenGL backend is not initialized",
                "release requires the Initialized backend state",
                "OpenGLBackend::release"));
            return;
        }
        const Result<void> threadResult = checkThreadAndContext("OpenGLBackend::release");
        if (!threadResult.hasValue()) {
            Error error = threadResult.error();
            error.code = code(OpenGLBackendErrorCode::ReleaseFailed);
            error.context = chunkContext("OpenGLBackend::release", chunkId);
            recordError(std::move(error));
            return;
        }
        auto found = m_chunks.find(chunkId);
        if (found == m_chunks.end()) return;
        const Result<void> result = found->second.reset(GlContextThreadToken::current());
        if (!result.hasValue()) {
            recordError(makeError(
                OpenGLBackendErrorCode::ReleaseFailed,
                "The OpenGL Chunk could not be released",
                result.error().diagnosticMessage,
                chunkContext("OpenGLBackend::release", chunkId).c_str()));
            return;
        }
        m_chunks.erase(found);
    } catch (const std::exception& exception) {
        recordError(makeError(
            OpenGLBackendErrorCode::ReleaseFailed,
            "The OpenGL Chunk release failed",
            exception.what(),
            "OpenGLBackend::release"));
    } catch (...) {
        recordError(makeError(
            OpenGLBackendErrorCode::ReleaseFailed,
            "The OpenGL Chunk release failed",
            "An unknown exception occurred during Chunk release",
            "OpenGLBackend::release"));
    }
}

void OpenGLBackend::shutdown() noexcept {
    shutdownNoexcept();
}

void OpenGLBackend::shutdownNoexcept() noexcept {
    try {
        if (m_state == OpenGLBackendState::Shutdown && m_chunks.empty()) {
            clearLastError();
            return;
        }
        if (m_state == OpenGLBackendState::Uninitialized && m_chunks.empty()) {
            m_state = OpenGLBackendState::Shutdown;
            clearLastError();
            return;
        }
        if (m_state != OpenGLBackendState::Initialized) {
            recordError(makeError(
                OpenGLBackendErrorCode::InvalidState,
                "The OpenGL backend cannot be shut down from its current state",
                "shutdown requires Initialized state when resources are present",
                "OpenGLBackend::shutdown"));
            return;
        }
        const Result<void> threadResult = checkThreadAndContext("OpenGLBackend::shutdown");
        if (!threadResult.hasValue()) {
            Error error = threadResult.error();
            error.code = code(OpenGLBackendErrorCode::ShutdownFailed);
            error.context = "OpenGLBackend::shutdown";
            recordError(std::move(error));
            return;
        }
        const GlContextThreadToken token = GlContextThreadToken::current();
        for (auto iterator = m_chunks.begin(); iterator != m_chunks.end();) {
            const ChunkId id = iterator->first;
            const Result<void> result = iterator->second.reset(token);
            if (!result.hasValue()) {
                recordError(makeError(
                    OpenGLBackendErrorCode::ShutdownFailed,
                    "The OpenGL backend could not release all Chunk resources",
                    result.error().diagnosticMessage,
                    chunkContext("OpenGLBackend::shutdown", id).c_str()));
                return;
            }
            iterator = m_chunks.erase(iterator);
        }
        m_currentFrame.reset();
        m_capabilities.reset();
        m_ownerThread = std::thread::id{};
        m_state = OpenGLBackendState::Shutdown;
        clearLastError();
    } catch (const std::exception& exception) {
        recordError(makeError(
            OpenGLBackendErrorCode::ShutdownFailed,
            "The OpenGL backend shutdown failed",
            exception.what(),
            "OpenGLBackend::shutdown"));
    } catch (...) {
        recordError(makeError(
            OpenGLBackendErrorCode::ShutdownFailed,
            "The OpenGL backend shutdown failed",
            "An unknown exception occurred during backend shutdown",
            "OpenGLBackend::shutdown"));
    }
}

void OpenGLBackend::resetMovedFrom() noexcept {
    m_state = OpenGLBackendState::Uninitialized;
    m_contextOperations.reset();
    m_capabilityQueries.reset();
    m_chunkOperations.reset();
    m_chunks.clear();
    m_currentFrame.reset();
    m_renderSize = {};
    m_capabilities.reset();
    m_lastError.reset();
    m_ownerThread = std::thread::id{};
}

} // namespace dzc::opengl
