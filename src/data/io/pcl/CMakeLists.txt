find_package(PCL CONFIG REQUIRED COMPONENTS io)

if(NOT TARGET pcl_io)
    message(FATAL_ERROR "PCL io component did not provide the pcl_io target")
endif()

add_library(dzc_data_pcl STATIC
    PclTargetAnchor.cpp
    PcdReader.cpp
    PlyReader.cpp
)

target_compile_features(dzc_data_pcl PUBLIC cxx_std_17)
target_include_directories(dzc_data_pcl PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PCL_INCLUDE_DIRS}
)
target_compile_definitions(dzc_data_pcl PRIVATE ${PCL_DEFINITIONS})
target_link_libraries(dzc_data_pcl
    PUBLIC dzc_data_core
    PRIVATE pcl_io
)
