#pragma once

#include "GlResource.h"
#include "GlBuffer.h"
#include "GlVertexArray.h"

#include <dzc/Result.h>
#include <data/chunk/Chunk.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <thread>

namespace dzc::opengl {

enum class GlChunkBufferTarget : std::uint8_t {
    ArrayBuffer
};

enum class GlChunkBufferUsage : std::uint8_t {
    StaticDraw
};

enum class GlChunkAttributeFormat : std::uint8_t {
    Float32,
    UInt8,
    UInt16
};

enum class GlChunkErrorCode : std::uint32_t {
    InvalidInput = 1U,
    CreationFailed = 2U,
    UploadFailed = 3U,
    InvalidThreadToken = 4U,
    OperationFailed = 5U,
    SizeOverflow = 6U
};

class IGlChunkUploadOperations : public IGlResourceOperations {
public:
    ~IGlChunkUploadOperations() override = default;

    virtual bool bindVertexArray(std::uint32_t vertexArrayId) const noexcept = 0;
    virtual bool bindArrayBuffer(std::uint32_t bufferId) const noexcept = 0;
    virtual bool uploadArrayBuffer(
        std::uint32_t bufferId,
        const std::vector<std::byte>& bytes,
        GlChunkBufferUsage usage) const noexcept = 0;
    virtual bool configureVertexAttribute(
        std::uint32_t attributeIndex,
        GlChunkAttributeFormat format,
        std::uint32_t componentCount,
        bool normalized,
        std::uint32_t stride) const noexcept = 0;
    virtual bool enableVertexAttribute(std::uint32_t attributeIndex) const noexcept = 0;
    virtual bool unbindArrayBuffer() const noexcept = 0;
    virtual bool unbindVertexArray() const noexcept = 0;
};

std::shared_ptr<const IGlChunkUploadOperations> makeDefaultGlChunkUploadOperations();

struct GlChunkResourceStats final {
    std::uint64_t pointCount{0U};
    dzc::AttributeSchema schema{};
    bool hasPosition{false};
    bool hasColor{false};
    bool hasIntensity{false};
    std::size_t positionBytes{0U};
    std::size_t colorBytes{0U};
    std::size_t intensityBytes{0U};
    std::size_t totalBytes{0U};
    bool valid{false};
};

class GlChunkResource final {
public:
    GlChunkResource() noexcept = default;
    ~GlChunkResource() noexcept;

    GlChunkResource(const GlChunkResource&) = delete;
    GlChunkResource& operator=(const GlChunkResource&) = delete;
    GlChunkResource(GlChunkResource&& other) noexcept;
    GlChunkResource& operator=(GlChunkResource&& other) noexcept;

    dzc::Result<void> upload(
        const GlContextThreadToken& token,
        const dzc::ChunkMetadata& metadata,
        const dzc::ChunkCpuData& cpuData,
        std::shared_ptr<const IGlChunkUploadOperations> operations = {});

    dzc::Result<void> reset(const GlContextThreadToken& token);

    bool isValid() const noexcept;
    std::uint32_t vertexArrayId() const noexcept { return mVertexArray.id(); }
    bool releasePending() const noexcept;
    std::uint64_t pointCount() const noexcept { return mStats.pointCount; }
    const dzc::AttributeSchema& schema() const noexcept { return mStats.schema; }
    const GlChunkResourceStats& stats() const noexcept { return mStats; }

private:
    void markMovedFrom() noexcept;

    GlVertexArray mVertexArray;
    GlBuffer mPositionBuffer;
    GlBuffer mColorBuffer;
    GlBuffer mIntensityBuffer;
    std::shared_ptr<const IGlChunkUploadOperations> mOperations;
    std::thread::id mOwnerThread;
    GlChunkResourceStats mStats{};
    bool mReleasePending{false};
};

} // namespace dzc::opengl
