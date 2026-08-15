#pragma once

#include "diagnostics/ILogSink.h"

#include <cstddef>
#include <memory>

namespace dzc::diagnostics {

class Logger final {
public:
    explicit Logger(std::shared_ptr<ILogSink> sink,
                    std::shared_ptr<ILogSink> fallback = {},
                    std::size_t capacity = 1024U);
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    bool write(const LogRecord& record);
    bool shutdown() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc::diagnostics
