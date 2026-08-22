#pragma once

#include "data/chunk/PointBatch.h"
#include "data/io/PointCloudSourceInfo.h"
#include "tasks/Cancellation.h"

#include <dzc/Result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace dzc {

struct PointCloudReadProgress final {
    std::uint64_t consumedSourcePoints{0U};
    std::optional<std::uint64_t> totalSourcePoints;
};

class IPointCloudReader {
public:
    virtual ~IPointCloudReader() = default;

    // Opens a source and returns its declared metadata.
    virtual Result<PointCloudSourceInfo> open(const std::string& path) = 0;

    // Reads at most maximumPoints, or returns an empty value when the source ends.
    virtual Result<std::optional<PointBatch>> readNext(
        std::size_t maximumPoints,
        tasks::CancellationToken token) = 0;

    // Reports consumed source points for the currently opened source.
    virtual Result<PointCloudReadProgress> readProgress() const = 0;

    // Releases the opened source and resets the reader state.
    virtual void close() noexcept = 0;

    IPointCloudReader(const IPointCloudReader&) = delete;
    IPointCloudReader& operator=(const IPointCloudReader&) = delete;
    IPointCloudReader(IPointCloudReader&&) = delete;
    IPointCloudReader& operator=(IPointCloudReader&&) = delete;

protected:
    IPointCloudReader() = default;
};

} // namespace dzc
