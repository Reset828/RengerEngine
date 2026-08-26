#pragma once

#include "OpenGLCapabilities.h"
#include "GlChunkResource.h"
#include "render/common/RenderBackendFactory.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>

namespace dzc::opengl {

enum class OpenGLBackendErrorCode : std::uint32_t {
    InvalidState = 1U,
    InvalidThreadToken = 2U,
    ContextUnavailable = 3U,
    FunctionsUnavailable = 4U,
    CapabilityCheckFailed = 5U,
    UploadFailed = 6U,
    UpdateFailed = 7U,
    ResizeFailed = 8U,
    ReleaseFailed = 9U,
    ShutdownFailed = 10U,
    OperationFailed = 11U
};

enum class OpenGLBackendState : std::uint8_t {
    Uninitialized,
    Initialized,
    Shutdown
};

class OpenGLBackend final : public dzc::IRenderBackend {
public:
    OpenGLBackend() noexcept;
    explicit OpenGLBackend(
        std::shared_ptr<const dzc::IRenderContextOperations> contextOperations,
        std::shared_ptr<const IOpenGLCapabilityQueries> capabilityQueries = {},
        std::shared_ptr<const IGlChunkUploadOperations> chunkOperations = {});

    ~OpenGLBackend() noexcept override;

    OpenGLBackend(const OpenGLBackend&) = delete;
    OpenGLBackend& operator=(const OpenGLBackend&) = delete;
    OpenGLBackend(OpenGLBackend&& other) noexcept;
    OpenGLBackend& operator=(OpenGLBackend&& other) noexcept;

    dzc::Result<void> init(const dzc::RenderBackendConfig& config) override;
    dzc::Result<void> upload(const dzc::UploadBatch& batch) override;
    dzc::Result<void> update(const dzc::RenderFrame& frame) override;
    dzc::Result<void> render() override;
    dzc::Result<void> resize(const dzc::RenderSize& size) override;
    void release(dzc::ChunkId chunkId) noexcept override;
    void shutdown() noexcept override;

    const std::optional<dzc::Error>& lastError() const noexcept { return m_lastError; }
    OpenGLBackendState state() const noexcept { return m_state; }
    bool isInitialized() const noexcept { return m_state == OpenGLBackendState::Initialized; }
    std::size_t chunkCount() const noexcept { return m_chunks.size(); }
    bool hasChunk(dzc::ChunkId chunkId) const noexcept;
    const std::optional<dzc::RenderFrame>& currentFrame() const noexcept { return m_currentFrame; }
    const dzc::RenderSize& renderSize() const noexcept { return m_renderSize; }
    const std::optional<OpenGLCapabilitySnapshot>& capabilities() const noexcept {
        return m_capabilities;
    }

private:
    struct ChunkIdHash final {
        std::size_t operator()(dzc::ChunkId id) const noexcept {
            return std::hash<std::uint64_t>{}(id.value);
        }
    };

    dzc::Result<void> checkReady(const char* operation) const;
    dzc::Result<void> checkThreadAndContext(const char* operation) const;
    dzc::Result<void> checkStateForInit() const;
    dzc::Result<void> makeFailure(
        OpenGLBackendErrorCode code,
        const char* userMessage,
        std::string diagnostic,
        const char* context) const;
    void recordError(dzc::Error error) noexcept;
    void clearLastError() noexcept;
    void resetMovedFrom() noexcept;
    void shutdownNoexcept() noexcept;

    OpenGLBackendState m_state{OpenGLBackendState::Uninitialized};
    std::shared_ptr<const dzc::IRenderContextOperations> m_contextOperations;
    std::shared_ptr<const IOpenGLCapabilityQueries> m_capabilityQueries;
    std::shared_ptr<const IGlChunkUploadOperations> m_chunkOperations;
    std::unordered_map<dzc::ChunkId, GlChunkResource, ChunkIdHash> m_chunks;
    std::optional<dzc::RenderFrame> m_currentFrame;
    dzc::RenderSize m_renderSize{};
    std::optional<OpenGLCapabilitySnapshot> m_capabilities;
    std::optional<dzc::Error> m_lastError;
    std::thread::id m_ownerThread;
};

} // namespace dzc::opengl
