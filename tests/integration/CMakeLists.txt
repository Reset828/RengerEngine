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