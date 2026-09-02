#include "OpenGLBackend.h"
#include "FakeTimerQueryOperations.h"
#include <dzc/OrbitCameraController.h>
#include "render/common/ShaderData.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace dzc;
using namespace dzc::opengl;

class FakeContext final : public IRenderContextOperations {
public:
  mutable bool current{true};
  mutable bool loaded{false};
  bool makeCurrent() const noexcept override { current = true; return true; }
  bool isCurrent() const noexcept override { return current; }
  bool loadFunctions() const noexcept override { loaded = true; return true; }
  bool functionsLoaded() const noexcept override { return loaded; }
  bool releaseCurrent() const noexcept override { current = false; return true; }
};
class FakeCapabilities final : public IOpenGLCapabilityQueries {
public:
  OpenGLVersion queryVersion() const override { return {4, 5, true}; }
  PointSizeLimits queryPointSizeLimits() const override { return {1.0F, 64.0F, 1.0F}; }
  OpenGLBufferLimits queryBufferLimits() const override { return {256U, 256U, 1024U * 1024U}; }
  OpenGLDeviceInfo queryDeviceInfo() const override { return {"Fake", "Fake", "Fake", "Fake"}; }
};
class FakeShader final : public IGlShaderOperations {
public:
  mutable std::uint32_t nextId{1000U};
  mutable std::size_t deletedShaders{};
  mutable std::size_t deletedPrograms{};
  bool createShader(GlShaderStage, std::uint32_t &id) const override { id = nextId++; return true; }
  bool setShaderSource(std::uint32_t, std::string_view source) const override { return !source.empty(); }
  bool compileShader(std::uint32_t, std::string &log) const override { log.clear(); return true; }
  bool createProgram(std::uint32_t &id) const override { id = nextId++; return true; }
  bool attachShader(std::uint32_t, std::uint32_t) const override { return true; }
  bool linkProgram(std::uint32_t, std::string &log) const override { log.clear(); return true; }
  bool deleteShader(std::uint32_t) const override { ++deletedShaders; return true; }
  bool deleteProgram(std::uint32_t) const override { ++deletedPrograms; return true; }
};
struct Viewport final { std::uint32_t x{}, y{}, width{}, height{}; };
struct Upload final { GlDrawBufferTarget target{}; std::vector<std::byte> bytes; };
class FakeDraw final : public IGlDrawOperations {
public:
  mutable std::uint32_t nextId{1U};
  mutable std::size_t deletedBuffers{};
  mutable std::vector<Viewport> viewports;
  mutable std::vector<Upload> uploads;
  mutable std::vector<std::uint32_t> draws;
  mutable std::size_t clearCalls{};
  mutable std::size_t useProgramCalls{};
  mutable bool failViewport{false};
  bool createBuffer(std::uint32_t &id) const noexcept override { id = nextId++; return true; }
  bool deleteBuffer(std::uint32_t) const noexcept override { ++deletedBuffers; return true; }
  bool labelBuffer(std::uint32_t, std::string_view) const noexcept override { return true; }
  bool createVertexArray(std::uint32_t &id) const noexcept override { id = nextId++; return true; }
  bool deleteVertexArray(std::uint32_t) const noexcept override { return true; }
  bool labelVertexArray(std::uint32_t, std::string_view) const noexcept override { return true; }
  bool bindVertexArray(std::uint32_t) const noexcept override { return true; }
  bool bindArrayBuffer(std::uint32_t) const noexcept override { return true; }
  bool uploadArrayBuffer(std::uint32_t, const std::vector<std::byte>&, GlChunkBufferUsage) const noexcept override { return true; }
  bool configureVertexAttribute(std::uint32_t, GlChunkAttributeFormat, std::uint32_t, bool, std::uint32_t) const noexcept override { return true; }
  bool enableVertexAttribute(std::uint32_t) const noexcept override { return true; }
  bool unbindArrayBuffer() const noexcept override { return true; }
  bool unbindVertexArray() const noexcept override { return true; }
  bool bindDrawBuffer(GlDrawBufferTarget, std::uint32_t) const noexcept override { return true; }
  bool uploadDrawBuffer(GlDrawBufferTarget target, std::uint32_t, const void *data, std::size_t size, GlDrawBufferUsage) const noexcept override {
    Upload upload; upload.target = target; upload.bytes.resize(size); if (size != 0U) std::memcpy(upload.bytes.data(), data, size); uploads.push_back(std::move(upload)); return true;
  }
  bool bindDrawBufferBase(GlDrawBufferTarget, std::uint32_t, std::uint32_t) const noexcept override { return true; }
  bool useProgram(std::uint32_t) const noexcept override { ++useProgramCalls; return true; }
  bool setProgramUniformUInt(std::uint32_t, std::string_view, std::uint32_t) const noexcept override { return true; }
  bool setViewport(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) const noexcept override {
    if (failViewport) return false; viewports.push_back({x, y, width, height}); return true;
  }
  bool clearColor(const ColorRgba&) const noexcept override { ++clearCalls; return true; }
  bool drawPoints(std::uint32_t count) const noexcept override { draws.push_back(count); return true; }
  void clearFrameCalls() const { uploads.clear(); draws.clear(); clearCalls = 0U; useProgramCalls = 0U; }
};

