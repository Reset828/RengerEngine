#include "OpenGLBackend.h"
#include "FakeTimerQueryOperations.h"
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
  bool makeCurrent() const noexcept override {
    current = true;
    return true;
  }
  bool isCurrent() const noexcept override { return current; }
  bool loadFunctions() const noexcept override {
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
  OpenGLVersion queryVersion() const override { return {4, 5, true}; }
  PointSizeLimits queryPointSizeLimits() const override {
    return {1.0F, 64.0F, 1.0F};
  }
  OpenGLBufferLimits queryBufferLimits() const override {
    return {256U, 256U, 1024U * 1024U};
  }
  OpenGLDeviceInfo queryDeviceInfo() const override {
    return {"Fake", "Fake", "Fake", "Fake"};
  }
};
class FakeShader final : public IGlShaderOperations {
public:
  mutable std::uint32_t nextId{1000U};
  bool createShader(GlShaderStage, std::uint32_t &id) const override {
    id = nextId++;
    return true;
  }
  bool setShaderSource(std::uint32_t, std::string_view source) const override {
    return !source.empty();
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
struct BufferUpload final {
  GlDrawBufferTarget target{};
  std::uint32_t id{};
  std::vector<std::byte> bytes;
};
struct BaseBinding final {
  GlDrawBufferTarget target{};
  std::uint32_t binding{};
  std::uint32_t id{};
};
class FakeDraw final : public IGlDrawOperations {
public:
  mutable std::uint32_t nextId{1U};
  mutable std::size_t createdBuffers{};
  mutable std::size_t createdVertexArrays{};
  mutable std::vector<BufferUpload> uploads;
  mutable std::vector<BaseBinding> bindings;
  mutable std::vector<std::uint32_t> boundVertexArrays;
  mutable std::vector<std::uint32_t> drawPointCounts;
  mutable std::vector<ColorRgba> clears;
  mutable std::uint32_t usedProgram{};
  mutable bool failDraw{false};

  bool createBuffer(std::uint32_t &id) const noexcept override {
    id = nextId++;
    ++createdBuffers;
    return true;
  }
  bool deleteBuffer(std::uint32_t) const noexcept override { return true; }
  bool labelBuffer(std::uint32_t, std::string_view) const noexcept override {
    return true;
  }
  bool createVertexArray(std::uint32_t &id) const noexcept override {
    id = nextId++;
    ++createdVertexArrays;
    return true;
  }
  bool deleteVertexArray(std::uint32_t) const noexcept override { return true; }
  bool labelVertexArray(std::uint32_t,
                        std::string_view) const noexcept override {
    return true;
  }
  bool bindVertexArray(std::uint32_t id) const noexcept override {
    boundVertexArrays.push_back(id);
    return true;
  }
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
  bool uploadDrawBuffer(GlDrawBufferTarget target, std::uint32_t id,
                        const void *data, std::size_t size,
                        GlDrawBufferUsage) const noexcept override {
    BufferUpload value;
    value.target = target;
    value.id = id;
    value.bytes.resize(size);
    if (size)
      std::memcpy(value.bytes.data(), data, size);
    uploads.push_back(std::move(value));
    return true;
  }
  bool bindDrawBufferBase(GlDrawBufferTarget target, std::uint32_t binding,
                          std::uint32_t id) const noexcept override {
    bindings.push_back({target, binding, id});
    return true;
  }
  bool useProgram(std::uint32_t id) const noexcept override {
    usedProgram = id;
    return id != 0;
  }
  bool setProgramUniformUInt(std::uint32_t, std::string_view,
                             std::uint32_t) const noexcept override {
    return true;
  }
  bool setViewport(std::uint32_t, std::uint32_t, std::uint32_t,
                   std::uint32_t) const noexcept override {
    return true;
  }
  bool clearColor(const ColorRgba &color) const noexcept override {
    clears.push_back(color);
    return true;
  }
  bool drawPoints(std::uint32_t count) const noexcept override {
    if (failDraw)
      return false;
    drawPointCounts.push_back(count);
    return true;
  }
  void clearFrameCalls() const {
    uploads.clear();
    bindings.clear();
    boundVertexArrays.clear();
    drawPointCounts.clear();
    clears.clear();
    usedProgram = 0U;
  }
};

ChunkUpload upload(std::uint64_t id, std::uint32_t count) {
  ChunkUpload value;
  value.metadata.id = {id};
  value.metadata.pointCount = count;
  value.metadata.schema.mask =
      static_cast<std::uint32_t>(PointAttribute::Position);
  value.cpuData.positions.resize(count);
  return value;
}
DrawChunk draw(std::uint64_t id, std::uint64_t count,
               glm::vec3 origin = glm::vec3{0.0F, 0.0F, 0.0F}) {
  DrawChunk value;
  value.chunkId = {id};
  value.pointCount = count;
  value.relativeOrigin = origin;
  value.schema.mask = static_cast<std::uint32_t>(PointAttribute::Position);
  return value;
}
RenderFrame frame(std::vector<DrawChunk> draws = {}) {
  RenderFrame value;
  value.frameId = {7U};
  value.size = {640U, 480U, 1.0F};
  value.backgroundColor = {0.1F, 0.2F, 0.3F, 1.0F};
  value.fixedColor = {0.4F, 0.5F, 0.6F, 1.0F};
  value.pointSize = 3.0F;
  value.draws = std::move(draws);
  return value;
}
struct Fixture {
  std::shared_ptr<FakeContext> context = std::make_shared<FakeContext>();
  std::shared_ptr<FakeCapabilities> capabilities =
      std::make_shared<FakeCapabilities>();
  std::shared_ptr<FakeDraw> operations = std::make_shared<FakeDraw>();
  std::shared_ptr<FakeShader> shader = std::make_shared<FakeShader>();
  OpenGLBackend backend{context, capabilities, operations, operations, shader, {},
                        std::make_shared<dzc::opengl::test::FakeTimerQueryOperations>()};
  Fixture() { assert(backend.init({RenderSize{640U, 480U, 1.0F}}).hasValue()); }
  ~Fixture() { backend.shutdown(); }
};
void assertRenderError(const Result<void> &result) {
  assert(!result.hasValue());
  assert(result.error().code ==
         static_cast<std::uint32_t>(OpenGLBackendErrorCode::RenderFailed));
}

template <class T> T decode(const std::vector<std::byte> &bytes) {
  assert(bytes.size() == sizeof(T));
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  return value;
}

void testInitializationAndDraws() {
  Fixture fixture;
  assert(fixture.operations->createdBuffers == 2U);
  UploadBatch batch;
  batch.chunks = {upload(1U, 2U), upload(2U, 3U), upload(3U, 4U)};
  assert(fixture.backend.upload(batch).hasValue());
  fixture.operations->clearFrameCalls();
  auto value = frame({draw(2U, 3U, {10.0F, 20.0F, 30.0F}),
                      draw(1U, 2U, {-1.0F, -2.0F, -3.0F})});
  assert(fixture.backend.update(value).hasValue());
  assert(fixture.backend.render().hasValue());
  assert((fixture.operations->drawPointCounts ==
          std::vector<std::uint32_t>{3U, 2U}));
  assert(fixture.backend.drawCount() == 2U);
  assert(fixture.backend.submittedPointCount() == 5U);
  assert(fixture.operations->clears.size() == 1U);
  assert(fixture.operations->clears[0] == value.backgroundColor);
  assert(fixture.operations->usedProgram != 0U);
  assert(fixture.operations->uploads.size() == 3U);
  assert(fixture.operations->uploads[0].target ==
         GlDrawBufferTarget::UniformBuffer);
  auto frameData =
      decode<dzc::render::FrameData>(fixture.operations->uploads[0].bytes);
  assert(frameData.pointSize == 3.0F);
  assert(frameData.fixedColor[0] == 0.4F);
  auto chunk0 =
      decode<dzc::render::ChunkData>(fixture.operations->uploads[1].bytes);
  auto chunk1 =
      decode<dzc::render::ChunkData>(fixture.operations->uploads[2].bytes);
  assert(chunk0.relativeChunkOrigin[0] == 10.0F);
  assert(chunk1.relativeChunkOrigin[2] == -3.0F);
  assert(fixture.operations->bindings[0].binding ==
         dzc::render::frameDataBinding);
  assert(fixture.operations->bindings[1].binding ==
         dzc::render::chunkDataBinding);
}
void testEmptyAndInvisible() {
  Fixture fixture;
  UploadBatch batch;
  batch.chunks = {upload(1U, 2U)};
  assert(fixture.backend.upload(batch).hasValue());
  fixture.operations->clearFrameCalls();
  assert(fixture.backend.update(frame()).hasValue());
  assert(fixture.backend.render().hasValue());
  assert(fixture.operations->drawPointCounts.empty());
  assert(fixture.backend.drawCount() == 0U);
  assert(fixture.backend.submittedPointCount() == 0U);
}
void testPrevalidationAndStatistics() {
  Fixture fixture;
  UploadBatch batch;
  batch.chunks = {upload(1U, 2U)};
  assert(fixture.backend.upload(batch).hasValue());
  assert(fixture.backend.update(frame({draw(1U, 2U)})).hasValue());
  assert(fixture.backend.render().hasValue());
  assert(fixture.backend.drawCount() == 1U);
  fixture.operations->clearFrameCalls();
  assert(fixture.backend.update(frame({draw(9U, 2U)})).hasValue());
  assertRenderError(fixture.backend.render());
  assert(fixture.operations->uploads.empty());
  assert(fixture.operations->clears.empty());
  assert(fixture.backend.drawCount() == 1U);
  assert(fixture.backend.submittedPointCount() == 2U);
  fixture.operations->clearFrameCalls();
  assert(fixture.backend.update(frame({draw(1U, 3U)})).hasValue());
  assertRenderError(fixture.backend.render());
  assert(fixture.operations->drawPointCounts.empty());
  assert(fixture.backend.drawCount() == 1U);
}
void testOperationFailurePreservesStatistics() {
  Fixture fixture;
  UploadBatch batch;
  batch.chunks = {upload(1U, 2U)};
  assert(fixture.backend.upload(batch).hasValue());
  assert(fixture.backend.update(frame({draw(1U, 2U)})).hasValue());
  assert(fixture.backend.render().hasValue());
  fixture.operations->clearFrameCalls();
  fixture.operations->failDraw = true;
  assertRenderError(fixture.backend.render());
  assert(fixture.backend.drawCount() == 1U);
  assert(fixture.backend.submittedPointCount() == 2U);
}
} // namespace
int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "--real-context") {
    std::cout << "SKIPPED: real OpenGL Context infrastructure is not available "
                 "in GL-008.\n";
    return 77;
  }
  testInitializationAndDraws();
  testEmptyAndInvisible();
  testPrevalidationAndStatistics();
  testOperationFailurePreservesStatistics();
  return 0;
}
