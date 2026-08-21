#pragma once

#include <cstdint>

namespace dzc {

enum class InputEventType : std::uint8_t {
    PointerMove,
    PointerButton,
    Wheel,
    Key,
    Focus,
    ResetRequest
};

struct InputEvent final {
    InputEventType type{InputEventType::PointerMove};
    std::uint32_t code{0};
    double valueX{0.0};
    double valueY{0.0};
    bool pressed{false};
    std::uint32_t modifiers{0};
};

} // namespace dzc