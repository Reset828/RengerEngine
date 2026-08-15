#pragma once

#include <chrono>

namespace dzc::diagnostics {

class IClock {
public:
    virtual ~IClock() = default;
    virtual std::chrono::steady_clock::time_point now() const noexcept = 0;
};

} // namespace dzc::diagnostics
