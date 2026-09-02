#include "OpenGLBackend.h"
#include "FakeTimerQueryOperations.h"
#include "render/common/ShaderData.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
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
  mutable std::vector<std::string> sources;
  mutable std::size_t deletedShaders{0U};
  mutable std::size_t deletedPrograms{0U};
  mutable bool failCompile{false};

  bool createShader(GlShaderStage, std::uint32_t& id) const override {
    id = nextId++;
    return true;
  }
  bool setShaderSource(std::uint32_t, std::string_view source) const override {
    sources.emplace_back(source);
    return !source.empty();
  }
  bool compileShader(std::uint32_t, std::string& log) const override {
    log = failCompile ? "Injected shader compilation failure" : "";
    return !failCompile;
  }
  bool createProgram(std::uint32_t& id) const override {
    id = nextId++;
    return true;
  }
  bool attachShader(std::uint32_t, std::uint32_t) const override { return true; }
  bool linkProgram(std::uint32_t, std::string& log) const override {
    log.clear();
    return true;
  }
  bool deleteShader(std::uint32_t) const override {
    ++deletedShaders;
    return true;
  }
  bool deleteProgram(std::uint32_t) const override {
    ++deletedPrograms;
    return true;
  }
};

struct BufferUpload final {
  GlDrawBufferTarget target;
  std::uint32_t bufferId;
  std::vector<std::byte> bytes;
};
struct BufferBinding final {
  GlDrawBufferTarget target;
  std::uint32_t binding;
  std::uint32_t bufferId;
};
struct UniformUpdate final {
  std::string name;
  std::uint32_t value;
};

bool operator==(const UniformUpdate& left, const UniformUpdate& right) noexcept {
  return left.name == right.name && left.value == right.value;
}

class FakeDraw final : public IGlDrawOperations {
public:
  mutable std::uint32_t nextId{1U};
  mutable std::size_t createdBuffers{0U};
  mutable std::size_t deletedBuffers{0U};
  mutable std::vector<BufferUpload> uploads;
  mutable std::vector<BufferBinding> bindings;
  mutable std::vector<UniformUpdate> uniforms;
  mutable std::vector<std::uint32_t> boundVaos;
  mutable std::vector<std::uint32_t> drawnPointCounts;
  mutable bool failUniform{false};

  bool createBuffer(std::uint32_t& id) const noexcept override {
    id = nextId++;
    ++createdBuffers;
    return true;
  }
  bool deleteBuffer(std::uint32_t) const noexcept override {
    ++deletedBuffers;
    return true;
  }
  bool labelBuffer(std::uint32_t, std::string_view) const noexcept override {
    return true;
  }
  bool createVertexArray(std::uint32_t& id) const noexcept override {
    id = nextId++;
    return true;
  }
  bool deleteVertexArray(std::uint32_t) const noexcept override { return true; }
  bool labelVertexArray(std::uint32_t, std::string_view) const noexcept override {
    return true;
  }
  bool bindVertexArray(std::uint32_t id) const noexcept override {
    boundVaos.push_back(id);
    return true;
  }
  bool bindArrayBuffer(std::uint32_t) const noexcept override { return true; }
  bool uploadArrayBuffer(std::uint32_t, const std::vector<std::byte>&,
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
  bool bindDrawBuffer(GlDrawBufferTarget, std::uint32_t) const noexcept override {
    return true;
  }
  bool uploadDrawBuffer(GlDrawBufferTarget target, std::uint32_t bufferId,
                        const void* data, std::size_t size,
                        GlDrawBufferUsage) const noexcept override {
    std::vector<std::byte> bytes(size);
    if (size != 0U)
      std::memcpy(bytes.data(), data, size);
    uploads.push_back({target, bufferId, std::move(bytes)});
    return true;
  }
  bool bindDrawBufferBase(GlDrawBufferTarget target, std::uint32_t binding,
                          std::uint32_t bufferId) const noexcept override {
    bindings.push_back({target, binding, bufferId});
    return true;
  }
  bool useProgram(std::uint32_t) const noexcept override { return true; }
  bool setProgramUniformUInt(std::uint32_t, std::string_view name,
                             std::uint32_t value) const noexcept override {
    if (failUniform)
      return false;
    uniforms.push_back({std::string(name), value});
    return true;
  }
  bool setViewport(std::uint32_t, std::uint32_t, std::uint32_t,
                   std::uint32_t) const noexcept override {
    return true;
  }
  bool clearColor(const ColorRgba&) const noexcept override { return true; }
  bool drawPoints(std::uint32_t pointCount) const noexcept override {
    drawnPointCounts.push_back(pointCount);
    return true;
  }

  void clearFrameCalls() const {
    uploads.clear();
    bindings.clear();
    uniforms.clear();
    boundVaos.clear();
    drawnPointCounts.clear();
  }
};

class CollectingLogSink final : public diagnostics::ILogSink {
public:
  std::vector<diagnostics::LogRecord> records;

