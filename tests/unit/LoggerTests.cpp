#include <diagnostics/Logger.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using dzc::diagnostics::ILogSink;
using dzc::diagnostics::LogLevel;
using dzc::diagnostics::LogRecord;
using dzc::diagnostics::Logger;

LogRecord makeRecord(std::string message, LogLevel level = LogLevel::Info) {
    LogRecord record;
    record.level = level;
    record.module = "Diagnostics.LoggerTest";
    record.errorCode = 17U;
    record.message = std::move(message);
    return record;
}

class ProbeSink final : public ILogSink {
public:
    bool write(const LogRecord& record) override {
        std::unique_lock lock(m_mutex);
        ++m_calls;
        m_entered = true;
        m_enteredCondition.notify_all();
        if (m_blockNext) {
            m_blockNext = false;
            m_releaseCondition.wait(lock, [this] {
                return m_released;
            });
        }
        if (m_failNext > 0U) {
            --m_failNext;
            return false;
        }
        m_records.push_back(record);
        m_recordCondition.notify_all();
        return true;
    }

    void blockNextWrite() {
        std::lock_guard lock(m_mutex);
        m_blockNext = true;
        m_released = false;
    }

    bool waitUntilEntered(std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        std::unique_lock lock(m_mutex);
        return m_enteredCondition.wait_for(lock, timeout, [this] {
            return m_entered;
        });
    }

    void releaseBlockedWrite() {
        std::lock_guard lock(m_mutex);
        m_released = true;
        m_releaseCondition.notify_all();
    }

    void failNextWrite() {
        std::lock_guard lock(m_mutex);
        ++m_failNext;
    }

    bool waitForRecordCount(std::size_t count,
                            std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
        std::unique_lock lock(m_mutex);
        return m_recordCondition.wait_for(lock, timeout, [this, count] {
            return m_records.size() >= count;
        });
    }

    std::vector<std::string> messages() const {
        std::lock_guard lock(m_mutex);
        std::vector<std::string> result;
        result.reserve(m_records.size());
        for (const auto& record : m_records) {
            result.push_back(record.message);
        }
        return result;
    }

