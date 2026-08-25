#pragma once

#include "data/chunk/GridBucketStore.h"
#include "tasks/Cancellation.h"

#include "data/chunk/Chunk.h"
#include <dzc/Result.h>

#include <vector>

namespace dzc {

class GridChunkBuilder final {
public:
    static Result<std::vector<Chunk>> build(
        const std::vector<std::vector<GridBucket>>& groups,
        tasks::CancellationToken token = {});
};

} // namespace dzc
