find_package(Qt5 5.15.19 CONFIG REQUIRED COMPONENTS Widgets)

set(DZC_APP_SOURCES
    main.cpp
    CommandLineOptions.cpp
    SettingsController.cpp
    EngineUiAdapter.cpp
    MainWindow.cpp
    StatusPresenter.cpp
    LogPanelModel.cpp
)

if(DZC_ENABLE_OPENGL)
    find_package(Qt5 5.15.19 CONFIG REQUIRED COMPONENTS OpenGL)
    list(APPEND DZC_APP_SOURCES OpenGLRenderWidget.cpp)
endif()

add_executable(dzc_app ${DZC_APP_SOURCES})

target_compile_features(dzc_app PRIVATE cxx_std_17)
target_include_directories(dzc_app PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/app)
target_link_libraries(dzc_app PRIVATE Qt5::Widgets)

if(DZC_ENABLE_OPENGL)
    target_compile_definitions(dzc_app PRIVATE DZC_HAS_OPENGL_RENDER_WIDGET=1)
    target_include_directories(dzc_app PRIVATE ${DZC_GLAD_INCLUDE_DIR})
    target_link_libraries(dzc_app PRIVATE Qt5::OpenGL dzc_render_opengl)
endif()