ChunkUpload upload(std::uint64_t id, std::uint32_t count) {
  ChunkUpload value; value.metadata.id = {id}; value.metadata.pointCount = count;
  value.metadata.schema.mask = static_cast<std::uint32_t>(PointAttribute::Position);
  value.cpuData.positions.resize(count); return value;
}
DrawChunk draw(std::uint64_t id, std::uint64_t count) {
  DrawChunk value; value.chunkId = {id}; value.pointCount = count; value.schema.mask = static_cast<std::uint32_t>(PointAttribute::Position); return value;
}
RenderFrame frame(RenderSize size, std::vector<DrawChunk> draws = {}) {
  RenderFrame value; value.frameId = {1U}; value.size = size; value.camera.view = glm::mat4(2.0F); value.camera.projection = glm::mat4(3.0F); value.backgroundColor = {0.1F, 0.2F, 0.3F, 1.0F}; value.pointSize = 2.0F; value.fixedColor = {0.8F, 0.7F, 0.6F, 1.0F}; value.heightRange = {-1.0F, 5.0F}; value.intensityRange = {0.0F, 1.0F}; value.draws = std::move(draws); return value;
}
struct Fixture final {
  std::shared_ptr<FakeContext> context{std::make_shared<FakeContext>()};
  std::shared_ptr<FakeCapabilities> capabilities{std::make_shared<FakeCapabilities>()};
  std::shared_ptr<FakeDraw> drawOperations{std::make_shared<FakeDraw>()};
  std::shared_ptr<FakeShader> shaderOperations{std::make_shared<FakeShader>()};
  OpenGLBackend backend{context, capabilities, drawOperations, drawOperations,
                      shaderOperations, {},
                      std::make_shared<dzc::opengl::test::FakeTimerQueryOperations>()};
  RenderSize size{100U, 50U, 1.5F};
  Fixture() { assert(backend.init({size}).hasValue()); }
};

template <class T> T decode(const std::vector<std::byte> &bytes) { assert(bytes.size() == sizeof(T)); T value{}; std::memcpy(&value, bytes.data(), sizeof(T)); return value; }
void assertError(const Result<void> &result, OpenGLBackendErrorCode code) { assert(!result.hasValue()); assert(result.error().code == static_cast<std::uint32_t>(code)); }

