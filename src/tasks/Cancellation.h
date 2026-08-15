#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace dzc::tasks {

class CancellationState;
class CancellationComposite;
class CancellationRegistration;
class TaskSystem;
class ConcurrencyGate;
class BackpressureController;

class CancellationToken final {
public:
    CancellationToken() noexcept = default;

    bool isCancellationRequested() const noexcept;

private:
    explicit CancellationToken(std::shared_ptr<const CancellationState> state) noexcept;
    static CancellationToken combine(
        const CancellationToken& first,
        const CancellationToken& second);
    std::vector<std::shared_ptr<CancellationRegistration>>
    registerCancellationNotification(std::function<void()> notification) const;

    std::shared_ptr<const CancellationState> m_state;
    std::shared_ptr<const CancellationComposite> m_composite;

    friend class CancellationSource;
    friend class TaskSystem;
    friend class ConcurrencyGate;
    friend class BackpressureController;
};

class CancellationSource final {
public:
    CancellationSource();
    ~CancellationSource();

    CancellationSource(const CancellationSource&) = delete;
    CancellationSource& operator=(const CancellationSource&) = delete;
    CancellationSource(CancellationSource&& other) noexcept;
    CancellationSource& operator=(CancellationSource&& other) noexcept;

    CancellationToken token() const noexcept;
    bool requestCancellation() noexcept;

private:
    std::shared_ptr<CancellationState> m_state;
};

} // namespace dzc::tasks