#include "EngineUiAdapter.h"
#include "MainWindow.h"
#if defined(DZC_HAS_OPENGL_RENDER_WIDGET)
#include "OpenGLRenderWidget.h"
#include "render/opengl/OpenGLBackend.h"
#endif

#include <QApplication>
#if defined(DZC_HAS_OPENGL_RENDER_WIDGET)
#include <QSurfaceFormat>
#endif

#include <memory>
#include <utility>


int main(int argc, char* argv[]) {
#if defined(DZC_HAS_OPENGL_RENDER_WIDGET)
    QSurfaceFormat format;
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    QSurfaceFormat::setDefaultFormat(format);
#endif

    QApplication application(argc, argv);

    dzc::EngineConfig config;
    dzc::Engine engine;
    dzc::EngineUiAdapter adapter(engine);
#if defined(DZC_HAS_OPENGL_RENDER_WIDGET)
    auto* renderWidget = new dzc::OpenGLRenderWidget(
        &adapter,
        config,
        [](std::shared_ptr<const dzc::IRenderContextOperations> operations) {
            return std::unique_ptr<dzc::IRenderBackend>(
                std::make_unique<dzc::opengl::OpenGLBackend>(std::move(operations)));
        });
    dzc::MainWindow window(&adapter, renderWidget);
#else
    dzc::MainWindow window(&adapter);
#endif
    window.show();

    return application.exec();
}
