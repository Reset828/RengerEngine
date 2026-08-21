cmake_minimum_required(VERSION 3.21)

project(DzcRenderEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DzcOptions.cmake)

add_subdirectory(src)

if(DZC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
    add_subdirectory(tests/performance)
    add_test(
        NAME dzc_target_boundary
        COMMAND ${CMAKE_COMMAND}
            -DDZC_TARGET_MANIFEST=${CMAKE_BINARY_DIR}/DzcTargetManifest.cmake
            -P ${CMAKE_SOURCE_DIR}/tests/cmake/TargetBoundaryCheck.cmake
    )
    include(${CMAKE_CURRENT_SOURCE_DIR}/CTestConfig.cmake)
endif()
