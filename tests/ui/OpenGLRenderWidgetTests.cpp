#include "OpenGLRenderWidget.h"
#include "EngineUiAdapter.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include <cassert>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace {

class RecordingBackend final : public dzc::IRenderBackend {
public:
    dzc::Result<void> init(const dzc::RenderBackendConfig&) override {
        initThread = std::this_thread::get_id();
        initialized = true;
        return dzc::Result<void>::success();
    }

    dzc::Result<void> upload(const dzc::UploadBatch&) override {
        return dzc::Result<void>::success();
    }

    dzc::Result<void> update(const dzc::RenderFrame&) override {
        updateThread = std::this_thread::get_id();
        ++updateCount;
        return dzc::Result<void>::success();
    }

    dzc::Result<void> render() override {
        renderThread = std::this_thread::get_id();
        ++renderCount;
        return dzc::Result<void>::success();
    }

    dzc::Result<void> resize(const dzc::RenderSize& value) override {
        resizeThread = std::this_thread::get_id();
        lastResize = value;
        ++resizeCount;
        return dzc::Result<void>::success();
    }

    void release(dzc::ChunkId) noexcept override {}

    void shutdown() noexcept override {
        shutdownThread = std::this_thread::get_id();
        shutdownCalled = true;
    }

    bool initialized{false};
    bool shutdownCalled{false};
    std::size_t updateCount{0U};
    std::size_t renderCount{0U};
    std::size_t resizeCount{0U};
    dzc::RenderSize lastResize{};
    std::thread::id initThread;
    std::thread::id updateThread;
    std::thread::id renderThread;
    std::thread::id resizeThread;
    std::thread::id shutdownThread;
};

class FakePort final : public dzc::IEngineUiPort {
public:
    dzc::Result<void> enqueueCommand(dzc::EngineCommand command) override {
        commands.push_back(std::move(command));
        return dzc::Result<void>::success();
    }

    std::shared_ptr<const dzc::EngineSnapshot> getSnapshot() const override {
        return snapshot;
    }

    std::vector<dzc::EngineEvent> pollEvents() override {
        return {};
    }

    dzc::Result<void> init(
        const dzc::EngineConfig&,
        std::unique_ptr<dzc::IRenderBackend> renderBackend,
        std::unique_ptr<dzc::IComputeBackend>) override {
        ++initCalls;
        backend = std::move(renderBackend);
        if (backend == nullptr) {
            return dzc::Result<void>::failure(dzc::Error{
                dzc::ErrorDomain::Configuration, 1U, "Missing backend", "Missing render backend", "FakePort"});
        }
        return backend->init(dzc::RenderBackendConfig{dzc::RenderSize{1U, 1U, 1.0F}});
    }

    dzc::Result<void> update(const dzc::FrameInput& value) override {
        ++updateCalls;
        lastInput = value;
        if (backend != nullptr) {
            dzc::RenderFrame frame;
                frame.size = value.renderSize;
                return backend->update(frame);
        }
        return dzc::Result<void>::success();
    }

    dzc::Result<void> render() override {
        ++renderCalls;
        return backend == nullptr ? dzc::Result<void>::success() : backend->render();
    }

    dzc::Result<void> resize(const dzc::RenderSize& value) override {
        lastResize = value;
        return backend == nullptr ? dzc::Result<void>::success() : backend->resize(value);
    }

    void shutdown() noexcept override {
        ++shutdownCalls;
        if (backend != nullptr) {
            backend->shutdown();
        }
    }

    std::vector<dzc::EngineCommand> commands;
    std::shared_ptr<const dzc::EngineSnapshot> snapshot = std::make_shared<dzc::EngineSnapshot>();
    std::unique_ptr<dzc::IRenderBackend> backend;
    dzc::FrameInput lastInput{};
    dzc::RenderSize lastResize{};
    std::size_t initCalls{0U};
    std::size_t updateCalls{0U};
    std::size_t renderCalls{0U};
    std::size_t shutdownCalls{0U};
};

int testOpenGlHostLifecycle() {
    FakePort port;
    dzc::EngineUiAdapter adapter(port);
    RecordingBackend* recordingBackend = nullptr;
    dzc::OpenGLRenderWidget widget(
        &adapter,
        dzc::EngineConfig{},
        [&recordingBackend](std::shared_ptr<const dzc::IRenderContextOperations>) {
            auto backend = std::make_unique<RecordingBackend>();
            recordingBackend = backend.get();
            return std::unique_ptr<dzc::IRenderBackend>(std::move(backend));
        });
    widget.resize(320, 200);
    widget.show();
    QCoreApplication::processEvents();

    if (!widget.initializationSucceeded()) {
        // Return CTest's conventional skipped status only when no usable
        // context was created. A valid context with a real initialization
        // failure must remain a test failure rather than being masked.
        return widget.context() == nullptr || !widget.isValid() ? 77 : 1;
    }

    assert(recordingBackend != nullptr);
    assert(widget.objectName() == QStringLiteral("openGLRenderWidget"));
    assert(port.initCalls == 1U);
    assert(recordingBackend->initialized);
    assert(recordingBackend->initThread == std::this_thread::get_id());
    assert(port.lastResize.width > 0U);
    assert(port.lastResize.height > 0U);
    assert(widget.frameSchedulingEnabled());

    widget.update();
    QCoreApplication::processEvents();
    widget.hide();
    assert(!widget.frameSchedulingEnabled());
    widget.show();
    assert(widget.frameSchedulingEnabled());

    widget.showMinimized();
    QCoreApplication::processEvents();
    assert(!widget.frameSchedulingEnabled());
    widget.showNormal();
    widget.show();
    QCoreApplication::processEvents();
    assert(widget.frameSchedulingEnabled());
    widget.hide();

    assert(port.updateCalls > 0U);
    assert(port.renderCalls == port.updateCalls);
    assert(recordingBackend->updateCount == port.updateCalls);
    assert(recordingBackend->renderCount == port.renderCalls);
    assert(port.lastInput.deltaSeconds >= 0.0);
    assert(port.lastInput.deltaSeconds <= 1.0);
    assert(port.lastInput.renderSize == port.lastResize);
    assert(recordingBackend->updateThread == std::this_thread::get_id());
    assert(recordingBackend->renderThread == std::this_thread::get_id());
    return 0;
}

void testMainWindowUsesInjectedRenderWidget() {
    FakePort port;
    dzc::EngineUiAdapter adapter(port);
    auto* widget = new dzc::OpenGLRenderWidget(&adapter, dzc::EngineConfig{},
        [](std::shared_ptr<const dzc::IRenderContextOperations>) {
            return std::unique_ptr<dzc::IRenderBackend>(std::make_unique<RecordingBackend>());
        });
    dzc::MainWindow window(&adapter, widget);
    assert(window.centralWidget() == widget);
    assert(window.findChild<QWidget*>(QStringLiteral("renderViewPlaceholder")) == nullptr);
    assert(window.findChild<QWidget*>(QStringLiteral("openGLRenderWidget")) == widget);
}

} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication application(argc, argv);
    const int lifecycleResult = testOpenGlHostLifecycle();
    if (lifecycleResult != 0) {
        return lifecycleResult;
    }
    testMainWindowUsesInjectedRenderWidget();
    return 0;
}
