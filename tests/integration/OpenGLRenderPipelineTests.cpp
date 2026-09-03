#include "OpenGLBackend.h"
#include "data/chunk/GridChunkBuilder.h"
#include "render/common/ShaderData.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using namespace dzc;
using namespace dzc::opengl;

constexpr std::uint32_t positionMask =
    static_cast<std::uint32_t>(PointAttribute::Position);
constexpr std::uint32_t colorMask =
    static_cast<std::uint32_t>(PointAttribute::Color);
constexpr std::uint32_t intensityMask =
    static_cast<std::uint32_t>(PointAttribute::Intensity);
constexpr std::uint32_t fullAttributeMask =
    positionMask | colorMask | intensityMask;

class FakeContextOperations final : public IRenderContextOperations {
public:
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

  mutable bool current{true};
  mutable bool loaded{false};
};

class FakeCapabilityQueries final : public IOpenGLCapabilityQueries {
public:
  OpenGLVersion queryVersion() const override { return {4, 5, true}; }
  PointSizeLimits queryPointSizeLimits() const override {
    return {1.0F, 64.0F, 1.0F};
  }
  OpenGLBufferLimits queryBufferLimits() const override {
    return {256U, 256U, 1024U * 1024U};
  }
  OpenGLDeviceInfo queryDeviceInfo() const override {
    return {"GL-012 Fake Vendor", "GL-012 Fake Renderer", "4.5 Fake Driver",
            "4.50 Fake GLSL"};
  }
};

struct ArrayUpload final {
  std::uint32_t bufferId{0U};
  std::vector<std::byte> bytes;
};

struct AttributeConfiguration final {
  std::uint32_t vertexArrayId{0U};
  std::uint32_t bufferId{0U};
  std::uint32_t attributeIndex{0U};
  GlChunkAttributeFormat format{GlChunkAttributeFormat::Float32};
  std::uint32_t componentCount{0U};
  bool normalized{false};
  std::uint32_t stride{0U};
};

struct DrawBufferUpload final {
  GlDrawBufferTarget target{GlDrawBufferTarget::UniformBuffer};
  std::uint32_t bufferId{0U};
  std::vector<std::byte> bytes;
};

struct DrawBufferBinding final {
  GlDrawBufferTarget target{GlDrawBufferTarget::UniformBuffer};
  std::uint32_t binding{0U};
  std::uint32_t bufferId{0U};
};

struct UniformUpdate final {
  std::string name;
  std::uint32_t value{0U};
};

struct ViewportCall final {
  std::uint32_t x{0U};
  std::uint32_t y{0U};
  std::uint32_t width{0U};
  std::uint32_t height{0U};
};

class FakeDrawOperations final : public IGlDrawOperations {
public:
  bool createBuffer(std::uint32_t &id) const noexcept override {
    id = nextId++;
    return liveBuffers.insert(id).second;
  }

  bool deleteBuffer(std::uint32_t id) const noexcept override {
    return liveBuffers.erase(id) == 1U;
  }

  bool labelBuffer(std::uint32_t id,
                   std::string_view) const noexcept override {
    return liveBuffers.count(id) == 1U;
  }

  bool createVertexArray(std::uint32_t &id) const noexcept override {
    id = nextId++;
    return liveVertexArrays.insert(id).second;
  }

  bool deleteVertexArray(std::uint32_t id) const noexcept override {
    return liveVertexArrays.erase(id) == 1U;
  }

  bool labelVertexArray(std::uint32_t id,
                        std::string_view) const noexcept override {
    return liveVertexArrays.count(id) == 1U;
  }

  bool bindVertexArray(std::uint32_t id) const noexcept override {
    if (id != 0U && liveVertexArrays.count(id) != 1U)
      return false;
    boundVertexArray = id;
    if (id != 0U)
      drawnVertexArrays.push_back(id);
    return true;
  }

  bool bindArrayBuffer(std::uint32_t id) const noexcept override {
    if (id != 0U && liveBuffers.count(id) != 1U)
      return false;
    boundArrayBuffer = id;
    return true;
  }

