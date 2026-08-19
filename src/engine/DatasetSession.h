#pragma once

#include "tasks/Cancellation.h"
#include "tasks/TaskCompletion.h"

#include <dzc/EngineSnapshot.h>
#include <dzc/Result.h>

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace dzc {

enum class DatasetSessionCompletionKind : std::uint8_t {
    Ignored,
    Loaded,
    Cancelled,
    Failed
};

struct DatasetSessionCompletion final {
    DatasetSessionCompletionKind kind{DatasetSessionCompletionKind::Ignored};
    DatasetId datasetId;
    TaskId taskId;
    std::optional<Error> error;
};

// Private single-consumer Dataset replacement state. It separates the active
// Scene dataset from a newer candidate so incomplete work never tears down a
// valid Scene reference.
class DatasetSession final {
public:
    DatasetSession() = default;

    Result<DatasetId> beginLoad(std::string path);
    bool requestCancel(DatasetId datasetId) noexcept;
    bool unload(DatasetId datasetId) noexcept;

    // Accepts a worker completion only when it belongs to the active candidate.
    DatasetSessionCompletion applyCompletion(tasks::TaskCompletion completion);

    // Provides a deterministic private seam until a Reader-backed task producer
    // is introduced. Engine drains this queue during its task-completion stage.
    void injectCompletionForTesting(tasks::TaskCompletion completion);
    std::deque<tasks::TaskCompletion> takeInjectedCompletions();

    std::optional<DatasetId> sceneDatasetId() const noexcept;
    DatasetSummary snapshotSummary() const;

private:
    struct Candidate final {
        DatasetSummary summary;
        tasks::CancellationSource cancellation;
    };

    Result<DatasetId> allocateDatasetId();
    void clearCandidate() noexcept;

    std::optional<DatasetSummary> m_active;
    std::optional<DatasetSummary> m_failed;
    std::optional<Candidate> m_candidate;
    std::deque<tasks::TaskCompletion> m_injectedCompletions;
    std::uint64_t m_nextDatasetId{1U};
    bool m_datasetIdExhausted{false};
};

} // namespace dzc
