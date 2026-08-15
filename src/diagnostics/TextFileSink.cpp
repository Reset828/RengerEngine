#include "diagnostics/TextFileSink.h"

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

namespace dzc::diagnostics {
namespace {

constexpr auto kFlushInterval = std::chrono::seconds(3);

} // namespace

class TextFileSink::Impl final {
public:
    explicit Impl(const std::filesystem::path& path) {
        m_stream.open(path, std::ios::binary | std::ios::out | std::ios::app);
        m_isOpen = m_stream.is_open();
        if (m_isOpen) {
            m_flushThread = std::thread(&Impl::flushLoop, this);
        }
    }

    ~Impl() {
        close();
    }

    bool write(const LogRecord& record) {
        const std::string line = formatLogRecord(record);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isOpen || m_closed || !m_stream.good()) {
            return false;
        }

        m_stream.write(line.data(), static_cast<std::streamsize>(line.size()));
        m_stream.put('\n');
        return m_stream.good();
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
        if (!m_isOpen) {
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

    bool isOpen() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_isOpen && !m_closed;
    }

private:
    void flushLoop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (!m_flushCondition.wait_for(lock, kFlushInterval,
                                          [this] { return m_stopRequested; })) {
            if (m_isOpen && !m_closed && m_stream.good()) {
                m_stream.flush();
            }
        }
    }

    mutable std::mutex m_mutex;
    std::mutex m_lifecycleMutex;
    std::condition_variable m_flushCondition;
    std::ofstream m_stream;
    std::thread m_flushThread;
    bool m_isOpen{false};
    bool m_closed{false};
    bool m_stopRequested{false};
};

TextFileSink::TextFileSink(const std::filesystem::path& path)
    : m_impl(std::make_unique<Impl>(path)) {}

TextFileSink::~TextFileSink() = default;

bool TextFileSink::write(const LogRecord& record) {
    return m_impl->write(record);
}

bool TextFileSink::close() noexcept {
    return m_impl->close();
}

bool TextFileSink::isOpen() const noexcept {
    return m_impl->isOpen();
}

} // namespace dzc::diagnostics
