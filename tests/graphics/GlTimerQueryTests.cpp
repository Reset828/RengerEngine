#include "OpenGLBackend.h"
#include "GlTimerQueryPool.h"
#include "FakeTimerQueryOperations.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <string_view>

namespace {

using dzc::opengl::GlContextThreadToken;
using dzc::opengl::GlTimerQueryErrorCode;
using dzc::opengl::GlTimerQueryPool;
using dzc::opengl::test::FakeTimerQueryOperations;

void assertError(const dzc::Result<void> &result, GlTimerQueryErrorCode code) {
  assert(!result.hasValue());
  assert(result.error().code == static_cast<std::uint32_t>(code));
}

template <typename T>
void assertError(const dzc::Result<T> &result, GlTimerQueryErrorCode code) {
  assert(!result.hasValue());
  assert(result.error().code == static_cast<std::uint32_t>(code));
}

void endFrame(GlTimerQueryPool &pool, const GlContextThreadToken &token) {
  assert(pool.beginFrame(token).hasValue());
  assert(pool.endFrame(token).hasValue());
}

void testDelayedResolutionAndReuse() {
  auto operations = std::make_shared<FakeTimerQueryOperations>();
  GlTimerQueryPool pool(operations);
  const auto token = GlContextThreadToken::current();
  assert(pool.initialize(token).hasValue());
  assert(operations->createCalls == GlTimerQueryPool::queryDelayFrames);

  for (std::size_t frame = 0U; frame < GlTimerQueryPool::queryDelayFrames;
       ++frame) {
    const auto result = pool.resolveElapsedMilliseconds(token);
    assert(result.hasValue());
    assert(!result.value().has_value());
    assert(operations->availabilityCalls == 0U);
    endFrame(pool, token);
  }

  operations->resultAvailable = false;
  auto delayed = pool.resolveElapsedMilliseconds(token);
  assert(delayed.hasValue());
  assert(!delayed.value().has_value());
  assert(operations->availabilityCalls == 1U);
  const auto priorBegins = operations->beginCalls;
  assert(pool.beginFrame(token).hasValue());
  assert(!pool.hasActiveQuery());
  assert(pool.endFrame(token).hasValue());
  assert(operations->beginCalls == priorBegins);

  operations->resultAvailable = true;
  operations->resultNanoseconds = 2500000U;
  delayed = pool.resolveElapsedMilliseconds(token);
  assert(delayed.hasValue());
  assert(delayed.value().has_value());
  assert(std::fabs(*delayed.value() - 2.5) < 1.0e-12);
  assert(operations->readCalls == 1U);
  endFrame(pool, token);
  assert(operations->beginCalls == priorBegins + 1U);
  assert(pool.reset(token).hasValue());
  assert(operations->deleteCalls == GlTimerQueryPool::queryDelayFrames);
}

void testFailuresAndThreadOwnership() {
  const auto token = GlContextThreadToken::current();
  auto createFailure = std::make_shared<FakeTimerQueryOperations>();
  createFailure->failCreate = true;
  GlTimerQueryPool failingCreate(createFailure);
  assertError(failingCreate.initialize(token), GlTimerQueryErrorCode::CreationFailed);

  auto partialCreateFailure = std::make_shared<FakeTimerQueryOperations>();
  partialCreateFailure->failCreateAfter = 1U;
  GlTimerQueryPool partialPool(partialCreateFailure);
  assertError(partialPool.initialize(token), GlTimerQueryErrorCode::CreationFailed);
  assert(partialCreateFailure->createCalls == 1U);
  assert(partialCreateFailure->deleteCalls == 1U);
  assert(!partialPool.isInitialized());

  auto retainedCreateFailure = std::make_shared<FakeTimerQueryOperations>();
  retainedCreateFailure->failCreateAfter = 1U;
  retainedCreateFailure->failDelete = true;
  GlTimerQueryPool retainedPool(retainedCreateFailure);
  assertError(retainedPool.initialize(token), GlTimerQueryErrorCode::CreationFailed);
  assert(retainedPool.releasePending());
  assertError(retainedPool.reset(token), GlTimerQueryErrorCode::ReleaseFailed);
  retainedCreateFailure->failDelete = false;
  assert(retainedPool.reset(token).hasValue());

  auto operations = std::make_shared<FakeTimerQueryOperations>();
  GlTimerQueryPool pool(operations);
  assert(pool.initialize(token).hasValue());
  operations->failBegin = true;
  assertError(pool.beginFrame(token), GlTimerQueryErrorCode::BeginFailed);
  operations->failBegin = false;
  assert(pool.beginFrame(token).hasValue());
  operations->failEnd = true;
  assertError(pool.endFrame(token), GlTimerQueryErrorCode::EndFailed);
  operations->failEnd = false;
  assert(pool.endFrame(token).hasValue());

  for (std::size_t frame = 1U; frame < GlTimerQueryPool::queryDelayFrames;
       ++frame)
    endFrame(pool, token);
  operations->failAvailability = true;
  assertError(pool.resolveElapsedMilliseconds(token),
              GlTimerQueryErrorCode::AvailabilityCheckFailed);
  operations->failAvailability = false;
  operations->failRead = true;
  assertError(pool.resolveElapsedMilliseconds(token),
              GlTimerQueryErrorCode::ResultReadFailed);
  operations->failRead = false;

  bool wrongThreadRejected = false;
  std::thread other([&] {
    const auto otherToken = GlContextThreadToken::current();
    const auto result = pool.resolveElapsedMilliseconds(otherToken);
    wrongThreadRejected = !result.hasValue() &&
                          result.error().code == static_cast<std::uint32_t>(
                                                      GlTimerQueryErrorCode::InvalidThreadToken);
  });
  other.join();
  assert(wrongThreadRejected);

  operations->failDelete = true;
  assertError(pool.reset(token), GlTimerQueryErrorCode::ReleaseFailed);
  operations->failDelete = false;
  assert(pool.reset(token).hasValue());
}

void testMoveOwnership() {
  auto operations = std::make_shared<FakeTimerQueryOperations>();
  const auto token = GlContextThreadToken::current();
  GlTimerQueryPool source(operations);
  assert(source.initialize(token).hasValue());
  GlTimerQueryPool moved(std::move(source));
  assert(!source.isInitialized());
  assert(moved.isInitialized());
  assert(moved.reset(token).hasValue());

  auto crossThreadOperations = std::make_shared<FakeTimerQueryOperations>();
  auto crossThreadPool = std::make_unique<GlTimerQueryPool>(crossThreadOperations);
  assert(crossThreadPool->initialize(token).hasValue());
  std::thread destroyOnOtherThread([pool = std::move(crossThreadPool)]() mutable {
    pool.reset();
  });
  destroyOnOtherThread.join();
  assert(crossThreadOperations->deleteCalls == 0U);
}


class BackendFakeContext final : public dzc::IRenderContextOperations {
public:
  mutable bool current{true};
  mutable bool loaded{false};
  bool makeCurrent() const noexcept override { current = true; return true; }
  bool isCurrent() const noexcept override { return current; }
  bool loadFunctions() const noexcept override { loaded = true; return true; }
  bool functionsLoaded() const noexcept override { return loaded; }
  bool releaseCurrent() const noexcept override { current = false; return true; }
};

class BackendFakeCapabilities final : public dzc::opengl::IOpenGLCapabilityQueries {
public:
  dzc::opengl::OpenGLVersion queryVersion() const override { return {4, 5, true}; }
  dzc::opengl::PointSizeLimits queryPointSizeLimits() const override { return {1.0F, 64.0F, 1.0F}; }
  dzc::opengl::OpenGLBufferLimits queryBufferLimits() const override { return {256U, 256U, 1024U * 1024U}; }
  dzc::opengl::OpenGLDeviceInfo queryDeviceInfo() const override { return {"Fake", "Fake", "Fake", "Fake"}; }
};

class BackendFakeDraw final : public dzc::opengl::IGlDrawOperations {
public:
  mutable std::uint32_t nextId{1U};
  mutable std::size_t deleteCalls{0U};
  mutable bool failDraw{false};
  bool createBuffer(std::uint32_t &id) const noexcept override { id = nextId++; return true; }
  bool deleteBuffer(std::uint32_t) const noexcept override { ++deleteCalls; return true; }
  bool labelBuffer(std::uint32_t, std::string_view) const noexcept override { return true; }
  bool createVertexArray(std::uint32_t &id) const noexcept override { id = nextId++; return true; }
  bool deleteVertexArray(std::uint32_t) const noexcept override { return true; }
  bool labelVertexArray(std::uint32_t, std::string_view) const noexcept override { return true; }
  bool bindVertexArray(std::uint32_t) const noexcept override { return true; }
  bool bindArrayBuffer(std::uint32_t) const noexcept override { return true; }
  bool uploadArrayBuffer(std::uint32_t, const std::vector<std::byte> &, dzc::opengl::GlChunkBufferUsage) const noexcept override { return true; }
  bool configureVertexAttribute(std::uint32_t, dzc::opengl::GlChunkAttributeFormat, std::uint32_t, bool, std::uint32_t) const noexcept override { return true; }
  bool enableVertexAttribute(std::uint32_t) const noexcept override { return true; }
  bool unbindArrayBuffer() const noexcept override { return true; }
  bool unbindVertexArray() const noexcept override { return true; }
  bool bindDrawBuffer(dzc::opengl::GlDrawBufferTarget, std::uint32_t) const noexcept override { return true; }
  bool uploadDrawBuffer(dzc::opengl::GlDrawBufferTarget, std::uint32_t, const void *, std::size_t, dzc::opengl::GlDrawBufferUsage) const noexcept override { return true; }
  bool bindDrawBufferBase(dzc::opengl::GlDrawBufferTarget, std::uint32_t, std::uint32_t) const noexcept override { return true; }
  bool useProgram(std::uint32_t) const noexcept override { return true; }
  bool setProgramUniformUInt(std::uint32_t, std::string_view, std::uint32_t) const noexcept override { return true; }
  bool setViewport(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) const noexcept override { return true; }
  bool clearColor(const dzc::ColorRgba &) const noexcept override { return true; }
  bool drawPoints(std::uint32_t) const noexcept override { return !failDraw; }
};

class BackendFakeShader final : public dzc::opengl::IGlShaderOperations {
public:
  mutable std::uint32_t nextId{1000U};
  bool createShader(dzc::opengl::GlShaderStage, std::uint32_t &id) const override { id = nextId++; return true; }
  bool setShaderSource(std::uint32_t, std::string_view) const override { return true; }
  bool compileShader(std::uint32_t, std::string &log) const override { log.clear(); return true; }
  bool createProgram(std::uint32_t &id) const override { id = nextId++; return true; }
  bool attachShader(std::uint32_t, std::uint32_t) const override { return true; }
  bool linkProgram(std::uint32_t, std::string &log) const override { log.clear(); return true; }
  bool deleteShader(std::uint32_t) const override { return true; }
  bool deleteProgram(std::uint32_t) const override { return true; }
};

dzc::RenderFrame emptyBackendFrame() {
  dzc::RenderFrame frame;
  frame.frameId = dzc::FrameId{1U};
  frame.size = dzc::RenderSize{64U, 64U, 1.0F};
  frame.pointSize = 1.0F;
  return frame;
}

void testBackendIntegration() {
  auto context = std::make_shared<BackendFakeContext>();
  auto capabilities = std::make_shared<BackendFakeCapabilities>();
  auto draw = std::make_shared<BackendFakeDraw>();
  auto shader = std::make_shared<BackendFakeShader>();
  auto timer = std::make_shared<FakeTimerQueryOperations>();
  timer->resultNanoseconds = 2500000U;
  dzc::opengl::OpenGLBackend backend{context, capabilities, draw, draw, shader, {}, timer};
  assert(backend.init({dzc::RenderSize{64U, 64U, 1.0F}}).hasValue());
  assert(timer->createCalls == GlTimerQueryPool::queryDelayFrames);
  assert(backend.update(emptyBackendFrame()).hasValue());
  for (std::size_t frame = 0U; frame < GlTimerQueryPool::queryDelayFrames; ++frame) {
    assert(backend.render().hasValue());
    assert(!backend.latestGpuFrameMilliseconds().has_value());
  }
  assert(backend.render().hasValue());
  assert(backend.latestGpuFrameMilliseconds().has_value());
  assert(std::fabs(*backend.latestGpuFrameMilliseconds() - 2.5) < 1.0e-12);
  assert(timer->beginCalls == 4U && timer->endCalls == 4U);
  assert(timer->availabilityCalls == 1U && timer->readCalls == 1U);
  assert(backend.resize({0U, 64U, 1.0F}).hasValue());
  assert(backend.render().hasValue());
  assert(!backend.latestGpuFrameMilliseconds().has_value());
  backend.shutdown();
  assert(timer->deleteCalls == GlTimerQueryPool::queryDelayFrames);
}

void testBackendTimerFailures() {
  auto context = std::make_shared<BackendFakeContext>();
  auto capabilities = std::make_shared<BackendFakeCapabilities>();
  auto draw = std::make_shared<BackendFakeDraw>();
  auto shader = std::make_shared<BackendFakeShader>();
  auto createFailure = std::make_shared<FakeTimerQueryOperations>();
  createFailure->failCreate = true;
  dzc::opengl::OpenGLBackend failed{context, capabilities, draw, draw, shader, {}, createFailure};
  assert(!failed.init({dzc::RenderSize{64U, 64U, 1.0F}}).hasValue());
  assert(!failed.isInitialized());

  auto timer = std::make_shared<FakeTimerQueryOperations>();
  dzc::opengl::OpenGLBackend backend{context, capabilities, draw, draw, shader, {}, timer};
  assert(backend.init({dzc::RenderSize{64U, 64U, 1.0F}}).hasValue());
  assert(backend.update(emptyBackendFrame()).hasValue());
  timer->failBegin = true;
  assert(!backend.render().hasValue());
  assert(!backend.latestGpuFrameMilliseconds().has_value());
  timer->failBegin = false;
  timer->failEnd = true;
  assert(!backend.render().hasValue());
  assert(!backend.latestGpuFrameMilliseconds().has_value());
  timer->failEnd = false;
  timer->failDelete = true;
  backend.shutdown();
  assert(backend.lastError().has_value());
  assert(backend.lastError()->code == static_cast<std::uint32_t>(dzc::opengl::OpenGLBackendErrorCode::ShutdownFailed));
  timer->failDelete = false;
  backend.shutdown();
  assert(backend.state() == dzc::opengl::OpenGLBackendState::Shutdown);
}
} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "--real-context") {
    std::cout << "SKIPPED: real OpenGL Context infrastructure is not available in GL-011.\n";
    return 77;
  }
  testDelayedResolutionAndReuse();
  testFailuresAndThreadOwnership();
  testMoveOwnership();
  testBackendIntegration();
  testBackendTimerFailures();
  return 0;
}
