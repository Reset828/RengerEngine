#include "data/chunk/GridBucketStore.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kCorruptDataCode = 2U;
constexpr std::uint32_t kBudgetExceededCode = 1U;

Error gridBucketStoreError(
    ErrorDomain domain,
    std::uint32_t code,
    const char* userMessage,
    const char* diagnosticMessage) {
    return Error{
        domain,
        code,
        userMessage,
        diagnosticMessage,
        "GridBucketStore"};
}

Error invalidConfigurationError(const char* diagnosticMessage) {
    return gridBucketStoreError(
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "Grid bucket store configuration is invalid",
        diagnosticMessage);
}

Error invalidBatchError(const char* diagnosticMessage) {
    return gridBucketStoreError(
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "Point batch cannot be added to the grid bucket store",
        diagnosticMessage);
}

Error budgetExceededError(const char* diagnosticMessage) {
    return gridBucketStoreError(
        ErrorDomain::Resource,
        kBudgetExceededCode,
        "Grid bucket CPU byte budget would be exceeded",
        diagnosticMessage);
}

bool checkedAdd(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool checkedMultiply(
    std::size_t count,
    std::size_t elementSize,
    std::uint64_t& result) noexcept {
    if (count > std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }

    const std::uint64_t count64 = static_cast<std::uint64_t>(count);
    const std::uint64_t elementSize64 = static_cast<std::uint64_t>(elementSize);
    if (elementSize64 != 0U && count64 > std::numeric_limits<std::uint64_t>::max() / elementSize64) {
        return false;
    }

    result = count64 * elementSize64;
    return true;
}

bool checkedBatchBytes(const PointBatch& batch, std::uint64_t& result) noexcept {
    result = 0U;
    const std::size_t pointCount = batch.positions.size();
    const std::size_t colorCount = batch.schema.hasColor() ? batch.colorsRgba8.size() : 0U;
    const std::size_t intensityCount = batch.schema.hasIntensity() ? batch.intensities.size() : 0U;

    std::uint64_t streamBytes = 0U;
    if (!checkedMultiply(pointCount, sizeof(glm::dvec3), streamBytes) ||
        !checkedAdd(result, streamBytes, result)) {
        return false;
    }
    if (!checkedMultiply(colorCount, sizeof(std::uint32_t), streamBytes) ||
        !checkedAdd(result, streamBytes, result)) {
        return false;
    }
    if (!checkedMultiply(intensityCount, sizeof(std::uint16_t), streamBytes) ||
        !checkedAdd(result, streamBytes, result)) {
        return false;
    }
    if (!checkedMultiply(pointCount, sizeof(std::uint64_t), streamBytes) ||
        !checkedAdd(result, streamBytes, result)) {
        return false;
    }
    return true;
}

bool schemasEqual(const AttributeSchema& left, const AttributeSchema& right) noexcept {
    return left.mask == right.mask;
}

} // namespace

GridBucketStore::GridBucketStore(
    const glm::dvec3& datasetMinimum,
    double cellSize,
    std::uint64_t byteBudget) noexcept
    : m_datasetMinimum(datasetMinimum),
      m_cellSize(cellSize),
      m_byteBudget(byteBudget) {}

Result<GridBucketStore> GridBucketStore::create(
    const glm::dvec3& datasetMinimum,
    double cellSize,
    std::uint64_t byteBudget) {
    if (!std::isfinite(datasetMinimum.x) ||
        !std::isfinite(datasetMinimum.y) ||
        !std::isfinite(datasetMinimum.z)) {
        return Result<GridBucketStore>::failure(
            invalidConfigurationError("Dataset minimum coordinates must be finite."));
    }
    if (!std::isfinite(cellSize) || cellSize <= 0.0) {
        return Result<GridBucketStore>::failure(
            invalidConfigurationError("Cell size must be finite and positive."));
    }
    if (byteBudget == 0U) {
        return Result<GridBucketStore>::failure(
            budgetExceededError("Grid bucket CPU byte budget must be positive."));
    }

    return Result<GridBucketStore>::success(
        GridBucketStore(datasetMinimum, cellSize, byteBudget));
}

Result<void> GridBucketStore::appendBatch(const PointBatch& batch) {
    const Result<void> validation = batch.validate();
    if (!validation.hasValue()) {
        return Result<void>::failure(validation.error());
    }

    if (batch.positions.empty()) {
        return Result<void>::success();
    }

    if (m_schema.has_value() && !schemasEqual(m_schema.value(), batch.schema)) {
        return Result<void>::failure(invalidBatchError(
            "Point batch attribute schema does not match the store schema."));
    }

    std::vector<GridCellKey> cellKeys;
    cellKeys.reserve(batch.positions.size());
    for (const glm::dvec3& position : batch.positions) {
        const Result<GridCellKey> key = GridCellKey::fromPosition(
            position,
            m_datasetMinimum,
            m_cellSize);
        if (!key.hasValue()) {
            return Result<void>::failure(key.error());
        }
        cellKeys.push_back(key.value());
    }

    std::uint64_t nextSourceIndex = 0U;
    if (!checkedAdd(
            m_nextSourceIndex,
            static_cast<std::uint64_t>(batch.positions.size()),
            nextSourceIndex)) {
        return Result<void>::failure(invalidBatchError(
            "Point batch source index range overflows uint64."));
    }

    std::uint64_t batchBytes = 0U;
    if (!checkedBatchBytes(batch, batchBytes)) {
        return Result<void>::failure(budgetExceededError(
            "Point batch byte size cannot be represented as uint64."));
    }

    std::uint64_t newResidentBytes = 0U;
    if (!checkedAdd(m_residentBytes, batchBytes, newResidentBytes) ||
        newResidentBytes > m_byteBudget) {
        return Result<void>::failure(budgetExceededError(
            "Adding the point batch would exceed the CPU byte budget."));
    }

    if (!m_schema.has_value()) {
        m_schema = batch.schema;
    }

    for (std::size_t index = 0U; index < batch.positions.size(); ++index) {
        const GridCellKey& key = cellKeys[index];
        auto [bucketIterator, inserted] = m_buckets.try_emplace(key);
        if (inserted) {
            bucketIterator->second.key = key;
            bucketIterator->second.points.schema = batch.schema;
        }

        GridBucket& bucket = bucketIterator->second;
        bucket.points.positions.push_back(batch.positions[index]);
        if (batch.schema.hasColor()) {
            bucket.points.colorsRgba8.push_back(batch.colorsRgba8[index]);
        }
        if (batch.schema.hasIntensity()) {
            bucket.points.intensities.push_back(batch.intensities[index]);
        }
        bucket.sourceIndices.push_back(
            m_nextSourceIndex + static_cast<std::uint64_t>(index));
    }

    m_residentBytes = newResidentBytes;
    m_pointCount += static_cast<std::uint64_t>(batch.positions.size());
    m_nextSourceIndex = nextSourceIndex;
    return Result<void>::success();
}

std::vector<GridBucket> GridBucketStore::snapshot() const {
    std::vector<GridBucket> result;
    result.reserve(m_buckets.size());
    for (const auto& entry : m_buckets) {
        result.push_back(entry.second);
    }
    return result;
}

void GridBucketStore::clear() noexcept {
    m_buckets.clear();
    m_schema.reset();
    m_residentBytes = 0U;
    m_pointCount = 0U;
    m_nextSourceIndex = 0U;
}

std::uint64_t GridBucketStore::residentBytes() const noexcept {
    return m_residentBytes;
}

std::uint64_t GridBucketStore::pointCount() const noexcept {
    return m_pointCount;
}

} // namespace dzc