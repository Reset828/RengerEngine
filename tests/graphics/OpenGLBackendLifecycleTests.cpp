#include "OpenGLBackend.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace dzc;
using namespace dzc::opengl;

class FakeContextOperations final : public IRenderContextOperations {
public:
  mutable bool current{true};
  mutable bool loadResult{true};
  mutable bool loaded{false};

  bool makeCurrent() const noexcept override {
    current = true;
    return true;
  }
  bool isCurrent() const noexcept override { return current; }
  bool loadFunctions() const noexcept override {
    if (!loadResult)
      return false;
    loaded = true;
    return true;
  }
  bool functionsLoaded() const noexcept override { return loaded; }
  bool releaseCurrent() const noexcept override {
    current = false;
    return true;
  }
};

class FakeCapabilities final : public IOpenGLCapabilityQueries {
public:
  OpenGLVersion version{4, 5, true};
  PointSizeLimits pointSize{1.0F, 64.0F, 1.0F};
  OpenGLBufferLimits buffers{256U, 256U, 1024U * 1024U};
  OpenGLDeviceInfo device{"Fake Vendor", "Fake Renderer", "Fake Driver",
                          "Fake GLSL"};

  OpenGLVersion queryVersion() const override { return version; }
  PointSizeLimits queryPointSizeLimits() const override { return pointSize; }
  OpenGLBufferLimits queryBufferLimits() const override { return buffers; }
  OpenGLDeviceInfo queryDeviceInfo() const override { return device; }
};

class FakeChunkOperations final : public IGlDrawOperations {
public:
  mutable std::uint32_t nextId{1U};
  mutable std::size_t createdBuffers{0U};
  mutable std::size_t deletedBuffers{0U};
  mutable std::size_t createdVertexArrays{0U};
  mutable std::size_t deletedVertexArrays{0U};
  mutable bool failDeleteBuffer{false};

  bool createBuffer(std::uint32_t &id) const noexcept override {
    id = nextId++;
    ++createdBuffers;
    return true;
  }
  bool deleteBuffer(std::uint32_t) const noexcept override {
    if (failDeleteBuffer)
      return false;
    ++deletedBuffers;
    return true;
  }
  bool labelBuffer(std::uint32_t, std::string_view) const noexcept override {
    return true;
  }
  bool createVertexArray(std::uint32_t &id) const noexcept override {
    id = nextId++;
    ++createdVertexArrays;
    return true;
  }
  bool deleteVertexArray(std::uint32_t) const noexcept override {
    ++deletedVertexArrays;
    return true;
  }
  bool labelVertexArray(std::uint32_t,
                        std::string_view) const noexcept override {
    return true;
  }
  bool bindVertexArray(std::uint32_t) const noexcept override { return true; }
  bool bindArrayBuffer(std::uint32_t) const noexcept override { return true; }
  bool uploadArrayBuffer(std::uint32_t, const std::vector<std::byte> &,
                         GlChunkBufferUsage) const noexcept override {
    return true;
  }
  bool configureVertexAttribute(std::uint32_t, GlChunkAttributeFormat,
                                std::uint32_t, bool,
                                std::uint32_t) const noexcept override {
    return true;
  }
  bool enableVertexAttribute(std::uint32_t) const noexcept override {
    return true;
  }
  bool unbindArrayBuffer() const noexcept override { return true; }
  bool unbindVertexArray() const noexcept override { return true; }
  bool bindDrawBuffer(GlDrawBufferTarget,
                      std::uint32_t) const noexcept override {
    return true;
  }
  bool uploadDrawBuffer(GlDrawBufferTarget, std::uint32_t, const void *,
                        std::size_t,
                        GlDrawBufferUsage) const noexcept override {
    return true;
  }
  bool bindDrawBufferBase(GlDrawBufferTarget, std::uint32_t,
                          std::uint32_t) const noexcept override {
    return true;
  }
  bool useProgram(std::uint32_t) const noexcept override { return true; }
  bool clearColor(const ColorRgba &) const noexcept override { return true; }
  bool drawPoints(std::uint32_t) const noexcept override { return true; }
};

class FakeShaderOperations final : public IGlShaderOperations {
public:
  mutable std::uint32_t nextId{1000U};
  bool createShader(GlShaderStage, std::uint32_t &id) const override {
    id = nextId++;
    return true;
  }
  bool setShaderSource(std::uint32_t, std::string_view) const override {
    return true;
  }
  bool compileShader(std::uint32_t, std::string &log) const override {
    log.clear();
    return true;
  }
  bool createProgram(std::uint32_t &id) const override {
    id = nextId++;
    return true;
  }
  bool attachShader(std::uint32_t, std::uint32_t) const override {
    return true;
  }
  bool linkProgram(std::uint32_t, std::string &log) const override {
    log.clear();
    return true;
  }
  bool deleteShader(std::uint32_t) const override { return true; }
  bool deleteProgram(std::uint32_t) const override { return true; }
};
std::shared_ptr<FakeChunkOperations> makeChunkOperations() {
  return std::make_shared<FakeChunkOperations>();
}

ChunkUpload makeUpload(std::uint64_t id, std::uint32_t count = 2U) {
  ChunkUpload upload;
  upload.metadata.id = ChunkId{id};
  upload.metadata.pointCount = count;
  upload.metadata.schema.mask =
      static_cast<std::uint32_t>(PointAttribute::Position);
  upload.cpuData.positions.resize(count);
  for (std::uint32_t index = 0U; index < count; ++index) {
    upload.cpuData.positions[index] =
        glm::vec3(static_cast<float>(index), 0.0F, 0.0F);
  }
  return upload;
}

std::unique_ptr<OpenGLBackend>
makeBackend(const std::shared_ptr<FakeContextOperations> &context,
            const std::shared_ptr<FakeCapabilities> &capabilities,
            const std::shared_ptr<FakeChunkOperations> &operations) {
  return std::make_unique<OpenGLBackend>(
      context, capabilities, operations, operations,
      std::make_shared<FakeShaderOperations>());
}

RenderBackendConfig config() {
  return RenderBackendConfig{RenderSize{640U, 480U, 1.0F}};
}

RenderFrame frame() {
  RenderFrame value;
  value.frameId = FrameId{1U};
  value.size = RenderSize{640U, 480U, 1.0F};
  value.pointSize = 2.0F;
  return value;
}

void assertError(const Result<void> &result, OpenGLBackendErrorCode expected) {
  assert(!result.hasValue());
  assert(result.error().domain == ErrorDomain::OpenGL);
  assert(result.error().code == static_cast<std::uint32_t>(expected));
  assert(!result.error().userMessage.empty());
  assert(!result.error().diagnosticMessage.empty());
  assert(!result.error().context.empty());
}

void testInitializationAndState() {
  auto context = std::make_shared<FakeContextOperations>();
  auto capabilities = std::make_shared<FakeCapabilities>();
  auto operations = makeChunkOperations();
  auto backend = makeBackend(context, capabilities, operations);
  assert(backend->init(config()).hasValue());
  assert(backend->isInitialized());
  assert(backend->capabilities().has_value());
  assert(backend->capabilities()->version.major == 4);
  assert(backend->init(config()).hasValue() == false);
  assert(backend->lastError().has_value() == false);
  RenderFrame current = frame();
  assert(backend->update(current).hasValue());
  assert(backend->render().hasValue());
  assert(backend->resize(RenderSize{800U, 600U, 2.0F}).hasValue());
  assert(backend->renderSize().width == 800U);
  backend->shutdown();
  assert(backend->state() == OpenGLBackendState::Shutdown);
  assert(!backend->render().hasValue());
}

void testInitializationFailures() {
  auto context = std::make_shared<FakeContextOperations>();
  auto capabilities = std::make_shared<FakeCapabilities>();
  auto operations = makeChunkOperations();
  auto backend = makeBackend(context, capabilities, operations);

  context->current = false;
  assertError(backend->init(config()),
              OpenGLBackendErrorCode::ContextUnavailable);
  context->current = true;
  context->loadResult = false;
  assertError(backend->init(config()),
              OpenGLBackendErrorCode::FunctionsUnavailable);

  context->loadResult = true;
  capabilities->version = {4, 4, true};
  assertError(backend->init(config()),
              OpenGLBackendErrorCode::CapabilityCheckFailed);

  capabilities->version = {4, 5, false};
  assertError(backend->init(config()),
              OpenGLBackendErrorCode::CapabilityCheckFailed);
}

