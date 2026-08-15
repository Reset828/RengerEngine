#include "tasks/BackpressureController.h"

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace dzc::tasks {
namespace {

std::size_t calculateFloorWatermark(
    std::size_t capacity,
    std::size_t percent) noexcept {
    const std::size_t wholeHundreds = capacity / 100U;
    const std::size_t remainder = capacity % 100U;
    return (wholeHundreds * percent) + ((remainder * percent) / 100U);
}

std::size_t calculateCeilingWatermark(
    std::size_t capacity,
    std::size_t percent) noexcept {
    const std::size_t wholeHundreds = capacity / 100U;
    const std::size_t remainder = capacity % 100U;
    return (wholeHundreds * percent) +
           (((remainder * percent) + 99U) / 100U);
}

} // namespace

class BackpressureController::Impl final {
public:
    class State final {
    public:
        State(
            std::size_t requestedCapacity,
            std::size_t requestedHighWatermark,
            std::size_t requestedLowWatermark) noexcept
            : capacity(requestedCapacity),
              highWatermark(requestedHighWatermark),
              lowWatermark(requestedLowWatermark) {}

        const std::size_t capacity;
        const std::size_t highWatermark;
        const std::size_t lowWatermark;
        std::size_t usage = 0U;
        bool paused = false;
        bool closed = false;
        std::mutex mutex;
        std::condition_variable condition;
    };

    Impl(
        std::size_t capacity,
        std::size_t highWatermark,
        std::size_t lowWatermark)
        : state(std::make_shared<State>(capacity, highWatermark, lowWatermark)) {}

    std::shared_ptr<State> state;
};

BackpressureController::BackpressureController(
    std::size_t capacity,
    std::size_t highWatermarkPercent,
    std::size_t lowWatermarkPercent) {
    if (capacity == 0U || highWatermarkPercent == 0U ||
        highWatermarkPercent > 100U ||
        lowWatermarkPercent >= highWatermarkPercent) {
        throw std::invalid_argument("Invalid BackpressureController watermarks.");
    }

    const std::size_t highWatermark =
        calculateCeilingWatermark(capacity, highWatermarkPercent);
    const std::size_t lowWatermark =
        calculateFloorWatermark(capacity, lowWatermarkPercent);
    m_impl = std::make_unique<Impl>(capacity, highWatermark, lowWatermark);
}

BackpressureController::~BackpressureController() {
    close();
}

void BackpressureController::updateUsage(std::size_t usage) noexcept {
    const auto* impl = m_impl.get();
    if (impl == nullptr) {
        return;
    }

    const auto state = impl->state;
    bool notifyWaiters = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->closed) {
            return;
        }

        state->usage = usage;
        if (usage >= state->highWatermark) {
            state->paused = true;
        } else if (usage <= state->lowWatermark && state->paused) {
            state->paused = false;
            notifyWaiters = true;
        }
    }

    if (notifyWaiters) {
        state->condition.notify_all();
    }
}

bool BackpressureController::waitUntilResumed(CancellationToken token) {
    const auto* impl = m_impl.get();
    if (impl == nullptr || token.isCancellationRequested()) {
        return false;
    }

    const auto state = impl->state;
    const std::weak_ptr<Impl::State> weakState = state;
    auto registrations = token.registerCancellationNotification([weakState] {
        if (const auto activeState = weakState.lock()) {
            activeState->condition.notify_all();
        }
    });

    std::unique_lock<std::mutex> lock(state->mutex);
    for (;;) {
        if (state->closed || token.isCancellationRequested()) {
            return false;
        }

        if (!state->paused) {
            return true;
        }

        state->condition.wait(lock);
    }
}

void BackpressureController::close() noexcept {
    const auto* impl = m_impl.get();
    if (impl == nullptr) {
        return;
    }

    const auto state = impl->state;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->closed = true;
    }
    state->condition.notify_all();
}

} // namespace dzc::tasks