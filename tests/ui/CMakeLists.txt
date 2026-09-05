find_package(Qt5 5.15.19 CONFIG REQUIRED COMPONENTS Core Gui Widgets)

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

add_executable(dzc_main_window_smoke MainWindowSmokeTests.cpp ${DZC_MAIN_WINDOW_SOURCES})
target_compile_features(dzc_main_window_smoke PRIVATE cxx_std_17)
target_include_directories(dzc_main_window_smoke PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/app)
target_link_libraries(dzc_main_window_smoke PRIVATE dzc_engine_core dzc_engine_api Qt5::Core Qt5::Gui Qt5::Widgets)
add_test(NAME dzc_main_window_smoke COMMAND dzc_main_window_smoke)
set_tests_properties(dzc_main_window_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "ui;qt;qt004")

add_executable(dzc_dataset_load_ui DatasetLoadUiTests.cpp ${DZC_MAIN_WINDOW_SOURCES})
target_compile_features(dzc_dataset_load_ui PRIVATE cxx_std_17)
target_include_directories(dzc_dataset_load_ui PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/app)
target_link_libraries(dzc_dataset_load_ui PRIVATE dzc_engine_core dzc_engine_api Qt5::Core Qt5::Gui Qt5::Widgets)
add_test(NAME dzc_dataset_load_ui COMMAND dzc_dataset_load_ui)
set_tests_properties(dzc_dataset_load_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "ui;app;qt;qt006")

add_executable(dzc_render_parameters_ui RenderParametersUiTests.cpp ${DZC_MAIN_WINDOW_SOURCES})
target_compile_features(dzc_render_parameters_ui PRIVATE cxx_std_17)
target_include_directories(dzc_render_parameters_ui PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/app)
target_link_libraries(dzc_render_parameters_ui PRIVATE dzc_engine_core dzc_engine_api Qt5::Core Qt5::Gui Qt5::Widgets)
add_test(NAME dzc_render_parameters_ui COMMAND dzc_render_parameters_ui)
set_tests_properties(dzc_render_parameters_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "ui;app;qt;qt007")

add_executable(dzc_status_ui StatusUiTests.cpp ${DZC_MAIN_WINDOW_SOURCES})
target_compile_features(dzc_status_ui PRIVATE cxx_std_17)
target_include_directories(dzc_status_ui PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/app)
target_link_libraries(dzc_status_ui PRIVATE dzc_engine_core dzc_engine_api Qt5::Core Qt5::Gui Qt5::Widgets)
add_test(NAME dzc_status_ui COMMAND dzc_status_ui)
set_tests_properties(dzc_status_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "ui;app;qt;qt008")
