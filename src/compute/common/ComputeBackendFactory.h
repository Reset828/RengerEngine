#pragma once

#include <dzc/EngineConfig.h>
#include <dzc/Result.h>

#include <functional>
#include <memory>
#include <utility>

namespace dzc {

// Minimal backend seam used by the composition root. Concrete compute methods
// are defined by the later compute-backend implementation task.
class IComputeBackend {
public:
    virtual ~IComputeBackend() = default;
};

class DisabledComputeBackend final : public IComputeBackend {};

struct ComputeBackendCreation final {
    std::unique_ptr<IComputeBackend> backend;
    bool degraded{false};
};

using ComputeBackendCreator = std::function<Result<std::unique_ptr<IComputeBackend>>(
    const EngineConfig& config)>;

class ComputeBackendFactory final {
public:
    // Creates a factory with an injected optional-compute backend creator.
    explicit ComputeBackendFactory(ComputeBackendCreator creator)
        : m_creator(std::move(creator)) {}

    // Creates the configured compute backend, applying CUDA mode semantics.
    Result<ComputeBackendCreation> create(const EngineConfig& config) const {
        if (config.cudaMode == OptionalFeatureMode::Off) {
            return Result<ComputeBackendCreation>::success(
                ComputeBackendCreation{std::make_unique<DisabledComputeBackend>(), false});
        }

        if (!m_creator) {
            if (config.cudaMode == OptionalFeatureMode::Auto) {
                return Result<ComputeBackendCreation>::success(
                    ComputeBackendCreation{std::make_unique<DisabledComputeBackend>(), true});
            }
            return Result<ComputeBackendCreation>::failure(Error{
                ErrorDomain::Internal,
                1,
                "Compute backend creator is not configured",
                "ComputeBackendFactory received an empty creator",
                "ComputeBackendFactory::create"});
        }

        Result<std::unique_ptr<IComputeBackend>> result = m_creator(config);
        if (result.hasValue()) {
            return Result<ComputeBackendCreation>::success(
                ComputeBackendCreation{std::move(result.value()), false});
        }

        if (config.cudaMode == OptionalFeatureMode::Auto) {
            return Result<ComputeBackendCreation>::success(
                ComputeBackendCreation{std::make_unique<DisabledComputeBackend>(), true});
        }

        return Result<ComputeBackendCreation>::failure(result.error());
    }

private:
    ComputeBackendCreator m_creator;
};

} // namespace dzc