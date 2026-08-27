#include "OpenGLBackend.h"

#include "render/common/ShaderData.h"

#include <cmath>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace dzc::opengl {
namespace {
std::uint32_t code(OpenGLBackendErrorCode value) noexcept {
  return static_cast<std::uint32_t>(value);
}
Error makeError(OpenGLBackendErrorCode value, const char *message,
                std::string diagnostic, const char *context) {
  return Error{ErrorDomain::OpenGL, code(value), message, std::move(diagnostic),
               context};
}
bool finitePositive(float value) noexcept {
  return std::isfinite(value) != 0 && value > 0.0F;
}
bool validSize(const RenderSize &value) noexcept {
  return value.width > 0U && value.height > 0U &&
         finitePositive(value.devicePixelRatio);
}
struct PhysicalViewport final {
  std::uint32_t width{0U};
  std::uint32_t height{0U};
};
bool makePhysicalViewport(const RenderSize &size,
                          PhysicalViewport &viewport) noexcept {
  if (!validSize(size))
    return false;
  const auto convert = [](std::uint32_t logicalSize, float pixelRatio,
                          std::uint32_t &physicalSize) noexcept {
    const long double rounded = std::round(
        static_cast<long double>(logicalSize) * static_cast<long double>(pixelRatio));
    if (!std::isfinite(rounded) || rounded < 1.0L ||
        rounded > static_cast<long double>(std::numeric_limits<std::int32_t>::max()))
      return false;
    physicalSize = static_cast<std::uint32_t>(rounded);
    return true;
  };
  return convert(size.width, size.devicePixelRatio, viewport.width) &&
         convert(size.height, size.devicePixelRatio, viewport.height);
}
std::string chunkContext(const char *operation, ChunkId id) {
  return std::string(operation) + " (ChunkId=" + std::to_string(id.value) + ")";
}
std::vector<std::byte> bytesOf(const void *data, std::size_t size) {
  std::vector<std::byte> result(size);
  if (size != 0)
    std::memcpy(result.data(), data, size);
  return result;
}
render::FrameData makeFrameData(const RenderFrame &frame) noexcept {
  render::FrameData data{};
  data.view = render::toColumnMajorArray(frame.camera.view);
  data.projection = render::toColumnMajorArray(frame.camera.projection);
  data.fixedColor = {frame.fixedColor.red, frame.fixedColor.green,
                     frame.fixedColor.blue, frame.fixedColor.alpha};
  data.heightRange = {frame.heightRange.x, frame.heightRange.y, 0.0F, 0.0F};
  data.intensityRange = {frame.intensityRange.x, frame.intensityRange.y, 0.0F, 0.0F};
  data.pointSize = frame.pointSize;
  data.shadingMode = render::toShaderShadingMode(frame.shadingMode);
  return data;
}
} // namespace

OpenGLBackend::OpenGLBackend() noexcept = default;
OpenGLBackend::OpenGLBackend(
    std::shared_ptr<const dzc::IRenderContextOperations> contextOperations,
    std::shared_ptr<const IOpenGLCapabilityQueries> capabilityQueries,
    std::shared_ptr<const IGlChunkUploadOperations> chunkOperations,
    std::shared_ptr<const IGlDrawOperations> drawOperations,
    std::shared_ptr<const IGlShaderOperations> shaderOperations,
    std::shared_ptr<dzc::diagnostics::ILogSink> logSink)
    : m_contextOperations(std::move(contextOperations)),
      m_capabilityQueries(std::move(capabilityQueries)),
      m_chunkOperations(std::move(chunkOperations)),
      m_drawOperations(std::move(drawOperations)),
      m_shaderOperations(std::move(shaderOperations)),
      m_logSink(std::move(logSink)) {}
