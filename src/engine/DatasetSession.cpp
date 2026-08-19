#include "engine/DatasetSession.h"

#include <limits>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kDatasetIdExhausted = 2U;
constexpr std::uint32_t kCancelledTaskCode = 7U;

Error datasetIdExhaustedError() {
    return Error{
        ErrorDomain::Internal,
        kDatasetIdExhausted,
        "Dataset identifier space is exhausted",
        "A new DatasetId cannot be allocated without reusing an existing identifier.",
        "DatasetSession::beginLoad"};
}

Error cancelledError() {
    return Error{
        ErrorDomain::Task,
        kCancelledTaskCode,
        "Dataset load cancelled",
        "The Dataset load was cancelled before it became current.",
        "DatasetSession"};
}

bool isCancellation(const Result<void>& result) noexcept {
    return !result.hasValue() && result.error().domain == ErrorDomain::Task &&
           result.error().code == kCancelledTaskCode;
}

} // namespace

Result<DatasetId> DatasetSession::allocateDatasetId() {
    if (m_datasetIdExhausted) {
        return Result<DatasetId>::failure(datasetIdExhaustedError());
    }

    const DatasetId datasetId{m_nextDatasetId};
    if (m_nextDatasetId == std::numeric_limits<std::uint64_t>::max()) {
        m_datasetIdExhausted = true;
    } else {
        ++m_nextDatasetId;
    }
    return Result<DatasetId>::success(datasetId);
}

Result<DatasetId> DatasetSession::beginLoad(std::string path) {
    const Result<DatasetId> id = allocateDatasetId();
    if (!id.hasValue()) {
        return id;
    }

    if (m_candidate.has_value()) {
        static_cast<void>(m_candidate->cancellation.requestCancellation());
    }
    m_failed.reset();
    m_candidate.emplace(Candidate{
        DatasetSummary{id.value(), DatasetState::Opening, std::move(path)},
        tasks::CancellationSource{}});
    return id;
}

bool DatasetSession::requestCancel(DatasetId datasetId) noexcept {
    if (!m_candidate.has_value() || m_candidate->summary.id != datasetId) {
        return false;
    }

    m_candidate->summary.state = DatasetState::Cancelling;
    static_cast<void>(m_candidate->cancellation.requestCancellation());
    return true;
}

bool DatasetSession::unload(DatasetId datasetId) noexcept {
    if (m_active.has_value() && m_active->id == datasetId) {
        m_active.reset();
        return true;
    }
    if (m_candidate.has_value() && m_candidate->summary.id == datasetId) {
        m_candidate->summary.state = DatasetState::Cancelling;
        static_cast<void>(m_candidate->cancellation.requestCancellation());
        return true;
    }
    if (m_failed.has_value() && m_failed->id == datasetId) {
        m_failed.reset();
        return true;
    }
    return false;
}

DatasetSessionCompletion DatasetSession::applyCompletion(tasks::TaskCompletion completion) {
    DatasetSessionCompletion result;
    result.taskId = completion.taskId;
    if (!completion.datasetId.has_value() || !m_candidate.has_value() ||
        completion.datasetId.value() != m_candidate->summary.id) {
        return result;
    }

    result.datasetId = completion.datasetId.value();
    const bool cancelled = m_candidate->summary.state == DatasetState::Cancelling ||
                           isCancellation(completion.result);
    if (cancelled) {
        clearCandidate();
        result.kind = DatasetSessionCompletionKind::Cancelled;
        result.error = cancelledError();
        return result;
    }

    if (completion.result.hasValue()) {
        DatasetSummary loaded = m_candidate->summary;
        loaded.state = DatasetState::Ready;
        loaded.progress = 1.0;
        m_active = std::move(loaded);
        clearCandidate();
        result.kind = DatasetSessionCompletionKind::Loaded;
        return result;
    }

    DatasetSummary failed = m_candidate->summary;
    failed.state = DatasetState::Error;
    clearCandidate();
    if (!m_active.has_value()) {
        m_failed = std::move(failed);
    }
    result.kind = DatasetSessionCompletionKind::Failed;
    result.error = completion.result.error();
    return result;
}

void DatasetSession::injectCompletionForTesting(tasks::TaskCompletion completion) {
    m_injectedCompletions.push_back(std::move(completion));
}

std::deque<tasks::TaskCompletion> DatasetSession::takeInjectedCompletions() {
    return std::move(m_injectedCompletions);
}

std::optional<DatasetId> DatasetSession::sceneDatasetId() const noexcept {
    return m_active.has_value() ? std::optional<DatasetId>{m_active->id} : std::nullopt;
}

DatasetSummary DatasetSession::snapshotSummary() const {
    if (m_active.has_value()) {
        return *m_active;
    }
    if (m_candidate.has_value()) {
        return m_candidate->summary;
    }
    if (m_failed.has_value()) {
        return *m_failed;
    }
    return {};
}

void DatasetSession::clearCandidate() noexcept {
    m_candidate.reset();
}

} // namespace dzc
