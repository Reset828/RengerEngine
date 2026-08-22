#include "data/io/PointCloudLoadTask.h"

#include <dzc/Error.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kInvalidValueCode = 1U;
constexpr std::uint32_t kCancelledCode = 7U;
constexpr std::uint32_t kInternalErrorCode = 1U;

Error makeError(
    ErrorDomain domain,
    std::uint32_t code,
    std::string userMessage,
    std::string diagnosticMessage,
    std::string context) {
    return Error{
        domain,
        code,
        std::move(userMessage),
        std::move(diagnosticMessage),
        std::move(context)};
}

Error invalidRequestError(std::string diagnosticMessage) {
    return makeError(
        ErrorDomain::Configuration,
        kInvalidValueCode,
        "Point cloud load request is invalid.",
        std::move(diagnosticMessage),
        "PointCloudLoadTask::submit");
}

Error cancelledError() {
    return makeError(
        ErrorDomain::Task,
        kCancelledCode,
        "Point cloud load cancelled.",
        "PointCloudLoadTask observed a requested cancellation.",
        "PointCloudLoadTask");
}

Error internalError(std::string diagnosticMessage) {
    return makeError(
        ErrorDomain::Internal,
        kInternalErrorCode,
        "Point cloud load failed unexpectedly.",
        std::move(diagnosticMessage),
        "PointCloudLoadTask");
}

Error flowControlClosedError(const char* controllerName) {
    return makeError(
        ErrorDomain::Internal,
        kInternalErrorCode,
        "Point cloud load flow control is unavailable.",
        std::string(controllerName) + " was closed while the load task was waiting.",
        "PointCloudLoadTask");
}

bool isCancelledError(const Error& error) noexcept {
    return error.domain == ErrorDomain::Task && error.code == kCancelledCode;
}

class ReaderCloseGuard final {
public:
    explicit ReaderCloseGuard(IPointCloudReader& reader) noexcept
        : m_reader(reader) {}

    ~ReaderCloseGuard() {
        close();
    }

    void close() noexcept {
        if (!m_closed) {
            m_reader.close();
            m_closed = true;
        }
    }

    ReaderCloseGuard(const ReaderCloseGuard&) = delete;
    ReaderCloseGuard& operator=(const ReaderCloseGuard&) = delete;

private:
    IPointCloudReader& m_reader;
    bool m_closed{false};
};

struct LoadState final {
    explicit LoadState(PointCloudLoadRequest&& value)
        : request(std::move(value)) {}

    PointCloudLoadRequest request;
};

struct ProgressState final {
    std::uint64_t consumedSourcePoints{0U};
    std::optional<std::uint64_t> totalSourcePoints;
    bool initialized{false};
};

Result<void> cancellationOrFlowControlError(
    tasks::CancellationToken token,
    const char* controllerName) {
    if (token.isCancellationRequested()) {
        return Result<void>::failure(cancelledError());
    }
    return Result<void>::failure(flowControlClosedError(controllerName));
}

Result<void> invokeEvent(
    PointCloudLoadRequest& request,
    EngineEvent event,
    tasks::CancellationToken token) {
    try {
        return request.onEvent(std::move(event), token);
    } catch (const std::exception& exception) {
        return Result<void>::failure(internalError(
            std::string("PointCloudLoadTask event callback threw: ") + exception.what()));
    } catch (...) {
        return Result<void>::failure(internalError(
            "PointCloudLoadTask event callback threw an unknown exception."));
    }
}

Result<void> invokeOpened(
    PointCloudLoadRequest& request,
    PointCloudSourceInfo sourceInfo,
    tasks::CancellationToken token) {
    try {
        return request.onOpened(std::move(sourceInfo), token);
    } catch (const std::exception& exception) {
        return Result<void>::failure(internalError(
            std::string("PointCloudLoadTask opened callback threw: ") + exception.what()));
    } catch (...) {
        return Result<void>::failure(internalError(
            "PointCloudLoadTask opened callback threw an unknown exception."));
    }
}

