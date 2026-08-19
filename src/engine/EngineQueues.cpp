#include "engine/EngineQueues.h"

#include "tasks/TaskSystem.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dzc {
namespace {

constexpr const char* kCriticalEventDropLog =
    "Dzc Engine: event queue full; a critical Engine event was dropped.\n";

bool isCriticalEvent(const EngineEvent& event) noexcept {
    return std::holds_alternative<ErrorEvent>(event) ||
           std::holds_alternative<DatasetLoadedEvent>(event) ||
           std::holds_alternative<DatasetLoadCancelledEvent>(event) ||
           std::holds_alternative<FeatureDegradedEvent>(event);
}

bool isProgressEventForDataset(const EngineEvent& event, DatasetId datasetId) noexcept {
    const auto* progress = std::get_if<DatasetProgressEvent>(&event);
    return progress != nullptr && progress->datasetId == datasetId;
}

ErrorEvent makeEventLossError() {
    return ErrorEvent{
        EventSeverity::RecoverableError,
        Error{
            ErrorDomain::Task,
            static_cast<std::uint32_t>(tasks::TaskErrorCode::QueueFull),
            "Engine event queue overflow",
            "A critical Engine event could not be delivered because the event queue was full.",
            "EngineEventQueue"},
        EventContext{}};
}

} // namespace

class EngineEventQueue::Impl final {
public:
    explicit Impl(std::size_t capacityIn)
        : capacity(capacityIn) {}

    const std::size_t capacity;
    std::deque<EngineEvent> events;
    std::optional<ErrorEvent> lostEvent;
    bool closed{false};
    std::mutex mutex;
};

EngineEventQueue::EngineEventQueue(std::size_t capacity) {
    if (capacity == 0U) {
        throw std::invalid_argument("EngineEventQueue capacity must be greater than zero");
    }
    m_impl = std::make_unique<Impl>(capacity);
}

EngineEventQueue::~EngineEventQueue() {
    close();
}

bool EngineEventQueue::tryPush(EngineEvent event) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->closed) {
        return false;
    }

    if (const auto* progress = std::get_if<DatasetProgressEvent>(&event)) {
        const auto existing = std::find_if(
            m_impl->events.begin(), m_impl->events.end(),
            [datasetId = progress->datasetId](const EngineEvent& queued) {
                return isProgressEventForDataset(queued, datasetId);
            });
        if (existing != m_impl->events.end()) {
            *existing = std::move(event);
            return true;
        }

        if (m_impl->events.size() == m_impl->capacity) {
            const auto oldestProgress = std::find_if(
                m_impl->events.begin(), m_impl->events.end(), [](const EngineEvent& queued) {
                    return std::holds_alternative<DatasetProgressEvent>(queued);
                });
            if (oldestProgress == m_impl->events.end()) {
                return false;
            }
            m_impl->events.erase(oldestProgress);
        }

        m_impl->events.emplace_back(std::move(event));
        return true;
    }

    if (m_impl->events.size() < m_impl->capacity) {
        m_impl->events.emplace_back(std::move(event));
        return true;
    }

    if (std::holds_alternative<MessageEvent>(event)) {
        return false;
    }

    if (isCriticalEvent(event)) {
        std::fputs(kCriticalEventDropLog, stderr);
        if (!m_impl->lostEvent.has_value()) {
            m_impl->lostEvent.emplace(makeEventLossError());
        }
    }
    return false;
}

std::vector<EngineEvent> EngineEventQueue::poll() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);

    std::vector<EngineEvent> result;
    result.reserve(m_impl->events.size() + (m_impl->lostEvent.has_value() ? 1U : 0U));
    if (m_impl->lostEvent.has_value()) {
        result.emplace_back(std::move(*m_impl->lostEvent));
        m_impl->lostEvent.reset();
    }
    while (!m_impl->events.empty()) {
        result.emplace_back(std::move(m_impl->events.front()));
        m_impl->events.pop_front();
    }
    return result;
}

void EngineEventQueue::close() noexcept {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->closed = true;
}

} // namespace dzc
