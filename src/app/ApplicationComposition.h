#pragma once

#include "../compute/common/ComputeBackendFactory.h"
#include "../render/common/RenderBackendFactory.h"

#include <dzc/EngineConfig.h>
#include <dzc/Result.h>

#include <memory>
#include <utility>

namespace dzc {

struct ApplicationComposition final {
    std::unique_ptr<IRenderBackend> renderBackend;
    std::unique_ptr<IComputeBackend> computeBackend;
    bool computeDegraded{false};

    // Assembles backend objects at the application composition root.
    static Result<ApplicationComposition> compose(
        const EngineConfig& config,
        const RenderBackendFactory& renderFactory,
        const ComputeBackendFactory& computeFactory) {
        Result<std::unique_ptr<IRenderBackend>> renderResult = renderFactory.create(config);
        if (!renderResult.hasValue()) {
            return Result<ApplicationComposition>::failure(renderResult.error());
        }

        Result<ComputeBackendCreation> computeResult = computeFactory.create(config);
        if (!computeResult.hasValue()) {
            return Result<ApplicationComposition>::failure(computeResult.error());
        }

        ComputeBackendCreation computeCreation = std::move(computeResult.value());
        return Result<ApplicationComposition>::success(ApplicationComposition{
            std::move(renderResult.value()),
            std::move(computeCreation.backend),
            computeCreation.degraded});
    }
};

} // namespace dzc