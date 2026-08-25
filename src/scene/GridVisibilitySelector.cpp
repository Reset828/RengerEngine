#include "scene/GridVisibilitySelector.h"

#include "scene/FrustumCulling.h"

#include <dzc/Error.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kDataFormatCode = 2U;
constexpr std::uint32_t kResourceCode = 1U;

Error dataFormatError(const char* diagnosticMessage) {
    return Error{
        ErrorDomain::DataFormat,
        kDataFormatCode,
        "Grid visibility input is invalid.",
        diagnosticMessage,
        "GridVisibilitySelector"};
}

Error resourceError(const char* diagnosticMessage) {
    return Error{
        ErrorDomain::Resource,
        kResourceCode,
        "Grid visibility selection requires more memory than is available.",
        diagnosticMessage,
        "GridVisibilitySelector"};
}

bool isFiniteFloat(float value) noexcept {
    return std::isfinite(value);
}

bool convertFiniteFloat(double value, float& converted) noexcept {
    if (!std::isfinite(value) ||
        std::abs(value) > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }

    converted = static_cast<float>(value);
    return isFiniteFloat(converted);
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

struct CacheKey final {
    DatasetId datasetId;
    ChunkId chunkId;

    friend bool operator<(const CacheKey& left, const CacheKey& right) noexcept {
        if (left.datasetId.value != right.datasetId.value) {
            return left.datasetId.value < right.datasetId.value;
        }
        return left.chunkId.value < right.chunkId.value;
    }
};

using PlaneCache = std::map<CacheKey, ViewFrustum::PlaneIndex>;

} // namespace

class GridVisibilitySelector::Impl final {
public:
    PlaneCache separatingPlanes;
};

GridVisibilitySelector::GridVisibilitySelector()
    : m_impl(std::make_unique<Impl>()) {}

GridVisibilitySelector::~GridVisibilitySelector() = default;

GridVisibilitySelector::GridVisibilitySelector(GridVisibilitySelector&&) noexcept = default;

GridVisibilitySelector& GridVisibilitySelector::operator=(GridVisibilitySelector&&) noexcept = default;

Result<GridVisibilityResult> GridVisibilitySelector::select(
    const Dataset& dataset,
    const ViewFrustum& frustum) {
    try {
        GridVisibilityResult result;
        result.totalPointCount = dataset.totalPointCount();
        const Result<FrustumCullingResult> frustumValidation = FrustumCulling::classify(
            frustum,
            dataset.metadata().bounds);
        if (!frustumValidation.hasValue()) {
            return Result<GridVisibilityResult>::failure(frustumValidation.error());
        }
        result.draws.reserve(dataset.chunkCount());

        PlaneCache nextPlanes = m_impl->separatingPlanes;
        for (auto cached = nextPlanes.begin(); cached != nextPlanes.end();) {
            if (cached->first.datasetId == dataset.metadata().id) {
                cached = nextPlanes.erase(cached);
            } else {
                ++cached;
            }
        }

        for (const Chunk& chunk : dataset.chunks()) {
            const ChunkMetadata& metadata = chunk.metadata();
            glm::vec3 relativeOrigin{0.0F};
            if (!convertFiniteFloat(metadata.origin.x, relativeOrigin.x) ||
                !convertFiniteFloat(metadata.origin.y, relativeOrigin.y) ||
                !convertFiniteFloat(metadata.origin.z, relativeOrigin.z)) {
                return Result<GridVisibilityResult>::failure(dataFormatError(
                    "Chunk metadata origin must be representable as finite float values."));
            }

            const ChunkState state = chunk.state();
            if (state != ChunkState::CpuResident && state != ChunkState::GpuResident) {
                continue;
            }

            const CacheKey cacheKey{dataset.metadata().id, metadata.id};
            std::optional<ViewFrustum::PlaneIndex> previousPlane;
            const auto cached = m_impl->separatingPlanes.find(cacheKey);
            if (cached != m_impl->separatingPlanes.end()) {
                previousPlane = cached->second;
            }

            const Result<FrustumCullingResult> culling = FrustumCulling::classify(
                frustum,
                metadata.bounds,
                previousPlane);
            if (!culling.hasValue()) {
                return Result<GridVisibilityResult>::failure(culling.error());
            }

            const FrustumCullingResult& cullingResult = culling.value();
            if (cullingResult.classification == FrustumClassification::Outside) {
                if (!cullingResult.separatingPlane.has_value()) {
                    return Result<GridVisibilityResult>::failure(dataFormatError(
                        "An outside Chunk classification must provide a separating plane."));
                }
                nextPlanes.emplace(cacheKey, cullingResult.separatingPlane.value());
                continue;
            }

            DrawChunk draw;
            draw.chunkId = metadata.id;
            draw.pointCount = metadata.pointCount;
            draw.relativeOrigin = relativeOrigin;
            draw.schema = metadata.schema;
            result.draws.push_back(draw);

            std::uint64_t nextVisiblePoints = 0U;
            if (!checkedAdd(result.visiblePointCount, metadata.pointCount, nextVisiblePoints)) {
                return Result<GridVisibilityResult>::failure(resourceError(
                    "Visible point count overflows uint64."));
            }
            result.visiblePointCount = nextVisiblePoints;

            std::uint64_t nextVisibleChunks = 0U;
            if (!checkedAdd(result.visibleChunkCount, 1U, nextVisibleChunks)) {
                return Result<GridVisibilityResult>::failure(resourceError(
                    "Visible Chunk count overflows uint64."));
            }
            result.visibleChunkCount = nextVisibleChunks;
        }

        m_impl->separatingPlanes.swap(nextPlanes);
        return Result<GridVisibilityResult>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result<GridVisibilityResult>::failure(resourceError(
            "Grid visibility selection requires more memory than is available."));
    } catch (const std::length_error&) {
        return Result<GridVisibilityResult>::failure(resourceError(
            "Grid visibility result size is not representable."));
    }
}

} // namespace dzc
