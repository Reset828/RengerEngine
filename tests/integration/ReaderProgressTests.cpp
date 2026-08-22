#include "data/io/PointCloudLoadTask.h"

#include <dzc/Error.h>

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using dzc::DatasetId;
using dzc::DatasetLoadCancelledEvent;
using dzc::DatasetLoadedEvent;
using dzc::DatasetProgressEvent;
using dzc::EngineEvent;
using dzc::Error;
using dzc::ErrorDomain;
using dzc::ErrorEvent;
using dzc::IPointCloudReader;
using dzc::MessageEvent;
using dzc::PointAttribute;
using dzc::PointBatch;
using dzc::PointCloudLoadRequest;
using dzc::PointCloudLoadTask;
using dzc::PointCloudReadProgress;
using dzc::PointCloudSourceInfo;
using dzc::Result;
using dzc::tasks::BackpressureController;
using dzc::tasks::CancellationSource;
using dzc::tasks::CancellationToken;
using dzc::tasks::ConcurrencyGate;
using dzc::tasks::TaskCompletion;
using dzc::tasks::TaskSystem;

constexpr std::uint32_t kInternalCode = 1U;
constexpr std::uint32_t kCancelledCode = 7U;

Error makeError(ErrorDomain domain, std::uint32_t code, const char* context) {
    return Error{domain, code, "Controlled test failure.", "Controlled diagnostic.", context};
}

PointBatch makeBatch(double x) {
    PointBatch batch;
    batch.schema.mask = static_cast<std::uint32_t>(PointAttribute::Position);
    batch.positions.emplace_back(x, 0.0, 0.0);
    return batch;
}

struct ReadBlocker final {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered{false};
    bool released{false};

    void waitForRelease() {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        condition.notify_all();
        condition.wait(lock, [this] { return released; });
    }

    void release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        condition.notify_all();
    }

    bool hasEntered() {
        std::lock_guard<std::mutex> lock(mutex);
        return entered;
    }
};

bool waitUntilEntered(const std::shared_ptr<ReadBlocker>& blocker) {
    std::unique_lock<std::mutex> lock(blocker->mutex);
    return blocker->condition.wait_for(lock, std::chrono::seconds(2), [blocker] {
        return blocker->entered;
    });
}
struct ReaderScript final {
    PointCloudSourceInfo sourceInfo;
    std::vector<std::optional<PointBatch>> results;
    std::vector<PointCloudReadProgress> progressValues;
    std::optional<Error> openError;
    std::optional<Error> readError;
    std::optional<Error> progressError;
    bool throwOpen{false};
    bool throwRead{false};
    bool throwProgress{false};
    std::shared_ptr<ReadBlocker> openBlocker;
    std::shared_ptr<ReadBlocker> readBlocker;
    std::size_t nextResult{0U};
    std::size_t nextProgress{0U};
    std::size_t closeCalls{0U};
    bool isOpen{false};
};

class ScriptReader final : public IPointCloudReader {
public:
    explicit ScriptReader(std::shared_ptr<ReaderScript> script)
        : m_script(std::move(script)) {}

    Result<PointCloudSourceInfo> open(const std::string&) override {
        if (m_script->openBlocker) {
            m_script->openBlocker->waitForRelease();
        }
        if (m_script->throwOpen) {
            throw std::runtime_error("open");
        }
        if (m_script->openError.has_value()) {
            return Result<PointCloudSourceInfo>::failure(*m_script->openError);
        }
        m_script->isOpen = true;
        return Result<PointCloudSourceInfo>::success(m_script->sourceInfo);
    }

    Result<std::optional<PointBatch>> readNext(std::size_t, CancellationToken) override {
        if (m_script->readBlocker) {
            m_script->readBlocker->waitForRelease();
        }
        if (m_script->throwRead) {
            throw std::runtime_error("read");
        }
        if (m_script->readError.has_value()) {
            return Result<std::optional<PointBatch>>::failure(*m_script->readError);
        }
        if (m_script->nextResult >= m_script->results.size()) {
            return Result<std::optional<PointBatch>>::success(std::nullopt);
        }
        return Result<std::optional<PointBatch>>::success(
            std::move(m_script->results[m_script->nextResult++]));
    }

