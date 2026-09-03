#pragma once

#include "dzc/Engine.h"
#include "dzc/EngineCommand.h"
#include "dzc/EngineEvent.h"
#include "dzc/EngineSnapshot.h"
#include "dzc/Result.h"

#include <QColor>
#include <QObject>
#include <QPointF>
#include <QSize>
#include <QString>
#include <Qt>

#include <memory>
#include <vector>

namespace dzc {

// Provides the Qt-free communication boundary used by EngineUiAdapter.
class IEngineUiPort {
public:
    virtual ~IEngineUiPort() = default;

    // Enqueues one validated command for the Engine.
    virtual Result<void> enqueueCommand(EngineCommand command) = 0;

    // Returns the latest immutable Engine snapshot.
    virtual std::shared_ptr<const EngineSnapshot> getSnapshot() const = 0;

    // Removes and returns the currently pending Engine events.
    virtual std::vector<EngineEvent> pollEvents() = 0;
};

class EngineUiAdapter final : public QObject {
public:
    // Creates an adapter over an application-provided communication port.
    explicit EngineUiAdapter(IEngineUiPort& port, QObject* parent = nullptr);

    // Creates an adapter over an existing Engine without taking its ownership.
    explicit EngineUiAdapter(Engine& engine, QObject* parent = nullptr);

    // Releases the private adapter implementation.
    ~EngineUiAdapter() override;

    EngineUiAdapter(const EngineUiAdapter&) = delete;
    EngineUiAdapter& operator=(const EngineUiAdapter&) = delete;
    EngineUiAdapter(EngineUiAdapter&&) = delete;
    EngineUiAdapter& operator=(EngineUiAdapter&&) = delete;

    // Converts a Qt path to UTF-8 and submits a dataset-load command.
    Result<void> loadDataset(const QString& path);

    // Submits a point-size command.
    Result<void> setPointSize(float pixels);

    // Submits a shading-mode command.
    Result<void> setShadingMode(ShadingMode mode);

    // Converts a QColor and submits a fixed-color command.
    Result<void> setFixedColor(const QColor& color);

    // Converts a QColor and submits a background-color command.
    Result<void> setBackgroundColor(const QColor& color);

    // Submits a CUDA mode command.
    Result<void> setCudaMode(OptionalFeatureMode mode);

    // Submits the view-reset command.
    Result<void> resetView();

    // Converts a widget-local pixel position and submits a pointer-move input.
    Result<void> submitPointerMove(
        const QPointF& position,
        const QSize& viewport,
        Qt::KeyboardModifiers modifiers = {});

    // Converts a widget-local pixel position and submits a pointer-button input.
    Result<void> submitPointerButton(
        Qt::MouseButton button,
        bool pressed,
        const QPointF& position,
        const QSize& viewport,
        Qt::KeyboardModifiers modifiers = {});

    // Submits a wheel input using the supplied signed delta.
    Result<void> submitWheel(
        double deltaY,
        Qt::KeyboardModifiers modifiers = {});

    // Converts a supported Qt key to a stable USB HID code and submits it.
    Result<void> submitKey(
        int qtKey,
        bool pressed,
        Qt::KeyboardModifiers modifiers = {});

    // Submits a focus input.
    Result<void> submitFocus(bool focused);

    // Submits a pending camera reset request as an input event.
    Result<void> submitResetRequest();

    // Returns the latest immutable snapshot from the communication port.
    std::shared_ptr<const EngineSnapshot> currentSnapshot() const;

    // Polls and removes pending events from the communication port.
    std::vector<EngineEvent> pollEvents();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc
