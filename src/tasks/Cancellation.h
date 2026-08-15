#pragma once

#include <memory>

namespace dzc::tasks {

class CancellationState;
class CancellationComposite;
class TaskSystem;

class CancellationToken final {
public:
    CancellationToken() noexcept = default;

    bool isCancellationRequested() const noexcept;

private:
    explicit CancellationToken(std::shared_ptr<const CancellationState> state) noexcept;
    static CancellationToken combine(
        const CancellationToken& first,
        const CancellationToken& second);

    std::shared_ptr<const CancellationState> m_state;
    std::shared_ptr<const CancellationComposite> m_composite;

    friend class CancellationSource;
    friend class TaskSystem;
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