  bool uploadArrayBuffer(std::uint32_t id,
                         const std::vector<std::byte> &bytes,
                         GlChunkBufferUsage) const noexcept override {
    if (id == 0U || id != boundArrayBuffer || liveBuffers.count(id) != 1U)
      return false;
    arrayUploads.push_back({id, bytes});
    return true;
  }

  bool configureVertexAttribute(std::uint32_t attributeIndex,
                                GlChunkAttributeFormat format,
                                std::uint32_t componentCount, bool normalized,
                                std::uint32_t stride) const noexcept override {
    if (boundVertexArray == 0U || boundArrayBuffer == 0U)
      return false;
    attributeConfigurations.push_back(
        {boundVertexArray, boundArrayBuffer, attributeIndex, format,
         componentCount, normalized, stride});
    return true;
  }

  bool enableVertexAttribute(std::uint32_t attributeIndex) const noexcept override {
    enabledAttributes.push_back(attributeIndex);
    return boundVertexArray != 0U;
  }

  bool unbindArrayBuffer() const noexcept override {
    boundArrayBuffer = 0U;
    return true;
  }

  bool unbindVertexArray() const noexcept override {
    boundVertexArray = 0U;
    return true;
  }

  bool bindDrawBuffer(GlDrawBufferTarget,
                      std::uint32_t bufferId) const noexcept override {
    return liveBuffers.count(bufferId) == 1U;
  }

  bool uploadDrawBuffer(GlDrawBufferTarget target, std::uint32_t bufferId,
                        const void *data, std::size_t size,
                        GlDrawBufferUsage) const noexcept override {
    if (liveBuffers.count(bufferId) != 1U || data == nullptr || size == 0U)
      return false;
    std::vector<std::byte> bytes(size);
    std::memcpy(bytes.data(), data, size);
    drawBufferUploads.push_back({target, bufferId, std::move(bytes)});
    return true;
  }

  bool bindDrawBufferBase(GlDrawBufferTarget target, std::uint32_t binding,
                          std::uint32_t bufferId) const noexcept override {
    if (liveBuffers.count(bufferId) != 1U)
      return false;
    drawBufferBindings.push_back({target, binding, bufferId});
    return true;
  }

  bool useProgram(std::uint32_t programId) const noexcept override {
    usedPrograms.push_back(programId);
    return programId != 0U;
  }

  bool setProgramUniformUInt(std::uint32_t programId, std::string_view name,
                             std::uint32_t value) const noexcept override {
    if (programId == 0U)
      return false;
    uniformUpdates.push_back({std::string(name), value});
    return true;
  }

  bool setViewport(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                   std::uint32_t height) const noexcept override {
    viewports.push_back({x, y, width, height});
    return width > 0U && height > 0U;
  }

  bool clearColor(const ColorRgba &color) const noexcept override {
    clearColors.push_back(color);
    return true;
  }

  bool drawPoints(std::uint32_t pointCount) const noexcept override {
    if (boundVertexArray == 0U || pointCount == 0U)
      return false;
    drawnPointCounts.push_back(pointCount);
    return true;
  }

  mutable std::uint32_t nextId{1U};
  mutable std::unordered_set<std::uint32_t> liveBuffers;
  mutable std::unordered_set<std::uint32_t> liveVertexArrays;
  mutable std::uint32_t boundArrayBuffer{0U};
  mutable std::uint32_t boundVertexArray{0U};
  mutable std::vector<ArrayUpload> arrayUploads;
  mutable std::vector<AttributeConfiguration> attributeConfigurations;
  mutable std::vector<std::uint32_t> enabledAttributes;
  mutable std::vector<DrawBufferUpload> drawBufferUploads;
  mutable std::vector<DrawBufferBinding> drawBufferBindings;
  mutable std::vector<UniformUpdate> uniformUpdates;
  mutable std::vector<ViewportCall> viewports;
  mutable std::vector<ColorRgba> clearColors;
  mutable std::vector<std::uint32_t> usedPrograms;
  mutable std::vector<std::uint32_t> drawnVertexArrays;
  mutable std::vector<std::uint32_t> drawnPointCounts;
};

class FakeShaderOperations final : public IGlShaderOperations {
public:
  bool createShader(GlShaderStage stage,
                    std::uint32_t &id) const override {
    id = nextId++;
    shaderStages[id] = stage;
    liveShaders.insert(id);
    return true;
  }

