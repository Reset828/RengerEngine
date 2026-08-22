#pragma once

#include "data/io/IPointCloudReader.h"

#include <memory>

namespace dzc {

// PCL-backed, header-only PCD reader. PCL implementation types stay private.
class PcdReader final : public IPointCloudReader {
public:
    PcdReader();
    ~PcdReader() override;

    PcdReader(const PcdReader&) = delete;
    PcdReader& operator=(const PcdReader&) = delete;
    PcdReader(PcdReader&&) = delete;
    PcdReader& operator=(PcdReader&&) = delete;

    Result<PointCloudSourceInfo> open(const std::string& path) override;
    Result<std::optional<PointBatch>> readNext(
        std::size_t maximumPoints,
        tasks::CancellationToken token) override;
    Result<PointCloudReadProgress> readProgress() const override;
    void close() noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc