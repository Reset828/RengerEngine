#include "scene/Scene.h"

#include <cmath>
#include <cstdint>

namespace dzc {
namespace {

constexpr std::uint32_t kInvalidPointSize = 1U;

bool isValidPointSize(float pointSize) noexcept {
    return std::isfinite(pointSize) && pointSize >= 1.0F && pointSize <= 64.0F;
}

} // namespace

Result<void> Scene::applyParameters(const SceneParameters& parameters) {
    if (!isValidPointSize(parameters.pointSize)) {
        return Result<void>::failure(Error{
            ErrorDomain::Configuration,
            kInvalidPointSize,
            "Invalid scene point size",
            "Scene point size must be finite and within [1.0, 64.0].",
            {}});
    }

    m_parameters = parameters;
    return Result<void>::success();
}

void Scene::setDataset(std::optional<DatasetId> datasetId) noexcept {
    m_datasetId = datasetId;
}

void Scene::clearDataset() noexcept {
    m_datasetId.reset();
}

SceneFrameInput Scene::frameInput() const {
    return SceneFrameInput{m_datasetId, m_parameters};
}

} // namespace dzc