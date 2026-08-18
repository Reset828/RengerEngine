#pragma once

#include <cstdint>

namespace dzc {

enum class EngineState : std::uint8_t {
    Created,
    Initializing,
    Ready,
    Running,
    Loading,
    Failed,
    ShuttingDown,
    Stopped
};

} // namespace dzc