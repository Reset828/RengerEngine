add_library(dzc_diagnostics STATIC
    MetricsRegistry.cpp
    PerformanceCsvWriter.cpp
    FrameStatistics.cpp
    Logger.cpp
    TextFileSink.cpp
    RotatingFileSink.cpp
)

target_compile_features(dzc_diagnostics PUBLIC cxx_std_17)

target_include_directories(dzc_diagnostics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)
