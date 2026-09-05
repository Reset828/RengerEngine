#pragma once

#include "dzc/Engine.h"
#include "dzc/EngineConfig.h"
#include "dzc/FrameInput.h"
#include "dzc/Result.h"
#include "compute/common/ComputeBackendFactory.h"
#include "render/common/RenderBackendFactory.h"

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
    virtual Result<void> enqueueCommand(EngineCommand command) = 0;
    virtual std::shared_ptr<const EngineSnapshot> getSnapshot() const = 0;
    virtual std::vector<EngineEvent> pollEvents() = 0;
    virtual Result<void> init(
        const EngineConfig& config,
        std::unique_ptr<IRenderBackend> renderBackend,
        std::unique_ptr<IComputeBackend> computeBackend) = 0;
    virtual Result<void> update(const FrameInput& input) = 0;
    virtual Result<void> render() = 0;
    virtual Result<void> resize(const RenderSize& size) = 0;
    virtual void shutdown() noexcept = 0;
};

class EngineUiAdapter final : public QObject {
public:
    explicit EngineUiAdapter(IEngineUiPort& port, QObject* parent = nullptr);
    explicit EngineUiAdapter(Engine& engine, QObject* parent = nullptr);
    ~EngineUiAdapter() override;

    EngineUiAdapter(const EngineUiAdapter&) = delete;
    EngineUiAdapter& operator=(const EngineUiAdapter&) = delete;
    EngineUiAdapter(EngineUiAdapter&&) = delete;
    EngineUiAdapter& operator=(EngineUiAdapter&&) = delete;

    Result<void> init(
        const EngineConfig& config,
        std::unique_ptr<IRenderBackend> renderBackend,
        std::unique_ptr<IComputeBackend> computeBackend);
    Result<void> update(const FrameInput& input);
    Result<void> render();
    Result<void> resize(const RenderSize& size);
    void shutdown() noexcept;

    Result<void> loadDataset(const QString& path);
    Result<void> setPointSize(float pixels);
    Result<void> setShadingMode(ShadingMode mode);
    Result<void> setFixedColor(const QColor& color);
    Result<void> setBackgroundColor(const QColor& color);
    Result<void> setCudaMode(OptionalFeatureMode mode);
    Result<void> resetView();
    Result<void> cancelDatasetLoad(DatasetId datasetId);
    Result<void> submitPointerMove(const QPointF& position, const QSize& viewport,
                                   Qt::KeyboardModifiers modifiers = {});
    Result<void> submitPointerButton(Qt::MouseButton button, bool pressed,
                                     const QPointF& position, const QSize& viewport,
                                     Qt::KeyboardModifiers modifiers = {});
    Result<void> submitWheel(double deltaY, Qt::KeyboardModifiers modifiers = {});
    Result<void> submitKey(int qtKey, bool pressed, Qt::KeyboardModifiers modifiers = {});
    Result<void> submitFocus(bool focused);
    Result<void> submitResetRequest();
    std::shared_ptr<const EngineSnapshot> currentSnapshot() const;
    std::vector<EngineEvent> pollEvents();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc