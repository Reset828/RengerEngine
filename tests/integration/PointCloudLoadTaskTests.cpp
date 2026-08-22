#include "data/io/PointCloudLoadTask.h"

#include <dzc/Error.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using dzc::Error;
using dzc::ErrorDomain;
using dzc::IPointCloudReader;
using dzc::PointAttribute;
using dzc::PointBatch;
using dzc::PointCloudLoadRequest;
using dzc::PointCloudLoadTask;
using dzc::PointCloudSourceInfo;
using dzc::Result;
using dzc::tasks::BackpressureController;
using dzc::tasks::CancellationSource;
using dzc::tasks::CancellationToken;
using dzc::tasks::ConcurrencyGate;
using dzc::tasks::TaskCompletion;
using dzc::tasks::TaskPriority;
using dzc::tasks::TaskSystem;

constexpr auto kWaitTimeout = std::chrono::seconds(2);
constexpr std::uint32_t kInvalidValueCode = 1U;
constexpr std::uint32_t kCancelledCode = 7U;
constexpr std::uint32_t kInternalCode = 1U;
constexpr std::uint32_t kCorruptDataCode = 2U;

Error makeError(ErrorDomain domain, std::uint32_t code, const char* context) {
    return Error{domain, code, "Test error.", "Controlled test failure.", context};
}

PointBatch makeValidBatch(double firstX) {
    PointBatch batch;
    batch.schema.mask = static_cast<std::uint32_t>(PointAttribute::Position);
    batch.positions.emplace_back(firstX, firstX + 1.0, firstX + 2.0);
    return batch;
}

PointCloudSourceInfo makeSourceInfo() {
    PointCloudSourceInfo info;
    info.schema.mask = static_cast<std::uint32_t>(PointAttribute::Position);
    info.declaredPointCount = 2U;
    return info;
}