    Result<PointCloudReadProgress> readProgress() const override {
        if (m_script->throwProgress) {
            throw std::runtime_error("progress");
        }
        if (m_script->progressError.has_value()) {
            return Result<PointCloudReadProgress>::failure(*m_script->progressError);
        }
        assert(m_script->nextProgress < m_script->progressValues.size());
        return Result<PointCloudReadProgress>::success(
            m_script->progressValues[m_script->nextProgress++]);
    }

    void close() noexcept override {
        ++m_script->closeCalls;
        m_script->isOpen = false;
    }

private:
    std::shared_ptr<ReaderScript> m_script;
};

PointCloudSourceInfo sourceInfo(std::uint64_t declaredCount) {
    PointCloudSourceInfo info;
    info.schema.mask = static_cast<std::uint32_t>(PointAttribute::Position);
    info.declaredPointCount = declaredCount;
    return info;
}

PointCloudReadProgress progress(
    std::uint64_t consumed,
    std::optional<std::uint64_t> total) {
    PointCloudReadProgress value;
    value.consumedSourcePoints = consumed;
    value.totalSourcePoints = total;
    return value;
}

TaskCompletion waitForCompletion(TaskSystem& taskSystem) {
    taskSystem.waitForCompletion();
    const auto completion = taskSystem.tryPopCompletion();
    assert(completion.has_value());
    assert(!taskSystem.tryPopCompletion().has_value());
    return *completion;
}

PointCloudLoadRequest makeRequest(
    const std::shared_ptr<ReaderScript>& script,
    std::vector<EngineEvent>& events) {
    PointCloudLoadRequest request;
    request.datasetId = DatasetId{41U};
    request.sourcePath = "scripted.pointcloud";
    request.reader = std::make_unique<ScriptReader>(script);
    request.maximumPointsPerBatch = 4U;
    request.concurrencyGate = std::make_shared<ConcurrencyGate>(1U);
    request.backpressureController = std::make_shared<BackpressureController>(2U);
    request.onOpened = [](PointCloudSourceInfo, CancellationToken) {
        return Result<void>::success();
    };
    request.onBatch = [](PointBatch&&, CancellationToken) {
        return Result<void>::success();
    };
    request.onEvent = [&events](EngineEvent event, CancellationToken) {
        events.emplace_back(std::move(event));
        return Result<void>::success();
    };
    return request;
}

void assertRecoverableInternalFailure(
    const TaskCompletion& completion,
    const std::vector<EngineEvent>& events) {
    assert(!completion.result.hasValue());
    assert(completion.result.error().domain == ErrorDomain::Internal);
    assert(completion.result.error().code == kInternalCode);
    assert(!events.empty());
    const auto* error = std::get_if<ErrorEvent>(&events.back());
    assert(error != nullptr);
    assert(error->severity == dzc::EventSeverity::RecoverableError);
    assert(error->error.domain == ErrorDomain::Internal);
    assert(error->error.code == kInternalCode);
    assert(error->context.datasetId == DatasetId{41U});
}

