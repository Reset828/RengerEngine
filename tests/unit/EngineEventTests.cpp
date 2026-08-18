#include "dzc/EngineEvent.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

using dzc::EngineEvent;

template <typename ValueType>
void verifyValueTypeSemantics() {
    static_assert(std::is_default_constructible_v<ValueType>);
    static_assert(std::is_move_constructible_v<ValueType>);
    static_assert(std::is_move_assignable_v<ValueType>);

    ValueType source{};
    ValueType moved{std::move(source)};
    ValueType assigned{};
    assigned = std::move(moved);
}

void testEventAlternatives() {
    static_assert(std::variant_size_v<EngineEvent> == 6U);
    static_assert(std::is_default_constructible_v<EngineEvent>);
    static_assert(std::is_move_constructible_v<EngineEvent>);
    static_assert(std::is_move_assignable_v<EngineEvent>);
    static_assert(!std::is_constructible_v<EngineEvent, int>);
    static_assert(!std::is_constructible_v<EngineEvent, std::string>);

    verifyValueTypeSemantics<dzc::EventContext>();
    verifyValueTypeSemantics<dzc::MessageEvent>();
    verifyValueTypeSemantics<dzc::ErrorEvent>();
    verifyValueTypeSemantics<dzc::DatasetProgressEvent>();
    verifyValueTypeSemantics<dzc::DatasetLoadedEvent>();
    verifyValueTypeSemantics<dzc::DatasetLoadCancelledEvent>();
    verifyValueTypeSemantics<dzc::FeatureDegradedEvent>();

    const dzc::MessageEvent message{
        dzc::EventSeverity::Warning,
        "dataset loading",
        {{1U}, {2U}, {3U}, {4U}}};
    const dzc::ErrorEvent error{
        dzc::EventSeverity::FatalError,
        {dzc::ErrorDomain::DataFormat, 7U, "bad data", "invalid header", "header"},
        {{5U}, {6U}, {7U}, {8U}}};
    const dzc::DatasetProgressEvent progress{{9U}, 10U, 20U};
    const dzc::DatasetLoadedEvent loaded{{11U}};
    const dzc::DatasetLoadCancelledEvent cancelled{{12U}};
    const dzc::FeatureDegradedEvent degraded{"CUDA", "unavailable"};

    const EngineEvent messageEvent{message};
    const auto& storedMessage = std::get<dzc::MessageEvent>(messageEvent);
    assert(storedMessage.severity == message.severity);
    assert(storedMessage.message == message.message);
    assert(storedMessage.context.datasetId == dzc::DatasetId{1U});
    assert(storedMessage.context.chunkId == dzc::ChunkId{2U});
    assert(storedMessage.context.taskId == dzc::TaskId{3U});
    assert(storedMessage.context.frameId == dzc::FrameId{4U});

    const EngineEvent errorEvent{error};
    const auto& storedError = std::get<dzc::ErrorEvent>(errorEvent);
    assert(storedError.severity == error.severity);
    assert(storedError.error.domain == dzc::ErrorDomain::DataFormat);
    assert(storedError.error.code == 7U);
    assert(storedError.error.userMessage == "bad data");
    assert(storedError.error.diagnosticMessage == "invalid header");
    assert(storedError.error.context == "header");
    assert(storedError.context.datasetId == dzc::DatasetId{5U});
    assert(storedError.context.chunkId == dzc::ChunkId{6U});
    assert(storedError.context.taskId == dzc::TaskId{7U});
    assert(storedError.context.frameId == dzc::FrameId{8U});

    const EngineEvent progressEvent{progress};
    const auto& storedProgress = std::get<dzc::DatasetProgressEvent>(progressEvent);
    assert(storedProgress.datasetId == progress.datasetId);
    assert(storedProgress.completedUnits == 10U);
    assert(storedProgress.totalUnits == 20U);
    assert(std::get<dzc::DatasetLoadedEvent>(EngineEvent{loaded}).datasetId == loaded.datasetId);
    assert(std::get<dzc::DatasetLoadCancelledEvent>(EngineEvent{cancelled}).datasetId == cancelled.datasetId);
    assert(std::get<dzc::FeatureDegradedEvent>(EngineEvent{degraded}).feature == "CUDA");
    assert(std::get<dzc::FeatureDegradedEvent>(EngineEvent{degraded}).reason == "unavailable");

    EngineEvent movable{message};
    EngineEvent moved{std::move(movable)};
    EngineEvent assigned{dzc::FeatureDegradedEvent{}};
    assigned = std::move(moved);
    assert(std::get<dzc::MessageEvent>(assigned).message == message.message);
}

void testDefaultValues() {
    assert(dzc::MessageEvent{}.severity == dzc::EventSeverity::Info);
    assert(dzc::MessageEvent{}.message.empty());
    assert(dzc::MessageEvent{}.context.datasetId == dzc::DatasetId{});
    assert(dzc::MessageEvent{}.context.chunkId == dzc::ChunkId{});
    assert(dzc::MessageEvent{}.context.taskId == dzc::TaskId{});
    assert(dzc::MessageEvent{}.context.frameId == dzc::FrameId{});
    assert(dzc::ErrorEvent{}.severity == dzc::EventSeverity::RecoverableError);
    assert(dzc::ErrorEvent{}.error.domain == dzc::ErrorDomain::General);
    assert(dzc::ErrorEvent{}.context.datasetId == dzc::DatasetId{});
    assert(dzc::DatasetProgressEvent{}.datasetId == dzc::DatasetId{});
    assert(dzc::DatasetProgressEvent{}.completedUnits == 0U);
    assert(dzc::DatasetProgressEvent{}.totalUnits == 0U);
    assert(dzc::DatasetLoadedEvent{}.datasetId == dzc::DatasetId{});
    assert(dzc::DatasetLoadCancelledEvent{}.datasetId == dzc::DatasetId{});
    assert(dzc::FeatureDegradedEvent{}.feature.empty());
    assert(dzc::FeatureDegradedEvent{}.reason.empty());
}

void testEachAlternativeCanBeStored() {
    EngineEvent event{dzc::MessageEvent{}};
    assert(std::holds_alternative<dzc::MessageEvent>(event));
    event = dzc::ErrorEvent{};
    assert(std::holds_alternative<dzc::ErrorEvent>(event));
    event = dzc::DatasetProgressEvent{};
    assert(std::holds_alternative<dzc::DatasetProgressEvent>(event));
    event = dzc::DatasetLoadedEvent{};
    assert(std::holds_alternative<dzc::DatasetLoadedEvent>(event));
    event = dzc::DatasetLoadCancelledEvent{};
    assert(std::holds_alternative<dzc::DatasetLoadCancelledEvent>(event));
    event = dzc::FeatureDegradedEvent{};
    assert(std::holds_alternative<dzc::FeatureDegradedEvent>(event));
}

} // namespace

int main() {
    testEventAlternatives();
    testDefaultValues();
    testEachAlternativeCanBeStored();
    return 0;
}