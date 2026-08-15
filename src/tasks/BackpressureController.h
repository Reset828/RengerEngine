#pragma once

#include "tasks/Cancellation.h"

#include <cstddef>
#include <memory>

namespace dzc::tasks {

class BackpressureController final {
public:
    explicit BackpressureController(
        std::size_t capacity,
        std::size_t highWatermarkPercent = 80U,
        std::size_t lowWatermarkPercent = 60U);
    ~BackpressureController();

    BackpressureController(const BackpressureController&) = delete;
    BackpressureController& operator=(const BackpressureController&) = delete;
    BackpressureController(BackpressureController&&) = delete;
    BackpressureController& operator=(BackpressureController&&) = delete;

    void updateUsage(std::size_t usage) noexcept;
    bool waitUntilResumed(CancellationToken token = {});
    void close() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc::tasks