void testKnownTotalEventsAreOrderedAndMonotonic() {
    TaskSystem taskSystem(1U, 8U, 8U);
    const auto script = std::make_shared<ReaderScript>();
    script->sourceInfo = sourceInfo(5U);
    script->results = {makeBatch(1.0), std::nullopt};
    script->progressValues = {progress(0U, 5U), progress(5U, 5U), progress(5U, 5U)};
    std::vector<EngineEvent> events;

    const auto submitted = PointCloudLoadTask::submit(taskSystem, makeRequest(script, events));
    assert(submitted.hasValue());
    const TaskCompletion completion = waitForCompletion(taskSystem);
    assert(completion.result.hasValue());
    assert(script->closeCalls == 1U);
    assert(events.size() == 5U);
    assert(std::get_if<MessageEvent>(&events[0]) != nullptr);
    assert(std::get_if<MessageEvent>(&events[1]) != nullptr);
    const auto* initial = std::get_if<DatasetProgressEvent>(&events[2]);
    const auto* advanced = std::get_if<DatasetProgressEvent>(&events[3]);
    assert(initial != nullptr && initial->completedUnits == 0U && initial->totalUnits == 5U);
    assert(advanced != nullptr && advanced->completedUnits == 5U && advanced->totalUnits == 5U);
    assert(std::get_if<DatasetLoadedEvent>(&events[4]) != nullptr);
}

void testUnknownTotalUsesStagesWithoutNumericProgress() {
    TaskSystem taskSystem(1U, 8U, 8U);
    const auto script = std::make_shared<ReaderScript>();
    script->sourceInfo = sourceInfo(0U);
    script->results = {std::nullopt};
    script->progressValues = {progress(0U, std::nullopt), progress(2U, std::nullopt)};
    std::vector<EngineEvent> events;

    assert(PointCloudLoadTask::submit(taskSystem, makeRequest(script, events)).hasValue());
    assert(waitForCompletion(taskSystem).result.hasValue());
    assert(events.size() == 3U);
    assert(std::get_if<MessageEvent>(&events[0]) != nullptr);
    assert(std::get_if<MessageEvent>(&events[1]) != nullptr);
    assert(std::get_if<DatasetLoadedEvent>(&events[2]) != nullptr);
}

void testZeroKnownTotalReportsZeroProgress() {
    TaskSystem taskSystem(1U, 8U, 8U);
    const auto script = std::make_shared<ReaderScript>();
    script->sourceInfo = sourceInfo(0U);
    script->results = {std::nullopt};
    script->progressValues = {progress(0U, 0U), progress(0U, 0U)};
    std::vector<EngineEvent> events;

    assert(PointCloudLoadTask::submit(taskSystem, makeRequest(script, events)).hasValue());
    assert(waitForCompletion(taskSystem).result.hasValue());
    assert(events.size() == 4U);
    const auto* initial = std::get_if<DatasetProgressEvent>(&events[2]);
    assert(initial != nullptr && initial->completedUnits == 0U && initial->totalUnits == 0U);
    assert(std::get_if<DatasetLoadedEvent>(&events[3]) != nullptr);
}

void runInvalidProgressCase(
    std::vector<std::optional<PointBatch>> results,
    std::vector<PointCloudReadProgress> progressValues) {
    TaskSystem taskSystem(1U, 8U, 8U);
    const auto script = std::make_shared<ReaderScript>();
    script->sourceInfo = sourceInfo(4U);
    script->results = std::move(results);
    script->progressValues = std::move(progressValues);
    std::vector<EngineEvent> events;

    assert(PointCloudLoadTask::submit(taskSystem, makeRequest(script, events)).hasValue());
    assertRecoverableInternalFailure(waitForCompletion(taskSystem), events);
    assert(script->closeCalls == 1U);
}

void testInvalidProgressFailsWithoutMisleadingCompletion() {
    runInvalidProgressCase({std::nullopt}, {progress(1U, 4U)});
    runInvalidProgressCase({std::nullopt}, {progress(0U, 4U), progress(5U, 4U)});
    runInvalidProgressCase({makeBatch(1.0), std::nullopt},
                           {progress(0U, 4U), progress(2U, 4U), progress(1U, 4U)});
    runInvalidProgressCase({makeBatch(1.0), std::nullopt},
                           {progress(0U, 4U), progress(2U, 4U), progress(2U, 5U)});
    runInvalidProgressCase({std::nullopt}, {progress(0U, 4U), progress(3U, 4U)});
}

