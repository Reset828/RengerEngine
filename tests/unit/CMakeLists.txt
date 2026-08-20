add_executable(dzc_engine_types_tests
    EngineTypesTests.cpp
    ${PROJECT_SOURCE_DIR}/src/engine/EngineTypes.cpp
)

target_compile_features(dzc_engine_types_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_engine_types_tests PRIVATE dzc_engine_api)

add_test(NAME dzc_engine_types COMMAND dzc_engine_types_tests)

add_executable(dzc_engine_command_tests
    EngineCommandTests.cpp
    ${PROJECT_SOURCE_DIR}/src/engine/EngineCommand.cpp
)

target_compile_features(dzc_engine_command_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_engine_command_tests PRIVATE dzc_engine_api)

add_test(NAME dzc_engine_command COMMAND dzc_engine_command_tests)

add_executable(dzc_engine_event_tests
    EngineEventTests.cpp
)

target_compile_features(dzc_engine_event_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_engine_event_tests PRIVATE dzc_engine_api)

add_test(NAME dzc_engine_event COMMAND dzc_engine_event_tests)

add_executable(dzc_engine_snapshot_tests
    EngineSnapshotTests.cpp
)

target_compile_features(dzc_engine_snapshot_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_engine_snapshot_tests PRIVATE dzc_engine_api)

add_test(NAME dzc_engine_snapshot COMMAND dzc_engine_snapshot_tests)

add_executable(dzc_engine_state_machine_tests
    EngineStateMachineTests.cpp
)

target_compile_features(dzc_engine_state_machine_tests PRIVATE cxx_std_17)
target_include_directories(dzc_engine_state_machine_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_engine_state_machine_tests PRIVATE dzc_engine_core dzc_engine_api)

add_test(NAME dzc_engine_state_machine COMMAND dzc_engine_state_machine_tests)
add_executable(dzc_scene_tests
    SceneTests.cpp
)

target_compile_features(dzc_scene_tests PRIVATE cxx_std_17)
target_include_directories(dzc_scene_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_scene_tests PRIVATE dzc_engine_core dzc_engine_api)

add_test(NAME dzc_scene COMMAND dzc_scene_tests)
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

add_executable(dzc_performance_csv_writer_tests
    PerformanceCsvWriterTests.cpp
)

target_compile_features(dzc_performance_csv_writer_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_performance_csv_writer_tests PRIVATE dzc_diagnostics)

add_test(NAME dzc_performance_csv_writer COMMAND dzc_performance_csv_writer_tests)
add_executable(dzc_performance_summary_writer_tests
    PerformanceSummaryWriterTests.cpp
)

target_compile_features(dzc_performance_summary_writer_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_performance_summary_writer_tests PRIVATE dzc_diagnostics)

add_test(NAME dzc_performance_summary_writer COMMAND dzc_performance_summary_writer_tests)
add_executable(dzc_cancellation_tests
    CancellationTests.cpp
)

target_compile_features(dzc_cancellation_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_cancellation_tests PRIVATE dzc_tasks)

add_test(NAME dzc_cancellation COMMAND dzc_cancellation_tests)
add_executable(dzc_bounded_queue_tests
    BoundedQueueTests.cpp
)

target_compile_features(dzc_bounded_queue_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_bounded_queue_tests PRIVATE dzc_tasks)

add_test(NAME dzc_bounded_queue COMMAND dzc_bounded_queue_tests)

add_executable(dzc_command_coalescer_tests
    CommandCoalescerTests.cpp
)

target_compile_features(dzc_command_coalescer_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_command_coalescer_tests PRIVATE dzc_tasks)

add_test(NAME dzc_command_coalescer COMMAND dzc_command_coalescer_tests)

add_executable(dzc_thread_configuration_tests
    ThreadConfigurationTests.cpp
)

target_compile_features(dzc_thread_configuration_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_thread_configuration_tests PRIVATE dzc_tasks)

add_test(NAME dzc_thread_configuration COMMAND dzc_thread_configuration_tests)
add_executable(dzc_task_system_tests
    TaskSystemTests.cpp
)

target_compile_features(dzc_task_system_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_task_system_tests PRIVATE dzc_tasks)

add_test(NAME dzc_task_system COMMAND dzc_task_system_tests)
add_executable(dzc_task_completion_tests
    TaskCompletionTests.cpp
)

target_compile_features(dzc_task_completion_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_task_completion_tests PRIVATE dzc_tasks)

add_test(NAME dzc_task_completion COMMAND dzc_task_completion_tests)
add_executable(dzc_concurrency_gate_tests
    ConcurrencyGateTests.cpp
)

