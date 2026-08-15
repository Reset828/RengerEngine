#pragma once

#include <optional>

#include <dzc/EngineTypes.h>
#include <dzc/Result.h>

namespace dzc::tasks {

struct TaskCompletion final {
    TaskId taskId;
    std::optional<DatasetId> datasetId;
    Result<void> result;
};

} // namespace dzc::tasks