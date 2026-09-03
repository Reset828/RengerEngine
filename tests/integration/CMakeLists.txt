add_executable(dzc_pcd_reader_tests
    PcdReaderTests.cpp
)

target_compile_features(dzc_pcd_reader_tests PRIVATE cxx_std_17)
target_include_directories(dzc_pcd_reader_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_pcd_reader_tests PRIVATE
    dzc_data_pcl
    dzc_data_core
    dzc_tasks
    dzc_engine_api
)

add_test(NAME dzc_pcd_reader COMMAND dzc_pcd_reader_tests)
add_executable(dzc_pcd_batch_tests
    PcdBatchTests.cpp
)

target_compile_features(dzc_pcd_batch_tests PRIVATE cxx_std_17)
target_include_directories(dzc_pcd_batch_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_pcd_batch_tests PRIVATE
    dzc_data_pcl
    dzc_data_core
    dzc_tasks
    dzc_engine_api
)

add_test(NAME dzc_pcd_batch COMMAND dzc_pcd_batch_tests)
add_executable(dzc_ply_reader_tests
    PlyReaderTests.cpp
)

target_compile_features(dzc_ply_reader_tests PRIVATE cxx_std_17)
target_include_directories(dzc_ply_reader_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_ply_reader_tests PRIVATE
    dzc_data_pcl
    dzc_data_core
    dzc_tasks
    dzc_engine_api
)

add_test(NAME dzc_ply_reader COMMAND dzc_ply_reader_tests)
add_executable(dzc_ply_batch_tests
    PlyBatchTests.cpp
)

target_compile_features(dzc_ply_batch_tests PRIVATE cxx_std_17)
target_include_directories(dzc_ply_batch_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_ply_batch_tests PRIVATE
    dzc_data_pcl
    dzc_data_core
    dzc_tasks
    dzc_engine_api
)

add_test(NAME dzc_ply_batch COMMAND dzc_ply_batch_tests)

add_executable(dzc_point_cloud_load_task_tests
    PointCloudLoadTaskTests.cpp
)

target_compile_features(dzc_point_cloud_load_task_tests PRIVATE cxx_std_17)
target_include_directories(dzc_point_cloud_load_task_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_point_cloud_load_task_tests PRIVATE
    dzc_data_core
    dzc_tasks
    dzc_engine_api
)

add_test(NAME dzc_point_cloud_load_task COMMAND dzc_point_cloud_load_task_tests)
add_executable(dzc_reader_progress_tests
    ReaderProgressTests.cpp
)

target_compile_features(dzc_reader_progress_tests PRIVATE cxx_std_17)
target_include_directories(dzc_reader_progress_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_reader_progress_tests PRIVATE
    dzc_data_core
    dzc_tasks
    dzc_engine_api
)

add_test(NAME dzc_reader_progress COMMAND dzc_reader_progress_tests)
add_executable(dzc_grid_visibility_tests
    GridVisibilityTests.cpp
)

target_compile_features(dzc_grid_visibility_tests PRIVATE cxx_std_17)
target_include_directories(dzc_grid_visibility_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_grid_visibility_tests PRIVATE
    dzc_engine_core
    dzc_data_core
    dzc_engine_api
)

add_test(NAME dzc_grid_visibility COMMAND dzc_grid_visibility_tests)
if(DZC_ENABLE_OPENGL)
    add_executable(dzc_opengl_render_pipeline_tests
        OpenGLRenderPipelineTests.cpp
    )

    target_compile_features(dzc_opengl_render_pipeline_tests PRIVATE cxx_std_17)
    target_include_directories(dzc_opengl_render_pipeline_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/render/opengl
    )
    target_link_libraries(dzc_opengl_render_pipeline_tests PRIVATE
        dzc_render_opengl
        dzc_data_core
    )

    add_test(NAME dzc_opengl_render_pipeline
        COMMAND dzc_opengl_render_pipeline_tests)
    set_tests_properties(dzc_opengl_render_pipeline PROPERTIES
        LABELS "integration;graphics;opengl;gl012")

    add_test(NAME dzc_opengl_render_pipeline_real_context
        COMMAND dzc_opengl_render_pipeline_tests --real-context)
    set_tests_properties(dzc_opengl_render_pipeline_real_context PROPERTIES
        LABELS "integration;graphics;opengl;gl012;skipped"
        SKIP_RETURN_CODE 77)
endif()