Result<void> invokeBatch(
    PointCloudLoadRequest& request,
    PointBatch batch,
    tasks::CancellationToken token) {
    try {
        return request.onBatch(std::move(batch), token);
    } catch (const std::exception& exception) {
        return Result<void>::failure(internalError(
            std::string("PointCloudLoadTask batch callback threw: ") + exception.what()));
    } catch (...) {
        return Result<void>::failure(internalError(
            "PointCloudLoadTask batch callback threw an unknown exception."));
    }
}

EngineEvent makeStageEvent(DatasetId datasetId, const char* message) {
    return MessageEvent{
        EventSeverity::Info,
        message,
        EventContext{datasetId, {}, {}, {}}};
}

EngineEvent makeProgressEvent(
    DatasetId datasetId,
    std::uint64_t completedUnits,
    std::uint64_t totalUnits) {
    return DatasetProgressEvent{datasetId, completedUnits, totalUnits};
}

EngineEvent makeErrorEvent(DatasetId datasetId, Error error) {
    return ErrorEvent{
        EventSeverity::RecoverableError,
        std::move(error),
        EventContext{datasetId, {}, {}, {}}};
}

Result<void> validateProgress(
    const PointCloudReadProgress& progress,
    ProgressState& state,
    bool isInitial,
    bool isEndOfFile) {
    if (isInitial && progress.consumedSourcePoints != 0U) {
        return Result<void>::failure(internalError(
            "Reader progress must begin with zero consumed source points."));
    }
    if (!state.initialized) {
        state.consumedSourcePoints = progress.consumedSourcePoints;
        state.totalSourcePoints = progress.totalSourcePoints;
        state.initialized = true;
    } else {
        if (state.totalSourcePoints != progress.totalSourcePoints) {
            return Result<void>::failure(internalError(
                "Reader progress changed total source point availability or value while open."));
        }
        if (progress.consumedSourcePoints < state.consumedSourcePoints) {
            return Result<void>::failure(internalError(
                "Reader progress regressed consumed source points while open."));
        }
        state.consumedSourcePoints = progress.consumedSourcePoints;
    }

    if (state.totalSourcePoints.has_value() &&
        state.consumedSourcePoints > *state.totalSourcePoints) {
        return Result<void>::failure(internalError(
            "Reader progress consumed source points beyond its declared total."));
    }
    if (isEndOfFile && state.totalSourcePoints.has_value() &&
        state.consumedSourcePoints != *state.totalSourcePoints) {
        return Result<void>::failure(internalError(
            "Reader reached EOF before consuming its declared total source points."));
    }
    return Result<void>::success();
}

Result<PointCloudReadProgress> readProgress(IPointCloudReader& reader) {
    try {
        return reader.readProgress();
    } catch (const std::exception& exception) {
        return Result<PointCloudReadProgress>::failure(internalError(
            std::string("PointCloudLoadTask reader progress query threw: ") + exception.what()));
    } catch (...) {
        return Result<PointCloudReadProgress>::failure(internalError(
            "PointCloudLoadTask reader progress query threw an unknown exception."));
    }
}

Result<PointCloudSourceInfo> openReader(
    PointCloudLoadRequest& request,
    IPointCloudReader& reader,
    tasks::CancellationToken token) {
    const auto lease = request.concurrencyGate->acquire(token);
    if (!lease.has_value()) {
        return Result<PointCloudSourceInfo>::failure(
            cancellationOrFlowControlError(token, "ConcurrencyGate").error());
    }
    if (token.isCancellationRequested()) {
        return Result<PointCloudSourceInfo>::failure(cancelledError());
    }
    try {
        return reader.open(request.sourcePath);
    } catch (const std::exception& exception) {
        return Result<PointCloudSourceInfo>::failure(internalError(
            std::string("PointCloudLoadTask reader open threw: ") + exception.what()));
    } catch (...) {
        return Result<PointCloudSourceInfo>::failure(internalError(
            "PointCloudLoadTask reader open threw an unknown exception."));
    }
}

