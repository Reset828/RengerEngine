#pragma once

#include "data/chunk/GridBucketStore.h"
#include "tasks/Cancellation.h"
#include <dzc/Result.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace dzc {

// Owns one temporary deterministic grid run and removes incomplete or invalid runs.
class GridRunFile final {
public:
    static Result<GridRunFile> create(const std::filesystem::path& directory);

    GridRunFile(const GridRunFile&) = delete;
    GridRunFile& operator=(const GridRunFile&) = delete;
    GridRunFile(GridRunFile&& other) noexcept;
    GridRunFile& operator=(GridRunFile&& other) noexcept;
    ~GridRunFile();

    // Serializes a complete sorted bucket snapshot into this unfinished run.
    Result<void> write(
        const std::vector<GridBucket>& buckets,
        tasks::CancellationToken token = {});

    // Atomically promotes the successfully written temporary file to a readable run.
    Result<void> complete() noexcept;

    // Reads a completed run and removes it if its content is unreadable or corrupt.
    Result<std::vector<GridBucket>> read(
        tasks::CancellationToken token = {}) const;

    const std::filesystem::path& path() const noexcept;
    bool isComplete() const noexcept;

private:
    class Impl;

    explicit GridRunFile(std::unique_ptr<Impl> impl) noexcept;

    void discard() const noexcept;

    mutable std::unique_ptr<Impl> m_impl;
};

} // namespace dzc
