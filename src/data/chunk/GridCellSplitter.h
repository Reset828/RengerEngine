#pragma once

#include "data/chunk/GridBucketStore.h"
#include "tasks/Cancellation.h"
#include <dzc/Result.h>

#include <vector>

namespace dzc {

class GridCellSplitter final {
public:
    static Result<std::vector<GridBucket>> split(
        const GridBucket& bucket,
        tasks::CancellationToken token = {});
};

} // namespace dzc