OpenGLBackend::~OpenGLBackend() noexcept { shutdownNoexcept(); }
OpenGLBackend::OpenGLBackend(OpenGLBackend &&other) noexcept
    : m_state(other.m_state),
      m_contextOperations(std::move(other.m_contextOperations)),
      m_capabilityQueries(std::move(other.m_capabilityQueries)),
      m_chunkOperations(std::move(other.m_chunkOperations)),
      m_drawOperations(std::move(other.m_drawOperations)),
      m_shaderOperations(std::move(other.m_shaderOperations)),
      m_logSink(std::move(other.m_logSink)),
      m_frameBuffer(std::move(other.m_frameBuffer)),
      m_chunkDataBuffer(std::move(other.m_chunkDataBuffer)),
      m_shaderProgram(std::move(other.m_shaderProgram)),
      m_drawCount(other.m_drawCount),
      m_submittedPointCount(other.m_submittedPointCount),
      m_chunks(std::move(other.m_chunks)),
      m_currentFrame(std::move(other.m_currentFrame)),
      m_renderSize(other.m_renderSize),
      m_renderSuspended(other.m_renderSuspended),
      m_capabilities(std::move(other.m_capabilities)),
      m_lastError(std::move(other.m_lastError)),
      m_ownerThread(other.m_ownerThread),
      m_missingColorWarningIssued(other.m_missingColorWarningIssued),
      m_missingIntensityWarningIssued(other.m_missingIntensityWarningIssued) {
  other.resetMovedFrom();
}
OpenGLBackend &OpenGLBackend::operator=(OpenGLBackend &&other) noexcept {
  if (this != &other) {
    shutdownNoexcept();
    m_state = other.m_state;
    m_contextOperations = std::move(other.m_contextOperations);
    m_capabilityQueries = std::move(other.m_capabilityQueries);
    m_chunkOperations = std::move(other.m_chunkOperations);
    m_drawOperations = std::move(other.m_drawOperations);
    m_shaderOperations = std::move(other.m_shaderOperations);
    m_logSink = std::move(other.m_logSink);
    m_missingColorWarningIssued = other.m_missingColorWarningIssued;
    m_missingIntensityWarningIssued = other.m_missingIntensityWarningIssued;
    m_frameBuffer = std::move(other.m_frameBuffer);
    m_chunkDataBuffer = std::move(other.m_chunkDataBuffer);
    m_shaderProgram = std::move(other.m_shaderProgram);
    m_drawCount = other.m_drawCount;
    m_submittedPointCount = other.m_submittedPointCount;
    m_chunks = std::move(other.m_chunks);
    m_currentFrame = std::move(other.m_currentFrame);
    m_renderSize = other.m_renderSize;
    m_renderSuspended = other.m_renderSuspended;
    m_capabilities = std::move(other.m_capabilities);
    m_lastError = std::move(other.m_lastError);
    m_ownerThread = other.m_ownerThread;
    other.resetMovedFrom();
  }
  return *this;
}
bool OpenGLBackend::hasChunk(ChunkId id) const noexcept {
  return m_chunks.find(id) != m_chunks.end();
}
Result<void> OpenGLBackend::makeFailure(OpenGLBackendErrorCode value,
                                        const char *message,
                                        std::string diagnostic,
                                        const char *context) const {
  return Result<void>::failure(
      makeError(value, message, std::move(diagnostic), context));
}
void OpenGLBackend::recordError(Error error) noexcept {
  try {
    m_lastError = std::move(error);
  } catch (...) {
  }
}
void OpenGLBackend::clearLastError() noexcept { m_lastError.reset(); }
void OpenGLBackend::warnMissingAttribute(const RenderFrame &frame,
                                         const DrawChunk &draw,
                                         bool color) noexcept {
  bool &issued = color ? m_missingColorWarningIssued
                       : m_missingIntensityWarningIssued;
  if (issued)
    return;
  issued = true;
  if (!m_logSink)
    return;
  try {
    diagnostics::LogRecord record;
    record.timestamp = std::chrono::system_clock::now();
    record.level = diagnostics::LogLevel::Warn;
    record.module = "OpenGLBackend";
    record.chunk = draw.chunkId.value;
    record.frame = frame.frameId.value;
    record.message = color
        ? "Chunk has no Color attribute; OriginalColor falls back to fixedColor"
        : "Chunk has no Intensity attribute; Intensity falls back to fixedColor";
    m_logSink->write(record);
  } catch (...) {
  }
}
Result<void> OpenGLBackend::checkStateForInit() const {
  if (m_state == OpenGLBackendState::Initialized)
    return makeFailure(OpenGLBackendErrorCode::InvalidState,
                       "OpenGL backend is already initialized",
                       "init cannot be called while the backend is Initialized",
                       "OpenGLBackend::init");
  return Result<void>::success();
}
Result<void> OpenGLBackend::checkThreadAndContext(const char *operation) const {
  const auto token = GlContextThreadToken::current();
  if (m_ownerThread != std::this_thread::get_id() || !token.isCurrentThread())
    return makeFailure(
        OpenGLBackendErrorCode::InvalidThreadToken,
        "OpenGL backend called from the wrong thread",
        "The operation must run on the thread that initialized the backend",
        operation);
  if (!m_contextOperations || !m_contextOperations->isCurrent())
    return makeFailure(OpenGLBackendErrorCode::ContextUnavailable,
                       "The OpenGL context is not current",
                       "The backend operation requires an active context on "
                       "the calling thread",
                       operation);
  if (!m_contextOperations->functionsLoaded())
    return makeFailure(
        OpenGLBackendErrorCode::FunctionsUnavailable,
        "OpenGL functions are not loaded",
        "The external GLAD loader has not reported loaded functions",
        operation);
  return Result<void>::success();
}
Result<void> OpenGLBackend::checkReady(const char *operation) const {
  if (m_state != OpenGLBackendState::Initialized)
    return makeFailure(
        OpenGLBackendErrorCode::InvalidState,
        "OpenGL backend is not initialized",
        "The requested operation requires the Initialized backend state",
        operation);
  return checkThreadAndContext(operation);
}