    std::size_t calls() const {
        std::lock_guard lock(m_mutex);
        return m_calls;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_enteredCondition;
    std::condition_variable m_releaseCondition;
    std::condition_variable m_recordCondition;
    std::vector<LogRecord> m_records;
    std::size_t m_calls{0U};
    std::size_t m_failNext{0U};
    bool m_blockNext{false};
    bool m_released{false};
    bool m_entered{false};
};

void fillBlockedQueue(Logger& logger, const std::shared_ptr<ProbeSink>& primary) {
    primary->blockNextWrite();
    assert(logger.write(makeRecord("blocking")));
    assert(primary->waitUntilEntered());
    assert(logger.write(makeRecord("queued")));
}

void testFifoAndShutdownDrainsQueue() {
    auto primary = std::make_shared<ProbeSink>();
    Logger logger(primary, {}, 8U);

    assert(logger.write(makeRecord("first")));
    assert(logger.write(makeRecord("second")));
    assert(logger.write(makeRecord("third")));
    assert(logger.shutdown());
    assert(logger.shutdown());

    const auto messages = primary->messages();
    assert((messages == std::vector<std::string>{"first", "second", "third"}));
}

void testLowLevelsAreDroppedWhenQueueIsFull() {
    auto primary = std::make_shared<ProbeSink>();
    Logger logger(primary, {}, 1U);
    fillBlockedQueue(logger, primary);

    assert(!logger.write(makeRecord("trace", LogLevel::Trace)));
    assert(!logger.write(makeRecord("debug", LogLevel::Debug)));
    assert(!logger.write(makeRecord("info", LogLevel::Info)));

    primary->releaseBlockedWrite();
    assert(logger.shutdown());
}

void testWarnWaitsForSpaceAndIsNotDropped() {
    auto primary = std::make_shared<ProbeSink>();
    Logger logger(primary, {}, 1U);
    fillBlockedQueue(logger, primary);

    std::atomic_bool result{false};
    std::atomic_bool completed{false};
    std::thread producer([&] {
        result.store(logger.write(makeRecord("warn", LogLevel::Warn)));
        completed.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(!completed.load());

    primary->releaseBlockedWrite();
    producer.join();
    assert(result.load());
    assert(logger.shutdown());
    assert(primary->waitForRecordCount(3U));
}

void testErrorUsesFallbackWhenQueueIsFull() {
    auto primary = std::make_shared<ProbeSink>();
    auto fallback = std::make_shared<ProbeSink>();
    Logger logger(primary, fallback, 1U);
    fillBlockedQueue(logger, primary);

    assert(logger.write(makeRecord("error", LogLevel::Error)));
    assert(fallback->waitForRecordCount(1U));
    assert(fallback->messages() == std::vector<std::string>{"error"});

    primary->releaseBlockedWrite();
    assert(logger.shutdown());
}

void testErrorFallbackFailureIsReported() {
    auto primary = std::make_shared<ProbeSink>();
    auto fallback = std::make_shared<ProbeSink>();
    fallback->failNextWrite();
    Logger logger(primary, fallback, 1U);
    fillBlockedQueue(logger, primary);

    assert(!logger.write(makeRecord("error", LogLevel::Error)));
    primary->releaseBlockedWrite();
    assert(logger.shutdown());
}

void testZeroCapacityUsesFallbackForWarnAndError() {
    auto primary = std::make_shared<ProbeSink>();
    auto fallback = std::make_shared<ProbeSink>();
    Logger logger(primary, fallback, 0U);

    assert(!logger.write(makeRecord("info", LogLevel::Info)));
    assert(logger.write(makeRecord("warn", LogLevel::Warn)));
    assert(logger.write(makeRecord("error", LogLevel::Error)));
    assert(fallback->waitForRecordCount(2U));
    assert(logger.shutdown());
}

void testShutdownRejectsNewRecordsAndIsThreadSafe() {
    auto primary = std::make_shared<ProbeSink>();
    Logger logger(primary, {}, 8U);

    std::vector<std::thread> shutdowners;
    for (int index = 0; index < 8; ++index) {
        shutdowners.emplace_back([&logger] {
            assert(logger.shutdown());
        });
    }
    for (auto& shutdowner : shutdowners) {
        shutdowner.join();
    }

    assert(!logger.write(makeRecord("after-shutdown")));
    assert(logger.shutdown());
}

void testConcurrentWritersHaveNoDataRace() {
    auto primary = std::make_shared<ProbeSink>();
    Logger logger(primary, {}, 64U);
    constexpr int producerCount = 4;
    constexpr int recordsPerProducer = 100;
    std::vector<std::thread> producers;
    std::size_t accepted = 0U;
    std::mutex acceptedMutex;

    for (int producerIndex = 0; producerIndex < producerCount; ++producerIndex) {
        producers.emplace_back([&, producerIndex] {
            std::size_t localAccepted = 0U;
            for (int recordIndex = 0; recordIndex < recordsPerProducer; ++recordIndex) {
                if (logger.write(makeRecord(std::to_string(producerIndex) + ":" +
                                             std::to_string(recordIndex)))) {
                    ++localAccepted;
                }
            }
            std::lock_guard lock(acceptedMutex);
            accepted += localAccepted;
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    assert(logger.shutdown());
    assert(primary->calls() >= accepted);
}

void testFailedPrimaryWriteDoesNotStopWorker() {
    auto primary = std::make_shared<ProbeSink>();
    primary->failNextWrite();
    Logger logger(primary, {}, 4U);

    assert(logger.write(makeRecord("failed")));
    assert(logger.write(makeRecord("after-failure")));
    assert(logger.shutdown());
    assert(primary->calls() == 2U);
    assert(primary->messages() == std::vector<std::string>{"after-failure"});
}

void testNullPrimaryStillAcceptsQueuedRecords() {
    auto fallback = std::make_shared<ProbeSink>();
    Logger logger({}, fallback, 2U);

    assert(logger.write(makeRecord("accepted")));
    assert(logger.shutdown());
    assert(fallback->calls() == 0U);
}

} // namespace

int main() {
    testFifoAndShutdownDrainsQueue();
    testLowLevelsAreDroppedWhenQueueIsFull();
    testWarnWaitsForSpaceAndIsNotDropped();
    testErrorUsesFallbackWhenQueueIsFull();
    testErrorFallbackFailureIsReported();
    testZeroCapacityUsesFallbackForWarnAndError();
    testShutdownRejectsNewRecordsAndIsThreadSafe();
    testConcurrentWritersHaveNoDataRace();
    testFailedPrimaryWriteDoesNotStopWorker();
    testNullPrimaryStillAcceptsQueuedRecords();
    return 0;
}



