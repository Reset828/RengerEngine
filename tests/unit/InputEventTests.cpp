#include "dzc/InputEvent.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

void testEventTypeDefinition() {
    static_assert(std::is_same_v<std::underlying_type_t<dzc::InputEventType>, std::uint8_t>);

    assert(static_cast<std::uint8_t>(dzc::InputEventType::PointerMove) == 0U);
    assert(static_cast<std::uint8_t>(dzc::InputEventType::PointerButton) == 1U);
    assert(static_cast<std::uint8_t>(dzc::InputEventType::Wheel) == 2U);
    assert(static_cast<std::uint8_t>(dzc::InputEventType::Key) == 3U);
    assert(static_cast<std::uint8_t>(dzc::InputEventType::Focus) == 4U);
    assert(static_cast<std::uint8_t>(dzc::InputEventType::ResetRequest) == 5U);
}

void testDefaultValuesAndValueSemantics() {
    static_assert(std::is_same_v<decltype(dzc::InputEvent::type), dzc::InputEventType>);
    static_assert(std::is_same_v<decltype(dzc::InputEvent::code), std::uint32_t>);
    static_assert(std::is_same_v<decltype(dzc::InputEvent::valueX), double>);
    static_assert(std::is_same_v<decltype(dzc::InputEvent::valueY), double>);
    static_assert(std::is_same_v<decltype(dzc::InputEvent::pressed), bool>);
    static_assert(std::is_same_v<decltype(dzc::InputEvent::modifiers), std::uint32_t>);

    static_assert(std::is_default_constructible_v<dzc::InputEvent>);
    static_assert(std::is_copy_constructible_v<dzc::InputEvent>);
    static_assert(std::is_copy_assignable_v<dzc::InputEvent>);
    static_assert(std::is_move_constructible_v<dzc::InputEvent>);
    static_assert(std::is_move_assignable_v<dzc::InputEvent>);

    const dzc::InputEvent event;
    assert(event.type == dzc::InputEventType::PointerMove);
    assert(event.code == 0U);
    assert(event.valueX == 0.0);
    assert(event.valueY == 0.0);
    assert(!event.pressed);
    assert(event.modifiers == 0U);

    dzc::InputEvent source;
    source.type = dzc::InputEventType::Key;
    source.code = 42U;
    source.valueX = -123.5;
    source.valueY = 456.25;
    source.pressed = true;
    source.modifiers = 7U;

    const dzc::InputEvent copied(source);
    dzc::InputEvent assigned;
    assigned = source;
    dzc::InputEvent moved(std::move(source));
    dzc::InputEvent moveAssigned;
    moveAssigned = std::move(assigned);

    const dzc::InputEvent* values[] = {&copied, &moved, &moveAssigned};

    for (const dzc::InputEvent* value : values) {
        assert(value->type == dzc::InputEventType::Key);
        assert(value->code == 42U);
        assert(value->valueX == -123.5);
        assert(value->valueY == 456.25);
        assert(value->pressed);
        assert(value->modifiers == 7U);
    }
}

void testAllEventTypesPreserveAbstractValues() {
    const dzc::InputEventType types[] = {
        dzc::InputEventType::PointerMove,
        dzc::InputEventType::PointerButton,
        dzc::InputEventType::Wheel,
        dzc::InputEventType::Key,
        dzc::InputEventType::Focus,
        dzc::InputEventType::ResetRequest};

    for (std::size_t index = 0; index < std::size(types); ++index) {
        dzc::InputEvent event;
        event.type = types[index];
        event.code = static_cast<std::uint32_t>(100U + index);
        event.valueX = static_cast<double>(index) - 2.5;
        event.valueY = static_cast<double>(index) + 4.25;
        event.pressed = (index % 2U) == 0U;
        event.modifiers = static_cast<std::uint32_t>(200U + index);

        assert(event.type == types[index]);
        assert(event.code == static_cast<std::uint32_t>(100U + index));
        assert(event.valueX == static_cast<double>(index) - 2.5);
        assert(event.valueY == static_cast<double>(index) + 4.25);
        assert(event.pressed == ((index % 2U) == 0U));
        assert(event.modifiers == static_cast<std::uint32_t>(200U + index));
    }
}

void testNumericValuesAreNotValidatedOrNormalized() {
    dzc::InputEvent event;
    event.code = std::numeric_limits<std::uint32_t>::max();
    event.modifiers = std::numeric_limits<std::uint32_t>::max();
    event.valueX = std::numeric_limits<double>::lowest();
    event.valueY = std::numeric_limits<double>::max();

    assert(event.code == std::numeric_limits<std::uint32_t>::max());
    assert(event.modifiers == std::numeric_limits<std::uint32_t>::max());
    assert(event.valueX == std::numeric_limits<double>::lowest());
    assert(event.valueY == std::numeric_limits<double>::max());

    event.valueX = std::numeric_limits<double>::quiet_NaN();
    event.valueY = std::numeric_limits<double>::infinity();
    assert(std::isnan(event.valueX));
    assert(std::isinf(event.valueY) && event.valueY > 0.0);

    event.valueX = -std::numeric_limits<double>::infinity();
    event.valueY = -0.0;
    assert(std::isinf(event.valueX) && event.valueX < 0.0);
    assert(event.valueY == 0.0);
    assert(std::signbit(event.valueY));
}

} // namespace

int main() {
    testEventTypeDefinition();
    testDefaultValuesAndValueSemantics();
    testAllEventTypesPreserveAbstractValues();
    testNumericValuesAreNotValidatedOrNormalized();
    return 0;
}