Result<void> OpenGLBackend::initializeDrawResources() {
  if (!m_drawOperations)
    m_drawOperations = makeDefaultGlDrawOperations();
  if (!m_shaderOperations)
    m_shaderOperations = makeDefaultGlShaderOperations();
  const auto token = GlContextThreadToken::current();
  std::shared_ptr<const IGlResourceOperations> resourceOperations = m_drawOperations;
  if (!m_frameBuffer.create(token, resourceOperations).hasValue()) {
    return makeFailure(OpenGLBackendErrorCode::OperationFailed,
                       "Frame UBO creation failed",
                       "Could not create the persistent FrameData buffer",
                       "OpenGLBackend::init Frame UBO");
  }
  if (!m_chunkDataBuffer.create(token, resourceOperations).hasValue()) {
    m_frameBuffer.reset(token);
    return makeFailure(OpenGLBackendErrorCode::OperationFailed,
                       "Chunk SSBO creation failed",
                       "Could not create the persistent ChunkData buffer",
                       "OpenGLBackend::init Chunk SSBO");
  }
#ifdef DZC_OPENGL_SHADER_DIR
  const std::filesystem::path shaderDir(DZC_OPENGL_SHADER_DIR);
  const auto shaderResult = m_shaderProgram.createFromFiles(
      token, shaderDir / "point_cloud.vert", shaderDir / "point_cloud.frag",
      m_shaderOperations);
#else
  const auto shaderResult = Result<void>::failure(
      Error{ErrorDomain::OpenGL, code(OpenGLBackendErrorCode::OperationFailed),
            "OpenGL shader source directory is not configured",
            "DZC_OPENGL_SHADER_DIR is missing", "OpenGLBackend::init Shader"});
#endif
  if (!shaderResult.hasValue()) {
    m_chunkDataBuffer.reset(token);
    m_frameBuffer.reset(token);
    return makeFailure(OpenGLBackendErrorCode::OperationFailed,
                       "Point cloud shader initialization failed",
                       shaderResult.error().diagnosticMessage,
                       "OpenGLBackend::init Shader");
  }
  return Result<void>::success();
}
Result<void> OpenGLBackend::resetDrawResources() {
  const auto token = GlContextThreadToken::current();
  bool failed = false;
  if (m_shaderProgram.isValid() || m_shaderProgram.releasePending())
    if (!m_shaderProgram.reset(token).hasValue())
      failed = true;
  if (m_chunkDataBuffer.isValid() || m_chunkDataBuffer.releasePending())
    if (!m_chunkDataBuffer.reset(token).hasValue())
      failed = true;
  if (m_frameBuffer.isValid() || m_frameBuffer.releasePending())
    if (!m_frameBuffer.reset(token).hasValue())
      failed = true;
  if (failed)
    return makeFailure(OpenGLBackendErrorCode::ShutdownFailed,
                       "OpenGL draw resources could not be released",
                       "One or more persistent draw resources remain owned",
                       "OpenGLBackend::shutdown draw resources");
  return Result<void>::success();
}

