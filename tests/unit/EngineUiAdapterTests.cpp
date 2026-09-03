#include "EngineUiAdapter.h"

#include <QColor>
#include <QPointF>
#include <QSize>

#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

class FakeEngineUiPort final : public dzc::IEngineUiPort {
public:
    dzc::Result<void> enqueueCommand(dzc::EngineCommand command) override {
        if (failure.has_value()) {
            return dzc::Result<void>::failure(*failure);
        }
        commands.push_back(std::move(command));
        return dzc::Result<void>::success();
    }

    std::shared_ptr<const dzc::EngineSnapshot> getSnapshot() const override {
        return snapshot;
    }

    std::vector<dzc::EngineEvent> pollEvents() override {
        std::vector<dzc::EngineEvent> result = std::move(events);
        events.clear();
        return result;
    }

    std::vector<dzc::EngineCommand> commands;
    std::shared_ptr<const dzc::EngineSnapshot> snapshot =
        std::make_shared<const dzc::EngineSnapshot>();
    std::vector<dzc::EngineEvent> events;
    std::optional<dzc::Error> failure;
};

const dzc::SubmitInputCommand* lastInput(const FakeEngineUiPort& port) {
    assert(!port.commands.empty());
    return std::get_if<dzc::SubmitInputCommand>(&port.commands.back());
}

void assertFailure(const dzc::Result<void>& result) {
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::General ||
           result.error().domain == dzc::ErrorDomain::Task ||
           result.error().domain == dzc::ErrorDomain::Internal);
    assert(result.error().code == 1U || result.error().code == 3U);
}

void testDatasetAndParameterConversions() {
    FakeEngineUiPort port;
    dzc::EngineUiAdapter adapter(port);

    assert(adapter.loadDataset(QString::fromUtf8("??/??.pcd")).hasValue());
    const auto& load = std::get<dzc::LoadDatasetCommand>(port.commands.back());
    assert(load.path == std::string("??/??.pcd"));

    assert(adapter.setPointSize(7.0F).hasValue());
    assert(std::get<dzc::SetPointSizeCommand>(port.commands.back()).pixels == 7.0F);
    assert(adapter.setShadingMode(dzc::ShadingMode::Height).hasValue());
    assert(std::get<dzc::SetShadingModeCommand>(port.commands.back()).mode == dzc::ShadingMode::Height);
    assert(adapter.setCudaMode(dzc::OptionalFeatureMode::On).hasValue());
    assert(std::get<dzc::SetCudaModeCommand>(port.commands.back()).mode == dzc::OptionalFeatureMode::On);
    assert(adapter.resetView().hasValue());
    assert(std::holds_alternative<dzc::ResetViewCommand>(port.commands.back()));

    const QColor color(51, 102, 153, 204);
    assert(adapter.setFixedColor(color).hasValue());
    const auto fixed = std::get<dzc::SetFixedColorCommand>(port.commands.back()).color;
    assert(std::fabs(fixed.red - 0.2F) < 0.001F);
    assert(std::fabs(fixed.green - 0.4F) < 0.001F);
    assert(std::fabs(fixed.blue - 0.6F) < 0.001F);
    assert(std::fabs(fixed.alpha - 0.8F) < 0.001F);

    assert(adapter.setBackgroundColor(QColor(Qt::black)).hasValue());
    assert(std::get<dzc::SetBackgroundColorCommand>(port.commands.back()).color ==
           dzc::ColorRgba{});
}

void testInvalidValuesDoNotReachPort() {
    FakeEngineUiPort port;
    dzc::EngineUiAdapter adapter(port);
    const std::size_t initialCount = port.commands.size();

    assertFailure(adapter.loadDataset(QString{}));
    assertFailure(adapter.setFixedColor(QColor{}));
    assertFailure(adapter.setBackgroundColor(QColor{}));
    assertFailure(adapter.setPointSize(0.0F));
    assertFailure(adapter.setPointSize(65.0F));
    assertFailure(adapter.setPointSize(std::numeric_limits<float>::quiet_NaN()));
    assert(port.commands.size() == initialCount);
}

