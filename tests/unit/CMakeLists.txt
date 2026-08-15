add_executable(dzc_engine_types_tests
    EngineTypesTests.cpp
    ${PROJECT_SOURCE_DIR}/src/engine/EngineTypes.cpp
)

target_compile_features(dzc_engine_types_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_engine_types_tests PRIVATE dzc_engine_api)

add_test(NAME dzc_engine_types COMMAND dzc_engine_types_tests)
add_executable(dzc_result_tests
    ResultTests.cpp
)

target_compile_features(dzc_result_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_result_tests PRIVATE dzc_engine_api)

add_test(NAME dzc_result COMMAND dzc_result_tests)
add_executable(dzc_engine_config_tests
    EngineConfigTests.cpp
)

target_compile_features(dzc_engine_config_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_engine_config_tests PRIVATE dzc_engine_api)

add_test(NAME dzc_engine_config COMMAND dzc_engine_config_tests)
add_executable(dzc_factory_contract_tests
    FactoryContractTests.cpp
)

target_compile_features(dzc_factory_contract_tests PRIVATE cxx_std_17)
target_include_directories(dzc_factory_contract_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(dzc_factory_contract_tests PRIVATE dzc_engine_api)

add_test(NAME dzc_factory_contract COMMAND dzc_factory_contract_tests)
add_executable(dzc_log_types_tests
    LogTypesTests.cpp
)

target_compile_features(dzc_log_types_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_log_types_tests PRIVATE dzc_diagnostics)

add_test(NAME dzc_log_types COMMAND dzc_log_types_tests)


add_executable(dzc_text_file_sink_tests
    TextFileSinkTests.cpp
)

target_compile_features(dzc_text_file_sink_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_text_file_sink_tests PRIVATE dzc_diagnostics)

add_test(NAME dzc_text_file_sink COMMAND dzc_text_file_sink_tests)

add_executable(dzc_log_rotation_tests
    LogRotationTests.cpp
)

target_compile_features(dzc_log_rotation_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_log_rotation_tests PRIVATE dzc_diagnostics)

add_test(NAME dzc_log_rotation COMMAND dzc_log_rotation_tests)
add_executable(dzc_logger_tests
    LoggerTests.cpp
)

target_compile_features(dzc_logger_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_logger_tests PRIVATE dzc_diagnostics)

add_test(NAME dzc_logger COMMAND dzc_logger_tests)

add_executable(dzc_frame_statistics_tests
    FrameStatisticsTests.cpp
)

target_compile_features(dzc_frame_statistics_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_frame_statistics_tests PRIVATE dzc_diagnostics)

add_test(NAME dzc_frame_statistics COMMAND dzc_frame_statistics_tests)

add_executable(dzc_metrics_registry_tests
    MetricsRegistryTests.cpp
)

target_compile_features(dzc_metrics_registry_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_metrics_registry_tests PRIVATE dzc_diagnostics)

add_test(NAME dzc_metrics_registry COMMAND dzc_metrics_registry_tests)
