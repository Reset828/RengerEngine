#include "scene/Scene.h"

#include <cassert>
#include <limits>
#include <optional>

namespace {

using dzc::ColorRgba;
using dzc::DatasetId;
using dzc::ErrorDomain;
using dzc::RenderSize;
using dzc::Scene;
using dzc::SceneParameters;
using dzc::ShadingMode;

void assertSuccess(const dzc::Result<void>& result) {
    assert(result.hasValue());
}

void assertInvalidPointSize(const dzc::Result<void>& result) {
    assert(!result.hasValue());
    assert(result.error().domain == ErrorDomain::Configuration);
    assert(result.error().code == 1U);
    assert(result.error().userMessage == "Invalid scene point size");
}

void testDefaults() {
    Scene scene;
    const auto input = scene.frameInput();
    assert(!input.datasetId.has_value());
    assert(input.parameters.pointSize == 1.0F);
    assert(input.parameters.shadingMode == ShadingMode::OriginalColor);
    assert(input.parameters.fixedColor == ColorRgba{});
    assert(input.parameters.backgroundColor == ColorRgba{});
    assert(input.parameters.renderSize == RenderSize{});
}

void testParameterReplacementAndFrameInput() {
    Scene scene;
    SceneParameters parameters;
    parameters.pointSize = 12.0F;
    parameters.shadingMode = ShadingMode::FixedColor;
    parameters.fixedColor = ColorRgba{0.1F, 0.2F, 0.3F, 1.0F};
    parameters.backgroundColor = ColorRgba{0.4F, 0.5F, 0.6F, 1.0F};
    parameters.renderSize = RenderSize{1920U, 1080U, 2.0F};

    assertSuccess(scene.applyParameters(parameters));
    scene.setDataset(DatasetId{42U});
    const auto input = scene.frameInput();
    assert(input.datasetId == std::optional<DatasetId>(DatasetId{42U}));
    assert(input.parameters.pointSize == 12.0F);
    assert(input.parameters.shadingMode == ShadingMode::FixedColor);
    assert(input.parameters.fixedColor == parameters.fixedColor);
    assert(input.parameters.backgroundColor == parameters.backgroundColor);
    assert(input.parameters.renderSize == parameters.renderSize);

    scene.clearDataset();
    assert(!scene.frameInput().datasetId.has_value());
}

void testInvalidPointSizePreservesParameters() {
    Scene scene;
    SceneParameters valid;
    valid.pointSize = 8.0F;
    valid.shadingMode = ShadingMode::Intensity;
    valid.renderSize = RenderSize{640U, 480U, 1.0F};
    assertSuccess(scene.applyParameters(valid));
    const auto before = scene.frameInput();

    const float invalidValues[] = {
        0.999F,
        64.001F,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()};
    for (const float pointSize : invalidValues) {
        SceneParameters invalid = valid;
        invalid.pointSize = pointSize;
        const auto result = scene.applyParameters(invalid);
        assertInvalidPointSize(result);
        const auto after = scene.frameInput();
        assert(after.datasetId == before.datasetId);
        assert(after.parameters.pointSize == before.parameters.pointSize);
        assert(after.parameters.shadingMode == before.parameters.shadingMode);
        assert(after.parameters.fixedColor == before.parameters.fixedColor);
        assert(after.parameters.backgroundColor == before.parameters.backgroundColor);
        assert(after.parameters.renderSize == before.parameters.renderSize);
    }
}

void testPointSizeBoundaries() {
    Scene scene;
    SceneParameters parameters;
    parameters.pointSize = 1.0F;
    assertSuccess(scene.applyParameters(parameters));
    parameters.pointSize = 64.0F;
    assertSuccess(scene.applyParameters(parameters));
}

} // namespace

int main() {
    testDefaults();
    testParameterReplacementAndFrameInput();
    testInvalidPointSizePreservesParameters();
    testPointSizeBoundaries();
    return 0;
}