#pragma once

#include "data/io/IPointCloudReader.h"

#include <memory>

namespace dzc {

// PCL-backed, header-only PLY reader. PCL implementation types stay private.
class PlyReader final : public IPointCloudReader {
public:
    PlyReader();
    ~PlyReader() override;

    PlyReader(const PlyReader&) = delete;
    PlyReader& operator=(const PlyReader&) = delete;
    PlyReader(PlyReader&&) = delete;
    PlyReader& operator=(PlyReader&&) = delete;

    Result<PointCloudSourceInfo> open(const std::string& path) override;
    Result<std::optional<PointBatch>> readNext(
        std::size_t maximumPoints,
        tasks::CancellationToken token) override;
    void close() noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc