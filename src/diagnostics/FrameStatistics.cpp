#include "diagnostics/FrameStatistics.h"

#include <algorithm>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace dzc::diagnostics {
namespace {

class SystemClock final : public IClock {
public:
    std::chrono::steady_clock::time_point now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
};

struct Sample final {
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::nanoseconds frameTime;
};

WindowStats calculateStats(const std::vector<Sample>& samples) {
    WindowStats result;
    result.sampleCount = samples.size();
    if (samples.empty()) {
        return result;
    }

    long double totalMilliseconds = 0.0L;
    for (const Sample& sample : samples) {
        totalMilliseconds +=
            std::chrono::duration<long double, std::milli>(sample.frameTime).count();
    }
    result.averageFrameTimeMs =
        static_cast<double>(totalMilliseconds / static_cast<long double>(samples.size()));

    if (samples.size() >= 2U) {
        const double elapsedSeconds =
            std::chrono::duration<double>(samples.back().timestamp - samples.front().timestamp)
                .count();
        if (elapsedSeconds > 0.0) {
            result.fps = static_cast<double>(samples.size()) / elapsedSeconds;
        }
    }

    return result;
}

} // namespace

class FrameStatistics::Impl final {
public:
    Impl(std::shared_ptr<IClock> clock,
         std::size_t frameWindow,
         std::chrono::milliseconds timeWindow)
        : m_clock(clock ? std::move(clock) : std::make_shared<SystemClock>()),
          m_frameWindow(frameWindow),
          m_timeWindow(timeWindow) {}

    bool addFrame(std::chrono::nanoseconds frameTime) {
        if (frameTime <= std::chrono::nanoseconds::zero()) {
            return false;
        }

        std::lock_guard lock(m_mutex);
        if (m_frameWindow == 0U && m_timeWindow <= std::chrono::milliseconds::zero()) {
            return false;
        }

        const auto timestamp = m_clock->now();
        if (m_hasLastTimestamp && timestamp < m_lastTimestamp) {
            return false;
        }

        m_samples.push_back(Sample{timestamp, frameTime});
        m_lastTimestamp = timestamp;
        m_hasLastTimestamp = true;
        prune(timestamp);
        return true;
    }

    Snapshot snapshot() {
        std::lock_guard lock(m_mutex);
        const auto timestamp = m_clock->now();
        prune(timestamp);

        Snapshot result;
        if (m_frameWindow > 0U) {
            const std::size_t count = std::min(m_frameWindow, m_samples.size());
            std::vector<Sample> frameSamples;
            frameSamples.reserve(count);
            const auto first = m_samples.size() - count;
            for (std::size_t index = first; index < m_samples.size(); ++index) {
                frameSamples.push_back(m_samples[index]);
            }
            result.frameWindow = calculateStats(frameSamples);
        }

        if (m_timeWindow > std::chrono::milliseconds::zero()) {
            const auto cutoff = timestamp - m_timeWindow;
            std::vector<Sample> timeSamples;
            for (const Sample& sample : m_samples) {
                if (sample.timestamp >= cutoff && sample.timestamp <= timestamp) {
                    timeSamples.push_back(sample);
                }
            }
            result.timeWindow = calculateStats(timeSamples);
        }

        return result;
    }

    void reset() {
        std::lock_guard lock(m_mutex);
        m_samples.clear();
        m_hasLastTimestamp = false;
        m_lastTimestamp = {};
    }

private:
    void prune(std::chrono::steady_clock::time_point timestamp) {
        const bool timeWindowValid = m_timeWindow > std::chrono::milliseconds::zero();
        const auto cutoff = timeWindowValid ? timestamp - m_timeWindow : timestamp;

        while (!m_samples.empty()) {
            const bool neededForFrame =
                m_frameWindow > 0U && m_samples.size() <= m_frameWindow;
            const bool neededForTime =
                timeWindowValid && m_samples.front().timestamp >= cutoff;
            if (neededForFrame || neededForTime) {
                break;
            }
            m_samples.pop_front();
        }
    }

    std::shared_ptr<IClock> m_clock;
    const std::size_t m_frameWindow;
    const std::chrono::milliseconds m_timeWindow;
    mutable std::mutex m_mutex;
    std::deque<Sample> m_samples;
    bool m_hasLastTimestamp{false};
    std::chrono::steady_clock::time_point m_lastTimestamp{};
};

FrameStatistics::FrameStatistics(std::shared_ptr<IClock> clock,
                                 std::size_t frameWindow,
                                 std::chrono::milliseconds timeWindow)
    : m_impl(std::make_unique<Impl>(std::move(clock), frameWindow, timeWindow)) {}

FrameStatistics::~FrameStatistics() = default;

bool FrameStatistics::addFrame(std::chrono::nanoseconds frameTime) {
    return m_impl->addFrame(frameTime);
}

Snapshot FrameStatistics::snapshot() const {
    return m_impl->snapshot();
}

void FrameStatistics::reset() {
    m_impl->reset();
}

} // namespace dzc::diagnostics
