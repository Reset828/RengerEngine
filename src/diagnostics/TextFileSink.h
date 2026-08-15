#pragma once

#include "diagnostics/ILogSink.h"

#include <filesystem>
#include <memory>

namespace dzc::diagnostics {

class TextFileSink final : public ILogSink {
public:
    explicit TextFileSink(const std::filesystem::path& path);
    ~TextFileSink() override;

    TextFileSink(const TextFileSink&) = delete;
    TextFileSink& operator=(const TextFileSink&) = delete;

    bool write(const LogRecord& record) override;
    bool close() noexcept;
    bool isOpen() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc::diagnostics
