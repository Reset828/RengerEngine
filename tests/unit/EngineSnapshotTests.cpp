#include "dzc/EngineSnapshot.h"

#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace {

template <typename ValueType>
void verifyValueTypeSemantics() {
    static_assert(std::is_default_constructible_v<ValueType>);
    static_assert(std::is_copy_constructible_v<ValueType>);
    static_assert(std::is_copy_assignable_v<ValueType>);
    static_assert(std::is_move_constructible_v<ValueType>);
    static_assert(std::is_move_assignable_v<ValueType>);

    ValueType source{};
    const ValueType copied{source};
    ValueType assigned{};
    assigned = copied;
    ValueType moved{std::move(assigned)};
    ValueType moveAssigned{};
    moveAssigned = std::move(moved);
}

void testEnumValues() {
    static_assert(static_cast<unsigned int>(dzc::EngineState::Created) == 0U);
    static_assert(static_cast<unsigned int>(dzc::EngineState::Initializing) == 1U);
    static_assert(static_cast<unsigned int>(dzc::EngineState::Ready) == 2U);
    static_assert(static_cast<unsigned int>(dzc::EngineState::Running) == 3U);
    static_assert(static_cast<unsigned int>(dzc::EngineState::Loading) == 4U);
    static_assert(static_cast<unsigned int>(dzc::EngineState::Failed) == 5U);
    static_assert(static_cast<unsigned int>(dzc::EngineState::ShuttingDown) == 6U);
    static_assert(static_cast<unsigned int>(dzc::EngineState::Stopped) == 7U);
    static_assert(static_cast<unsigned int>(dzc::DatasetState::None) == 0U);
    static_assert(static_cast<unsigned int>(dzc::DatasetState::Opening) == 1U);
    static_assert(static_cast<unsigned int>(dzc::DatasetState::Building) == 2U);
    static_assert(static_cast<unsigned int>(dzc::DatasetState::Ready) == 3U);
    static_assert(static_cast<unsigned int>(dzc::DatasetState::Cancelling) == 4U);
    static_assert(static_cast<unsigned int>(dzc::DatasetState::Error) == 5U);
}

void testValueTypeSemanticsAndCopyCost() {
    verifyValueTypeSemantics<dzc::DatasetSummary>();
    verifyValueTypeSemantics<dzc::PerformanceSnapshot>();
    verifyValueTypeSemantics<dzc::MemorySnapshot>();
    verifyValueTypeSemantics<dzc::EngineSnapshot>();
    static_assert(sizeof(dzc::EngineSnapshot) <= 512U);
}

void testDefaultValues() {
    const dzc::DatasetSummary dataset{};
    assert(dataset.id == dzc::DatasetId{});
    assert(dataset.state == dzc::DatasetState::None);
    assert(dataset.displayName.empty());
    assert(dataset.totalPointCount == 0U);
    assert(dataset.visiblePointCount == 0U);
    assert(dataset.chunkCount == 0U);
    assert(dataset.visibleChunkCount == 0U);
    assert(dataset.progress == 0.0);

    const dzc::PerformanceSnapshot performance{};
    assert(performance.framesPerSecond == 0.0);
    assert(performance.cpuFrameMilliseconds == 0.0);
    assert(performance.gpuFrameMilliseconds == 0.0);
    assert(performance.uploadedBytesThisFrame == 0U);
    assert(performance.recordingWorkerCount == 0U);

    const dzc::MemorySnapshot memory{};
    assert(memory.cpuResidentBytes == 0U);
    assert(memory.cpuBudgetBytes == 0U);
    assert(memory.gpuResidentBytes == 0U);
    assert(memory.gpuBudgetBytes == 0U);

    const dzc::EngineSnapshot snapshot{};
    assert(snapshot.frameId == dzc::FrameId{});
    assert(snapshot.state == dzc::EngineState::Created);
    assert(snapshot.backend == dzc::RenderBackendType::OpenGL);
    assert(!snapshot.cudaAvailable);
    assert(!snapshot.cudaEnabled);
    assert(snapshot.dataset.state == dzc::DatasetState::None);
    assert(snapshot.pointSize == 1.0F);
    assert(snapshot.shadingMode == dzc::ShadingMode::OriginalColor);
    assert(snapshot.fixedColor == dzc::ColorRgba{});
    assert(snapshot.backgroundColor == dzc::ColorRgba{});
    assert(!snapshot.mostRecentError.has_value());
}