void testInitialAndRepeatedViewport() {
  Fixture fixture;
  assert(fixture.drawOperations->viewports.size() == 1U);
  assert((fixture.drawOperations->viewports[0].width == 150U));
  assert(fixture.drawOperations->viewports[0].height == 75U);
  assert(fixture.backend.resize({320U, 200U, 2.0F}).hasValue());
  assert((fixture.backend.renderSize() == RenderSize{320U, 200U, 2.0F}));
  assert(fixture.drawOperations->viewports.back().width == 640U);
  assert(fixture.drawOperations->viewports.back().height == 400U);
}
void testFrameSizeSyncAndProjectionPassthrough() {
  Fixture fixture;
  assert(fixture.backend.upload({{upload(1U, 2U)}}).hasValue());
  OrbitCameraController camera;
  auto input = frame(fixture.size, {draw(1U, 2U)});
  input.camera = camera.matrices(fixture.size);
  assert(fixture.backend.update(input).hasValue());
  assert(fixture.backend.render().hasValue());
  const auto initialData = decode<render::FrameData>(fixture.drawOperations->uploads.front().bytes);
  assert(initialData.projection == render::toColumnMajorArray(input.camera.projection));
  assert(fixture.backend.resize({200U, 100U, 1.0F}).hasValue());
  assert(!fixture.backend.currentFrame().has_value());
  assertError(fixture.backend.update(input), OpenGLBackendErrorCode::UpdateFailed);
  auto resized = frame({200U, 100U, 1.0F}, {draw(1U, 2U)});
  resized.camera = camera.matrices(resized.size);
  assert(fixture.backend.update(resized).hasValue());
  assert(fixture.backend.render().hasValue());
  const auto resizedData = decode<render::FrameData>(fixture.drawOperations->uploads[fixture.drawOperations->uploads.size() - 2U].bytes);
  assert(resizedData.projection == render::toColumnMajorArray(resized.camera.projection));
  assert(fixture.backend.drawCount() == 1U);
}
void testResizeFailurePreservesState() {
  Fixture fixture;
  assert(fixture.backend.upload({{upload(1U, 2U)}}).hasValue());
  assert(fixture.backend.update(frame(fixture.size, {draw(1U, 2U)})).hasValue());
  fixture.drawOperations->failViewport = true;
  assertError(fixture.backend.resize({200U, 100U, 1.0F}), OpenGLBackendErrorCode::ResizeFailed);
  assert(fixture.backend.renderSize() == fixture.size);
  assert(fixture.backend.currentFrame().has_value());
  fixture.drawOperations->failViewport = false;
  assert(fixture.backend.render().hasValue());
}
void testInitialViewportFailureReleasesDrawResources() {
  auto context = std::make_shared<FakeContext>();
  auto capabilities = std::make_shared<FakeCapabilities>();
  auto drawOperations = std::make_shared<FakeDraw>();
  auto shaderOperations = std::make_shared<FakeShader>();
  drawOperations->failViewport = true;
  OpenGLBackend backend{context, capabilities, drawOperations, drawOperations,
                      shaderOperations, {},
                      std::make_shared<dzc::opengl::test::FakeTimerQueryOperations>()};
  assertError(backend.init({RenderSize{100U, 50U, 1.0F}}),
              OpenGLBackendErrorCode::OperationFailed);
  assert(!backend.isInitialized());
  assert(drawOperations->deletedBuffers == 2U);
  assert(shaderOperations->deletedPrograms == 1U);
  assert(shaderOperations->deletedShaders == 2U);
}
void testZeroSizePauseAndResume() {
  Fixture fixture;
  assert(fixture.backend.upload({{upload(1U, 2U)}}).hasValue());
  assert(fixture.backend.update(frame(fixture.size, {draw(1U, 2U)})).hasValue());
  assert(fixture.backend.render().hasValue());
  fixture.drawOperations->clearFrameCalls();
  assert(fixture.backend.resize({0U, 50U, 1.5F}).hasValue());
  assert(fixture.drawOperations->viewports.size() == 1U);
  assert(!fixture.backend.update(frame({0U, 50U, 1.5F})).hasValue());
  assert(fixture.backend.render().hasValue());
  assert(fixture.backend.drawCount() == 0U && fixture.backend.submittedPointCount() == 0U);
  assert(fixture.drawOperations->uploads.empty() && fixture.drawOperations->clearCalls == 0U && fixture.drawOperations->useProgramCalls == 0U);
  assert(fixture.backend.resize(fixture.size).hasValue());
  assert(!fixture.backend.render().hasValue());
  assert(fixture.backend.update(frame(fixture.size, {draw(1U, 2U)})).hasValue());
  assert(fixture.backend.render().hasValue());
  const auto viewportCount = fixture.drawOperations->viewports.size();
  assert(fixture.backend.resize({100U, 0U, 1.5F}).hasValue());
  assert(fixture.drawOperations->viewports.size() == viewportCount);
  assert(fixture.backend.render().hasValue());
  assert(fixture.backend.drawCount() == 0U);
}
void testInvalidPhysicalViewport() {
  Fixture fixture;
  const auto previous = fixture.backend.renderSize();
  assertError(fixture.backend.resize({1U, 10U, 0.001F}), OpenGLBackendErrorCode::ResizeFailed);
  assert((fixture.backend.renderSize() == previous));
  assertError(fixture.backend.resize({0xffffffffU, 10U, 1.0F}), OpenGLBackendErrorCode::ResizeFailed);
  assert((fixture.backend.renderSize() == previous));
}
} // namespace
int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "--real-context") {
    std::cout << "SKIPPED: real OpenGL Context infrastructure is not available in GL-010.\n";
    return 77;
  }
  testInitialAndRepeatedViewport();
  testFrameSizeSyncAndProjectionPassthrough();
  testResizeFailurePreservesState();
  testInitialViewportFailureReleasesDrawResources();
  testZeroSizePauseAndResume();
  testInvalidPhysicalViewport();
  return 0;
}