  bool write(const diagnostics::LogRecord& record) override {
    records.push_back(record);
    return true;
  }
};

constexpr std::uint32_t positionMask =
    static_cast<std::uint32_t>(PointAttribute::Position);
constexpr std::uint32_t colorMask = static_cast<std::uint32_t>(PointAttribute::Color);
constexpr std::uint32_t intensityMask =
    static_cast<std::uint32_t>(PointAttribute::Intensity);

ChunkUpload makeUpload(std::uint64_t id, std::uint32_t pointCount,
                       std::uint32_t schemaMask) {
  ChunkUpload result;
  result.metadata.id = {id};
  result.metadata.pointCount = pointCount;
  result.metadata.schema.mask = schemaMask;
  result.cpuData.positions.resize(pointCount);
  if ((schemaMask & colorMask) != 0U)
    result.cpuData.colorsRgba8.resize(pointCount, 0x11223344U);
  if ((schemaMask & intensityMask) != 0U)
    result.cpuData.intensities.resize(pointCount, 32768U);
  return result;
}

RenderFrame makeFrame(ShadingMode mode, std::uint32_t schemaMask,
                      float pointSize = 5.0F) {
  RenderFrame result;
  result.frameId = {9U};
  result.size = {64U, 64U, 1.0F};
  result.pointSize = pointSize;
  result.shadingMode = mode;
  result.fixedColor = {0.2F, 0.3F, 0.4F, 0.8F};
  result.heightRange = {-10.0F, 10.0F};
  result.intensityRange = {0.1F, 0.9F};
  result.draws.push_back({ChunkId{1U}, 2U, glm::vec3{1.0F, 2.0F, 3.0F},
                          AttributeSchema{schemaMask}});
  return result;
}

template <class T> T decode(const std::vector<std::byte>& bytes) {
  assert(bytes.size() == sizeof(T));
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  return value;
}

class Fixture final {
public:
  std::shared_ptr<FakeContext> context = std::make_shared<FakeContext>();
  std::shared_ptr<FakeCapabilities> capabilities =
      std::make_shared<FakeCapabilities>();
  std::shared_ptr<FakeDraw> draw = std::make_shared<FakeDraw>();
  std::shared_ptr<FakeShader> shader = std::make_shared<FakeShader>();
  OpenGLBackend backend{context, capabilities, draw, draw, shader, {},
                        std::make_shared<dzc::opengl::test::FakeTimerQueryOperations>()};

