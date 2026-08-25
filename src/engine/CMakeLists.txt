add_library(dzc_engine_api INTERFACE)
target_compile_features(dzc_engine_api INTERFACE cxx_std_17)

target_include_directories(dzc_engine_api INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_library(dzc_engine_core STATIC
    Engine.cpp
    DatasetSession.cpp
    EngineCoordinator.cpp
    EngineQueues.cpp
    EngineCommand.cpp
    EngineStateMachine.cpp
    ${PROJECT_SOURCE_DIR}/src/scene/Scene.cpp
    ${PROJECT_SOURCE_DIR}/src/scene/FrustumCulling.cpp
    ${PROJECT_SOURCE_DIR}/src/scene/GridVisibilitySelector.cpp
    ${PROJECT_SOURCE_DIR}/src/camera/ViewFrustum.cpp
    ${PROJECT_SOURCE_DIR}/src/camera/OrbitCameraController.cpp
)

target_compile_features(dzc_engine_core PUBLIC cxx_std_17)
target_include_directories(dzc_engine_core PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)
