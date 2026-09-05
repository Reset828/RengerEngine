#pragma once

#include "dzc/EngineConfig.h"
#include "dzc/Result.h"
#include "render/common/RenderBackendFactory.h"

#include <QOpenGLWidget>

#include <chrono>
#include <functional>
#include <memory>

namespace dzc {

class EngineUiAdapter;

// Qt host for an externally-owned Engine. The widget owns only the Qt view and
// the injected backend instance; Engine ownership remains in the composition root.
class OpenGLRenderWidget final : public QOpenGLWidget {
public:
    using BackendFactory = std::function<std::unique_ptr<IRenderBackend>(
        std::shared_ptr<const IRenderContextOperations>)>;
    using RefreshCallback = std::function<void()>;

    explicit OpenGLRenderWidget(
        EngineUiAdapter* adapter,
        EngineConfig config,
        BackendFactory backendFactory = {},
        QWidget* parent = nullptr);
    ~OpenGLRenderWidget() override;

    OpenGLRenderWidget(const OpenGLRenderWidget&) = delete;
    OpenGLRenderWidget& operator=(const OpenGLRenderWidget&) = delete;

    void setRefreshCallback(RefreshCallback callback);
    bool initializationAttempted() const noexcept { return m_initializationAttempted; }
    bool initializationSucceeded() const noexcept { return m_initialized; }
    bool frameSchedulingEnabled() const noexcept { return m_frameSchedulingEnabled; }

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void recordFailure(const char* operation, const Error* error = nullptr) noexcept;
    void requestFrameIfRenderable();
    void updateSchedulingForWindowState();
    void shutdownEngineWithCurrentContext() noexcept;
    RenderSize currentRenderSize() const noexcept;
    bool isRenderable() const noexcept;

    EngineUiAdapter* m_adapter{nullptr};
    EngineConfig m_config;
    BackendFactory m_backendFactory;
    RefreshCallback m_refreshCallback;
    std::shared_ptr<const IRenderContextOperations> m_contextOperations;
    bool m_initializationAttempted{false};
    bool m_initialized{false};
    bool m_frameSchedulingEnabled{false};
    bool m_shutdownAttempted{false};
    bool m_failureRecorded{false};
    std::chrono::steady_clock::time_point m_lastFrameTime{};
    QWidget* m_stateWindow{nullptr};
};

} // namespace dzc