  Fixture() {
    assert(backend.init({RenderSize{64U, 64U, 1.0F}}).hasValue());
    assert(backend.upload(
        {{makeUpload(1U, 2U, positionMask | colorMask | intensityMask)}})
               .hasValue());
  }
  ~Fixture() { backend.shutdown(); }
};

void testShaderSourceContract() {
  Fixture fixture;
  assert(fixture.shader->sources.size() == 2U);
  const std::string& vertex = fixture.shader->sources[0];
  const std::string& fragment = fixture.shader->sources[1];
  assert(vertex.find("gl_PointSize = pointSize") != std::string::npos);
  assert(vertex.find("vertexHeight = localPosition.z") != std::string::npos);
  assert(fragment.find("uniform FrameData") != std::string::npos);
  assert(fragment.find("range.x == range.y") != std::string::npos);
  assert(fragment.find("return 0.5") != std::string::npos);
  assert(fragment.find("vec3(0.0, 0.0, 1.0)") != std::string::npos);
  assert(fragment.find("vec3(0.0, 1.0, 1.0)") != std::string::npos);
  assert(fragment.find("vec3(0.0, 1.0, 0.0)") != std::string::npos);
  assert(fragment.find("vec3(1.0, 1.0, 0.0)") != std::string::npos);
  assert(fragment.find("vec3(1.0, 0.0, 0.0)") != std::string::npos);
  assert(fragment.find("drawHasColor != 0u ? vertexColor : fixedColor") !=
         std::string::npos);
  assert(fragment.find("drawHasIntensity != 0u") != std::string::npos);
}

void testModesAndFrameData() {
  Fixture fixture;
  for (const auto mode : {ShadingMode::OriginalColor, ShadingMode::FixedColor,
                          ShadingMode::Height, ShadingMode::Intensity}) {
    fixture.draw->clearFrameCalls();
    const auto input = makeFrame(mode, positionMask | colorMask | intensityMask);
    assert(fixture.backend.update(input).hasValue());
    assert(fixture.backend.render().hasValue());
    assert(fixture.draw->uploads.size() == 2U);
    const auto data = decode<render::FrameData>(fixture.draw->uploads[0].bytes);
    assert(data.shadingMode == render::toShaderShadingMode(mode));
    assert(data.pointSize == input.pointSize);
    const std::array<float, 4> expectedHeight{-10.0F, 10.0F, 0.0F, 0.0F};
    assert(data.heightRange == expectedHeight);
    const std::array<float, 4> expectedIntensity{0.1F, 0.9F, 0.0F, 0.0F};
    assert(data.intensityRange == expectedIntensity);
    assert((fixture.draw->uniforms ==
            std::vector<UniformUpdate>{{"drawHasColor", 1U},
                                       {"drawHasIntensity", 1U}}));
    assert((fixture.draw->drawnPointCounts == std::vector<std::uint32_t>{2U}));
    assert(fixture.draw->bindings.size() == 2U);
    assert(fixture.draw->bindings[0].binding == render::frameDataBinding);
    assert(fixture.draw->bindings[1].binding == render::chunkDataBinding);
  }

  fixture.draw->clearFrameCalls();
  const auto next = makeFrame(ShadingMode::FixedColor,
                              positionMask | colorMask | intensityMask, 9.0F);
  assert(fixture.backend.update(next).hasValue());
  assert(fixture.backend.render().hasValue());
  assert(decode<render::FrameData>(fixture.draw->uploads[0].bytes).pointSize ==
         9.0F);
}

void testMissingAttributesWarnOnce() {
  auto context = std::make_shared<FakeContext>();
  auto capabilities = std::make_shared<FakeCapabilities>();
  auto draw = std::make_shared<FakeDraw>();
  auto shader = std::make_shared<FakeShader>();
  auto sink = std::make_shared<CollectingLogSink>();
  OpenGLBackend backend{context, capabilities, draw, draw, shader, sink,
                        std::make_shared<dzc::opengl::test::FakeTimerQueryOperations>()};
  assert(backend.init({RenderSize{64U, 64U, 1.0F}}).hasValue());
  assert(backend.upload({{makeUpload(1U, 2U, positionMask)}}).hasValue());

  const auto original = makeFrame(ShadingMode::OriginalColor, positionMask);
  assert(backend.update(original).hasValue());
  assert(backend.render().hasValue());
  assert(backend.render().hasValue());
  assert(sink->records.size() == 1U);
  assert(sink->records[0].level == diagnostics::LogLevel::Warn);
  assert(sink->records[0].module == "OpenGLBackend");
  assert(sink->records[0].chunk == 1U);
  assert(sink->records[0].frame == 9U);
  assert(sink->records[0].message.find("Color") != std::string::npos);

  const auto intensity = makeFrame(ShadingMode::Intensity, positionMask);
  assert(backend.update(intensity).hasValue());
  assert(backend.render().hasValue());
  assert(backend.render().hasValue());
  assert(sink->records.size() == 2U);
  assert(sink->records[1].message.find("Intensity") != std::string::npos);
  assert((draw->uniforms.size() >= 2U));
  assert(draw->uniforms[draw->uniforms.size() - 2U].value == 0U);
  assert(draw->uniforms.back().value == 0U);
  backend.shutdown();
}

void testInvalidRangeUniformFailureAndStatistics() {
  Fixture fixture;
  auto invalid = makeFrame(ShadingMode::Height, positionMask | colorMask);
  invalid.heightRange = {2.0F, 1.0F};
  assert(!fixture.backend.update(invalid).hasValue());
  invalid = makeFrame(ShadingMode::Intensity, positionMask | colorMask);
  invalid.intensityRange = {0.0F, std::numeric_limits<float>::infinity()};
  assert(!fixture.backend.update(invalid).hasValue());

  const auto valid = makeFrame(ShadingMode::Height, positionMask | colorMask);
  assert(fixture.backend.update(valid).hasValue());
  assert(fixture.backend.render().hasValue());
  assert(fixture.backend.drawCount() == 1U);
  assert(fixture.backend.submittedPointCount() == 2U);

  fixture.draw->clearFrameCalls();
  fixture.draw->failUniform = true;
  assert(!fixture.backend.render().hasValue());
  assert(fixture.draw->drawnPointCounts.empty());
  assert(fixture.backend.drawCount() == 1U);
  assert(fixture.backend.submittedPointCount() == 2U);
}

void testShaderInitializationFailureReleasesBuffers() {
  auto context = std::make_shared<FakeContext>();
  auto capabilities = std::make_shared<FakeCapabilities>();
  auto draw = std::make_shared<FakeDraw>();
  auto shader = std::make_shared<FakeShader>();
  shader->failCompile = true;
  OpenGLBackend backend{context, capabilities, draw, draw, shader, {},
                        std::make_shared<dzc::opengl::test::FakeTimerQueryOperations>()};
  const auto result = backend.init({RenderSize{64U, 64U, 1.0F}});
  assert(!result.hasValue());
  assert(draw->createdBuffers == 2U);
  assert(draw->deletedBuffers == 2U);
  assert(shader->deletedShaders == 1U);
  assert(shader->deletedPrograms == 0U);
  backend.shutdown();
}

} // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--real-context") {
    std::cout << "SKIPPED: real OpenGL Context infrastructure is not available "
                 "in GL-009.\n";
    return 77;
  }
  testShaderSourceContract();
  testModesAndFrameData();
  testMissingAttributesWarnOnce();
  testInvalidRangeUniformFailureAndStatistics();
  testShaderInitializationFailureReleasesBuffers();
  return 0;
}