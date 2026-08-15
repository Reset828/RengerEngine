#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace dzc::diagnostics {

struct PerformanceSummary final {
    std::optional<std::string> projectVersion;
    std::optional<std::string> buildType;
    std::optional<std::string> operatingSystem;
    std::optional<std::string> cpu;
    std::optional<std::string> gpu;
    std::optional<std::string> driver;
    std::optional<std::string> memory;
    std::optional<std::string> gpuMemory;
    std::optional<std::string> cuda;
    std::optional<std::string> datasetIdentity;
    std::optional<std::string> backend;
    std::optional<std::string> parameters;
    std::optional<std::string> benchmarkHardware;
    std::optional<std::string> cameraPath;

    std::optional<std::uint64_t> pointCount;
    std::optional<std::uint64_t> sampleFrameCount;
    std::optional<std::uint64_t> errorCount;

    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;

    std::optional<double> averageFps;
    std::optional<double> averageCpuFrameMilliseconds;
    std::optional<double> averageGpuFrameMilliseconds;
    std::optional<double> lowFrameRatePercentile;
};

class PerformanceSummaryWriter final {
public:
    explicit PerformanceSummaryWriter(const std::filesystem::path& path);
    ~PerformanceSummaryWriter();

    PerformanceSummaryWriter(const PerformanceSummaryWriter&) = delete;
    PerformanceSummaryWriter& operator=(const PerformanceSummaryWriter&) = delete;
    PerformanceSummaryWriter(PerformanceSummaryWriter&&) = delete;
    PerformanceSummaryWriter& operator=(PerformanceSummaryWriter&&) = delete;

    bool write(const PerformanceSummary& summary);
    bool close() noexcept;
    bool isOpen() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc::diagnostics