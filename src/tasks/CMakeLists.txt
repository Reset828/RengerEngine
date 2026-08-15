add_library(dzc_tasks STATIC
    Cancellation.cpp
)

target_compile_features(dzc_tasks PUBLIC cxx_std_17)

target_include_directories(dzc_tasks PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)