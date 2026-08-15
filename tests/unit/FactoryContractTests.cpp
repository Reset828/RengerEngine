#include "src/app/ApplicationComposition.h"
#include "src/compute/common/ComputeBackendFactory.h"
#include "src/render/common/RenderBackendFactory.h"

#include <cassert>
#include <memory>
#include <string>

namespace {

class FakeRenderBackend final : public dzc::IRenderBackend {};
class FakeComputeBackend final : public dzc::IComputeBackend {};

dzc::Error unavailableError(const char* context) {
    return dzc::Error{
        dzc::ErrorDomain::Interop,
        7,
        "Requested backend is unavailable",
        "The test creator reported backend unavailability",
        context};
}

void testRenderFactoryForwardsSuccess() {
    bool called = false;
    dzc::RenderBackendFactory factory(
        [&called](const dzc::EngineConfig& config)
            -> dzc::Result<std::unique_ptr<dzc::IRenderBackend>> {
            called = true;
            assert(config.backend == dzc::RenderBackendType::Vulkan);
            return dzc::Result<std::unique_ptr<dzc::IRenderBackend>>::success(
                std::make_unique<FakeRenderBackend>());
        });

    dzc::EngineConfig config;
    config.backend = dzc::RenderBackendType::Vulkan;
    const auto result = factory.create(config);
    assert(result.hasValue());
    assert(result.value() != nullptr);
    assert(called);
}

void testRenderFactoryForwardsExplicitFailure() {
    dzc::RenderBackendFactory factory(
        [](const dzc::EngineConfig&) -> dzc::Result<std::unique_ptr<dzc::IRenderBackend>> {
            return dzc::Result<std::unique_ptr<dzc::IRenderBackend>>::failure(
                unavailableError("RenderBackendFactory::create"));
        });

    const auto result = factory.create(dzc::EngineConfig{});
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::Interop);
    assert(result.error().code == 7);
}

void testComputeOffDoesNotCreateBackend() {
    bool called = false;
    dzc::ComputeBackendFactory factory(
        [&called](const dzc::EngineConfig&) -> dzc::Result<std::unique_ptr<dzc::IComputeBackend>> {
            called = true;
            return dzc::Result<std::unique_ptr<dzc::IComputeBackend>>::success(
                std::make_unique<FakeComputeBackend>());
        });

    dzc::EngineConfig config;
    config.cudaMode = dzc::OptionalFeatureMode::Off;
    const auto result = factory.create(config);
    assert(result.hasValue());
    assert(result.value().backend != nullptr);
    assert(!result.value().degraded);
    assert(!called);
}

void testComputeOnReturnsExplicitFailure() {
    dzc::ComputeBackendFactory factory(
        [](const dzc::EngineConfig&) -> dzc::Result<std::unique_ptr<dzc::IComputeBackend>> {
            return dzc::Result<std::unique_ptr<dzc::IComputeBackend>>::failure(
                unavailableError("ComputeBackendFactory::create"));
        });

    dzc::EngineConfig config;
    config.cudaMode = dzc::OptionalFeatureMode::On;
    const auto result = factory.create(config);
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::Interop);
    assert(result.error().code == 7);
}

void testComputeAutoFallsBackToDisabled() {
    dzc::ComputeBackendFactory factory(
        [](const dzc::EngineConfig&) -> dzc::Result<std::unique_ptr<dzc::IComputeBackend>> {
            return dzc::Result<std::unique_ptr<dzc::IComputeBackend>>::failure(
                unavailableError("ComputeBackendFactory::create"));
        });

    dzc::EngineConfig config;
    config.cudaMode = dzc::OptionalFeatureMode::Auto;
    const auto result = factory.create(config);
    assert(result.hasValue());
    assert(result.value().backend != nullptr);
    assert(result.value().degraded);
}

void testCompositionRootAssemblesFakeBackends() {
    dzc::RenderBackendFactory renderFactory(
        [](const dzc::EngineConfig&) -> dzc::Result<std::unique_ptr<dzc::IRenderBackend>> {
            return dzc::Result<std::unique_ptr<dzc::IRenderBackend>>::success(
                std::make_unique<FakeRenderBackend>());
        });
    dzc::ComputeBackendFactory computeFactory(
        [](const dzc::EngineConfig&) -> dzc::Result<std::unique_ptr<dzc::IComputeBackend>> {
            return dzc::Result<std::unique_ptr<dzc::IComputeBackend>>::success(
                std::make_unique<FakeComputeBackend>());
        });

    const auto result = dzc::ApplicationComposition::compose(
        dzc::EngineConfig{}, renderFactory, computeFactory);
    assert(result.hasValue());
    assert(result.value().renderBackend != nullptr);
    assert(result.value().computeBackend != nullptr);
    assert(!result.value().computeDegraded);
}

} // namespace

int main() {
    testRenderFactoryForwardsSuccess();
    testRenderFactoryForwardsExplicitFailure();
    testComputeOffDoesNotCreateBackend();
    testComputeOnReturnsExplicitFailure();
    testComputeAutoFallsBackToDisabled();
    testCompositionRootAssemblesFakeBackends();
    return 0;
}