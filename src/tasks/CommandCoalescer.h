#pragma once

#include <dzc/EngineCommand.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace dzc::tasks {

// Compatibility aliases retain the TS-003-facing namespace while the command
// protocol itself remains the public dzc::EngineCommand value protocol.
using ::dzc::CancelDatasetLoadCommand;
using ::dzc::EngineCommand;
using ::dzc::LoadDatasetCommand;
using ::dzc::ResetViewCommand;
using ::dzc::ResizeCommand;
using ::dzc::SetBackgroundColorCommand;
using ::dzc::SetCudaModeCommand;
using ::dzc::SetFixedColorCommand;
using ::dzc::SetPointSizeCommand;
using ::dzc::SetShadingModeCommand;
using ::dzc::ShutdownCommand;
using ::dzc::UnloadDatasetCommand;

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
