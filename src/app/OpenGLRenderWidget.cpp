#include <glad/glad.h>
#include "OpenGLRenderWidget.h"

#include "EngineUiAdapter.h"
#include "compute/common/ComputeBackendFactory.h"
#include "render/opengl/OpenGLBackend.h"



#include <QByteArray>
#include <QDebug>
#include <QEvent>
#include <QOpenGLContext>
#include <QSurface>
#include <QSurfaceFormat>
#include <QShowEvent>
#include <QHideEvent>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <utility>

namespace dzc {
namespace {

class QtRenderContextOperations final : public IRenderContextOperations {
public:
    QtRenderContextOperations(QOpenGLContext& context, QSurface& surface) noexcept
        : m_context(&context), m_surface(&surface) {}

    bool makeCurrent() const noexcept override {
        return m_context != nullptr && m_surface != nullptr && m_context->makeCurrent(m_surface);
    }

    bool isCurrent() const noexcept override {
        return m_context != nullptr && QOpenGLContext::currentContext() == m_context;
    }

    bool loadFunctions() const noexcept override {
        if (!isCurrent()) {
            return false;
        }
        const int loaded = gladLoadGLLoader([](const char* name) -> void* {
            auto* context = QOpenGLContext::currentContext();
            return context == nullptr
                ? nullptr
                : reinterpret_cast<void*>(context->getProcAddress(QByteArray(name)));
        });
        m_functionsLoaded = loaded != 0;
        return m_functionsLoaded;
    }

    bool functionsLoaded() const noexcept override { return m_functionsLoaded; }