Result<void> OpenGLBackend::init(const RenderBackendConfig &config) {
  clearLastError();
  auto state = checkStateForInit();
  if (!state.hasValue())
    return state;
  PhysicalViewport initialViewport;
  if (!makePhysicalViewport(config.initialSize, initialViewport))
    return makeFailure(
        OpenGLBackendErrorCode::OperationFailed,
        "The initial OpenGL render size is invalid",
        "Logical dimensions must be nonzero and the rounded physical viewport "
        "must fit GLsizei",
        "OpenGLBackend::init");
  if (!m_contextOperations)
    return makeFailure(
        OpenGLBackendErrorCode::ContextUnavailable,
        "An OpenGL context operation table is required",
        "OpenGLBackend was constructed without external context operations",
        "OpenGLBackend::init");
  if (!m_contextOperations->isCurrent())
    return makeFailure(OpenGLBackendErrorCode::ContextUnavailable,
                       "The OpenGL context is not current",
                       "The caller must activate the context before "
                       "initializing OpenGLBackend",
                       "OpenGLBackend::init");
  if (!m_contextOperations->loadFunctions() ||
      !m_contextOperations->functionsLoaded())
    return makeFailure(
        OpenGLBackendErrorCode::FunctionsUnavailable,
        "OpenGL functions could not be loaded",
        "The externally supplied GLAD loader did not report success",
        "OpenGLBackend::init");
  auto capabilityResult =
      m_capabilityQueries ? OpenGLCapabilities::inspect(*m_capabilityQueries)
                          : OpenGLCapabilities::queryCurrentContext();
  if (!capabilityResult.hasValue())
    return makeFailure(
        OpenGLBackendErrorCode::CapabilityCheckFailed,
        "The OpenGL context does not satisfy backend capabilities",
        capabilityResult.error().diagnosticMessage,
        "OpenGLBackend::init capability check");
  try {
    m_capabilities = capabilityResult.value();
    m_renderSize = config.initialSize;
    m_drawCount = 0U;
    m_submittedPointCount = 0U;
    m_currentFrame.reset();
    m_ownerThread = std::this_thread::get_id();
    auto resources = initializeDrawResources();
    if (!resources.hasValue()) {
      m_ownerThread = std::thread::id{};
      m_capabilities.reset();
      return resources;
    }
    if (!m_drawOperations ||
        !m_drawOperations->setViewport(0U, 0U, initialViewport.width,
                                       initialViewport.height)) {
      resetDrawResources();
      m_currentFrame.reset();
      m_renderSize = {};
      m_ownerThread = std::thread::id{};
      m_capabilities.reset();
      return makeFailure(OpenGLBackendErrorCode::OperationFailed,
                         "Initial OpenGL viewport setup failed",
                         "The injected OpenGL viewport operation failed",
                         "OpenGLBackend::init");
    }
    m_renderSuspended = false;
    m_missingColorWarningIssued = false;
    m_missingIntensityWarningIssued = false;
    m_state = OpenGLBackendState::Initialized;
    return Result<void>::success();
  } catch (const std::exception &e) {
    m_ownerThread = std::thread::id{};
    m_capabilities.reset();
    return makeFailure(OpenGLBackendErrorCode::OperationFailed,
                       "OpenGL backend initialization failed", e.what(),
                       "OpenGLBackend::init");
  } catch (...) {
    m_ownerThread = std::thread::id{};
    m_capabilities.reset();
    return makeFailure(OpenGLBackendErrorCode::OperationFailed,
                       "OpenGL backend initialization failed",
                       "An unknown initialization exception occurred",
                       "OpenGLBackend::init");
  }
}