void testFailuresExceptionsAndCancellationProduceRecoverableEvents() {
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto script = std::make_shared<ReaderScript>();
        script->sourceInfo = sourceInfo(1U);
        script->throwRead = true;
        script->progressValues = {progress(0U, 1U)};
        std::vector<EngineEvent> events;
        assert(PointCloudLoadTask::submit(taskSystem, makeRequest(script, events)).hasValue());
        assertRecoverableInternalFailure(waitForCompletion(taskSystem), events);
    }
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto script = std::make_shared<ReaderScript>();
        script->sourceInfo = sourceInfo(1U);
        script->results = {std::nullopt};
        script->progressValues = {progress(0U, 1U), progress(1U, 1U)};
        std::vector<EngineEvent> events;
        PointCloudLoadRequest request = makeRequest(script, events);
        request.onEvent = [&events](EngineEvent event, CancellationToken) {
            events.emplace_back(std::move(event));
            if (std::holds_alternative<MessageEvent>(events.back())) {
                return Result<void>::failure(makeError(ErrorDomain::Configuration, 9U, "event"));
            }
            return Result<void>::success();
        };
        assert(PointCloudLoadTask::submit(taskSystem, std::move(request)).hasValue());
        const TaskCompletion completion = waitForCompletion(taskSystem);
        assert(!completion.result.hasValue());
        assert(completion.result.error().domain == ErrorDomain::Configuration);
        assert(std::get_if<ErrorEvent>(&events.back()) != nullptr);
    }
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto script = std::make_shared<ReaderScript>();
        script->sourceInfo = sourceInfo(0U);
        script->progressValues = {progress(0U, 0U)};
        std::vector<EngineEvent> events;
        CancellationSource cancellation;
        assert(cancellation.requestCancellation());
        PointCloudLoadRequest request = makeRequest(script, events);
        request.cancellationToken = cancellation.token();
        assert(PointCloudLoadTask::submit(taskSystem, std::move(request)).hasValue());
        const TaskCompletion completion = waitForCompletion(taskSystem);
        assert(!completion.result.hasValue());
        assert(completion.result.error().domain == ErrorDomain::Task);
        assert(completion.result.error().code == kCancelledCode);
        assert(events.size() == 1U);
        assert(std::get_if<DatasetLoadCancelledEvent>(&events.front()) != nullptr);
    }
}

PointBatch makeInvalidBatch() {
    PointBatch batch;
    batch.schema.mask = static_cast<std::uint32_t>(PointAttribute::Color);
    return batch;
}

void assertFailureWithErrorEvent(
    const TaskCompletion& completion,
    const std::vector<EngineEvent>& events,
    ErrorDomain domain,
    std::uint32_t code) {
    assert(!completion.result.hasValue());
    assert(completion.result.error().domain == domain);
    assert(completion.result.error().code == code);
    std::size_t errorCount = 0U;
    for (const EngineEvent& event : events) {
        if (const auto* error = std::get_if<ErrorEvent>(&event)) {
            ++errorCount;
            assert(error->severity == dzc::EventSeverity::RecoverableError);
            assert(error->error.domain == domain);
            assert(error->error.code == code);
            assert(error->context.datasetId == DatasetId{41U});
        }
    }
    assert(errorCount == 1U);
}

