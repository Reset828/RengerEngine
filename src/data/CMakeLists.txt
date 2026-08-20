find_package(glm CONFIG REQUIRED)

add_library(dzc_data_core STATIC
    chunk/Bounds3d.cpp
    chunk/PointBatch.cpp
)

target_compile_features(dzc_data_core PUBLIC cxx_std_17)
target_include_directories(dzc_data_core PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_data_core PUBLIC glm::glm)

dzc_add_interface_target(dzc_data_pcl)

# PCL discovery and linking are intentionally deferred until the concrete
# point-cloud I/O implementation. This is the only target allowed to link PCL.