void testPointerConversionAndModifiers() {
    FakeEngineUiPort port;
    dzc::EngineUiAdapter adapter(port);
    const auto modifiers = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier |
                            Qt::MetaModifier;

    assert(adapter.submitPointerMove(QPointF(50.0, 25.0), QSize(100, 50), modifiers).hasValue());
    const auto* move = lastInput(port);
    assert(move != nullptr);
    assert(move->event.type == dzc::InputEventType::PointerMove);
    assert(move->event.valueX == 0.5);
    assert(move->event.valueY == 0.5);
    assert(move->event.modifiers == (dzc::input::kModifierShift | dzc::input::kModifierControl |
                                     dzc::input::kModifierAlt | dzc::input::kModifierMeta));

    assert(adapter.submitPointerButton(
                       Qt::LeftButton,
                       true,
                       QPointF(25.0, 10.0),
                       QSize(100, 40),
                       Qt::NoModifier)
               .hasValue());
    const auto* left = lastInput(port);
    assert(left != nullptr);
    assert(left->event.type == dzc::InputEventType::PointerButton);
    assert(left->event.code == dzc::input::kPointerLeftButtonCode);
    assert(left->event.pressed);
    assert(left->event.valueX == 0.25);
    assert(left->event.valueY == 0.25);

    assert(adapter.submitPointerButton(
                       Qt::RightButton,
                       false,
                       QPointF(100.0, 40.0),
                       QSize(100, 40),
                       Qt::NoModifier)
               .hasValue());
    const auto* right = lastInput(port);
    assert(right != nullptr);
    assert(right->event.code == dzc::input::kPointerRightButtonCode);
    assert(!right->event.pressed);

    const std::size_t count = port.commands.size();
    assertFailure(adapter.submitPointerMove(QPointF(-1.0, 0.0), QSize(100, 40)));
    assertFailure(adapter.submitPointerMove(QPointF(0.0, 0.0), QSize(0, 40)));
    assertFailure(adapter.submitPointerButton(Qt::MiddleButton, true, QPointF(0.0, 0.0), QSize(100, 40)));
    assert(port.commands.size() == count);
}

void testWheelKeyFocusAndResetInputs() {
    FakeEngineUiPort port;
    dzc::EngineUiAdapter adapter(port);

    assert(adapter.submitWheel(120.0, Qt::ShiftModifier).hasValue());
    const auto* wheel = lastInput(port);
    assert(wheel != nullptr);
    assert(wheel->event.type == dzc::InputEventType::Wheel);
    assert(wheel->event.valueY == 120.0);
    assert(wheel->event.modifiers == dzc::input::kModifierShift);

    assert(adapter.submitKey(Qt::Key_A, true, Qt::ControlModifier).hasValue());
    const auto* key = lastInput(port);
    assert(key != nullptr);
    assert(key->event.type == dzc::InputEventType::Key);
    assert(key->event.code == 0x04U);
    assert(key->event.pressed);
    assert(key->event.modifiers == dzc::input::kModifierControl);

    assert(adapter.submitKey(Qt::Key_0, false).hasValue());
    assert(lastInput(port)->event.code == 0x27U);
    const std::size_t count = port.commands.size();
    assertFailure(adapter.submitKey(Qt::Key_Insert, true));
    assertFailure(adapter.submitWheel(std::numeric_limits<double>::infinity()));
    assert(port.commands.size() == count);

    assert(adapter.submitFocus(true).hasValue());
    assert(lastInput(port)->event.type == dzc::InputEventType::Focus);
    assert(lastInput(port)->event.pressed);
    assert(adapter.submitFocus(false).hasValue());
    assert(!lastInput(port)->event.pressed);
    assert(adapter.submitResetRequest().hasValue());
    assert(lastInput(port)->event.type == dzc::InputEventType::ResetRequest);
}

void testSnapshotEventsAndPortFailure() {
    FakeEngineUiPort port;
    auto snapshot = std::make_shared<dzc::EngineSnapshot>();
    snapshot->frameId.value = 99U;
    port.snapshot = snapshot;
    port.events.emplace_back(dzc::MessageEvent{});
    dzc::EngineUiAdapter adapter(port);

    assert(adapter.currentSnapshot() == snapshot);
    const auto events = adapter.pollEvents();
    assert(events.size() == 1U);
    assert(port.events.empty());
    assert(adapter.pollEvents().empty());

    port.failure = dzc::Error{
        dzc::ErrorDomain::Task, 3U, "full", "queue is full", "FakeEngineUiPort"};
    const std::size_t count = port.commands.size();
    const auto result = adapter.resetView();
    assertFailure(result);
    assert(result.error().domain == dzc::ErrorDomain::Task);
    assert(port.commands.size() == count);
}

void testCommandOrder() {
    FakeEngineUiPort port;
    dzc::EngineUiAdapter adapter(port);
    assert(adapter.resetView().hasValue());
    assert(adapter.setPointSize(2.0F).hasValue());
    assert(adapter.submitWheel(-120.0).hasValue());
    assert(port.commands.size() == 3U);
    assert(std::holds_alternative<dzc::ResetViewCommand>(port.commands[0]));
    assert(std::holds_alternative<dzc::SetPointSizeCommand>(port.commands[1]));
    assert(std::holds_alternative<dzc::SubmitInputCommand>(port.commands[2]));
}

} // namespace

int main() {
    testDatasetAndParameterConversions();
    testInvalidValuesDoNotReachPort();
    testPointerConversionAndModifiers();
    testWheelKeyFocusAndResetInputs();
    testSnapshotEventsAndPortFailure();
    testCommandOrder();
    return 0;
}
