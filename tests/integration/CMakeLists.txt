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