Result<std::optional<PointBatch>> readNext(
    PointCloudLoadRequest& request,
    IPointCloudReader& reader,
    tasks::CancellationToken token) {
    const auto lease = request.concurrencyGate->acquire(token);
    if (!lease.has_value()) {
        return Result<std::optional<PointBatch>>::failure(
            cancellationOrFlowControlError(token, "ConcurrencyGate").error());
    }
    if (token.isCancellationRequested()) {
        return Result<std::optional<PointBatch>>::failure(cancelledError());
    }
    try {
        return reader.readNext(request.maximumPointsPerBatch, token);
    } catch (const std::exception& exception) {
        return Result<std::optional<PointBatch>>::failure(internalError(
            std::string("PointCloudLoadTask reader readNext threw: ") + exception.what()));
    } catch (...) {
        return Result<std::optional<PointBatch>>::failure(internalError(
            "PointCloudLoadTask reader readNext threw an unknown exception."));
    }
}

Result<void> executeLoad(LoadState& state, tasks::CancellationToken token) {
    PointCloudLoadRequest& request = state.request;
    IPointCloudReader& reader = *request.reader;
    ProgressState progressState;

    if (token.isCancellationRequested()) {
        return Result<void>::failure(cancelledError());
    }
    Result<void> eventResult = invokeEvent(
        request,
        makeStageEvent(request.datasetId, "Opening point cloud source."),
        token);
    if (token.isCancellationRequested()) {
        return Result<void>::failure(cancelledError());
    }
    if (!eventResult.hasValue()) {
        return eventResult;
    }

    Result<PointCloudSourceInfo> opened = openReader(request, reader, token);
    if (token.isCancellationRequested()) {
        return Result<void>::failure(cancelledError());
    }
    if (!opened.hasValue()) {
        return Result<void>::failure(opened.error());
    }

    Result<PointCloudReadProgress> initialProgress = readProgress(reader);
    if (token.isCancellationRequested()) {
        return Result<void>::failure(cancelledError());
    }
    if (!initialProgress.hasValue()) {
        return Result<void>::failure(initialProgress.error());
    }
    Result<void> progressValidation = validateProgress(initialProgress.value(), progressState, true, false);
    if (!progressValidation.hasValue()) {
        return progressValidation;
    }

    Result<void> openedResult = invokeOpened(request, std::move(opened.value()), token);
    if (token.isCancellationRequested()) {
        return Result<void>::failure(cancelledError());
    }
    if (!openedResult.hasValue()) {
        return openedResult;
    }

    eventResult = invokeEvent(
        request,
        makeStageEvent(request.datasetId, "Reading point cloud source."),
        token);
    if (token.isCancellationRequested()) {
        return Result<void>::failure(cancelledError());
    }
    if (!eventResult.hasValue()) {
        return eventResult;
    }
    if (progressState.totalSourcePoints.has_value()) {
        eventResult = invokeEvent(
            request,
            makeProgressEvent(
                request.datasetId,
                progressState.consumedSourcePoints,
                *progressState.totalSourcePoints),
            token);
        if (token.isCancellationRequested()) {
            return Result<void>::failure(cancelledError());
        }
        if (!eventResult.hasValue()) {
            return eventResult;
        }
    }

    for (;;) {
        if (token.isCancellationRequested()) {
            return Result<void>::failure(cancelledError());
        }
        if (!request.backpressureController->waitUntilResumed(token)) {
            return cancellationOrFlowControlError(token, "BackpressureController");
        }
        if (token.isCancellationRequested()) {
            return Result<void>::failure(cancelledError());
        }

        Result<std::optional<PointBatch>> next = readNext(request, reader, token);
        if (token.isCancellationRequested()) {
            return Result<void>::failure(cancelledError());
        }
        if (!next.hasValue()) {
            return Result<void>::failure(next.error());
        }

        const bool endOfFile = !next.value().has_value();
        const std::uint64_t previousConsumed = progressState.consumedSourcePoints;
        Result<PointCloudReadProgress> currentProgress = readProgress(reader);
        if (token.isCancellationRequested()) {
            return Result<void>::failure(cancelledError());
        }
        if (!currentProgress.hasValue()) {
            return Result<void>::failure(currentProgress.error());
        }
        progressValidation = validateProgress(currentProgress.value(), progressState, false, endOfFile);
        if (!progressValidation.hasValue()) {
            return progressValidation;
        }
        if (progressState.totalSourcePoints.has_value() &&
            progressState.consumedSourcePoints > previousConsumed) {
            eventResult = invokeEvent(
                request,
                makeProgressEvent(
                    request.datasetId,
                    progressState.consumedSourcePoints,
                    *progressState.totalSourcePoints),
                token);
            if (token.isCancellationRequested()) {
                return Result<void>::failure(cancelledError());
            }
            if (!eventResult.hasValue()) {
                return eventResult;
            }
        }

        if (endOfFile) {
            eventResult = invokeEvent(
                request,
                EngineEvent{DatasetLoadedEvent{request.datasetId}},
                token);
            if (token.isCancellationRequested()) {
                return Result<void>::failure(cancelledError());
            }
            return eventResult;
        }

        PointBatch batch = std::move(*next.value());
        const Result<void> batchValidation = batch.validate();
        if (!batchValidation.hasValue()) {
            return batchValidation;
        }
        if (token.isCancellationRequested()) {
            return Result<void>::failure(cancelledError());
        }
        Result<void> batchResult = invokeBatch(request, std::move(batch), token);
        if (token.isCancellationRequested()) {
            return Result<void>::failure(cancelledError());
        }
        if (!batchResult.hasValue()) {
            return batchResult;
        }
    }
}

