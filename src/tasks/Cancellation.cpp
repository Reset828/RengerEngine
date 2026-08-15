#include "tasks/Cancellation.h"

#include <atomic>
#include <utility>

namespace dzc::tasks {

class CancellationState final {
public:
    std::atomic_bool cancellationRequested{false};
};

CancellationToken::CancellationToken(std::shared_ptr<const CancellationState> state) noexcept
    : m_state(std::move(state)) {}

bool CancellationToken::isCancellationRequested() const noexcept {
    return m_state != nullptr &&
           m_state->cancellationRequested.load(std::memory_order_acquire);
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