  bool setShaderSource(std::uint32_t id,
                       std::string_view source) const override {
    if (liveShaders.count(id) != 1U || source.empty())
      return false;
    shaderSources[id] = std::string(source);
    return true;
  }

  bool compileShader(std::uint32_t id, std::string &log) const override {
    if (liveShaders.count(id) != 1U)
      return false;
    if (failCompile) {
      log = "Injected GL-012 shader compilation failure";
      return false;
    }
    log.clear();
    return true;
  }

  bool createProgram(std::uint32_t &id) const override {
    id = nextId++;
    livePrograms.insert(id);
    return true;
  }

  bool attachShader(std::uint32_t programId,
                    std::uint32_t shaderId) const override {
    return livePrograms.count(programId) == 1U &&
           liveShaders.count(shaderId) == 1U;
  }

  bool linkProgram(std::uint32_t programId, std::string &log) const override {
    log.clear();
    return livePrograms.count(programId) == 1U;
  }

  bool deleteShader(std::uint32_t id) const override {
    shaderSources.erase(id);
    shaderStages.erase(id);
    return liveShaders.erase(id) == 1U;
  }

  bool deleteProgram(std::uint32_t id) const override {
    return livePrograms.erase(id) == 1U;
  }

  mutable bool failCompile{false};
  mutable std::uint32_t nextId{1000U};
  mutable std::unordered_set<std::uint32_t> liveShaders;
  mutable std::unordered_set<std::uint32_t> livePrograms;
  mutable std::unordered_map<std::uint32_t, GlShaderStage> shaderStages;
  mutable std::unordered_map<std::uint32_t, std::string> shaderSources;
};

class FakeTimerQueryOperations final : public IGlTimerQueryOperations {
public:
  bool createTimerQuery(std::uint32_t &id) const noexcept override {
    id = nextId++;
    return liveQueries.insert(id).second;
  }

  bool deleteTimerQuery(std::uint32_t id) const noexcept override {
    return liveQueries.erase(id) == 1U;
  }

  bool beginElapsedTimeQuery(std::uint32_t id) const noexcept override {
    if (activeQuery != 0U || liveQueries.count(id) != 1U)
      return false;
    activeQuery = id;
    ++beginCalls;
    return true;
  }

  bool endElapsedTimeQuery() const noexcept override {
    if (activeQuery == 0U)
      return false;
    activeQuery = 0U;
    ++endCalls;
    return true;
  }

  bool isTimerQueryResultAvailable(std::uint32_t id,
                                   bool &available) const noexcept override {
    if (liveQueries.count(id) != 1U)
      return false;
    ++availabilityCalls;
    available = true;
    return true;
  }

  bool readTimerQueryResultNanoseconds(
      std::uint32_t id, std::uint64_t &nanoseconds) const noexcept override {
    if (liveQueries.count(id) != 1U)
      return false;
    ++readCalls;
    nanoseconds = resultNanoseconds;
    return true;
  }

  mutable std::uint32_t nextId{2000U};
  mutable std::uint32_t activeQuery{0U};
  mutable std::uint32_t beginCalls{0U};
  mutable std::uint32_t endCalls{0U};
  mutable std::uint32_t availabilityCalls{0U};
  mutable std::uint32_t readCalls{0U};
  mutable std::uint64_t resultNanoseconds{2500000U};
  mutable std::unordered_set<std::uint32_t> liveQueries;
};

template <class T> T decode(const std::vector<std::byte> &bytes) {
  assert(bytes.size() == sizeof(T));
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  return value;
}

GridBucket makeFullAttributeBucket() {
  GridBucket bucket;
  bucket.key = {2, -1, 4};
  bucket.points.schema.mask = fullAttributeMask;
  bucket.points.positions = {
      {1000000.0, 2000000.0, 10.0},
      {1000002.0, 2000002.0, 20.0},
      {1000004.0, 2000004.0, 30.0},
  };
  bucket.points.colorsRgba8 = {0xFF0000FFU, 0x00FF00FFU, 0x0000FFFFU};
  bucket.points.intensities = {100U, 500U, 900U};
  bucket.sourceIndices = {0U, 1U, 2U};
  return bucket;
}