void testUploadAndRelease() {
  auto context = std::make_shared<FakeContextOperations>();
  auto capabilities = std::make_shared<FakeCapabilities>();
  auto operations = makeChunkOperations();
  auto backend = makeBackend(context, capabilities, operations);
  assert(backend->init(config()).hasValue());

  UploadBatch batch;
  batch.chunks.push_back(makeUpload(10U));
  batch.chunks.push_back(makeUpload(20U));
  assert(backend->upload(batch).hasValue());
  assert(backend->chunkCount() == 2U);
  assert(backend->hasChunk(ChunkId{10U}));
  assert(backend->hasChunk(ChunkId{20U}));

  const std::size_t oldDeletes = operations->deletedBuffers;
  UploadBatch replacement;
  replacement.chunks.push_back(makeUpload(10U, 3U));
  assert(backend->upload(replacement).hasValue());
  assert(backend->chunkCount() == 2U);
  assert(operations->deletedBuffers == oldDeletes + 3U);

  backend->release(ChunkId{20U});
  assert(!backend->hasChunk(ChunkId{20U}));
  backend->release(ChunkId{20U});
  backend->shutdown();
  assert(backend->chunkCount() == 0U);
}

void testUpdateValidationAndThread() {
  auto context = std::make_shared<FakeContextOperations>();
  auto capabilities = std::make_shared<FakeCapabilities>();
  auto operations = makeChunkOperations();
  auto backend = makeBackend(context, capabilities, operations);
  assert(backend->init(config()).hasValue());

  RenderFrame invalid = frame();
  invalid.pointSize = 0.0F;
  assertError(backend->update(invalid), OpenGLBackendErrorCode::UpdateFailed);
  assertError(backend->resize(RenderSize{}),
              OpenGLBackendErrorCode::ResizeFailed);

  std::optional<Result<void>> threadResult;
  std::thread worker([&] {
    UploadBatch empty;
    threadResult = backend->upload(empty);
  });
  worker.join();
  assert(threadResult.has_value());
  assertError(*threadResult, OpenGLBackendErrorCode::InvalidThreadToken);

  context->current = false;
  backend->release(ChunkId{999U});
  assert(backend->lastError().has_value());
  assert(backend->lastError()->domain == ErrorDomain::OpenGL);
  backend->shutdown();
  assert(backend->state() == OpenGLBackendState::Initialized);
  context->current = true;
  backend->shutdown();
  assert(backend->state() == OpenGLBackendState::Shutdown);
}

void testShutdownFailureAndMove() {
  auto context = std::make_shared<FakeContextOperations>();
  auto capabilities = std::make_shared<FakeCapabilities>();
  auto operations = makeChunkOperations();
  auto backend = makeBackend(context, capabilities, operations);
  assert(backend->init(config()).hasValue());
  UploadBatch batch;
  batch.chunks.push_back(makeUpload(1U));
  assert(backend->upload(batch).hasValue());

  operations->failDeleteBuffer = true;
  backend->shutdown();
  assert(backend->state() == OpenGLBackendState::Initialized);
  assert(backend->chunkCount() == 1U);
  assert(backend->lastError().has_value());
  assert(backend->lastError()->domain == ErrorDomain::OpenGL);

  operations->failDeleteBuffer = false;
  OpenGLBackend moved(std::move(*backend));
  assert(moved.isInitialized());
  assert(backend->state() == OpenGLBackendState::Uninitialized);
  moved.shutdown();
  assert(moved.state() == OpenGLBackendState::Shutdown);
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "--real-context") {
    std::cout << "SKIPPED: real OpenGL Context infrastructure is not available "
                 "in GL-007.\n";
    return 77;
  }
  testInitializationAndState();
  testInitializationFailures();
  testUploadAndRelease();
  testUpdateValidationAndThread();
  testShutdownFailureAndMove();
  return 0;
}
