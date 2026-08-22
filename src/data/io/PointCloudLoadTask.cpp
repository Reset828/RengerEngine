#include "data/io/PointCloudLoadTask.h"

#include <dzc/Error.h>

#include <cstdint>
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

Error flowControlClosedError(const char* controllerName) {
    return makeError(
        ErrorDomain::Internal,
        kInternalErrorCode,
        "Point cloud load flow control is unavailable.",
        std::string(controllerName) + " was closed while the load task was waiting.",
        "PointCloudLoadTask");
}

class ReaderCloseGuard final {
public:
    explicit ReaderCloseGuard(IPointCloudReader& reader) noexcept
        : m_reader(reader) {}

    ~ReaderCloseGuard() {
        m_reader.close();
    }

    ReaderCloseGuard(const ReaderCloseGuard&) = delete;
    ReaderCloseGuard& operator=(const ReaderCloseGuard&) = delete;

private:
    IPointCloudReader& m_reader;
};

struct LoadState final {
    explicit LoadState(PointCloudLoadRequest&& value)
        : request(std::move(value)) {}

    PointCloudLoadRequest request;
};

Result<void> cancellationOrFlowControlError(
    tasks::CancellationToken token,
    const char* controllerName) {
    if (token.isCancellationRequested()) {
        return Result<void>::failure(cancelledError());
    }
    return Result<void>::failure(flowControlClosedError(controllerName));
}

Result<void> runLoad(LoadState& state, tasks::CancellationToken token) {
    PointCloudLoadRequest& request = state.request;
    IPointCloudReader& reader = *request.reader;
    ReaderCloseGuard closeGuard(reader);

    if (token.isCancellationRequested()) {
        return Result<void>::failure(cancelledError());
    }

    Result<PointCloudSourceInfo> opened = Result<PointCloudSourceInfo>::failure(
        flowControlClosedError("ConcurrencyGate"));
    {
        const auto lease = request.concurrencyGate->acquire(token);
        if (!lease.has_value()) {
            return cancellationOrFlowControlError(token, "ConcurrencyGate");
        }
        if (token.isCancellationRequested()) {
            return Result<void>::failure(cancelledError());
        }
        opened = reader.open(request.sourcePath);
    }

    if (token.isCancellationRequested()) {
        return Result<void>::failure(cancelledError());
    }
    if (!opened.hasValue()) {
        return Result<void>::failure(opened.error());
    }

    Result<void> openedCallback = request.onOpened(std::move(opened.value()), token);
    if (token.isCancellationRequested()) {
        return Result<void>::failure(cancelledError());
    }
    if (!openedCallback.hasValue()) {
        return openedCallback;
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

        Result<std::optional<PointBatch>> next =
            Result<std::optional<PointBatch>>::failure(flowControlClosedError("ConcurrencyGate"));
        {
            const auto lease = request.concurrencyGate->acquire(token);
            if (!lease.has_value()) {
                return cancellationOrFlowControlError(token, "ConcurrencyGate");
            }
            if (token.isCancellationRequested()) {
                return Result<void>::failure(cancelledError());
            }
            next = reader.readNext(request.maximumPointsPerBatch, token);
        }

        if (token.isCancellationRequested()) {
            return Result<void>::failure(cancelledError());
        }
        if (!next.hasValue()) {
            return Result<void>::failure(next.error());
        }
        if (!next.value().has_value()) {
            return Result<void>::success();
        }

        PointBatch batch = std::move(*next.value());
        const Result<void> validated = batch.validate();
        if (!validated.hasValue()) {
            return validated;
        }
        if (token.isCancellationRequested()) {
            return Result<void>::failure(cancelledError());
        }

        Result<void> batchCallback = request.onBatch(std::move(batch), token);
        if (token.isCancellationRequested()) {
            return Result<void>::failure(cancelledError());
        }
        if (!batchCallback.hasValue()) {
            return batchCallback;
        }
    }
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
