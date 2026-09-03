#pragma once

#include "dzc/EngineConfig.h"
#include "dzc/EngineTypes.h"
#include "dzc/InputEvent.h"

#include <string>
#include <variant>

namespace dzc {

struct LoadDatasetCommand final {
    std::string path;
};

struct CancelDatasetLoadCommand final {
    DatasetId datasetId;
};

struct UnloadDatasetCommand final {
    DatasetId datasetId;
};

struct SetPointSizeCommand final {
    float pixels{1.0F};
};

struct SetShadingModeCommand final {
    ShadingMode mode{ShadingMode::OriginalColor};
};

struct SetFixedColorCommand final {
    ColorRgba color;
};

struct SetBackgroundColorCommand final {
    ColorRgba color;
};

struct SetCudaModeCommand final {
    OptionalFeatureMode mode{OptionalFeatureMode::Auto};
};

struct ResetViewCommand final {};

struct ResizeCommand final {
    RenderSize size;
};

struct ShutdownCommand final {};

struct SubmitInputCommand final {
    InputEvent event;
};

using EngineCommand = std::variant<
    LoadDatasetCommand,
    CancelDatasetLoadCommand,
    UnloadDatasetCommand,
    SetPointSizeCommand,
    SetShadingModeCommand,
    SetFixedColorCommand,
    SetBackgroundColorCommand,
    SetCudaModeCommand,
    ResetViewCommand,
    ResizeCommand,
    ShutdownCommand,
    SubmitInputCommand>;

// Returns whether the command satisfies the constraints defined by EC-001.
bool isValidEngineCommand(const EngineCommand& command) noexcept;

} // namespace dzc