RenderFrame makeFrame(const Chunk &chunk, FrameId frameId, RenderSize size,
                      ShadingMode mode, const glm::dvec3 &cameraOrigin) {
  RenderFrame frame;
  frame.frameId = frameId;
  frame.camera.view = glm::mat4{1.0F};
  frame.camera.projection = glm::mat4{1.0F};
  frame.camera.cameraOrigin = cameraOrigin;
  frame.size = size;
  frame.backgroundColor = {0.05F, 0.1F, 0.15F, 1.0F};
  frame.pointSize = 4.0F;
  frame.shadingMode = mode;
  frame.fixedColor = {0.25F, 0.5F, 0.75F, 1.0F};
  frame.heightRange = {10.0F, 30.0F};
  frame.intensityRange = {100.0F, 900.0F};
  const glm::dvec3 relative = chunk.metadata().origin - cameraOrigin;
  frame.draws.push_back(
      {chunk.metadata().id, chunk.metadata().pointCount,
       glm::vec3{relative}, chunk.metadata().schema});
  return frame;
}

const DrawBufferUpload &lastUpload(const FakeDrawOperations &operations,
                                   GlDrawBufferTarget target) {
  for (auto it = operations.drawBufferUploads.rbegin();
       it != operations.drawBufferUploads.rend(); ++it) {
    if (it->target == target)
      return *it;
  }
  assert(false);
  return operations.drawBufferUploads.front();
}

void assertUploadLayout(const FakeDrawOperations &operations) {
  assert(operations.arrayUploads.size() == 3U);
  assert(operations.arrayUploads[0].bytes.size() == 3U * 3U * sizeof(float));
  assert(operations.arrayUploads[1].bytes.size() == 3U * 4U * sizeof(std::uint8_t));
  assert(operations.arrayUploads[2].bytes.size() == 3U * sizeof(std::uint16_t));

  assert(operations.attributeConfigurations.size() == 3U);
  const auto &position = operations.attributeConfigurations[0];
  assert(position.attributeIndex == 0U);
  assert(position.format == GlChunkAttributeFormat::Float32);
  assert(position.componentCount == 3U);
  assert(!position.normalized);
  assert(position.stride == 3U * sizeof(float));

  const auto &color = operations.attributeConfigurations[1];
  assert(color.attributeIndex == 1U);
  assert(color.format == GlChunkAttributeFormat::UInt8);
  assert(color.componentCount == 4U);
  assert(color.normalized);
  assert(color.stride == 4U * sizeof(std::uint8_t));

  const auto &intensity = operations.attributeConfigurations[2];
  assert(intensity.attributeIndex == 2U);
  assert(intensity.format == GlChunkAttributeFormat::UInt16);
  assert(intensity.componentCount == 1U);
  assert(intensity.normalized);
  assert(intensity.stride == sizeof(std::uint16_t));
}

