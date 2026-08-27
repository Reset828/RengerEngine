#pragma once

#include "data/chunk/Chunk.h"
#include "dzc/CameraTypes.h"
#include "dzc/EngineTypes.h"
#include "dzc/EngineConfig.h"

#include <cstdint>
#include <vector>

namespace dzc {

// Backend-independent description of a visible chunk.
struct DrawChunk final {
    ChunkId chunkId;
    std::uint64_t pointCount{0U};
    glm::vec3 relativeOrigin{0.0F};
    AttributeSchema schema;
};

struct ChunkUpload final {
    ChunkMetadata metadata;
    ChunkCpuData cpuData;
};

struct UploadBatch final {
    std::vector<ChunkUpload> chunks;
};

struct RenderFrame final {
    FrameId frameId;
    CameraMatrices camera;
    RenderSize size;
    ColorRgba backgroundColor;
    float pointSize{1.0F};
    ShadingMode shadingMode{ShadingMode::OriginalColor};
    ColorRgba fixedColor;
    glm::vec2 heightRange{0.0F};
    glm::vec2 intensityRange{0.0F};
    std::vector<DrawChunk> draws;
};

struct RenderBackendConfig final {
    RenderSize initialSize;
};

// The platform/application layer owns the actual context and loader.
class IRenderContextOperations {
public:
    virtual ~IRenderContextOperations() = default;

    virtual bool makeCurrent() const noexcept = 0;
    virtual bool isCurrent() const noexcept = 0;
    virtual bool loadFunctions() const noexcept = 0;
    virtual bool functionsLoaded() const noexcept = 0;
    virtual bool releaseCurrent() const noexcept = 0;
};

} // namespace dzc
