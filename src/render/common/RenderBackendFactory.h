#pragma once

#include "render/common/RenderBackendTypes.h"
#include "dzc/EngineConfig.h"
#include "dzc/Error.h"
#include "dzc/Result.h"

#include <functional>
#include <memory>
#include <utility>

namespace dzc {

// Backend-independent lifecycle seam. Concrete backend implementations may
// expose additional diagnostics without putting API-specific handles here.
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    virtual Result<void> init(const RenderBackendConfig& config) = 0;
    virtual Result<void> upload(const UploadBatch& batch) = 0;
    virtual Result<void> update(const RenderFrame& frame) = 0;
    virtual Result<void> render() = 0;
    virtual Result<void> resize(const RenderSize& size) = 0;
    virtual void release(ChunkId chunkId) noexcept = 0;
    virtual void shutdown() noexcept = 0;
};

using RenderBackendCreator = std::function<Result<std::unique_ptr<IRenderBackend>>(
    const EngineConfig& config)>;

class RenderBackendFactory final {
public:
    explicit RenderBackendFactory(RenderBackendCreator creator)
        : m_creator(std::move(creator)) {}

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