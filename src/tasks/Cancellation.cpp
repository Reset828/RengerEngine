#include "tasks/Cancellation.h"

#include <atomic>
#include <utility>

namespace dzc::tasks {

class CancellationState final {
public:
    std::atomic_bool cancellationRequested{false};
};

class CancellationComposite final {
public:
    CancellationComposite(CancellationToken firstToken, CancellationToken secondToken)
        : first(std::move(firstToken)),
          second(std::move(secondToken)) {}

    CancellationToken first;
    CancellationToken second;
};

CancellationToken::CancellationToken(std::shared_ptr<const CancellationState> state) noexcept
    : m_state(std::move(state)) {}

CancellationToken CancellationToken::combine(
    const CancellationToken& first,
    const CancellationToken& second) {
    if (first.m_state == nullptr && first.m_composite == nullptr) {
        return second;
    }
    if (second.m_state == nullptr && second.m_composite == nullptr) {
        return first;
    }

    CancellationToken combined;
    combined.m_composite = std::make_shared<CancellationComposite>(first, second);
    return combined;
}

bool CancellationToken::isCancellationRequested() const noexcept {
    if (m_state != nullptr &&
        m_state->cancellationRequested.load(std::memory_order_acquire)) {
        return true;
    }

    return m_composite != nullptr &&
           (m_composite->first.isCancellationRequested() ||
            m_composite->second.isCancellationRequested());
}

CancellationSource::CancellationSource()
    : m_state(std::make_shared<CancellationState>()) {}

CancellationSource::~CancellationSource() {
    requestCancellation();
}

CancellationSource::CancellationSource(CancellationSource&& other) noexcept
    : m_state(std::move(other.m_state)) {}

CancellationSource& CancellationSource::operator=(CancellationSource&& other) noexcept {
    if (this != &other) {
        requestCancellation();
        m_state = std::move(other.m_state);
    }
    return *this;
}

CancellationToken CancellationSource::token() const noexcept {
    return CancellationToken(m_state);
}

bool CancellationSource::requestCancellation() noexcept {
    if (m_state == nullptr) {
        return false;
    }

    bool expected = false;
    return m_state->cancellationRequested.compare_exchange_strong(
        expected,
        true,
        std::memory_order_acq_rel,
        std::memory_order_acquire);
}

} // namespace dzc::tasks