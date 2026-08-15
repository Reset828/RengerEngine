#pragma once

#include "diagnostics/IClock.h"

#include <chrono>
#include <cstddef>
#include <memory>

namespace dzc::diagnostics {

struct WindowStats {
    std::size_t sampleCount = 0U;
    double fps = 0.0;
    double averageFrameTimeMs = 0.0;
};

struct Snapshot {
    WindowStats frameWindow;
    WindowStats timeWindow;
};

class FrameStatistics final {
public:
    explicit FrameStatistics(
        std::shared_ptr<IClock> clock = {},
        std::size_t frameWindow = 120U,
        std::chrono::milliseconds timeWindow = std::chrono::seconds(1));

    ~FrameStatistics();

    FrameStatistics(const FrameStatistics&) = delete;
    FrameStatistics& operator=(const FrameStatistics&) = delete;
    FrameStatistics(FrameStatistics&&) = delete;
    FrameStatistics& operator=(FrameStatistics&&) = delete;

    bool addFrame(std::chrono::nanoseconds frameTime);
    Snapshot snapshot() const;
    void reset();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc::diagnostics
