#pragma once

#include <memory>

namespace dzc::tasks {

class CancellationState;

class CancellationToken final {
public:
    CancellationToken() noexcept = default;

    bool isCancellationRequested() const noexcept;

private:
    explicit CancellationToken(std::shared_ptr<const CancellationState> state) noexcept;

    std::shared_ptr<const CancellationState> m_state;

    friend class CancellationSource;
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