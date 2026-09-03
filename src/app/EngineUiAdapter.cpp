#include "EngineUiAdapter.h"

#include <QByteArray>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kInvalidArgument = 1U;

Error invalidArgument(const char* context, const char* diagnostic) {
    return Error{
        ErrorDomain::General,
        kInvalidArgument,
        "Invalid UI argument",
        diagnostic,
        context};
}

Error unsupportedArgument(const char* context, const char* diagnostic) {
    return Error{
        ErrorDomain::General,
        kInvalidArgument,
        "Unsupported UI input",
        diagnostic,
        context};
}

Result<void> invalidResult(const char* context, const char* diagnostic) {
    return Result<void>::failure(invalidArgument(context, diagnostic));
}

Result<void> unsupportedResult(const char* context, const char* diagnostic) {
    return Result<void>::failure(unsupportedArgument(context, diagnostic));
}

std::uint32_t convertModifiers(Qt::KeyboardModifiers modifiers) noexcept {
    std::uint32_t result = 0U;
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        result |= input::kModifierShift;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        result |= input::kModifierControl;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        result |= input::kModifierAlt;
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        result |= input::kModifierMeta;
    }
    return result;
}

std::optional<std::uint32_t> convertKey(int qtKey) noexcept {
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        return 0x04U + static_cast<std::uint32_t>(qtKey - Qt::Key_A);
    }
    if (qtKey >= Qt::Key_1 && qtKey <= Qt::Key_9) {
        return 0x1EU + static_cast<std::uint32_t>(qtKey - Qt::Key_1);
    }
    if (qtKey == Qt::Key_0) {
        return 0x27U;
    }

    switch (qtKey) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return 0x28U;
    case Qt::Key_Escape:
        return 0x29U;
    case Qt::Key_Backspace:
        return 0x2AU;
    case Qt::Key_Tab:
        return 0x2BU;
    case Qt::Key_Space:
        return 0x2CU;
    case Qt::Key_Right:
        return 0x4FU;
    case Qt::Key_Left:
        return 0x50U;
    case Qt::Key_Down:
        return 0x51U;
    case Qt::Key_Up:
        return 0x52U;
    case Qt::Key_F1:
        return 0x3AU;
    case Qt::Key_F2:
        return 0x3BU;
    case Qt::Key_F3:
        return 0x3CU;
    case Qt::Key_F4:
        return 0x3DU;
    case Qt::Key_F5:
        return 0x3EU;
    case Qt::Key_F6:
        return 0x3FU;
    case Qt::Key_F7:
        return 0x40U;
    case Qt::Key_F8:
        return 0x41U;
    case Qt::Key_F9:
        return 0x42U;
    case Qt::Key_F10:
        return 0x43U;
    case Qt::Key_F11:
        return 0x44U;
    case Qt::Key_F12:
        return 0x45U;
    default:
        return std::nullopt;
    }
}

Result<InputEvent> pointerEvent(
    InputEventType type,
    const QPointF& position,
    const QSize& viewport,
    std::uint32_t code,
    bool pressed,
    Qt::KeyboardModifiers modifiers,
    const char* context) {
    if (viewport.width() <= 0 || viewport.height() <= 0) {
        return Result<InputEvent>::failure(
            invalidArgument(context, "The input viewport must have positive width and height."));
    }

    const double x = position.x();
    const double y = position.y();
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return Result<InputEvent>::failure(
            invalidArgument(context, "The pointer position must contain finite coordinates."));
    }

    const double normalizedX = x / static_cast<double>(viewport.width());
    const double normalizedY = y / static_cast<double>(viewport.height());
    if (normalizedX < 0.0 || normalizedX > 1.0 || normalizedY < 0.0 || normalizedY > 1.0) {
        return Result<InputEvent>::failure(
            invalidArgument(context, "The pointer position must be inside the input viewport."));
    }

    return Result<InputEvent>::success(InputEvent{
        type,
        code,
        normalizedX,
        normalizedY,
        pressed,
        convertModifiers(modifiers)});
}

class EngineUiPort final : public IEngineUiPort {
public:
    explicit EngineUiPort(Engine& engine) noexcept
        : m_engine(engine) {}

    Result<void> enqueueCommand(EngineCommand command) override {
        return m_engine.enqueueCommand(std::move(command));
    }

    std::shared_ptr<const EngineSnapshot> getSnapshot() const override {
        return m_engine.getSnapshot();
    }

    std::vector<EngineEvent> pollEvents() override {
        return m_engine.pollEvents();
    }

private:
    Engine& m_engine;
};

} // namespace

struct EngineUiAdapter::Impl final {
    explicit Impl(IEngineUiPort& portReference) noexcept
        : port(&portReference) {}

    explicit Impl(std::unique_ptr<IEngineUiPort> ownedPortValue) noexcept
        : ownedPort(std::move(ownedPortValue)), port(ownedPort.get()) {}

    std::unique_ptr<IEngineUiPort> ownedPort;
    IEngineUiPort* port{nullptr};
};

EngineUiAdapter::EngineUiAdapter(IEngineUiPort& port, QObject* parent)
    : QObject(parent),
      m_impl(std::make_unique<Impl>(port)) {}

EngineUiAdapter::EngineUiAdapter(Engine& engine, QObject* parent)
    : QObject(parent),
      m_impl(std::make_unique<Impl>(std::make_unique<EngineUiPort>(engine))) {}

EngineUiAdapter::~EngineUiAdapter() = default;