template <typename Predicate>
bool waitUntil(Predicate&& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

struct IoTracker final {
    std::mutex mutex;
    std::size_t activeCalls{0U};
    std::size_t peakCalls{0U};
};

struct ReaderState final {
    std::mutex mutex;
    std::condition_variable condition;
    PointCloudSourceInfo sourceInfo{makeSourceInfo()};
    std::vector<PointBatch> batches;
    std::optional<Error> openError;
    std::optional<Error> readError;
    std::shared_ptr<IoTracker> ioTracker;
    bool blockOpen{false};
    bool blockRead{false};
    std::size_t nextBatch{0U};
    std::size_t openCalls{0U};
    std::size_t readCalls{0U};
    std::size_t closeCalls{0U};
    std::size_t activeIoCalls{0U};
    std::size_t peakIoCalls{0U};
    std::vector<std::thread::id> ioThreads;
};

void beginTrackedIo(const std::shared_ptr<IoTracker>& tracker) {
    if (!tracker) {
        return;
    }
    std::lock_guard<std::mutex> lock(tracker->mutex);
    ++tracker->activeCalls;
    tracker->peakCalls = std::max(tracker->peakCalls, tracker->activeCalls);
}

void endTrackedIo(const std::shared_ptr<IoTracker>& tracker) {
    if (!tracker) {
        return;
    }
    std::lock_guard<std::mutex> lock(tracker->mutex);
    assert(tracker->activeCalls > 0U);
    --tracker->activeCalls;
}

class FakeReader final : public IPointCloudReader {
public:
    explicit FakeReader(std::shared_ptr<ReaderState> state)
        : m_state(std::move(state)) {}

    Result<PointCloudSourceInfo> open(const std::string&) override {
        std::optional<Error> error;
        PointCloudSourceInfo sourceInfo;
        std::shared_ptr<IoTracker> tracker;
        {
            std::unique_lock<std::mutex> lock(m_state->mutex);
            ++m_state->openCalls;
            ++m_state->activeIoCalls;
            m_state->peakIoCalls = std::max(m_state->peakIoCalls, m_state->activeIoCalls);
            tracker = m_state->ioTracker;
            beginTrackedIo(tracker);
            m_state->ioThreads.push_back(std::this_thread::get_id());
            m_state->condition.notify_all();
            m_state->condition.wait(lock, [this] { return !m_state->blockOpen; });
            --m_state->activeIoCalls;
            error = m_state->openError;
            sourceInfo = m_state->sourceInfo;
            m_state->condition.notify_all();
        }
        endTrackedIo(tracker);
        if (error.has_value()) {
            return Result<PointCloudSourceInfo>::failure(*error);
        }
        return Result<PointCloudSourceInfo>::success(std::move(sourceInfo));
    }

    Result<std::optional<PointBatch>> readNext(
        std::size_t,
        CancellationToken) override {
        std::optional<Error> error;
        std::optional<PointBatch> batch;
        std::shared_ptr<IoTracker> tracker;
        {
            std::unique_lock<std::mutex> lock(m_state->mutex);
            ++m_state->readCalls;
            ++m_state->activeIoCalls;
            m_state->peakIoCalls = std::max(m_state->peakIoCalls, m_state->activeIoCalls);
            tracker = m_state->ioTracker;
            beginTrackedIo(tracker);
            m_state->ioThreads.push_back(std::this_thread::get_id());
            m_state->condition.notify_all();
            m_state->condition.wait(lock, [this] { return !m_state->blockRead; });
            --m_state->activeIoCalls;
            error = m_state->readError;
            if (!error.has_value() && m_state->nextBatch < m_state->batches.size()) {
                batch.emplace(std::move(m_state->batches[m_state->nextBatch]));
                ++m_state->nextBatch;
            }
            m_state->condition.notify_all();
        }
        endTrackedIo(tracker);
        if (error.has_value()) {
            return Result<std::optional<PointBatch>>::failure(*error);
        }
        return Result<std::optional<PointBatch>>::success(std::move(batch));
    }

    void close() noexcept override {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        ++m_state->closeCalls;
        m_state->condition.notify_all();
    }

private:
    std::shared_ptr<ReaderState> m_state;
};
struct CallbackState final {
    std::mutex mutex;
    std::condition_variable condition;
    std::size_t openedCalls{0U};
    std::size_t batchCalls{0U};
    std::vector<std::string> order;
    std::vector<double> firstPositions;
    std::vector<std::thread::id> callbackThreads;
};

PointCloudLoadRequest makeRequest(
    std::shared_ptr<ReaderState> readerState,
    std::shared_ptr<ConcurrencyGate> gate,
    std::shared_ptr<BackpressureController> backpressure,
    dzc::DatasetId datasetId,
    dzc::PointCloudLoadOpenedCallback onOpened,
    dzc::PointCloudLoadBatchCallback onBatch,
    CancellationToken token = {}) {
    PointCloudLoadRequest request;
    request.datasetId = datasetId;
    request.sourcePath = "controlled.fake";
    request.reader = std::make_unique<FakeReader>(std::move(readerState));
    request.maximumPointsPerBatch = 8U;
    request.concurrencyGate = std::move(gate);
    request.backpressureController = std::move(backpressure);
    request.onOpened = std::move(onOpened);
    request.onBatch = std::move(onBatch);
    request.cancellationToken = std::move(token);
    request.priority = TaskPriority::Normal;
    return request;
}

void recordOpened(const std::shared_ptr<CallbackState>& state) {
    std::lock_guard<std::mutex> lock(state->mutex);
    ++state->openedCalls;
    state->order.emplace_back("opened");
    state->callbackThreads.push_back(std::this_thread::get_id());
    state->condition.notify_all();
}

void recordBatch(const std::shared_ptr<CallbackState>& state, PointBatch&& batch) {
    std::lock_guard<std::mutex> lock(state->mutex);
    ++state->batchCalls;
    state->order.emplace_back("batch");
    state->firstPositions.push_back(batch.positions.front().x);
    state->callbackThreads.push_back(std::this_thread::get_id());
    state->condition.notify_all();
}

TaskCompletion waitForSingleCompletion(TaskSystem& taskSystem) {
    taskSystem.waitForCompletion();
    const auto completion = taskSystem.tryPopCompletion();
    assert(completion.has_value());
    assert(!taskSystem.tryPopCompletion().has_value());
    return *completion;
}

void assertSuccess(const TaskCompletion& completion, dzc::DatasetId datasetId) {
    assert(completion.datasetId.has_value());
    assert(*completion.datasetId == datasetId);
    assert(completion.result.hasValue());
}

void assertError(
    const TaskCompletion& completion,
    ErrorDomain domain,
    std::uint32_t code) {
    assert(!completion.result.hasValue());
    assert(completion.result.error().domain == domain);
    assert(completion.result.error().code == code);
}

void testWorkerCallbacksMetadataBatchingAndEof() {
    TaskSystem taskSystem(1U, 8U, 8U);
    const auto gate = std::make_shared<ConcurrencyGate>(1U);
    const auto backpressure = std::make_shared<BackpressureController>(4U);
    const auto reader = std::make_shared<ReaderState>();
    reader->batches.push_back(makeValidBatch(1.0));
    reader->batches.push_back(makeValidBatch(10.0));
    const auto callbacks = std::make_shared<CallbackState>();
    const std::thread::id submitterThread = std::this_thread::get_id();
    const dzc::DatasetId datasetId{101U};

    const auto submitted = PointCloudLoadTask::submit(
        taskSystem,
        makeRequest(
            reader,
            gate,
            backpressure,
            datasetId,
            [callbacks](PointCloudSourceInfo info, CancellationToken) {
                assert(info.schema.hasPosition());
                recordOpened(callbacks);
                return Result<void>::success();
            },
            [callbacks](PointBatch&& batch, CancellationToken) {
                recordBatch(callbacks, std::move(batch));
                return Result<void>::success();
            }));
    assert(submitted.hasValue());

    const TaskCompletion completion = waitForSingleCompletion(taskSystem);
    assertSuccess(completion, datasetId);
    {
        std::lock_guard<std::mutex> lock(callbacks->mutex);
        assert(callbacks->openedCalls == 1U);
        assert(callbacks->batchCalls == 2U);
        assert(callbacks->order.size() == 3U);
        assert(callbacks->order.front() == "opened");
        assert(callbacks->firstPositions == std::vector<double>({1.0, 10.0}));
        for (const std::thread::id threadId : callbacks->callbackThreads) {
            assert(threadId != submitterThread);
        }
    }
    {
        std::lock_guard<std::mutex> lock(reader->mutex);
        assert(reader->openCalls == 1U);
        assert(reader->readCalls == 3U);
        assert(reader->closeCalls == 1U);
        for (const std::thread::id threadId : reader->ioThreads) {
            assert(threadId != submitterThread);
        }
    }
}

void testSharedGateLimitsOpenAndReadConcurrency() {
    TaskSystem taskSystem(3U, 8U, 8U);
    const auto gate = std::make_shared<ConcurrencyGate>(2U);
    const auto backpressure = std::make_shared<BackpressureController>(4U);
    const auto ioTracker = std::make_shared<IoTracker>();
    std::vector<std::shared_ptr<ReaderState>> readers;
    std::vector<std::shared_ptr<CallbackState>> callbacks;
    readers.reserve(3U);
    callbacks.reserve(3U);

    for (std::size_t index = 0U; index < 3U; ++index) {
        const auto reader = std::make_shared<ReaderState>();
        reader->blockRead = true;
        reader->ioTracker = ioTracker;
        const auto callback = std::make_shared<CallbackState>();
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(
                reader,
                gate,
                backpressure,
                dzc::DatasetId{200U + index},
                [callback](PointCloudSourceInfo, CancellationToken) {
                    recordOpened(callback);
                    return Result<void>::success();
                },
                [callback](PointBatch&& batch, CancellationToken) {
                    recordBatch(callback, std::move(batch));
                    return Result<void>::success();
                }));
        assert(submitted.hasValue());
        readers.push_back(reader);
        callbacks.push_back(callback);
    }

    assert(waitUntil([&] {
        std::lock_guard<std::mutex> lock(ioTracker->mutex);
        return ioTracker->activeCalls == 2U;
    }));

    std::size_t peak = 0U;
    {
        std::lock_guard<std::mutex> lock(ioTracker->mutex);
        peak = ioTracker->peakCalls;
    }
    for (const auto& reader : readers) {
        std::lock_guard<std::mutex> lock(reader->mutex);
        reader->blockRead = false;
        reader->condition.notify_all();
    }

    taskSystem.waitForCompletion();
    std::size_t completions = 0U;
    while (const auto completion = taskSystem.tryPopCompletion()) {
        assert(completion->result.hasValue());
        ++completions;
    }
    assert(completions == 3U);
    assert(peak <= 2U);
    for (const auto& reader : readers) {
        std::lock_guard<std::mutex> lock(reader->mutex);
        assert(reader->peakIoCalls <= 1U);
        assert(reader->readCalls == 1U);
        assert(reader->closeCalls == 1U);
    }
}

void testBackpressurePausesBeforeNextRead() {
    TaskSystem taskSystem(1U, 8U, 8U);
    const auto gate = std::make_shared<ConcurrencyGate>(1U);
    const auto backpressure = std::make_shared<BackpressureController>(2U, 50U, 0U);
    backpressure->updateUsage(1U);
    const auto reader = std::make_shared<ReaderState>();
    reader->batches.push_back(makeValidBatch(1.0));
    reader->batches.push_back(makeValidBatch(2.0));
    const auto callbacks = std::make_shared<CallbackState>();

    const auto submitted = PointCloudLoadTask::submit(
        taskSystem,
        makeRequest(
            reader,
            gate,
            backpressure,
            dzc::DatasetId{301U},
            [callbacks](PointCloudSourceInfo, CancellationToken) {
                recordOpened(callbacks);
                return Result<void>::success();
            },
            [callbacks, backpressure](PointBatch&& batch, CancellationToken) {
                recordBatch(callbacks, std::move(batch));
                backpressure->updateUsage(1U);
                return Result<void>::success();
            }));
    assert(submitted.hasValue());
    assert(waitUntil([&] {
        std::lock_guard<std::mutex> lock(callbacks->mutex);
        return callbacks->openedCalls == 1U;
    }));
    {
        std::lock_guard<std::mutex> lock(reader->mutex);
        assert(reader->readCalls == 0U);
    }

    backpressure->updateUsage(0U);
    assert(waitUntil([&] {
        std::lock_guard<std::mutex> lock(callbacks->mutex);
        return callbacks->batchCalls == 1U;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    {
        std::lock_guard<std::mutex> lock(reader->mutex);
        assert(reader->readCalls == 1U);
    }

    backpressure->updateUsage(0U);
    assert(waitUntil([&] {
        std::lock_guard<std::mutex> lock(callbacks->mutex);
        return callbacks->batchCalls == 2U;
    }));
    backpressure->updateUsage(0U);
    const TaskCompletion completion = waitForSingleCompletion(taskSystem);
    assertSuccess(completion, dzc::DatasetId{301U});
}

void testCancellationStopsNewDelivery() {
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto gate = std::make_shared<ConcurrencyGate>(1U);
        const auto backpressure = std::make_shared<BackpressureController>(2U);
        CancellationSource source;
        assert(source.requestCancellation());
        const auto reader = std::make_shared<ReaderState>();
        const auto callbacks = std::make_shared<CallbackState>();
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(
                reader, gate, backpressure, dzc::DatasetId{401U},
                [callbacks](PointCloudSourceInfo, CancellationToken) {
                    recordOpened(callbacks);
                    return Result<void>::success();
                },
                [callbacks](PointBatch&& batch, CancellationToken) {
                    recordBatch(callbacks, std::move(batch));
                    return Result<void>::success();
                },
                source.token()));
        assert(submitted.hasValue());
        assertError(waitForSingleCompletion(taskSystem), ErrorDomain::Task, kCancelledCode);
        std::lock_guard<std::mutex> lock(reader->mutex);
        assert(reader->openCalls == 0U);
        assert(reader->readCalls == 0U);
        assert(reader->closeCalls == 1U);
    }

    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto gate = std::make_shared<ConcurrencyGate>(1U);
        const auto heldLease = gate->acquire();
        assert(heldLease.has_value());
        const auto backpressure = std::make_shared<BackpressureController>(2U);
        CancellationSource source;
        const auto reader = std::make_shared<ReaderState>();
        const auto callbacks = std::make_shared<CallbackState>();
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(
                reader, gate, backpressure, dzc::DatasetId{402U},
                [callbacks](PointCloudSourceInfo, CancellationToken) {
                    recordOpened(callbacks);
                    return Result<void>::success();
                },
                [callbacks](PointBatch&& batch, CancellationToken) {
                    recordBatch(callbacks, std::move(batch));
                    return Result<void>::success();
                },
                source.token()));
        assert(submitted.hasValue());
        assert(source.requestCancellation());
        assertError(waitForSingleCompletion(taskSystem), ErrorDomain::Task, kCancelledCode);
        std::lock_guard<std::mutex> lock(reader->mutex);
        assert(reader->openCalls == 0U);
        assert(reader->readCalls == 0U);
    }

    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto gate = std::make_shared<ConcurrencyGate>(1U);
        const auto backpressure = std::make_shared<BackpressureController>(2U, 50U, 0U);
        backpressure->updateUsage(1U);
        CancellationSource source;
        const auto reader = std::make_shared<ReaderState>();
        const auto callbacks = std::make_shared<CallbackState>();
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(
                reader, gate, backpressure, dzc::DatasetId{403U},
                [callbacks](PointCloudSourceInfo, CancellationToken) {
                    recordOpened(callbacks);
                    return Result<void>::success();
                },
                [callbacks](PointBatch&& batch, CancellationToken) {
                    recordBatch(callbacks, std::move(batch));
                    return Result<void>::success();
                },
                source.token()));
        assert(submitted.hasValue());
        assert(waitUntil([&] {
            std::lock_guard<std::mutex> lock(callbacks->mutex);
            return callbacks->openedCalls == 1U;
        }));
        assert(source.requestCancellation());
        assertError(waitForSingleCompletion(taskSystem), ErrorDomain::Task, kCancelledCode);
        std::lock_guard<std::mutex> lock(reader->mutex);
        assert(reader->readCalls == 0U);
    }

    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto gate = std::make_shared<ConcurrencyGate>(1U);
        const auto backpressure = std::make_shared<BackpressureController>(2U);
        CancellationSource source;
        const auto reader = std::make_shared<ReaderState>();
        reader->blockRead = true;
        reader->batches.push_back(makeValidBatch(8.0));
        const auto callbacks = std::make_shared<CallbackState>();
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(
                reader, gate, backpressure, dzc::DatasetId{404U},
                [callbacks](PointCloudSourceInfo, CancellationToken) {
                    recordOpened(callbacks);
                    return Result<void>::success();
                },
                [callbacks](PointBatch&& batch, CancellationToken) {
                    recordBatch(callbacks, std::move(batch));
                    return Result<void>::success();
                },
                source.token()));
        assert(submitted.hasValue());
        assert(waitUntil([&] {
            std::lock_guard<std::mutex> lock(reader->mutex);
            return reader->readCalls == 1U;
        }));
        assert(source.requestCancellation());
        {
            std::lock_guard<std::mutex> lock(reader->mutex);
            reader->blockRead = false;
            reader->condition.notify_all();
        }
        assertError(waitForSingleCompletion(taskSystem), ErrorDomain::Task, kCancelledCode);
        std::lock_guard<std::mutex> lock(callbacks->mutex);
        assert(callbacks->batchCalls == 0U);
    }

    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto reader = std::make_shared<ReaderState>();
        reader->batches.push_back(makeValidBatch(11.0));
        reader->batches.push_back(makeValidBatch(12.0));
        const auto callbacks = std::make_shared<CallbackState>();
        CancellationSource source;
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(
                reader, std::make_shared<ConcurrencyGate>(1U),
                std::make_shared<BackpressureController>(2U), dzc::DatasetId{405U},
                [callbacks](PointCloudSourceInfo, CancellationToken) {
                    recordOpened(callbacks);
                    return Result<void>::success();
                },
                [callbacks, &source](PointBatch&& batch, CancellationToken) {
                    recordBatch(callbacks, std::move(batch));
                    assert(source.requestCancellation());
                    return Result<void>::success();
                },
                source.token()));
        assert(submitted.hasValue());
        assertError(waitForSingleCompletion(taskSystem), ErrorDomain::Task, kCancelledCode);
        {
            std::lock_guard<std::mutex> lock(callbacks->mutex);
            assert(callbacks->batchCalls == 1U);
        }
        std::lock_guard<std::mutex> lock(reader->mutex);
        assert(reader->readCalls == 1U);
    }
}

