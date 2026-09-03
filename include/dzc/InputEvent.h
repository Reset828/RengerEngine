#pragma once

#include <cstdint>

namespace dzc {

namespace input {

inline constexpr std::uint32_t kPointerLeftButtonCode = 0U;
inline constexpr std::uint32_t kPointerRightButtonCode = 2U;
inline constexpr std::uint32_t kModifierShift = 1U;
inline constexpr std::uint32_t kModifierControl = 2U;
inline constexpr std::uint32_t kModifierAlt = 4U;
inline constexpr std::uint32_t kModifierMeta = 8U;

} // namespace input

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