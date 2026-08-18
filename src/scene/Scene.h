#pragma once

#include <dzc/EngineConfig.h>
#include <dzc/EngineTypes.h>
#include <dzc/Result.h>

#include <optional>

namespace dzc {

struct SceneParameters final {
    float pointSize{1.0F};
    ShadingMode shadingMode{ShadingMode::OriginalColor};
    ColorRgba fixedColor;
    ColorRgba backgroundColor;
    RenderSize renderSize;
};

struct SceneFrameInput final {
    std::optional<DatasetId> datasetId;
    SceneParameters parameters;
};

class Scene final {
public:
    Scene() noexcept = default;

    // Applies a complete parameter set, preserving the old set on failure.
    Result<void> applyParameters(const SceneParameters& parameters);

    // Replaces the current dataset reference for the single consumer thread.
    void setDataset(std::optional<DatasetId> datasetId) noexcept;

    // Clears the current dataset reference.
    void clearDataset() noexcept;

    // Returns the current backend-independent frame input by value.
    SceneFrameInput frameInput() const;

private:
    std::optional<DatasetId> m_datasetId;
    SceneParameters m_parameters;
};

} // namespace dzc