void testFailuresAndClosedFlowControl() {
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto reader = std::make_shared<ReaderState>();
        reader->openError = makeError(ErrorDomain::DataFormat, kCorruptDataCode, "FakeReader::open");
        const auto callbacks = std::make_shared<CallbackState>();
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(reader, std::make_shared<ConcurrencyGate>(1U),
                std::make_shared<BackpressureController>(2U), dzc::DatasetId{501U},
                [callbacks](PointCloudSourceInfo, CancellationToken) {
                    recordOpened(callbacks);
                    return Result<void>::success();
                },
                [callbacks](PointBatch&& batch, CancellationToken) {
                    recordBatch(callbacks, std::move(batch));
                    return Result<void>::success();
                }));
        assert(submitted.hasValue());
        assertError(waitForSingleCompletion(taskSystem), ErrorDomain::DataFormat, kCorruptDataCode);
        std::lock_guard<std::mutex> lock(callbacks->mutex);
        assert(callbacks->openedCalls == 0U);
    }

    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto reader = std::make_shared<ReaderState>();
        reader->readError = makeError(ErrorDomain::DataFormat, kCorruptDataCode, "FakeReader::readNext");
        const auto callbacks = std::make_shared<CallbackState>();
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(reader, std::make_shared<ConcurrencyGate>(1U),
                std::make_shared<BackpressureController>(2U), dzc::DatasetId{502U},
                [callbacks](PointCloudSourceInfo, CancellationToken) {
                    recordOpened(callbacks);
                    return Result<void>::success();
                },
                [callbacks](PointBatch&& batch, CancellationToken) {
                    recordBatch(callbacks, std::move(batch));
                    return Result<void>::success();
                }));
        assert(submitted.hasValue());
        assertError(waitForSingleCompletion(taskSystem), ErrorDomain::DataFormat, kCorruptDataCode);
    }

    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto reader = std::make_shared<ReaderState>();
        reader->batches.emplace_back();
        const auto callbacks = std::make_shared<CallbackState>();
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(reader, std::make_shared<ConcurrencyGate>(1U),
                std::make_shared<BackpressureController>(2U), dzc::DatasetId{503U},
                [callbacks](PointCloudSourceInfo, CancellationToken) {
                    recordOpened(callbacks);
                    return Result<void>::success();
                },
                [callbacks](PointBatch&& batch, CancellationToken) {
                    recordBatch(callbacks, std::move(batch));
                    return Result<void>::success();
                }));
        assert(submitted.hasValue());
        assertError(waitForSingleCompletion(taskSystem), ErrorDomain::DataFormat, kCorruptDataCode);
    }

    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto reader = std::make_shared<ReaderState>();
        const auto callbackError = makeError(ErrorDomain::General, 91U, "onOpened");
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(reader, std::make_shared<ConcurrencyGate>(1U),
                std::make_shared<BackpressureController>(2U), dzc::DatasetId{504U},
                [callbackError](PointCloudSourceInfo, CancellationToken) {
                    return Result<void>::failure(callbackError);
                },
                [](PointBatch&&, CancellationToken) {
                    return Result<void>::success();
                }));
        assert(submitted.hasValue());
        assertError(waitForSingleCompletion(taskSystem), ErrorDomain::General, 91U);
        std::lock_guard<std::mutex> lock(reader->mutex);
        assert(reader->readCalls == 0U);
    }

    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto reader = std::make_shared<ReaderState>();
        reader->batches.push_back(makeValidBatch(3.0));
        const auto callbackError = makeError(ErrorDomain::General, 92U, "onBatch");
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(reader, std::make_shared<ConcurrencyGate>(1U),
                std::make_shared<BackpressureController>(2U), dzc::DatasetId{505U},
                [](PointCloudSourceInfo, CancellationToken) {
                    return Result<void>::success();
                },
                [callbackError](PointBatch&&, CancellationToken) {
                    return Result<void>::failure(callbackError);
                }));
        assert(submitted.hasValue());
        assertError(waitForSingleCompletion(taskSystem), ErrorDomain::General, 92U);
        std::lock_guard<std::mutex> lock(reader->mutex);
        assert(reader->readCalls == 1U);
    }

    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto gate = std::make_shared<ConcurrencyGate>(1U);
        gate->close();
        const auto reader = std::make_shared<ReaderState>();
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(reader, gate, std::make_shared<BackpressureController>(2U),
                dzc::DatasetId{506U},
                [](PointCloudSourceInfo, CancellationToken) { return Result<void>::success(); },
                [](PointBatch&&, CancellationToken) { return Result<void>::success(); }));
        assert(submitted.hasValue());
        assertError(waitForSingleCompletion(taskSystem), ErrorDomain::Internal, kInternalCode);
    }

    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto backpressure = std::make_shared<BackpressureController>(2U);
        backpressure->close();
        const auto reader = std::make_shared<ReaderState>();
        const auto submitted = PointCloudLoadTask::submit(
            taskSystem,
            makeRequest(reader, std::make_shared<ConcurrencyGate>(1U), backpressure,
                dzc::DatasetId{507U},
                [](PointCloudSourceInfo, CancellationToken) { return Result<void>::success(); },
                [](PointBatch&&, CancellationToken) { return Result<void>::success(); }));
        assert(submitted.hasValue());
        assertError(waitForSingleCompletion(taskSystem), ErrorDomain::Internal, kInternalCode);
        std::lock_guard<std::mutex> lock(reader->mutex);
        assert(reader->openCalls == 1U);
        assert(reader->readCalls == 0U);
    }
}

