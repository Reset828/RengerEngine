#pragma once

#include "data/io/IPointCloudReader.h"

#include <dzc/Result.h>

#include <functional>
#include <memory>
#include <string>

namespace dzc {

using PointCloudReaderCreator = std::function<std::unique_ptr<IPointCloudReader>()>;

class PointCloudReaderRegistry final {
public:
    // Creates a registry with injected PCD and PLY reader creators.
    PointCloudReaderRegistry(
        PointCloudReaderCreator pcdCreator,
        PointCloudReaderCreator plyCreator);
    ~PointCloudReaderRegistry();

    PointCloudReaderRegistry(const PointCloudReaderRegistry&) = delete;
    PointCloudReaderRegistry& operator=(const PointCloudReaderRegistry&) = delete;
    PointCloudReaderRegistry(PointCloudReaderRegistry&& other) noexcept;
    PointCloudReaderRegistry& operator=(PointCloudReaderRegistry&& other) noexcept;

    // Creates a reader selected by the source path's final extension.
    Result<std::unique_ptr<IPointCloudReader>> create(const std::string& path) const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc
