#pragma once

#include "data/chunk/GridBucketStore.h"
#include "tasks/Cancellation.h"
#include <dzc/Result.h>

#include <vector>

namespace dzc {

class GridRunMerger final {
public:
    static Result<std::vector<GridBucket>> merge(
        const std::vector<std::vector<GridBucket>>& inputs,
        tasks::CancellationToken token = {});
};

} // namespace dzc