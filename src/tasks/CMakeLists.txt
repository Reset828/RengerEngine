add_library(dzc_tasks STATIC
    Cancellation.cpp
    BoundedQueue.h
    CommandCoalescer.cpp
    CommandCoalescer.h
    ThreadConfiguration.cpp
    ThreadConfiguration.h
    TaskSystem.cpp
    TaskSystem.h
    TaskCompletionQueue.cpp
    TaskCompletion.h
    TaskCompletionQueue.h
)

target_compile_features(dzc_tasks PUBLIC cxx_std_17)

target_link_libraries(dzc_tasks PUBLIC dzc_engine_api)

target_include_directories(dzc_tasks PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)
