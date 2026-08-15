#include "diagnostics/Logger.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace dzc::diagnostics {

class Logger::Impl final {
public:
    Impl(std::shared_ptr<ILogSink> sink,
         std::shared_ptr<ILogSink> fallback,
         std::size_t capacity)
        : m_sink(std::move(sink)),
          m_fallback(std::move(fallback)),
          m_capacity(capacity),
          m_worker(&Impl::run, this) {}

    ~Impl() {
        (void)shutdown();
    }

    bool write(const LogRecord& record) {
        std::unique_lock lock(m_mutex);

        if (!m_accepting) {
            return false;
        }

        if (m_capacity == 0U) {
            const auto fallback = m_fallback;
            lock.unlock();
            if (record.level == LogLevel::Warn || record.level == LogLevel::Error) {
                return fallback != nullptr && fallback->write(record);
            }
            return false;
        }

        if (m_queue.size() >= m_capacity) {
            if (record.level == LogLevel::Trace || record.level == LogLevel::Debug ||
                record.level == LogLevel::Info) {
                return false;
            }

            if (record.level == LogLevel::Error) {
                const auto fallback = m_fallback;
                lock.unlock();
                return fallback != nullptr && fallback->write(record);
            }

            m_notFull.wait(lock, [this] {
                return !m_accepting || m_queue.size() < m_capacity;
            });
            if (!m_accepting) {
                return false;
            }
        }

        m_queue.push_back(record);
        lock.unlock();
        m_notEmpty.notify_one();
        return true;
    }

    bool shutdown() noexcept {
        std::unique_lock lock(m_mutex);

        if (m_joined) {
            return true;
        }

        m_accepting = false;
        m_notEmpty.notify_all();
        m_notFull.notify_all();

        if (m_joinInProgress) {
            m_shutdownComplete.wait(lock, [this] {
                return m_joined;
            });
            return true;
        }

        m_joinInProgress = true;
        lock.unlock();

        if (m_worker.joinable()) {
            m_worker.join();
        }

        lock.lock();
        m_joined = true;
        m_joinInProgress = false;
        lock.unlock();
        m_shutdownComplete.notify_all();
        return true;
    }

private:
    void run() noexcept {
        for (;;) {
            LogRecord record;
            {
                std::unique_lock lock(m_mutex);
                m_notEmpty.wait(lock, [this] {
                    return !m_accepting || !m_queue.empty();
                });

                if (m_queue.empty()) {
                    if (!m_accepting) {
                        break;
                    }
                    continue;
                }

                record = std::move(m_queue.front());
                m_queue.pop_front();
            }

            m_notFull.notify_one();
            if (m_sink != nullptr) {
                (void)m_sink->write(record);
            }
        }
    }

    std::shared_ptr<ILogSink> m_sink;
    std::shared_ptr<ILogSink> m_fallback;
    const std::size_t m_capacity;

    std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    std::condition_variable m_shutdownComplete;
    std::deque<LogRecord> m_queue;
    bool m_accepting{true};
    bool m_joinInProgress{false};
    bool m_joined{false};
    std::thread m_worker;
};

Logger::Logger(std::shared_ptr<ILogSink> sink,
               std::shared_ptr<ILogSink> fallback,
               std::size_t capacity)
    : m_impl(std::make_unique<Impl>(std::move(sink), std::move(fallback), capacity)) {}

Logger::~Logger() = default;

bool Logger::write(const LogRecord& record) {
    return m_impl->write(record);
}

bool Logger::shutdown() noexcept {
    return m_impl->shutdown();
}

} // namespace dzc::diagnostics