    bool releaseCurrent() const noexcept override {
        if (m_context == nullptr || QOpenGLContext::currentContext() != m_context) {
            return false;
        }
        m_context->doneCurrent();
        return true;
    }

private:
    QOpenGLContext* m_context{nullptr};
    QSurface* m_surface{nullptr};
    mutable bool m_functionsLoaded{false};
};

std::uint32_t physicalDimension(int logicalDimension, qreal devicePixelRatio) noexcept {
    const double ratio = std::isfinite(devicePixelRatio) && devicePixelRatio > 0.0
        ? static_cast<double>(devicePixelRatio)
        : 1.0;
    const double physical = std::round(static_cast<double>(std::max(0, logicalDimension)) * ratio);
    if (!std::isfinite(physical) || physical <= 0.0) {
        return 0U;
    }
    const double maxValue = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(std::min(physical, maxValue));
}

RenderSize makeRenderSize(int width, int height, qreal devicePixelRatio) noexcept {
    const float ratio = std::isfinite(devicePixelRatio) && devicePixelRatio > 0.0
        ? static_cast<float>(devicePixelRatio)
        : 1.0F;
    return RenderSize{
        physicalDimension(width, devicePixelRatio),
        physicalDimension(height, devicePixelRatio),
        ratio};
}

} // namespace

OpenGLRenderWidget::OpenGLRenderWidget(
    EngineUiAdapter* adapter,
    EngineConfig config,
    BackendFactory backendFactory,
    QWidget* parent)
    : QOpenGLWidget(parent),
      m_adapter(adapter),
      m_config(std::move(config)),
      m_backendFactory(std::move(backendFactory)) {
    setObjectName(QStringLiteral("openGLRenderWidget"));
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

OpenGLRenderWidget::~OpenGLRenderWidget() {
    shutdownEngineWithCurrentContext();
    if (m_stateWindow != nullptr) {
        m_stateWindow->removeEventFilter(this);
        m_stateWindow = nullptr;
    }
}

void OpenGLRenderWidget::setRefreshCallback(RefreshCallback callback) {
    m_refreshCallback = std::move(callback);
}

void OpenGLRenderWidget::recordFailure(const char* operation, const Error* error) noexcept {
    if (m_failureRecorded) {
        return;
    }
    m_failureRecorded = true;
    m_frameSchedulingEnabled = false;
    if (error != nullptr) {
        qWarning().noquote() << operation << QString::fromUtf8(error->userMessage.data(),
            static_cast<int>(error->userMessage.size()));
    } else {
        qWarning().noquote() << operation;
    }
}

RenderSize OpenGLRenderWidget::currentRenderSize() const noexcept {
    return makeRenderSize(width(), height(), devicePixelRatioF());
}

bool OpenGLRenderWidget::isRenderable() const noexcept {
    if (!isVisible() || !m_initialized || !m_frameSchedulingEnabled) {
        return false;
    }
    const QWidget* owner = window();
    return owner == nullptr || (owner->windowState() & Qt::WindowMinimized) == 0;
}

void OpenGLRenderWidget::requestFrameIfRenderable() {
    if (isRenderable()) {
        update();
    }
}

void OpenGLRenderWidget::initializeGL() {
    if (m_initializationAttempted) {
        return;
    }
    m_initializationAttempted = true;
    QOpenGLContext* current = QOpenGLContext::currentContext();
    if (current == nullptr || context() != current || current->surface() == nullptr) {
        recordFailure("OpenGLRenderWidget::initializeGL: no current context");
        return;
    }
    const QSurfaceFormat actual = current->format();
    if (actual.profile() != QSurfaceFormat::CoreProfile || actual.majorVersion() < 4 ||
        (actual.majorVersion() == 4 && actual.minorVersion() < 5)) {
        recordFailure("OpenGLRenderWidget::initializeGL: OpenGL 4.5 Core is required");
        return;
    }

    m_contextOperations = std::make_shared<QtRenderContextOperations>(*current, *current->surface());
    QObject::connect(current, &QOpenGLContext::aboutToBeDestroyed, this,
        [this] { shutdownEngineWithCurrentContext(); }, Qt::DirectConnection);
    if (!m_backendFactory) {
        m_backendFactory = [](std::shared_ptr<const IRenderContextOperations> operations) {
            return std::unique_ptr<IRenderBackend>(
                std::make_unique<opengl::OpenGLBackend>(std::move(operations)));
        };
    }

    try {
        std::unique_ptr<IRenderBackend> backend = m_backendFactory(m_contextOperations);
        if (backend == nullptr || m_adapter == nullptr) {
            recordFailure("OpenGLRenderWidget::initializeGL: backend or adapter is missing");
            return;
        }
        const Result<void> initialized = m_adapter->init(
            m_config, std::move(backend), std::make_unique<DisabledComputeBackend>());
        if (!initialized.hasValue()) {
            recordFailure("OpenGLRenderWidget::initializeGL: Engine initialization failed",
                          &initialized.error());
            return;
        }
    } catch (const std::exception& exception) {
        recordFailure(exception.what());
        return;
    } catch (...) {
        recordFailure("OpenGLRenderWidget::initializeGL: unknown initialization exception");
        return;
    }
    m_initialized = true;
    m_frameSchedulingEnabled = true;
    m_lastFrameTime = std::chrono::steady_clock::now();
}

void OpenGLRenderWidget::resizeGL(int width, int height) {
    if (!m_initialized || m_adapter == nullptr) {
        return;
    }
    const RenderSize size = makeRenderSize(width, height, devicePixelRatioF());
    const Result<void> resized = m_adapter->resize(size);
    if (!resized.hasValue()) {
        recordFailure("OpenGLRenderWidget::resizeGL: Engine resize failed", &resized.error());
        return;
    }
    requestFrameIfRenderable();
}

void OpenGLRenderWidget::paintGL() {
    if (!isRenderable() || m_adapter == nullptr) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    double delta = std::chrono::duration<double>(now - m_lastFrameTime).count();
    m_lastFrameTime = now;
    if (!std::isfinite(delta) || delta < 0.0) {
        delta = 0.0;
    }
    delta = std::min(delta, 1.0);
    const Result<void> updated = m_adapter->update(FrameInput{delta, currentRenderSize()});
    if (!updated.hasValue()) {
        recordFailure("OpenGLRenderWidget::paintGL: Engine update failed", &updated.error());
        return;
    }
    const Result<void> rendered = m_adapter->render();
    if (!rendered.hasValue()) {
        recordFailure("OpenGLRenderWidget::paintGL: Engine render failed", &rendered.error());
        return;
    }
    if (m_refreshCallback) {
        m_refreshCallback();
    }
    requestFrameIfRenderable();
}

void OpenGLRenderWidget::showEvent(QShowEvent* event) {
    QOpenGLWidget::showEvent(event);
    QWidget* owner = window();
    if (owner != nullptr && owner != this && owner != m_stateWindow) {
        if (m_stateWindow != nullptr) {
            m_stateWindow->removeEventFilter(this);
        }
        m_stateWindow = owner;
        m_stateWindow->installEventFilter(this);
    }
    updateSchedulingForWindowState();
    requestFrameIfRenderable();
}

void OpenGLRenderWidget::hideEvent(QHideEvent* event) {
    m_frameSchedulingEnabled = false;
    QOpenGLWidget::hideEvent(event);
}

void OpenGLRenderWidget::changeEvent(QEvent* event) {
    QOpenGLWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateSchedulingForWindowState();
        requestFrameIfRenderable();
    }
}

bool OpenGLRenderWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_stateWindow && event->type() == QEvent::WindowStateChange) {
        updateSchedulingForWindowState();
        requestFrameIfRenderable();
    }
    return QOpenGLWidget::eventFilter(watched, event);
}

void OpenGLRenderWidget::updateSchedulingForWindowState() {
    const QWidget* owner = window();
    const bool minimized = owner != nullptr &&
        (owner->windowState() & Qt::WindowMinimized) != 0;
    m_frameSchedulingEnabled = m_initialized && !m_failureRecorded &&
        isVisible() && !minimized;
}

void OpenGLRenderWidget::shutdownEngineWithCurrentContext() noexcept {
    if (m_shutdownAttempted || m_adapter == nullptr || !m_initialized) {
        return;
    }
    m_shutdownAttempted = true;
    makeCurrent();
    if (m_contextOperations != nullptr && m_contextOperations->isCurrent()) {
        m_adapter->shutdown();
        m_contextOperations->releaseCurrent();
    } else {
        recordFailure("OpenGLRenderWidget: Engine shutdown skipped without current context");
    }
    m_initialized = false;
    m_frameSchedulingEnabled = false;
}

} // namespace dzc