target_compile_features(dzc_concurrency_gate_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_concurrency_gate_tests PRIVATE dzc_tasks)

add_test(NAME dzc_concurrency_gate COMMAND dzc_concurrency_gate_tests)
add_executable(dzc_backpressure_tests
    BackpressureTests.cpp
)

target_compile_features(dzc_backpressure_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_backpressure_tests PRIVATE dzc_tasks)

add_test(NAME dzc_backpressure COMMAND dzc_backpressure_tests)
add_executable(dzc_task_system_shutdown_tests
    TaskSystemShutdownTests.cpp
)

target_compile_features(dzc_task_system_shutdown_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_task_system_shutdown_tests PRIVATE dzc_tasks)

add_test(NAME dzc_task_system_shutdown COMMAND dzc_task_system_shutdown_tests)

add_executable(dzc_engine_public_api_tests
    EnginePublicApiTests.cpp
)

target_compile_features(dzc_engine_public_api_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_engine_public_api_tests PRIVATE dzc_engine_core dzc_engine_api)

add_test(NAME dzc_engine_public_api COMMAND dzc_engine_public_api_tests)
add_executable(dzc_engine_queue_policy_tests
    EngineQueuePolicyTests.cpp
)

target_compile_features(dzc_engine_queue_policy_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_engine_queue_policy_tests PRIVATE dzc_engine_core dzc_engine_api)

add_test(NAME dzc_engine_queue_policy COMMAND dzc_engine_queue_policy_tests)

add_executable(dzc_snapshot_publication_tests
    SnapshotPublicationTests.cpp
)

target_compile_features(dzc_snapshot_publication_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_snapshot_publication_tests PRIVATE dzc_engine_core dzc_engine_api)

add_test(NAME dzc_snapshot_publication COMMAND dzc_snapshot_publication_tests)
add_executable(dzc_engine_coordinator_tests
    EngineCoordinatorTests.cpp
)

target_compile_features(dzc_engine_coordinator_tests PRIVATE cxx_std_17)
target_link_libraries(dzc_engine_coordinator_tests PRIVATE dzc_engine_core dzc_engine_api)

add_test(NAME dzc_engine_coordinator COMMAND dzc_engine_coordinator_tests)
add_executable(dzc_dataset_replacement_tests
    DatasetReplacementTests.cpp
)

target_compile_features(dzc_dataset_replacement_tests PRIVATE cxx_std_17)
target_include_directories(dzc_dataset_replacement_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_dataset_replacement_tests PRIVATE dzc_engine_core dzc_engine_api)

add_test(NAME dzc_dataset_replacement COMMAND dzc_dataset_replacement_tests)

add_executable(dzc_engine_lifecycle_rollback_tests
    EngineLifecycleRollbackTests.cpp
)

target_compile_features(dzc_engine_lifecycle_rollback_tests PRIVATE cxx_std_17)
target_include_directories(dzc_engine_lifecycle_rollback_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_engine_lifecycle_rollback_tests PRIVATE dzc_engine_core dzc_engine_api)

add_test(NAME dzc_engine_lifecycle_rollback COMMAND dzc_engine_lifecycle_rollback_tests)

add_executable(dzc_point_attributes_tests
    PointAttributesTests.cpp
)

target_compile_features(dzc_point_attributes_tests PRIVATE cxx_std_17)
target_include_directories(dzc_point_attributes_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_point_attributes_tests PRIVATE dzc_data_core dzc_engine_api)

add_test(NAME dzc_point_attributes COMMAND dzc_point_attributes_tests)

add_executable(dzc_bounds3d_tests
    Bounds3dTests.cpp
)

target_compile_features(dzc_bounds3d_tests PRIVATE cxx_std_17)
target_include_directories(dzc_bounds3d_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_bounds3d_tests PRIVATE dzc_data_core dzc_engine_api)

add_test(NAME dzc_bounds3d COMMAND dzc_bounds3d_tests)

add_executable(dzc_point_batch_tests
    PointBatchTests.cpp
)

target_compile_features(dzc_point_batch_tests PRIVATE cxx_std_17)
target_include_directories(dzc_point_batch_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_point_batch_tests PRIVATE dzc_data_core dzc_engine_api)

add_test(NAME dzc_point_batch COMMAND dzc_point_batch_tests)

add_executable(dzc_intensity_quantizer_tests
    IntensityQuantizerTests.cpp
)

target_compile_features(dzc_intensity_quantizer_tests PRIVATE cxx_std_17)
target_include_directories(dzc_intensity_quantizer_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_intensity_quantizer_tests PRIVATE dzc_data_core dzc_engine_api)

add_test(NAME dzc_intensity_quantizer COMMAND dzc_intensity_quantizer_tests)
