#pragma once

#include <dzc/EngineConfig.h>
#include <dzc/EngineTypes.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace dzc::tasks {

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
    ShutdownCommand>;

class CommandCoalescer final {
public:
    explicit CommandCoalescer(std::size_t capacity = 1024U);
    ~CommandCoalescer();

    CommandCoalescer(const CommandCoalescer&) = delete;
    CommandCoalescer& operator=(const CommandCoalescer&) = delete;
    CommandCoalescer(CommandCoalescer&&) = delete;
    CommandCoalescer& operator=(CommandCoalescer&&) = delete;

    bool push(EngineCommand command);
    std::optional<EngineCommand> pop();
    std::vector<EngineCommand> popBatch(std::size_t maxCount);
    void close() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc::tasks