void testReaderAndCallbackFailuresPreserveCauseOrNormalizeExceptions() {
    const Error controlled = makeError(ErrorDomain::DataFormat, 22U, "controlled");

    auto runReaderFailure = [&controlled](const std::function<void(ReaderScript&)>& configure,
                                           ErrorDomain expectedDomain,
                                           std::uint32_t expectedCode) {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto script = std::make_shared<ReaderScript>();
        script->sourceInfo = sourceInfo(1U);
        script->progressValues = {progress(0U, 1U)};
        configure(*script);
        std::vector<EngineEvent> events;
        assert(PointCloudLoadTask::submit(taskSystem, makeRequest(script, events)).hasValue());
        assertFailureWithErrorEvent(waitForCompletion(taskSystem), events, expectedDomain, expectedCode);
        assert(script->closeCalls == 1U);
    };

    runReaderFailure([&controlled](ReaderScript& script) { script.openError = controlled; },
                     ErrorDomain::DataFormat, 22U);
    runReaderFailure([](ReaderScript& script) { script.throwOpen = true; },
                     ErrorDomain::Internal, kInternalCode);
    runReaderFailure([&controlled](ReaderScript& script) {
        script.results = {std::nullopt};
        script.readError = controlled;
    }, ErrorDomain::DataFormat, 22U);
    runReaderFailure([](ReaderScript& script) {
        script.results = {std::nullopt};
        script.throwRead = true;
    }, ErrorDomain::Internal, kInternalCode);
    runReaderFailure([&controlled](ReaderScript& script) { script.progressError = controlled; },
                     ErrorDomain::DataFormat, 22U);
    runReaderFailure([](ReaderScript& script) { script.throwProgress = true; },
                     ErrorDomain::Internal, kInternalCode);

    auto runCallbackFailure = [&controlled](
                                  const std::function<void(PointCloudLoadRequest&)>& configure,
                                  ErrorDomain expectedDomain,
                                  std::uint32_t expectedCode) {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto script = std::make_shared<ReaderScript>();
        script->sourceInfo = sourceInfo(1U);
        script->results = {makeBatch(1.0), std::nullopt};
        script->progressValues = {progress(0U, 1U), progress(1U, 1U), progress(1U, 1U)};
        std::vector<EngineEvent> events;
        PointCloudLoadRequest request = makeRequest(script, events);
        configure(request);
        assert(PointCloudLoadTask::submit(taskSystem, std::move(request)).hasValue());
        assertFailureWithErrorEvent(waitForCompletion(taskSystem), events, expectedDomain, expectedCode);
        assert(script->closeCalls == 1U);
    };

    runCallbackFailure([&controlled](PointCloudLoadRequest& request) {
        request.onOpened = [&controlled](PointCloudSourceInfo, CancellationToken) {
            return Result<void>::failure(controlled);
        };
    }, ErrorDomain::DataFormat, 22U);
    runCallbackFailure([](PointCloudLoadRequest& request) {
        request.onOpened = [](PointCloudSourceInfo, CancellationToken) -> Result<void> {
            throw std::runtime_error("opened");
        };
    }, ErrorDomain::Internal, kInternalCode);
    runCallbackFailure([&controlled](PointCloudLoadRequest& request) {
        request.onBatch = [&controlled](PointBatch&&, CancellationToken) {
            return Result<void>::failure(controlled);
        };
    }, ErrorDomain::DataFormat, 22U);
    runCallbackFailure([](PointCloudLoadRequest& request) {
        request.onBatch = [](PointBatch&&, CancellationToken) -> Result<void> {
            throw std::runtime_error("batch");
        };
    }, ErrorDomain::Internal, kInternalCode);
}

