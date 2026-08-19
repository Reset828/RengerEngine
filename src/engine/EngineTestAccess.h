#pragma once

#include <dzc/Engine.h>

#include "tasks/TaskCompletion.h"

namespace dzc {

// Private test seam for deterministic Dataset completion ordering. This header
// is intentionally outside the public include tree.
class EngineTestAccess final {
public:
    static bool injectDatasetCompletion(Engine& engine, tasks::TaskCompletion completion);
};

} // namespace dzc