Result<void> OpenGLBackend::upload(const UploadBatch &batch) {
  clearLastError();
  auto ready = checkReady("OpenGLBackend::upload");
  if (!ready.hasValue())
    return ready;
  for (const auto &item : batch.chunks) {
    try {
      GlChunkResource resource;
      std::shared_ptr<const IGlChunkUploadOperations> chunkOperations =
          m_chunkOperations;
      if (!chunkOperations)
        chunkOperations = m_drawOperations;
      auto result =
          resource.upload(GlContextThreadToken::current(), item.metadata,
                          item.cpuData, std::move(chunkOperations));
      if (!result.hasValue())
        return makeFailure(
            OpenGLBackendErrorCode::UploadFailed, "OpenGL Chunk upload failed",
            result.error().diagnosticMessage + "; " +
                chunkContext("upload failed", item.metadata.id),
            chunkContext("OpenGLBackend::upload", item.metadata.id).c_str());
      auto existing = m_chunks.find(item.metadata.id);
      if (existing != m_chunks.end()) {
        auto old = existing->second.reset(GlContextThreadToken::current());
        if (!old.hasValue())
          return makeFailure(OpenGLBackendErrorCode::OperationFailed,
                             "The existing OpenGL Chunk could not be replaced",
                             old.error().diagnosticMessage,
                             chunkContext("OpenGLBackend::upload replacement",
                                          item.metadata.id)
                                 .c_str());
        existing->second = std::move(resource);
      } else
        m_chunks.emplace(item.metadata.id, std::move(resource));
    } catch (const std::exception &e) {
      return makeFailure(
          OpenGLBackendErrorCode::OperationFailed, "OpenGL Chunk upload failed",
          e.what(),
          chunkContext("OpenGLBackend::upload", item.metadata.id).c_str());
    } catch (...) {
      return makeFailure(
          OpenGLBackendErrorCode::OperationFailed, "OpenGL Chunk upload failed",
          "An unknown exception occurred while storing the Chunk resource",
          chunkContext("OpenGLBackend::upload", item.metadata.id).c_str());
    }
  }
  return Result<void>::success();
}
Result<void> OpenGLBackend::update(const RenderFrame &frame) {
  clearLastError();
  auto ready = checkReady("OpenGLBackend::update");
  if (!ready.hasValue())
    return ready;
  if (m_renderSuspended)
    return makeFailure(OpenGLBackendErrorCode::UpdateFailed,
                       "The OpenGL renderer is suspended for a zero-sized viewport",
                       "A valid resize and a fresh camera frame are required before update",
                       "OpenGLBackend::update");
  const auto validRange = [](const glm::vec2 &range) noexcept {
    return std::isfinite(range.x) != 0 && std::isfinite(range.y) != 0 &&
           range.x <= range.y;
  };
  if (!validSize(frame.size) || !finitePositive(frame.pointSize) ||
      !validRange(frame.heightRange) || !validRange(frame.intensityRange))
    return makeFailure(
        OpenGLBackendErrorCode::UpdateFailed,
        "The OpenGL frame input is invalid",
        "Render size, pointSize, and attribute ranges must be finite; ranges must be ordered",
        "OpenGLBackend::update");
  if (frame.size != m_renderSize)
    return makeFailure(OpenGLBackendErrorCode::UpdateFailed,
                       "The OpenGL frame size does not match the active viewport",
                       "Call the camera controller with the latest RenderSize after resize",
                       "OpenGLBackend::update");
  try {
    m_currentFrame = frame;
    return Result<void>::success();
  } catch (const std::exception &e) {
    return makeFailure(OpenGLBackendErrorCode::UpdateFailed,
                       "The OpenGL frame could not be stored", e.what(),
                       "OpenGLBackend::update");
  } catch (...) {
    return makeFailure(OpenGLBackendErrorCode::UpdateFailed,
                       "The OpenGL frame could not be stored",
                       "An unknown exception occurred while storing the frame",
                       "OpenGLBackend::update");
  }
}

