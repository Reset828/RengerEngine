find_package(Qt5 5.15.19 CONFIG REQUIRED COMPONENTS Core Gui Widgets)
if(DZC_ENABLE_OPENGL)
    find_package(Qt5 5.15.19 CONFIG REQUIRED COMPONENTS OpenGL)
endif()

add_executable(dzc_qt_application_smoke
    QtApplicationSmokeTests.cpp
)
target_compile_features(dzc_qt_application_smoke PRIVATE cxx_std_17)
target_link_libraries(dzc_qt_application_smoke PRIVATE Qt5::Widgets)
add_test(NAME dzc_qt_application_smoke COMMAND dzc_qt_application_smoke)
set_tests_properties(dzc_qt_application_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "ui;qt;qt001")

set(DZC_MAIN_WINDOW_SOURCES
    ${PROJECT_SOURCE_DIR}/src/app/MainWindow.cpp
    ${PROJECT_SOURCE_DIR}/src/app/StatusPresenter.cpp
    ${PROJECT_SOURCE_DIR}/src/app/LogPanelModel.cpp
    ${PROJECT_SOURCE_DIR}/src/app/EngineUiAdapter.cpp
    ${PROJECT_SOURCE_DIR}/src/app/CommandLineOptions.cpp
    ${PROJECT_SOURCE_DIR}/src/app/SettingsController.cpp
)

function(dzc_configure_ui_target target_name)
    target_compile_features(${target_name} PRIVATE cxx_std_17)
    target_include_directories(${target_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/app
    )
    target_link_libraries(${target_name} PRIVATE
        dzc_engine_core
        dzc_engine_api
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
    )
endfunction()

add_executable(dzc_main_window_smoke MainWindowSmokeTests.cpp ${DZC_MAIN_WINDOW_SOURCES})
dzc_configure_ui_target(dzc_main_window_smoke)
add_test(NAME dzc_main_window_smoke COMMAND dzc_main_window_smoke)
set_tests_properties(dzc_main_window_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "ui;qt;qt004")

add_executable(dzc_dataset_load_ui DatasetLoadUiTests.cpp ${DZC_MAIN_WINDOW_SOURCES})
dzc_configure_ui_target(dzc_dataset_load_ui)
add_test(NAME dzc_dataset_load_ui COMMAND dzc_dataset_load_ui)
set_tests_properties(dzc_dataset_load_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "ui;app;qt;qt006")

add_executable(dzc_render_parameters_ui RenderParametersUiTests.cpp ${DZC_MAIN_WINDOW_SOURCES})
dzc_configure_ui_target(dzc_render_parameters_ui)
add_test(NAME dzc_render_parameters_ui COMMAND dzc_render_parameters_ui)
set_tests_properties(dzc_render_parameters_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "ui;app;qt;qt007")

add_executable(dzc_status_ui StatusUiTests.cpp ${DZC_MAIN_WINDOW_SOURCES})
dzc_configure_ui_target(dzc_status_ui)
add_test(NAME dzc_status_ui COMMAND dzc_status_ui)
set_tests_properties(dzc_status_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "ui;app;qt;qt008")

if(DZC_ENABLE_OPENGL)
    add_executable(dzc_opengl_render_widget OpenGLRenderWidgetTests.cpp
        ${DZC_MAIN_WINDOW_SOURCES}
        ${PROJECT_SOURCE_DIR}/src/app/OpenGLRenderWidget.cpp
    )
    dzc_configure_ui_target(dzc_opengl_render_widget)
    target_compile_definitions(dzc_opengl_render_widget PRIVATE DZC_HAS_OPENGL_RENDER_WIDGET=1)
    target_include_directories(dzc_opengl_render_widget PRIVATE ${DZC_GLAD_INCLUDE_DIR})
    target_link_libraries(dzc_opengl_render_widget PRIVATE
        dzc_render_opengl
        Qt5::OpenGL
    )
    add_test(NAME dzc_opengl_render_widget COMMAND dzc_opengl_render_widget)
    set_tests_properties(dzc_opengl_render_widget PROPERTIES
        SKIP_RETURN_CODE 77
        LABELS "ui;app;qt;qt009"
    )
endif()
