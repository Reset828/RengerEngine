#pragma once

#include "diagnostics/ILogSink.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace dzc::diagnostics {

class RotatingFileSink final : public ILogSink {
public:
    // Opens the active file and configures the byte and file-count limits.
    explicit RotatingFileSink(const std::filesystem::path& path,
                              std::uintmax_t maxBytes = 20U * 1024U * 1024U,
                              std::size_t maxFiles = 10U,
                              std::shared_ptr<ILogSink> fallback = {});
    ~RotatingFileSink() override;

    RotatingFileSink(const RotatingFileSink&) = delete;
    RotatingFileSink& operator=(const RotatingFileSink&) = delete;

    // Writes one formatted record, rotating before the write when required.
    bool write(const LogRecord& record) override;

    // Flushes and closes the active file. Repeated calls are harmless.
    bool close() noexcept;

    // Reports whether the active file is currently available for writing.
    bool isOpen() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc::diagnostics
