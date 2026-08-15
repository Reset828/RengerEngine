#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace dzc::diagnostics {

struct PerformanceCsvRow final {
    std::chrono::system_clock::time_point utcTime{};

    std::uint64_t frameId = 0U;

    std::optional<std::string> backend;
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;

    std::optional<double> cpuFrameMilliseconds;
    std::optional<double> gpuFrameMilliseconds;
    std::optional<double> framesPerSecond;

    std::optional<std::uint64_t> visiblePoints;
    std::optional<std::uint64_t> submittedPoints;
    std::optional<std::uint64_t> visibleChunks;
    std::optional<std::uint64_t> cpuResidentBytes;
    std::optional<std::uint64_t> gpuResidentBytes;
    std::optional<std::uint64_t> uploadBytes;
    std::optional<std::uint64_t> lodMisses;

    std::optional<std::uint32_t> recordingWorkers;
};

class PerformanceCsvWriter final {
public:
    explicit PerformanceCsvWriter(const std::filesystem::path& path);
    ~PerformanceCsvWriter();

    PerformanceCsvWriter(const PerformanceCsvWriter&) = delete;
    PerformanceCsvWriter& operator=(const PerformanceCsvWriter&) = delete;
    PerformanceCsvWriter(PerformanceCsvWriter&&) = delete;
    PerformanceCsvWriter& operator=(PerformanceCsvWriter&&) = delete;

    bool write(const PerformanceCsvRow& row);
    bool close() noexcept;
    bool isOpen() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc::diagnostics