void testNormalizedChunkRenderPipeline() {
  const auto built = GridChunkBuilder::build({{makeFullAttributeBucket()}});
  assert(built.hasValue());
  assert(built.value().size() == 1U);
  const Chunk &chunk = built.value().front();
  assert(chunk.state() == ChunkState::CpuResident);
  assert(chunk.cpuData() != nullptr);
  assert(chunk.metadata().pointCount == 3U);
  assert(chunk.metadata().schema.mask == fullAttributeMask);
  assert(chunk.metadata().origin == (glm::dvec3{1000002.0, 2000002.0, 20.0}));
  assert(chunk.cpuData()->positions[0] == (glm::vec3{-2.0F, -2.0F, -10.0F}));
  assert(chunk.cpuData()->positions[1] == (glm::vec3{0.0F, 0.0F, 0.0F}));
  assert(chunk.cpuData()->positions[2] == (glm::vec3{2.0F, 2.0F, 10.0F}));

  auto context = std::make_shared<FakeContextOperations>();
  auto capabilities = std::make_shared<FakeCapabilityQueries>();
  auto draw = std::make_shared<FakeDrawOperations>();
  auto shader = std::make_shared<FakeShaderOperations>();
  auto timer = std::make_shared<FakeTimerQueryOperations>();
  OpenGLBackend backend{context, capabilities, draw, draw, shader, {}, timer};

  const RenderSize initialSize{320U, 240U, 1.0F};
  assert(backend.init({initialSize}).hasValue());
  assert(backend.isInitialized());
  assert(backend.capabilities().has_value());
  assert(backend.capabilities()->version.major == 4);
  assert(backend.capabilities()->version.minor == 5);
  assert(backend.capabilities()->version.isCoreProfile);
  assert(draw->viewports.size() == 1U);
  assert(draw->viewports[0].width == 320U);
  assert(draw->viewports[0].height == 240U);
  assert(draw->liveBuffers.size() == 2U);
  assert(shader->liveShaders.size() == 2U);
  assert(shader->livePrograms.size() == 1U);
  assert(timer->liveQueries.size() == GlTimerQueryPool::queryDelayFrames);

  UploadBatch uploadBatch;
  uploadBatch.chunks.push_back({chunk.metadata(), *chunk.cpuData()});
  assert(backend.upload(uploadBatch).hasValue());
  assert(backend.hasChunk(chunk.metadata().id));
  assert(backend.chunkCount() == 1U);
  assert(draw->liveBuffers.size() == 5U);
  assert(draw->liveVertexArrays.size() == 1U);
  assertUploadLayout(*draw);

  const std::array<ShadingMode, 4U> modes = {
      ShadingMode::OriginalColor,
      ShadingMode::FixedColor,
      ShadingMode::Height,
      ShadingMode::Intensity,
  };
  const glm::dvec3 cameraOrigin{1000000.0, 2000000.0, 0.0};

  for (std::size_t index = 0U; index < modes.size(); ++index) {
    RenderSize size = initialSize;
    if (index == 2U) {
      size = {640U, 360U, 1.5F};
      assert(backend.resize(size).hasValue());
      assert(draw->viewports.size() == 2U);
      assert(draw->viewports.back().width == 960U);
      assert(draw->viewports.back().height == 540U);
    } else if (index > 2U) {
      size = {640U, 360U, 1.5F};
    }

    const RenderFrame frame =
        makeFrame(chunk, FrameId{index + 1U}, size, modes[index], cameraOrigin);
    assert(backend.update(frame).hasValue());
    assert(backend.render().hasValue());
    assert(backend.drawCount() == 1U);
    assert(backend.submittedPointCount() == 3U);

    const auto frameData =
        decode<render::FrameData>(lastUpload(*draw, GlDrawBufferTarget::UniformBuffer).bytes);
    assert(frameData.view == render::toColumnMajorArray(glm::mat4{1.0F}));
    assert(frameData.projection == render::toColumnMajorArray(glm::mat4{1.0F}));
    assert(frameData.shadingMode == render::toShaderShadingMode(modes[index]));
    assert(frameData.pointSize == 4.0F);
    assert(frameData.fixedColor == (std::array<float, 4U>{0.25F, 0.5F, 0.75F, 1.0F}));
    assert(frameData.heightRange == (std::array<float, 4U>{10.0F, 30.0F, 0.0F, 0.0F}));
    assert(frameData.intensityRange ==
           (std::array<float, 4U>{100.0F, 900.0F, 0.0F, 0.0F}));

    const auto chunkData =
        decode<render::ChunkData>(lastUpload(*draw, GlDrawBufferTarget::ShaderStorageBuffer).bytes);
    assert(chunkData.relativeChunkOrigin ==
           (std::array<float, 4U>{2.0F, 2.0F, 20.0F, 0.0F}));
  }

  assert(draw->drawnPointCounts ==
         (std::vector<std::uint32_t>{3U, 3U, 3U, 3U}));
  std::uint64_t cumulativeSubmittedPointCount = 0U;
  for (const std::uint32_t pointCount : draw->drawnPointCounts)
    cumulativeSubmittedPointCount += pointCount;
  assert(draw->drawnPointCounts.size() == 4U);
  assert(cumulativeSubmittedPointCount == 12U);
  assert(draw->clearColors.size() == 4U);
  assert(draw->usedPrograms.size() == 4U);
  assert(draw->uniformUpdates.size() == 8U);
  for (std::size_t index = 0U; index < draw->uniformUpdates.size(); index += 2U) {
    assert(draw->uniformUpdates[index].name == "drawHasColor");
    assert(draw->uniformUpdates[index].value == 1U);
    assert(draw->uniformUpdates[index + 1U].name == "drawHasIntensity");
    assert(draw->uniformUpdates[index + 1U].value == 1U);
  }

  std::size_t frameBindingCount = 0U;
  std::size_t chunkBindingCount = 0U;
  for (const auto &binding : draw->drawBufferBindings) {
    if (binding.target == GlDrawBufferTarget::UniformBuffer &&
        binding.binding == render::frameDataBinding)
      ++frameBindingCount;
    if (binding.target == GlDrawBufferTarget::ShaderStorageBuffer &&
        binding.binding == render::chunkDataBinding)
      ++chunkBindingCount;
  }
  assert(frameBindingCount == 4U);
  assert(chunkBindingCount == 4U);

  assert(timer->beginCalls == 4U);
  assert(timer->endCalls == 4U);
  assert(timer->availabilityCalls == 1U);
  assert(timer->readCalls == 1U);
  assert(backend.latestGpuFrameMilliseconds().has_value());
  assert(std::fabs(*backend.latestGpuFrameMilliseconds() - 2.5) < 1.0e-12);

  backend.release(chunk.metadata().id);
  assert(!backend.lastError().has_value());
  assert(!backend.hasChunk(chunk.metadata().id));
  assert(backend.chunkCount() == 0U);
  assert(draw->liveVertexArrays.empty());
  assert(draw->liveBuffers.size() == 2U);

  backend.shutdown();
  assert(backend.state() == OpenGLBackendState::Shutdown);
  assert(!backend.lastError().has_value());
  assert(draw->liveBuffers.empty());
  assert(draw->liveVertexArrays.empty());
  assert(shader->liveShaders.empty());
  assert(shader->livePrograms.empty());
  assert(timer->liveQueries.empty());
  assert(timer->activeQuery == 0U);
}