void testInvalidBatchAndEventFailureRules() {
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto script = std::make_shared<ReaderScript>();
        script->sourceInfo = sourceInfo(1U);
        script->results = {makeInvalidBatch()};
        script->progressValues = {progress(0U, 1U), progress(1U, 1U)};
        std::vector<EngineEvent> events;
        assert(PointCloudLoadTask::submit(taskSystem, makeRequest(script, events)).hasValue());
        assertFailureWithErrorEvent(waitForCompletion(taskSystem), events, ErrorDomain::DataFormat, 2U);
    }
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto script = std::make_shared<ReaderScript>();
        script->sourceInfo = sourceInfo(1U);
        script->progressValues = {progress(0U, 1U)};
        std::vector<EngineEvent> events;
        PointCloudLoadRequest request = makeRequest(script, events);
        request.onEvent = [](EngineEvent, CancellationToken) -> Result<void> {
            throw std::runtime_error("event");
        };
        assert(PointCloudLoadTask::submit(taskSystem, std::move(request)).hasValue());
        const TaskCompletion completion = waitForCompletion(taskSystem);
        assert(!completion.result.hasValue());
        assert(completion.result.error().domain == ErrorDomain::Internal);
        assert(completion.result.error().code == kInternalCode);
        assert(script->closeCalls == 1U);
        assert(events.empty());
    }
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto script = std::make_shared<ReaderScript>();
        script->sourceInfo = sourceInfo(1U);
        script->readError = makeError(ErrorDomain::DataFormat, 23U, "read");
        script->progressValues = {progress(0U, 1U)};
        std::vector<EngineEvent> events;
        PointCloudLoadRequest request = makeRequest(script, events);
        request.onEvent = [&events](EngineEvent event, CancellationToken) {
            const bool isFinalError = std::holds_alternative<ErrorEvent>(event);
            events.emplace_back(std::move(event));
            if (isFinalError) {
                return Result<void>::failure(makeError(ErrorDomain::Configuration, 99U, "final event"));
            }
            return Result<void>::success();
        };
        assert(PointCloudLoadTask::submit(taskSystem, std::move(request)).hasValue());
        assertFailureWithErrorEvent(waitForCompletion(taskSystem), events, ErrorDomain::DataFormat, 23U);
    }
}

void testCancellationProducesOnlyCancellationTerminalEvent() {
    TaskSystem taskSystem(1U, 8U, 8U);
    const auto script = std::make_shared<ReaderScript>();
    script->sourceInfo = sourceInfo(0U);
    std::vector<EngineEvent> events;
    CancellationSource cancellation;
    assert(cancellation.requestCancellation());
    PointCloudLoadRequest request = makeRequest(script, events);
    request.cancellationToken = cancellation.token();
    assert(PointCloudLoadTask::submit(taskSystem, std::move(request)).hasValue());
    const TaskCompletion completion = waitForCompletion(taskSystem);
    assert(!completion.result.hasValue());
    assert(completion.result.error().domain == ErrorDomain::Task);
    assert(completion.result.error().code == kCancelledCode);
    assert(script->closeCalls == 1U);
    assert(events.size() == 1U);
    assert(std::get_if<DatasetLoadCancelledEvent>(&events.front()) != nullptr);
}

void testFailureDoesNotPreventIndependentRetry() {
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto failedScript = std::make_shared<ReaderScript>();
        failedScript->sourceInfo = sourceInfo(1U);
        failedScript->readError = makeError(ErrorDomain::DataFormat, 24U, "first");
        failedScript->progressValues = {progress(0U, 1U)};
        std::vector<EngineEvent> events;
        assert(PointCloudLoadTask::submit(taskSystem, makeRequest(failedScript, events)).hasValue());
        assertFailureWithErrorEvent(waitForCompletion(taskSystem), events, ErrorDomain::DataFormat, 24U);
    }
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto successfulScript = std::make_shared<ReaderScript>();
        successfulScript->sourceInfo = sourceInfo(1U);
        successfulScript->results = {std::nullopt};
        successfulScript->progressValues = {progress(0U, 1U), progress(1U, 1U)};
        std::vector<EngineEvent> events;
        assert(PointCloudLoadTask::submit(taskSystem, makeRequest(successfulScript, events)).hasValue());
        assert(waitForCompletion(taskSystem).result.hasValue());
        assert(std::get_if<DatasetLoadedEvent>(&events.back()) != nullptr);
    }
}
void assertCancelledOnly(
    const TaskCompletion& completion,
    const std::vector<EngineEvent>& events) {
    assert(!completion.result.hasValue());
    assert(completion.result.error().domain == ErrorDomain::Task);
    assert(completion.result.error().code == kCancelledCode);
    assert(!events.empty());
    assert(std::get_if<DatasetLoadCancelledEvent>(&events.back()) != nullptr);
    for (const EngineEvent& event : events) {
        assert(std::get_if<DatasetLoadedEvent>(&event) == nullptr);
        assert(std::get_if<ErrorEvent>(&event) == nullptr);
    }
}