Result<void> runLoad(LoadState& state, tasks::CancellationToken token) {
    IPointCloudReader& reader = *state.request.reader;
    ReaderCloseGuard closeGuard(reader);
    Result<void> result = Result<void>::failure(internalError("PointCloudLoadTask execution was not started."));
    try {
        result = executeLoad(state, token);
    } catch (const std::exception& exception) {
        result = Result<void>::failure(internalError(
            std::string("PointCloudLoadTask execution threw: ") + exception.what()));
    } catch (...) {
        result = Result<void>::failure(internalError(
            "PointCloudLoadTask execution threw an unknown exception."));
    }

    const bool cancelled = token.isCancellationRequested() ||
        (!result.hasValue() && isCancelledError(result.error()));
    closeGuard.close();

    if (cancelled) {
        static_cast<void>(invokeEvent(
            state.request,
            EngineEvent{DatasetLoadCancelledEvent{state.request.datasetId}},
            token));
        return Result<void>::failure(cancelledError());
    }
    if (result.hasValue()) {
        return result;
    }

    const Error cause = result.error();
    static_cast<void>(invokeEvent(
        state.request,
        makeErrorEvent(state.request.datasetId, cause),
        token));
    return Result<void>::failure(cause);
}

} // namespace

Result<TaskId> PointCloudLoadTask::submit(
    tasks::TaskSystem& taskSystem,
    PointCloudLoadRequest request) {
    if (request.sourcePath.empty()) {
        return Result<TaskId>::failure(invalidRequestError("sourcePath must not be empty."));
    }
    if (request.maximumPointsPerBatch == 0U) {
        return Result<TaskId>::failure(invalidRequestError(
            "maximumPointsPerBatch must be greater than zero."));
    }
    if (!request.reader) {
        return Result<TaskId>::failure(invalidRequestError("reader must not be null."));
    }
    if (!request.concurrencyGate) {
        return Result<TaskId>::failure(invalidRequestError("concurrencyGate must not be null."));
    }
    if (!request.backpressureController) {
        return Result<TaskId>::failure(invalidRequestError("backpressureController must not be null."));
    }
    if (!request.onOpened) {
        return Result<TaskId>::failure(invalidRequestError("onOpened must not be empty."));
    }
    if (!request.onBatch) {
        return Result<TaskId>::failure(invalidRequestError("onBatch must not be empty."));
    }
    if (!request.onEvent) {
        return Result<TaskId>::failure(invalidRequestError("onEvent must not be empty."));
    }

    const auto state = std::make_shared<LoadState>(std::move(request));
    return taskSystem.submitForDataset(
        state->request.datasetId,
        state->request.priority,
        state->request.cancellationToken,
        [state](tasks::CancellationToken token) {
            return runLoad(*state, token);
        });
}

} // namespace dzc
