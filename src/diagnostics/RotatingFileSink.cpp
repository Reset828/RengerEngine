#include "diagnostics/RotatingFileSink.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace dzc::diagnostics {
namespace {

constexpr auto kFlushInterval = std::chrono::seconds(3);
constexpr std::uintmax_t kLineTerminatorBytes = 1U;

std::filesystem::path rotatedPath(const std::filesystem::path& activePath,
                                  std::size_t index) {
    return std::filesystem::path(activePath.string() + "." + std::to_string(index));
}

} // namespace

class RotatingFileSink::Impl final {
public:
    Impl(const std::filesystem::path& path,
         std::uintmax_t maxBytes,
         std::size_t maxFiles,
         std::shared_ptr<ILogSink> fallback)
        : m_path(path),
          m_maxBytes(maxBytes),
          m_maxFiles(maxFiles),
          m_fallback(std::move(fallback)) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_maxBytes != 0U && m_maxFiles != 0U) {
                openCurrentLocked(false);
            }
        }
        if (m_isOpen) {
            m_flushThread = std::thread(&Impl::flushLoop, this);
        }
    }

    ~Impl() {
        close();
    }

    bool write(const LogRecord& record) {
        const std::string line = formatLogRecord(record);
        const auto lineBytes = static_cast<std::uintmax_t>(line.size()) + kLineTerminatorBytes;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed) {
            return false;
        }

        if (!m_isOpen || !m_stream.good()) {
            return writeFallbackLocked(record, line);
        }

        const bool exceedsLimit = m_currentBytes >= m_maxBytes ||
                                  lineBytes > (m_maxBytes - m_currentBytes);
        if (m_currentBytes > 0U && exceedsLimit) {
            if (!rotateLocked() && (!m_isOpen || !m_stream.good())) {
                return writeFallbackLocked(record, line);
            }
        }

        if (m_isOpen && m_stream.good() && writeLineLocked(line, lineBytes)) {
            return true;
        }
        return writeFallbackLocked(record, line);
    }

    bool close() noexcept {
        std::lock_guard<std::mutex> lifecycleLock(m_lifecycleMutex);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_closed) {
                return true;
            }
            m_closed = true;
            m_stopRequested = true;
        }
        m_flushCondition.notify_all();

        if (m_flushThread.joinable()) {
            m_flushThread.join();
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        return closeCurrentLocked();
    }

    bool isOpen() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_isOpen && !m_closed;
    }

private:
    bool openCurrentLocked(bool truncate) {
        if (m_stream.is_open()) {
            m_stream.close();
        }
        m_stream.clear();

        const auto mode = std::ios::binary | std::ios::out |
                          (truncate ? std::ios::trunc : std::ios::app);
        m_stream.open(m_path, mode);
        m_isOpen = m_stream.is_open();
        m_currentBytes = 0U;
        if (!m_isOpen) {
            return false;
        }

        std::error_code error;
        const auto fileSize = std::filesystem::file_size(m_path, error);
        if (!error) {
            m_currentBytes = fileSize;
        }
        return true;
    }

    bool closeCurrentLocked() noexcept {
        if (!m_stream.is_open()) {
            m_isOpen = false;
            return true;
        }

        bool success = m_stream.good();
        m_stream.flush();
        success = success && m_stream.good();
        m_stream.close();
        success = success && !m_stream.fail();
        m_isOpen = false;
        return success;
    }

    bool writeLineLocked(const std::string& line, std::uintmax_t lineBytes) {
        m_stream.write(line.data(), static_cast<std::streamsize>(line.size()));
        m_stream.put('\n');
        if (!m_stream.good()) {
            closeCurrentLocked();
            return false;
        }
        m_currentBytes += lineBytes;
        return true;
    }

    bool writeFallbackLocked(const LogRecord& record, const std::string& line) {
        if (m_fallback) {
            return m_fallback->write(record);
        }

        std::string stderrLine = line;
        stderrLine.push_back('\n');
        return std::fwrite(stderrLine.data(), 1, stderrLine.size(), stderr) == stderrLine.size();
    }

    bool rotateLocked() {
        if (!closeCurrentLocked()) {
            openCurrentLocked(false);
            return false;
        }

        bool success = true;
        const std::size_t backupCount = m_maxFiles > 0U ? m_maxFiles - 1U : 0U;
        if (backupCount == 0U) {
            std::error_code error;
            const bool activeExists = std::filesystem::exists(m_path, error);
            if (error) {
                success = false;
            } else if (activeExists && !std::filesystem::remove(m_path, error)) {
                success = false;
            }
        } else {
            const auto oldest = rotatedPath(m_path, backupCount);
            std::error_code error;
            const bool oldestExists = std::filesystem::exists(oldest, error);
            if (error) {
                success = false;
            } else if (oldestExists && !std::filesystem::remove(oldest, error)) {
                success = false;
            }

            for (std::size_t index = backupCount; success && index > 1U; --index) {
                const auto source = rotatedPath(m_path, index - 1U);
                const auto destination = rotatedPath(m_path, index);
                error.clear();
                const bool sourceExists = std::filesystem::exists(source, error);
                if (error) {
                    success = false;
                    break;
                }
                if (sourceExists) {
                    std::filesystem::rename(source, destination, error);
                    if (error) {
                        success = false;
                    }
                }
            }

            if (success) {
                const auto newest = rotatedPath(m_path, 1U);
                error.clear();
                const bool activeExists = std::filesystem::exists(m_path, error);
                if (error || !activeExists) {
                    success = false;
                } else {
                    std::filesystem::rename(m_path, newest, error);
                    success = !error;
                }
            }
        }

        if (success && openCurrentLocked(true)) {
            return true;
        }
        openCurrentLocked(false);
        return false;
    }

    void flushLoop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (!m_flushCondition.wait_for(lock, kFlushInterval,
                                          [this] { return m_stopRequested; })) {
            if (m_isOpen && !m_closed && m_stream.good()) {
                m_stream.flush();
            }
        }
    }

    const std::filesystem::path m_path;
    const std::uintmax_t m_maxBytes;
    const std::size_t m_maxFiles;
    const std::shared_ptr<ILogSink> m_fallback;

    mutable std::mutex m_mutex;
    std::mutex m_lifecycleMutex;
    std::condition_variable m_flushCondition;
    std::ofstream m_stream;
    std::thread m_flushThread;
    std::uintmax_t m_currentBytes{0U};
    bool m_isOpen{false};
    bool m_closed{false};
    bool m_stopRequested{false};
};

RotatingFileSink::RotatingFileSink(const std::filesystem::path& path,
                                   std::uintmax_t maxBytes,
                                   std::size_t maxFiles,
                                   std::shared_ptr<ILogSink> fallback)
    : m_impl(std::make_unique<Impl>(path, maxBytes, maxFiles, std::move(fallback))) {}

RotatingFileSink::~RotatingFileSink() = default;

bool RotatingFileSink::write(const LogRecord& record) {
    return m_impl->write(record);
}

bool RotatingFileSink::close() noexcept {
    return m_impl->close();
}

bool RotatingFileSink::isOpen() const noexcept {
    return m_impl->isOpen();
}

} // namespace dzc::diagnostics

