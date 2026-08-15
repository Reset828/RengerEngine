#include "tasks/ConcurrencyGate.h"

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace dzc::tasks {

class ConcurrencyGate::Impl final {
public:
    explicit Impl(std::size_t requestedCapacity)
        : capacity(requestedCapacity),
          availablePermits(requestedCapacity) {}

    void releasePermit() noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        if (availablePermits < capacity) {
            ++availablePermits;
        }
        condition.notify_all();
    }

    const std::size_t capacity;
    std::size_t availablePermits;
    bool closed = false;
    std::mutex mutex;
    std::condition_variable condition;
};

ConcurrencyGate::Lease::Lease(std::shared_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl)),
      m_holdsPermit(m_impl != nullptr) {}

ConcurrencyGate::Lease::~Lease() {
    release();
}

ConcurrencyGate::Lease::Lease(Lease&& other) noexcept
    : m_impl(std::move(other.m_impl)),
      m_holdsPermit(other.m_holdsPermit) {
    other.m_holdsPermit = false;
}

ConcurrencyGate::Lease& ConcurrencyGate::Lease::operator=(Lease&& other) noexcept {
    if (this != &other) {
        release();
        m_impl = std::move(other.m_impl);
        m_holdsPermit = other.m_holdsPermit;
        other.m_holdsPermit = false;
    }
    return *this;
}

void ConcurrencyGate::Lease::release() noexcept {
    if (!m_holdsPermit || m_impl == nullptr) {
        return;
    }

    m_impl->releasePermit();
    m_holdsPermit = false;
}

ConcurrencyGate::ConcurrencyGate(std::size_t capacity) {
    if (capacity == 0U) {
        throw std::invalid_argument("ConcurrencyGate capacity must be greater than zero.");
    }

    m_impl = std::make_shared<Impl>(capacity);
}

ConcurrencyGate::~ConcurrencyGate() {
    close();
}

std::optional<ConcurrencyGate::Lease> ConcurrencyGate::acquire(
    CancellationToken token) {
    const auto impl = m_impl;
    if (impl == nullptr || token.isCancellationRequested()) {
        return std::nullopt;
    }

    const std::weak_ptr<Impl> weakImpl = impl;
    auto registrations = token.registerCancellationNotification([weakImpl] {
        if (const auto activeImpl = weakImpl.lock()) {
            activeImpl->condition.notify_all();
        }
    });

    std::unique_lock<std::mutex> lock(impl->mutex);
    for (;;) {
        if (impl->closed || token.isCancellationRequested()) {
            return std::nullopt;
        }

        if (impl->availablePermits != 0U) {
            --impl->availablePermits;
            return Lease(impl);
        }

        impl->condition.wait(lock);
    }
}

void ConcurrencyGate::close() noexcept {
    const auto impl = m_impl;
    if (impl == nullptr) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->closed = true;
    }
    impl->condition.notify_all();
}

} // namespace dzc::tasks