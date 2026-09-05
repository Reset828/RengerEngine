find_package(Qt5 5.15.19 CONFIG REQUIRED COMPONENTS Widgets)

add_executable(dzc_app
    main.cpp
    CommandLineOptions.cpp
    SettingsController.cpp
    EngineUiAdapter.cpp
    MainWindow.cpp
    StatusPresenter.cpp
    LogPanelModel.cpp
)

target_compile_features(dzc_app PRIVATE cxx_std_17)
target_link_libraries(dzc_app PRIVATE Qt5::Widgets)