void testCancellationWhileWaitingAndDuringReadSuppressesDelivery() {
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto script = std::make_shared<ReaderScript>();
        script->sourceInfo = sourceInfo(1U);
        std::vector<EngineEvent> events;
        CancellationSource cancellation;
        const auto gate = std::make_shared<ConcurrencyGate>(1U);
        const auto heldLease = gate->acquire();
        assert(heldLease.has_value());
        PointCloudLoadRequest request = makeRequest(script, events);
        request.concurrencyGate = gate;
        request.cancellationToken = cancellation.token();
        assert(PointCloudLoadTask::submit(taskSystem, std::move(request)).hasValue());
        assert(cancellation.requestCancellation());
        assertCancelledOnly(waitForCompletion(taskSystem), events);
        assert(script->closeCalls == 1U);
        assert(!script->isOpen);
    }
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto script = std::make_shared<ReaderScript>();
        script->sourceInfo = sourceInfo(1U);
        script->progressValues = {progress(0U, 1U)};
        std::vector<EngineEvent> events;
        CancellationSource cancellation;
        const auto opened = std::make_shared<ReadBlocker>();
        PointCloudLoadRequest request = makeRequest(script, events);
        request.backpressureController = std::make_shared<BackpressureController>(2U, 50U, 0U);
        request.backpressureController->updateUsage(1U);
        request.cancellationToken = cancellation.token();
        request.onOpened = [opened](PointCloudSourceInfo, CancellationToken) {
            opened->waitForRelease();
            return Result<void>::success();
        };
        assert(PointCloudLoadTask::submit(taskSystem, std::move(request)).hasValue());
        assert(waitUntilEntered(opened));
        opened->release();
        assert(cancellation.requestCancellation());
        assertCancelledOnly(waitForCompletion(taskSystem), events);
        assert(script->closeCalls == 1U);
        assert(script->nextResult == 0U);
    }
    {
        TaskSystem taskSystem(1U, 8U, 8U);
        const auto script = std::make_shared<ReaderScript>();
        script->sourceInfo = sourceInfo(1U);
        script->results = {makeBatch(1.0)};
        script->progressValues = {progress(0U, 1U)};
        script->readBlocker = std::make_shared<ReadBlocker>();
        std::size_t batchCalls = 0U;
        std::vector<EngineEvent> events;
        CancellationSource cancellation;
        PointCloudLoadRequest request = makeRequest(script, events);
        request.cancellationToken = cancellation.token();
        request.onBatch = [&batchCalls](PointBatch&&, CancellationToken) {
            ++batchCalls;
            return Result<void>::success();
        };
        assert(PointCloudLoadTask::submit(taskSystem, std::move(request)).hasValue());
        assert(waitUntilEntered(script->readBlocker));
        assert(cancellation.requestCancellation());
        script->readBlocker->release();
        assertCancelledOnly(waitForCompletion(taskSystem), events);
        assert(script->closeCalls == 1U);
        assert(batchCalls == 0U);
    }
}
} // namespace

int main() {
    testKnownTotalEventsAreOrderedAndMonotonic();
    testUnknownTotalUsesStagesWithoutNumericProgress();
    testZeroKnownTotalReportsZeroProgress();
    testInvalidProgressFailsWithoutMisleadingCompletion();
    testFailuresExceptionsAndCancellationProduceRecoverableEvents();
    testReaderAndCallbackFailuresPreserveCauseOrNormalizeExceptions();
    testInvalidBatchAndEventFailureRules();
    testCancellationProducesOnlyCancellationTerminalEvent();
    testFailureDoesNotPreventIndependentRetry();
    testCancellationWhileWaitingAndDuringReadSuppressesDelivery();
    return 0;
}
