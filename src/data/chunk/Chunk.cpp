#include "data/chunk/Chunk.h"

#include <cmath>
#include <cstdint>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kInvalidStateCode = static_cast<std::uint32_t>(ChunkErrorCode::InvalidState);
constexpr std::uint32_t kCorruptDataCode = static_cast<std::uint32_t>(ChunkErrorCode::CorruptData);

bool isFinite(const glm::dvec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Error corruptMetadataError(const char* diagnostic) {
    return Error{
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "Chunk metadata is invalid",
        diagnostic,
        "Chunk::create"};
}

Error corruptCpuDataError(const char* diagnostic) {
    return Error{
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "Chunk CPU data is inconsistent with its metadata",
        diagnostic,
        "Chunk::completeCpuLoad"};
}

} // namespace

Chunk::Chunk(ChunkMetadata metadata) noexcept
    : m_metadata(std::move(metadata)) {}

Result<Chunk> Chunk::create(ChunkMetadata metadata) {
    if (metadata.pointCount == 0U) {
        return Result<Chunk>::failure(corruptMetadataError("ChunkMetadata.pointCount must be greater than zero."));
    }

    if (!metadata.schema.hasPosition()) {
        return Result<Chunk>::failure(corruptMetadataError("ChunkMetadata.schema must declare Position."));
    }

    if (!metadata.bounds.isValid()) {
        return Result<Chunk>::failure(corruptMetadataError("ChunkMetadata.bounds must be valid."));
    }

    if (!isFinite(metadata.origin)) {
        return Result<Chunk>::failure(corruptMetadataError("ChunkMetadata.origin must have finite components."));
    }

    return Result<Chunk>::success(Chunk(std::move(metadata)));
}

const ChunkMetadata& Chunk::metadata() const noexcept {
    return m_metadata;
}

ChunkState Chunk::state() const noexcept {
    return m_state;
}

bool Chunk::hasCpuData() const noexcept {
    return m_cpuData.has_value();
}

const ChunkCpuData* Chunk::cpuData() const noexcept {
    return m_cpuData ? &m_cpuData.value() : nullptr;
}

Result<void> Chunk::beginCpuLoad() {
    if (m_state != ChunkState::MetadataOnly) {
        return invalidStateError("Chunk::beginCpuLoad");
    }

    m_state = ChunkState::LoadingCpu;
    return Result<void>::success();
}

Result<void> Chunk::cancelCpuLoad() {
    if (m_state != ChunkState::LoadingCpu) {
        return invalidStateError("Chunk::cancelCpuLoad");
    }

    m_state = ChunkState::MetadataOnly;
    return Result<void>::success();
}

Result<void> Chunk::completeCpuLoad(ChunkCpuData data) {
    if (m_state != ChunkState::LoadingCpu) {
        return invalidStateError("Chunk::completeCpuLoad");
    }

    const Result<void> validation = validateCpuData(data);
    if (!validation.hasValue()) {
        enterError();
        return validation;
    }

    m_cpuData.emplace(std::move(data));
    m_state = ChunkState::CpuResident;
    return Result<void>::success();
}

Result<void> Chunk::failCpuLoad() {
    if (m_state != ChunkState::LoadingCpu) {
        return invalidStateError("Chunk::failCpuLoad");
    }

    enterError();
    return Result<void>::success();
}

Result<void> Chunk::queueUpload() {
    if (m_state != ChunkState::CpuResident) {
        return invalidStateError("Chunk::queueUpload");
    }

    m_state = ChunkState::UploadQueued;
    return Result<void>::success();
}

Result<void> Chunk::cancelUpload() {
    if (m_state != ChunkState::UploadQueued) {
        return invalidStateError("Chunk::cancelUpload");
    }

    m_state = ChunkState::CpuResident;
    return Result<void>::success();
}

Result<void> Chunk::failUpload() {
    if (m_state != ChunkState::UploadQueued) {
        return invalidStateError("Chunk::failUpload");
    }

    m_state = ChunkState::CpuResident;
    return Result<void>::success();
}

Result<void> Chunk::completeUpload() {
    if (m_state != ChunkState::UploadQueued) {
        return invalidStateError("Chunk::completeUpload");
    }

    m_state = ChunkState::GpuResident;
    return Result<void>::success();
}

Result<void> Chunk::beginGpuEviction() {
    if (m_state != ChunkState::GpuResident) {
        return invalidStateError("Chunk::beginGpuEviction");
    }

    m_state = ChunkState::EvictPending;
    m_evictionKind = EvictionKind::Gpu;
    return Result<void>::success();
}

Result<void> Chunk::completeGpuEviction() {
    if (m_state != ChunkState::EvictPending || m_evictionKind != EvictionKind::Gpu) {
        return invalidStateError("Chunk::completeGpuEviction");
    }

    m_state = ChunkState::CpuResident;
    m_evictionKind = EvictionKind::None;
    return Result<void>::success();
}

Result<void> Chunk::beginCpuEviction() {
    if (m_state != ChunkState::CpuResident) {
        return invalidStateError("Chunk::beginCpuEviction");
    }

    m_state = ChunkState::EvictPending;
    m_evictionKind = EvictionKind::Cpu;
    return Result<void>::success();
}

Result<void> Chunk::completeCpuEviction() {
    if (m_state != ChunkState::EvictPending || m_evictionKind != EvictionKind::Cpu) {
        return invalidStateError("Chunk::completeCpuEviction");
    }

    m_cpuData.reset();
    m_state = ChunkState::MetadataOnly;
    m_evictionKind = EvictionKind::None;
    return Result<void>::success();
}

Result<void> Chunk::resetError() {
    if (m_state != ChunkState::Error) {
        return invalidStateError("Chunk::resetError");
    }

    m_cpuData.reset();
    m_evictionKind = EvictionKind::None;
    m_state = ChunkState::MetadataOnly;
    return Result<void>::success();
}

Result<void> Chunk::validateCpuData(const ChunkCpuData& data) const {
    const std::uint64_t positionCount = static_cast<std::uint64_t>(data.positions.size());
    if (positionCount != m_metadata.pointCount) {
        return Result<void>::failure(corruptCpuDataError("Position stream length must equal ChunkMetadata.pointCount."));
    }

    const std::uint64_t colorCount = static_cast<std::uint64_t>(data.colorsRgba8.size());
    if (m_metadata.schema.hasColor()) {
        if (colorCount != m_metadata.pointCount) {
            return Result<void>::failure(corruptCpuDataError("Declared Color stream length must equal ChunkMetadata.pointCount."));
        }
    } else if (colorCount != 0U) {
        return Result<void>::failure(corruptCpuDataError("Undeclared Color stream must be empty."));
    }

    const std::uint64_t intensityCount = static_cast<std::uint64_t>(data.intensities.size());
    if (m_metadata.schema.hasIntensity()) {
        if (intensityCount != m_metadata.pointCount) {
            return Result<void>::failure(corruptCpuDataError("Declared Intensity stream length must equal ChunkMetadata.pointCount."));
        }
    } else if (intensityCount != 0U) {
        return Result<void>::failure(corruptCpuDataError("Undeclared Intensity stream must be empty."));
    }

    return Result<void>::success();
}

Result<void> Chunk::invalidStateError(const char* operation) const {
    return Result<void>::failure(Error{
        ErrorDomain::Internal,
        kInvalidStateCode,
        "Invalid Chunk state transition",
        "The requested Chunk operation is not legal from the current state.",
        operation});
}

void Chunk::enterError() noexcept {
    m_cpuData.reset();
    m_evictionKind = EvictionKind::None;
    m_state = ChunkState::Error;
}

} // namespace dzc