void testInvalidShaderRollsBackInitialization() {
  auto context = std::make_shared<FakeContextOperations>();
  auto capabilities = std::make_shared<FakeCapabilityQueries>();
  auto draw = std::make_shared<FakeDrawOperations>();
  auto shader = std::make_shared<FakeShaderOperations>();
  auto timer = std::make_shared<FakeTimerQueryOperations>();
  shader->failCompile = true;

  OpenGLBackend backend{context, capabilities, draw, draw, shader, {}, timer};
  const auto result = backend.init({RenderSize{320U, 240U, 1.0F}});
  assert(!result.hasValue());
  assert(result.error().domain == ErrorDomain::OpenGL);
  assert(result.error().code ==
         static_cast<std::uint32_t>(OpenGLBackendErrorCode::OperationFailed));
  assert(result.error().diagnosticMessage.find(
             "Injected GL-012 shader compilation failure") != std::string::npos);
  assert(backend.state() == OpenGLBackendState::Uninitialized);
  assert(!backend.isInitialized());
  assert(draw->nextId == 3U);
  assert(shader->nextId == 1001U);
  assert(timer->nextId == 2000U);
  assert(draw->liveBuffers.empty());
  assert(draw->liveVertexArrays.empty());
  assert(shader->liveShaders.empty());
  assert(shader->livePrograms.empty());
  assert(timer->liveQueries.empty());

  backend.shutdown();
  assert(backend.state() == OpenGLBackendState::Shutdown);
  assert(!backend.lastError().has_value());
  assert(draw->liveBuffers.empty());
  assert(shader->liveShaders.empty());
  assert(shader->livePrograms.empty());
  assert(timer->liveQueries.empty());
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "--real-context") {
    std::cout << "SKIPPED: GL-012 does not create Qt, WGL, EGL, or another "
                 "real headless/offscreen OpenGL Context; the injected pipeline "
                 "test is used instead.\n";
    return 77;
  }

  testNormalizedChunkRenderPipeline();
  testInvalidShaderRollsBackInitialization();
  return 0;
}
