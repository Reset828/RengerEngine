#include "tasks/Cancellation.h"

#include <atomic>
#include <list>
#include <mutex>
#include <utility>

namespace dzc::tasks {

class CancellationRegistration final {
public:
    explicit CancellationRegistration(std::function<void()> notification)
        : m_notification(std::move(notification)) {}

    void notify() const noexcept {
        if (!m_active.load(std::memory_order_acquire)) {
            return;
        }

        try {
            m_notification();
        } catch (...) {
            // Cancellation notification is an internal best-effort wake-up only.
        }
    }

    ~CancellationRegistration() {
        m_active.store(false, std::memory_order_release);
    }

private:
    std::function<void()> m_notification;
    std::atomic_bool m_active{true};
};

class CancellationState final {
public:
    std::atomic_bool cancellationRequested{false};

    void registerNotification(
        const std::shared_ptr<CancellationRegistration>& registration) const {
        bool notifyImmediately = false;
        {
            std::lock_guard<std::mutex> lock(m_notificationMutex);
            if (cancellationRequested.load(std::memory_order_acquire)) {
                notifyImmediately = true;
            } else {
                for (auto iterator = m_notifications.begin();
                     iterator != m_notifications.end();) {
                    if (iterator->expired()) {
                        iterator = m_notifications.erase(iterator);
                    } else {
                        ++iterator;
                    }
                }
                m_notifications.emplace_back(registration);
            }
        }

        if (notifyImmediately) {
            registration->notify();
        }
    }

    void notifyRegistrations() const noexcept {
        std::lock_guard<std::mutex> lock(m_notificationMutex);
        for (auto iterator = m_notifications.begin();
             iterator != m_notifications.end();) {
            const auto registration = iterator->lock();
            if (registration == nullptr) {
                iterator = m_notifications.erase(iterator);
                continue;
            }

            registration->notify();
            ++iterator;
        }
    }

private:
    mutable std::mutex m_notificationMutex;
    mutable std::list<std::weak_ptr<CancellationRegistration>> m_notifications;
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

std::vector<std::shared_ptr<CancellationRegistration>>
CancellationToken::registerCancellationNotification(
    std::function<void()> notification) const {
    std::vector<std::shared_ptr<CancellationRegistration>> registrations;

    if (m_state != nullptr) {
        auto registration = std::make_shared<CancellationRegistration>(notification);
        m_state->registerNotification(registration);
        registrations.emplace_back(std::move(registration));
    }

    if (m_composite != nullptr) {
        auto firstRegistrations =
            m_composite->first.registerCancellationNotification(notification);
        auto secondRegistrations =
            m_composite->second.registerCancellationNotification(std::move(notification));

        registrations.reserve(
            registrations.size() + firstRegistrations.size() + secondRegistrations.size());
        for (auto& registration : firstRegistrations) {
            registrations.emplace_back(std::move(registration));
        }
        for (auto& registration : secondRegistrations) {
            registrations.emplace_back(std::move(registration));
        }
    }

    return registrations;
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
    if (!m_state->cancellationRequested.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return false;
    }

    m_state->notifyRegistrations();
    return true;
}

} // namespace dzc::tasks