Result<void> EngineUiAdapter::loadDataset(const QString& path) {
    if (path.isEmpty()) {
        return invalidResult("EngineUiAdapter::loadDataset", "The dataset path must not be empty.");
    }

    const QByteArray utf8 = path.toUtf8();
    const Result<void> validation =
        m_impl->port->enqueueCommand(EngineCommand{LoadDatasetCommand{std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()))}});
    return validation;
}

Result<void> EngineUiAdapter::setPointSize(float pixels) {
    const EngineCommand command{SetPointSizeCommand{pixels}};
    if (!isValidEngineCommand(command)) {
        return invalidResult("EngineUiAdapter::setPointSize", "Point size must be finite and within [1, 64].");
    }
    return m_impl->port->enqueueCommand(command);
}

Result<void> EngineUiAdapter::setShadingMode(ShadingMode mode) {
    return m_impl->port->enqueueCommand(EngineCommand{SetShadingModeCommand{mode}});
}

Result<void> EngineUiAdapter::setFixedColor(const QColor& color) {
    if (!color.isValid()) {
        return invalidResult("EngineUiAdapter::setFixedColor", "The fixed color must be valid.");
    }
    const ColorRgba converted{static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
        static_cast<float>(color.blueF()), static_cast<float>(color.alphaF())};
    return m_impl->port->enqueueCommand(EngineCommand{SetFixedColorCommand{converted}});
}

Result<void> EngineUiAdapter::setBackgroundColor(const QColor& color) {
    if (!color.isValid()) {
        return invalidResult("EngineUiAdapter::setBackgroundColor", "The background color must be valid.");
    }
    const ColorRgba converted{static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
        static_cast<float>(color.blueF()), static_cast<float>(color.alphaF())};
    return m_impl->port->enqueueCommand(EngineCommand{SetBackgroundColorCommand{converted}});
}

Result<void> EngineUiAdapter::setCudaMode(OptionalFeatureMode mode) {
    return m_impl->port->enqueueCommand(EngineCommand{SetCudaModeCommand{mode}});
}

Result<void> EngineUiAdapter::resetView() {
    return m_impl->port->enqueueCommand(EngineCommand{ResetViewCommand{}});
}

Result<void> EngineUiAdapter::submitPointerMove(
    const QPointF& position,
    const QSize& viewport,
    Qt::KeyboardModifiers modifiers) {
    const Result<InputEvent> event = pointerEvent(
        InputEventType::PointerMove,
        position,
        viewport,
        0U,
        false,
        modifiers,
        "EngineUiAdapter::submitPointerMove");
    if (!event.hasValue()) {
        return Result<void>::failure(event.error());
    }
    return m_impl->port->enqueueCommand(EngineCommand{SubmitInputCommand{event.value()}});
}

Result<void> EngineUiAdapter::submitPointerButton(
    Qt::MouseButton button,
    bool pressed,
    const QPointF& position,
    const QSize& viewport,
    Qt::KeyboardModifiers modifiers) {
    std::uint32_t code = 0U;
    if (button == Qt::LeftButton) {
        code = input::kPointerLeftButtonCode;
    } else if (button == Qt::RightButton) {
        code = input::kPointerRightButtonCode;
    } else {
        return unsupportedResult(
            "EngineUiAdapter::submitPointerButton",
            "Only the left and right mouse buttons are supported.");
    }

    const Result<InputEvent> event = pointerEvent(
        InputEventType::PointerButton,
        position,
        viewport,
        code,
        pressed,
        modifiers,
        "EngineUiAdapter::submitPointerButton");
    if (!event.hasValue()) {
        return Result<void>::failure(event.error());
    }
    return m_impl->port->enqueueCommand(EngineCommand{SubmitInputCommand{event.value()}});
}

Result<void> EngineUiAdapter::submitWheel(
    double deltaY,
    Qt::KeyboardModifiers modifiers) {
    if (!std::isfinite(deltaY)) {
        return invalidResult("EngineUiAdapter::submitWheel", "The wheel delta must be finite.");
    }
    const InputEvent event{
        InputEventType::Wheel,
        0U,
        0.0,
        deltaY,
        false,
        convertModifiers(modifiers)};
    return m_impl->port->enqueueCommand(EngineCommand{SubmitInputCommand{event}});
}

Result<void> EngineUiAdapter::submitKey(
    int qtKey,
    bool pressed,
    Qt::KeyboardModifiers modifiers) {
    const std::optional<std::uint32_t> code = convertKey(qtKey);
    if (!code.has_value()) {
        return unsupportedResult(
            "EngineUiAdapter::submitKey",
            "The Qt key is not included in the stable input mapping.");
    }
    const InputEvent event{
        InputEventType::Key,
        *code,
        0.0,
        0.0,
        pressed,
        convertModifiers(modifiers)};
    return m_impl->port->enqueueCommand(EngineCommand{SubmitInputCommand{event}});
}

Result<void> EngineUiAdapter::submitFocus(bool focused) {
    const InputEvent event{
        InputEventType::Focus,
        0U,
        0.0,
        0.0,
        focused,
        0U};
    return m_impl->port->enqueueCommand(EngineCommand{SubmitInputCommand{event}});
}

Result<void> EngineUiAdapter::submitResetRequest() {
    const InputEvent event{
        InputEventType::ResetRequest,
        0U,
        0.0,
        0.0,
        false,
        0U};
    return m_impl->port->enqueueCommand(EngineCommand{SubmitInputCommand{event}});
}

std::shared_ptr<const EngineSnapshot> EngineUiAdapter::currentSnapshot() const {
    return m_impl->port->getSnapshot();
}

std::vector<EngineEvent> EngineUiAdapter::pollEvents() {
    return m_impl->port->pollEvents();
}

} // namespace dzc