void testPopulatedSnapshotAndConstSharedPointer() {
    dzc::EngineSnapshot snapshot{};
    snapshot.frameId = {42U};
    snapshot.state = dzc::EngineState::Running;
    snapshot.backend = dzc::RenderBackendType::Vulkan;
    snapshot.cudaAvailable = true;
    snapshot.cudaEnabled = true;
    snapshot.dataset = {{7U}, dzc::DatasetState::Ready, "terrain.dzcpc", 1000U, 800U, 20U, 16U, 0.8};
    snapshot.performance = {60.0, 10.0, 5.0, 4096U, 4U};
    snapshot.memory = {1024U, 2048U, 512U, 1024U};
    snapshot.pointSize = 5.0F;
    snapshot.shadingMode = dzc::ShadingMode::Height;
    snapshot.fixedColor = {1.0F, 0.5F, 0.25F, 1.0F};
    snapshot.backgroundColor = {0.1F, 0.2F, 0.3F, 1.0F};
    snapshot.mostRecentError = dzc::Error{
        dzc::ErrorDomain::Resource, 2U, "resource warning", "budget exceeded", "gpu"};

    const dzc::EngineSnapshot copied{snapshot};
    assert(copied.frameId == dzc::FrameId{42U});
    assert(copied.state == dzc::EngineState::Running);
    assert(copied.backend == dzc::RenderBackendType::Vulkan);
    assert(copied.cudaAvailable);
    assert(copied.cudaEnabled);
    assert(copied.dataset.id == dzc::DatasetId{7U});
    assert(copied.dataset.state == dzc::DatasetState::Ready);
    assert(copied.dataset.displayName == "terrain.dzcpc");
    assert(copied.dataset.totalPointCount == 1000U);
    assert(copied.dataset.visiblePointCount == 800U);
    assert(copied.dataset.chunkCount == 20U);
    assert(copied.dataset.visibleChunkCount == 16U);
    assert(copied.dataset.progress == 0.8);
    assert(copied.performance.framesPerSecond == 60.0);
    assert(copied.performance.cpuFrameMilliseconds == 10.0);
    assert(copied.performance.gpuFrameMilliseconds == 5.0);
    assert(copied.performance.uploadedBytesThisFrame == 4096U);
    assert(copied.performance.recordingWorkerCount == 4U);
    assert(copied.memory.cpuResidentBytes == 1024U);
    assert(copied.memory.cpuBudgetBytes == 2048U);
    assert(copied.memory.gpuResidentBytes == 512U);
    assert(copied.memory.gpuBudgetBytes == 1024U);
    assert(copied.pointSize == 5.0F);
    assert(copied.shadingMode == dzc::ShadingMode::Height);
    const dzc::ColorRgba expectedFixedColor{1.0F, 0.5F, 0.25F, 1.0F};
    assert(copied.fixedColor == expectedFixedColor);
    const dzc::ColorRgba expectedBackgroundColor{0.1F, 0.2F, 0.3F, 1.0F};
    assert(copied.backgroundColor == expectedBackgroundColor);
    assert(copied.mostRecentError.has_value());
    assert(copied.mostRecentError->domain == dzc::ErrorDomain::Resource);
    assert(copied.mostRecentError->code == 2U);
    assert(copied.mostRecentError->userMessage == "resource warning");
    assert(copied.mostRecentError->diagnosticMessage == "budget exceeded");
    assert(copied.mostRecentError->context == "gpu");

    const std::shared_ptr<const dzc::EngineSnapshot> published =
        std::make_shared<dzc::EngineSnapshot>(snapshot);
    assert(published->frameId == dzc::FrameId{42U});
    assert(published->dataset.displayName == "terrain.dzcpc");
}

} // namespace

int main() {
    testEnumValues();
    testValueTypeSemanticsAndCopyCost();
    testDefaultValues();
    testPopulatedSnapshotAndConstSharedPointer();
    return 0;
}