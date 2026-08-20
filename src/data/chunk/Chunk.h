#pragma once

#include "data/chunk/Bounds3d.h"
#include "data/chunk/PointAttributes.h"

#include <dzc/EngineTypes.h>
#include <dzc/Result.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace dzc {

struct ChunkCpuData final {
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> colorsRgba8;
    std::vector<std::uint16_t> intensities;
};

enum class ChunkState : std::uint8_t {
    MetadataOnly,
    LoadingCpu,
    CpuResident,
    UploadQueued,
    GpuResident,
    EvictPending,
    Error
};

struct ChunkMetadata final {
    ChunkId id;
    std::uint64_t pointCount{0U};
    Bounds3d bounds;
    glm::dvec3 origin{0.0};
    AttributeSchema schema;
};

enum class ChunkErrorCode : std::uint32_t {
    InvalidState = 1U,
    CorruptData = 2U
};

class Chunk final {
public:
    static Result<Chunk> create(ChunkMetadata metadata);

    const ChunkMetadata& metadata() const noexcept;
    ChunkState state() const noexcept;
    bool hasCpuData() const noexcept;
    const ChunkCpuData* cpuData() const noexcept;

    Result<void> beginCpuLoad();
    Result<void> cancelCpuLoad();
    Result<void> completeCpuLoad(ChunkCpuData data);
    Result<void> failCpuLoad();

    Result<void> queueUpload();
    Result<void> cancelUpload();
    Result<void> failUpload();
    Result<void> completeUpload();

    Result<void> beginGpuEviction();
    Result<void> completeGpuEviction();
    Result<void> beginCpuEviction();
    Result<void> completeCpuEviction();

    Result<void> resetError();

private:
    enum class EvictionKind : std::uint8_t {
        None,
        Gpu,
        Cpu
    };

    explicit Chunk(ChunkMetadata metadata) noexcept;

    Result<void> validateCpuData(const ChunkCpuData& data) const;
    Result<void> invalidStateError(const char* operation) const;
    void enterError() noexcept;

    ChunkMetadata m_metadata;
    ChunkState m_state{ChunkState::MetadataOnly};
    std::optional<ChunkCpuData> m_cpuData;
    EvictionKind m_evictionKind{EvictionKind::None};
};

} // namespace dzc