void testInvalidRequestsDoNotSubmit() {
    TaskSystem taskSystem(1U, 8U, 8U);
    const auto reader = std::make_shared<ReaderState>();
    const auto gate = std::make_shared<ConcurrencyGate>(1U);
    const auto backpressure = std::make_shared<BackpressureController>(2U);
    const auto opened = [](PointCloudSourceInfo, CancellationToken) {
        return Result<void>::success();
    };
    const auto batch = [](PointBatch&&, CancellationToken) {
        return Result<void>::success();
    };

    PointCloudLoadRequest zeroMaximum = makeRequest(reader, gate, backpressure, dzc::DatasetId{601U}, opened, batch);
    zeroMaximum.maximumPointsPerBatch = 0U;
    const auto invalidMaximum = PointCloudLoadTask::submit(taskSystem, std::move(zeroMaximum));
    assert(!invalidMaximum.hasValue());
    assert(invalidMaximum.error().domain == ErrorDomain::Configuration);
    assert(invalidMaximum.error().code == kInvalidValueCode);

    PointCloudLoadRequest emptyPath = makeRequest(reader, gate, backpressure, dzc::DatasetId{602U}, opened, batch);
    emptyPath.sourcePath.clear();
    const auto invalidPath = PointCloudLoadTask::submit(taskSystem, std::move(emptyPath));
    assert(!invalidPath.hasValue());
    assert(invalidPath.error().domain == ErrorDomain::Configuration);
    assert(invalidPath.error().code == kInvalidValueCode);

    PointCloudLoadRequest nullReader;
    nullReader.sourcePath = "controlled.fake";
    nullReader.maximumPointsPerBatch = 1U;
    nullReader.concurrencyGate = gate;
    nullReader.backpressureController = backpressure;
    nullReader.onOpened = opened;
    nullReader.onBatch = batch;
    const auto invalidReader = PointCloudLoadTask::submit(taskSystem, std::move(nullReader));
    assert(!invalidReader.hasValue());
    assert(invalidReader.error().domain == ErrorDomain::Configuration);
    assert(invalidReader.error().code == kInvalidValueCode);

    PointCloudLoadRequest nullGate = makeRequest(reader, gate, backpressure, dzc::DatasetId{603U}, opened, batch);
    nullGate.concurrencyGate.reset();
    const auto invalidGate = PointCloudLoadTask::submit(taskSystem, std::move(nullGate));
    assert(!invalidGate.hasValue());
    assert(invalidGate.error().domain == ErrorDomain::Configuration);
    assert(invalidGate.error().code == kInvalidValueCode);

    PointCloudLoadRequest nullBackpressure = makeRequest(reader, gate, backpressure, dzc::DatasetId{604U}, opened, batch);
    nullBackpressure.backpressureController.reset();
    const auto invalidBackpressure = PointCloudLoadTask::submit(taskSystem, std::move(nullBackpressure));
    assert(!invalidBackpressure.hasValue());
    assert(invalidBackpressure.error().domain == ErrorDomain::Configuration);
    assert(invalidBackpressure.error().code == kInvalidValueCode);

    PointCloudLoadRequest nullOpened = makeRequest(reader, gate, backpressure, dzc::DatasetId{605U}, {}, batch);
    const auto invalidOpened = PointCloudLoadTask::submit(taskSystem, std::move(nullOpened));
    assert(!invalidOpened.hasValue());
    assert(invalidOpened.error().domain == ErrorDomain::Configuration);
    assert(invalidOpened.error().code == kInvalidValueCode);

    PointCloudLoadRequest nullBatch = makeRequest(reader, gate, backpressure, dzc::DatasetId{606U}, opened, {});
    const auto invalidBatch = PointCloudLoadTask::submit(taskSystem, std::move(nullBatch));
    assert(!invalidBatch.hasValue());
    assert(invalidBatch.error().domain == ErrorDomain::Configuration);
    assert(invalidBatch.error().code == kInvalidValueCode);

    assert(!taskSystem.tryPopCompletion().has_value());
}

} // namespace

int main() {
    testWorkerCallbacksMetadataBatchingAndEof();
    testSharedGateLimitsOpenAndReadConcurrency();
    testBackpressurePausesBeforeNextRead();
    testCancellationStopsNewDelivery();
    testFailuresAndClosedFlowControl();
    testInvalidRequestsDoNotSubmit();
    return 0;
}
