#pragma once

#include "tasks/Cancellation.h"

#include <cstddef>
#include <memory>
#include <optional>

namespace dzc::tasks {

class ConcurrencyGate final {
private:
    class Impl;

public:
    class Lease final {
    public:
        ~Lease();

        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;

    private:
        explicit Lease(std::shared_ptr<Impl> impl) noexcept;
        void release() noexcept;

        std::shared_ptr<Impl> m_impl;
        bool m_holdsPermit = false;

        friend class ConcurrencyGate;
    };

    explicit ConcurrencyGate(std::size_t capacity = 2U);
    ~ConcurrencyGate();

    ConcurrencyGate(const ConcurrencyGate&) = delete;
    ConcurrencyGate& operator=(const ConcurrencyGate&) = delete;
    ConcurrencyGate(ConcurrencyGate&&) = delete;
    ConcurrencyGate& operator=(ConcurrencyGate&&) = delete;

    std::optional<Lease> acquire(CancellationToken token = {});
    void close() noexcept;

private:
    std::shared_ptr<Impl> m_impl;
};

} // namespace dzc::tasks