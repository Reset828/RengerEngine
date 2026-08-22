#pragma once

#include "data/io/IPointCloudReader.h"
#include "tasks/BackpressureController.h"
#include "tasks/ConcurrencyGate.h"
#include "tasks/TaskSystem.h"

#include <dzc/EngineTypes.h>
#include <dzc/Result.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace dzc {

using PointCloudLoadOpenedCallback =
    std::function<Result<void>(PointCloudSourceInfo, tasks::CancellationToken)>;
using PointCloudLoadBatchCallback =
    std::function<Result<void>(PointBatch&&, tasks::CancellationToken)>;

struct PointCloudLoadRequest final {
    DatasetId datasetId;
    std::string sourcePath;
    std::unique_ptr<IPointCloudReader> reader;
    std::size_t maximumPointsPerBatch{0U};
    std::shared_ptr<tasks::ConcurrencyGate> concurrencyGate;
    std::shared_ptr<tasks::BackpressureController> backpressureController;
    PointCloudLoadOpenedCallback onOpened;
    PointCloudLoadBatchCallback onBatch;
    tasks::CancellationToken cancellationToken;
    tasks::TaskPriority priority{tasks::TaskPriority::Normal};
};

// Submits one Reader-owned point-cloud load to a TaskSystem worker. Callbacks
// are invoked on that worker, never on the submitter thread.
class PointCloudLoadTask final {
public:
    static Result<TaskId> submit(
        tasks::TaskSystem& taskSystem,
        PointCloudLoadRequest request);

    PointCloudLoadTask() = delete;
};

} // namespace dzc
