#pragma once

#include <dzc/EngineConfig.h>
#include <dzc/Result.h>

#include <functional>
#include <memory>
#include <utility>

namespace dzc {

// Minimal backend seam used by the composition root. Concrete render methods
// are defined by the later render-backend implementation task.
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
};

using RenderBackendCreator = std::function<Result<std::unique_ptr<IRenderBackend>>(
    const EngineConfig& config)>;

class RenderBackendFactory final {
public:
    // Creates a factory with an injected backend creator.
    explicit RenderBackendFactory(RenderBackendCreator creator)
        : m_creator(std::move(creator)) {}

    // Creates the backend selected by config.backend or returns a failure.
    Result<std::unique_ptr<IRenderBackend>> create(const EngineConfig& config) const {
        if (!m_creator) {
            return Result<std::unique_ptr<IRenderBackend>>::failure(Error{
                ErrorDomain::Internal,
                1,
                "Render backend creator is not configured",
                "RenderBackendFactory received an empty creator",
                "RenderBackendFactory::create"});
        }
        return m_creator(config);
    }

private:
    RenderBackendCreator m_creator;
};

} // namespace dzc