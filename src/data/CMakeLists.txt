add_library(dzc_data_core STATIC
    chunk/Bounds3d.cpp
    chunk/GridParameters.cpp
    chunk/GridCellKey.cpp
    chunk/GridBucketStore.cpp
    chunk/PointBatch.cpp
    chunk/IntensityQuantizer.cpp
    chunk/CoordinateLocalizer.cpp
    chunk/Chunk.cpp
    chunk/Dataset.cpp
    chunk/RelativeOrigin.cpp
    io/PointCloudReaderRegistry.cpp
    io/PointCloudLoadTask.cpp
    io/PointCloudLoadTask.h
)

target_compile_features(dzc_data_core PUBLIC cxx_std_17)
target_include_directories(dzc_data_core PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(dzc_data_core PUBLIC glm::glm)

# PCL is isolated in its concrete I/O implementation target.
add_subdirectory(io/pcl)