Result<void> OpenGLBackend::render() {
  clearLastError();
  auto ready = checkReady("OpenGLBackend::render");
  if (!ready.hasValue())
    return ready;
  if (m_renderSuspended) {
    m_drawCount = 0U;
    m_submittedPointCount = 0U;
    return Result<void>::success();
  }
  if (!m_currentFrame)
    return makeFailure(OpenGLBackendErrorCode::InvalidState,
                       "No frame has been submitted to the OpenGL backend",
                       "render requires a successful update call first",
                       "OpenGLBackend::render");
  const auto &frame = *m_currentFrame;
  const std::uint32_t maxCount =
      static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
  for (const auto &draw : frame.draws) {
    auto found = m_chunks.find(draw.chunkId);
    if (found == m_chunks.end() || !found->second.isValid())
      return makeFailure(
          OpenGLBackendErrorCode::RenderFailed,
          "A visible OpenGL Chunk resource is missing",
          "Every DrawChunk must reference a valid uploaded Chunk",
          chunkContext("OpenGLBackend::render", draw.chunkId).c_str());
    if (draw.pointCount != found->second.pointCount())
      return makeFailure(
          OpenGLBackendErrorCode::RenderFailed,
          "A visible Chunk point count is inconsistent",
          "DrawChunk.pointCount must equal the uploaded GPU resource point "
          "count",
          chunkContext("OpenGLBackend::render", draw.chunkId).c_str());
    if (found->second.pointCount() > maxCount)
      return makeFailure(
          OpenGLBackendErrorCode::RenderFailed,
          "A visible Chunk is too large to draw",
          "The point count exceeds GLsizei maximum",
          chunkContext("OpenGLBackend::render", draw.chunkId).c_str());
  }
  try {
    const auto frameData = makeFrameData(frame);
    const auto frameBytes = bytesOf(&frameData, sizeof(frameData));
    const auto chunkData = render::makeChunkData(glm::vec3(0.0F));
    const auto chunkBytes = bytesOf(&chunkData, sizeof(chunkData));
    auto fail = [&](const char *message) {
      return makeFailure(OpenGLBackendErrorCode::RenderFailed, message,
                         "The injected OpenGL draw operation failed",
                         "OpenGLBackend::render");
    };
    if (!m_drawOperations || !m_frameBuffer.isValid() ||
        !m_chunkDataBuffer.isValid() || !m_shaderProgram.isValid())
      return fail("OpenGL draw resources are unavailable");
    if (!m_drawOperations->bindDrawBuffer(GlDrawBufferTarget::UniformBuffer,
                                          m_frameBuffer.id()) ||
        !m_drawOperations->uploadDrawBuffer(
            GlDrawBufferTarget::UniformBuffer, m_frameBuffer.id(),
            frameBytes.data(), frameBytes.size(),
            GlDrawBufferUsage::DynamicDraw) ||
        !m_drawOperations->bindDrawBufferBase(GlDrawBufferTarget::UniformBuffer,
                                              render::frameDataBinding,
                                              m_frameBuffer.id()))
      return fail("FrameData UBO update failed");
    if (!m_drawOperations->clearColor(frame.backgroundColor) ||
        !m_drawOperations->useProgram(m_shaderProgram.id()))
      return fail("OpenGL frame setup failed");
    std::uint64_t nextDraws = 0U, nextPoints = 0U;
    for (const auto &draw : frame.draws) {
      const auto &resource = m_chunks.at(draw.chunkId);
      const auto data = render::makeChunkData(draw.relativeOrigin);
      const auto bytes = bytesOf(&data, sizeof(data));
      if (!m_drawOperations->setProgramUniformUInt(
              m_shaderProgram.id(), "drawHasColor", draw.schema.hasColor() ? 1U : 0U) ||
          !m_drawOperations->setProgramUniformUInt(
              m_shaderProgram.id(), "drawHasIntensity",
              draw.schema.hasIntensity() ? 1U : 0U))
        return fail("OpenGL shading state update failed");
      if (!draw.schema.hasColor() && frame.shadingMode == ShadingMode::OriginalColor)
        warnMissingAttribute(frame, draw, true);
      if (!draw.schema.hasIntensity() && frame.shadingMode == ShadingMode::Intensity)
        warnMissingAttribute(frame, draw, false);
      if (!m_drawOperations->bindDrawBuffer(
              GlDrawBufferTarget::ShaderStorageBuffer,
              m_chunkDataBuffer.id()) ||
          !m_drawOperations->uploadDrawBuffer(
              GlDrawBufferTarget::ShaderStorageBuffer, m_chunkDataBuffer.id(),
              bytes.data(), bytes.size(), GlDrawBufferUsage::DynamicDraw) ||
          !m_drawOperations->bindDrawBufferBase(
              GlDrawBufferTarget::ShaderStorageBuffer, render::chunkDataBinding,
              m_chunkDataBuffer.id()) ||
          !m_drawOperations->bindVertexArray(resource.vertexArrayId()) ||
          !m_drawOperations->drawPoints(
              static_cast<std::uint32_t>(resource.pointCount())))
        return fail("OpenGL Chunk draw failed");
      ++nextDraws;
      nextPoints += resource.pointCount();
    }
    m_drawCount = nextDraws;
    m_submittedPointCount = nextPoints;
    return Result<void>::success();
  } catch (const std::exception &e) {
    return makeFailure(OpenGLBackendErrorCode::RenderFailed,
                       "OpenGL rendering failed", e.what(),
                       "OpenGLBackend::render");
  } catch (...) {
    return makeFailure(OpenGLBackendErrorCode::RenderFailed,
                       "OpenGL rendering failed",
                       "An unknown exception occurred during rendering",
                       "OpenGLBackend::render");
  }
}
Result<void> OpenGLBackend::resize(const RenderSize &size) {
  clearLastError();
  auto ready = checkReady("OpenGLBackend::resize");
  if (!ready.hasValue())
    return ready;
  if (!finitePositive(size.devicePixelRatio))
    return makeFailure(OpenGLBackendErrorCode::ResizeFailed,
                       "The OpenGL render size is invalid",
                       "devicePixelRatio must be finite and positive",
                       "OpenGLBackend::resize");
  if (size.width == 0U || size.height == 0U) {
    m_renderSize = size;
    m_currentFrame.reset();
    m_renderSuspended = true;
    return Result<void>::success();
  }
  PhysicalViewport viewport;
  if (!makePhysicalViewport(size, viewport))
    return makeFailure(OpenGLBackendErrorCode::ResizeFailed,
                       "The OpenGL physical viewport is invalid",
                       "The rounded physical viewport must be nonzero and fit GLsizei",
                       "OpenGLBackend::resize");
  if (!m_drawOperations ||
      !m_drawOperations->setViewport(0U, 0U, viewport.width, viewport.height))
    return makeFailure(OpenGLBackendErrorCode::ResizeFailed,
                       "OpenGL viewport setup failed",
                       "The injected OpenGL viewport operation failed",
                       "OpenGLBackend::resize");
  const bool changed = size != m_renderSize;
  m_renderSize = size;
  m_renderSuspended = false;
  if (changed)
    m_currentFrame.reset();
  return Result<void>::success();
}
void OpenGLBackend::release(ChunkId id) noexcept {
  clearLastError();
  try {
    if (m_state != OpenGLBackendState::Initialized) {
      recordError(makeError(OpenGLBackendErrorCode::InvalidState,
                            "The OpenGL backend is not initialized",
                            "release requires the Initialized backend state",
                            "OpenGLBackend::release"));
      return;
    }
    auto ready = checkThreadAndContext("OpenGLBackend::release");
    if (!ready.hasValue()) {
      auto error = ready.error();
      error.code = code(OpenGLBackendErrorCode::ReleaseFailed);
      error.context = chunkContext("OpenGLBackend::release", id);
      recordError(std::move(error));
      return;
    }
    auto found = m_chunks.find(id);
    if (found == m_chunks.end())
      return;
    auto result = found->second.reset(GlContextThreadToken::current());
    if (!result.hasValue()) {
      recordError(
          makeError(OpenGLBackendErrorCode::ReleaseFailed,
                    "The OpenGL Chunk could not be released",
                    result.error().diagnosticMessage,
                    chunkContext("OpenGLBackend::release", id).c_str()));
      return;
    }
    m_chunks.erase(found);
  } catch (const std::exception &e) {
    recordError(makeError(OpenGLBackendErrorCode::ReleaseFailed,
                          "The OpenGL Chunk release failed", e.what(),
                          "OpenGLBackend::release"));
  } catch (...) {
    recordError(makeError(OpenGLBackendErrorCode::ReleaseFailed,
                          "The OpenGL Chunk release failed",
                          "An unknown exception occurred during Chunk release",
                          "OpenGLBackend::release"));
  }
}
void OpenGLBackend::shutdown() noexcept { shutdownNoexcept(); }
void OpenGLBackend::shutdownNoexcept() noexcept {
  try {
    if (m_state == OpenGLBackendState::Shutdown && m_chunks.empty() &&
        !m_frameBuffer.isValid() && !m_chunkDataBuffer.isValid() &&
        !m_shaderProgram.isValid()) {
      clearLastError();
      return;
    }
    if (m_state == OpenGLBackendState::Uninitialized && m_chunks.empty() &&
        !m_frameBuffer.isValid() && !m_chunkDataBuffer.isValid() &&
        !m_shaderProgram.isValid()) {
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
    auto ready = checkThreadAndContext("OpenGLBackend::shutdown");
    if (!ready.hasValue()) {
      auto error = ready.error();
      error.code = code(OpenGLBackendErrorCode::ShutdownFailed);
      error.context = "OpenGLBackend::shutdown";
      recordError(std::move(error));
      return;
    }
    const auto token = GlContextThreadToken::current();
    for (auto it = m_chunks.begin(); it != m_chunks.end();) {
      auto result = it->second.reset(token);
      if (!result.hasValue()) {
        recordError(makeError(
            OpenGLBackendErrorCode::ShutdownFailed,
            "The OpenGL backend could not release all Chunk resources",
            result.error().diagnosticMessage, "OpenGLBackend::shutdown"));
        return;
      }
      it = m_chunks.erase(it);
    }
    auto draw = resetDrawResources();
    if (!draw.hasValue()) {
      recordError(draw.error());
      return;
    }
    m_currentFrame.reset();
    m_capabilities.reset();
    m_ownerThread = std::thread::id{};
    m_renderSuspended = false;
    m_missingColorWarningIssued = false;
    m_missingIntensityWarningIssued = false;
    m_state = OpenGLBackendState::Shutdown;
    clearLastError();
  } catch (const std::exception &e) {
    recordError(makeError(OpenGLBackendErrorCode::ShutdownFailed,
                          "The OpenGL backend shutdown failed", e.what(),
                          "OpenGLBackend::shutdown"));
  } catch (...) {
    recordError(
        makeError(OpenGLBackendErrorCode::ShutdownFailed,
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
  m_drawOperations.reset();
  m_shaderOperations.reset();
  m_logSink.reset();
  m_frameBuffer = GlBuffer{};
  m_chunkDataBuffer = GlBuffer{};
  m_shaderProgram = GlShaderProgram{};
  m_drawCount = 0U;
  m_submittedPointCount = 0U;
  m_chunks.clear();
  m_currentFrame.reset();
  m_renderSize = {};
  m_renderSuspended = false;
  m_capabilities.reset();
  m_lastError.reset();
  m_ownerThread = std::thread::id{};
  m_missingColorWarningIssued = false;
  m_missingIntensityWarningIssued = false;
}
} // namespace dzc::opengl
