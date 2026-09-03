find_package(Qt5 5.15.19 CONFIG REQUIRED COMPONENTS Widgets)

add_executable(dzc_qt_application_smoke
    QtApplicationSmokeTests.cpp
)

target_compile_features(dzc_qt_application_smoke PRIVATE cxx_std_17)
target_link_libraries(dzc_qt_application_smoke PRIVATE Qt5::Widgets)

add_test(NAME dzc_qt_application_smoke
    COMMAND dzc_qt_application_smoke)
set_tests_properties(dzc_qt_application_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "ui;qt;qt001")