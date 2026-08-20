#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace dzc {

struct CameraState final {
    glm::dvec3 position{0.0};
    glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
    double verticalFieldOfViewRadians{0.0};
    double nearPlane{0.0};
    double farPlane{0.0};
};

struct CameraMatrices final {
    glm::mat4 view{1.0F};
    glm::mat4 projection{1.0F};
    glm::dvec3 cameraOrigin{0.0};
};

} // namespace dzc