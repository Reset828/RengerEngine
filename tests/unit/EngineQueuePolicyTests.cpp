#include "engine/EngineQueues.h"

#include "tasks/TaskSystem.h"

#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace {

using dzc::DatasetId;
using dzc::DatasetLoadedEvent;
using dzc::DatasetProgressEvent;
using dzc::EngineEvent;
using dzc::EngineEventQueue;
using dzc::ErrorDomain;
using dzc::ErrorEvent;
using dzc::EventSeverity;
using dzc::FeatureDegradedEvent;
using dzc::MessageEvent;
using dzc::tasks::TaskErrorCode;

void testFifoCloseAndDrain() {
    EngineEventQueue queue(2U);
    assert(queue.tryPush(MessageEvent{EventSeverity::Info, "first", {}}));
    assert(queue.tryPush(MessageEvent{EventSeverity::Warning, "second", {}}));
    queue.close();
    assert(!queue.tryPush(MessageEvent{EventSeverity::Info, "rejected", {}}));

    const std::vector<EngineEvent> events = queue.poll();
    assert(events.size() == 2U);
    assert(std::get<MessageEvent>(events[0]).message == "first");
    assert(std::get<MessageEvent>(events[1]).message == "second");
    assert(queue.poll().empty());
}

void testProgressCoalescesAtOriginalPosition() {
    EngineEventQueue queue(2U);
    assert(queue.tryPush(DatasetProgressEvent{DatasetId{7U}, 1U, 10U}));
    assert(queue.tryPush(MessageEvent{EventSeverity::Info, "after-progress", {}}));
    assert(queue.tryPush(DatasetProgressEvent{DatasetId{7U}, 8U, 10U}));

    const std::vector<EngineEvent> events = queue.poll();
    assert(events.size() == 2U);
    const auto& progress = std::get<DatasetProgressEvent>(events[0]);
    assert(progress.datasetId == DatasetId{7U});
    assert(progress.completedUnits == 8U);
    assert(progress.totalUnits == 10U);
    assert(std::get<MessageEvent>(events[1]).message == "after-progress");
}

void testProgressReplacesOldestProgressAndMessagesDrop() {
    EngineEventQueue queue(2U);
    assert(queue.tryPush(DatasetProgressEvent{DatasetId{1U}, 1U, 10U}));
    assert(queue.tryPush(MessageEvent{EventSeverity::Info, "retained", {}}));
    assert(queue.tryPush(DatasetProgressEvent{DatasetId{2U}, 2U, 10U}));
    assert(!queue.tryPush(MessageEvent{EventSeverity::Info, "dropped", {}}));

    const std::vector<EngineEvent> events = queue.poll();
    assert(events.size() == 2U);
    assert(std::get<MessageEvent>(events[0]).message == "retained");
    const auto& progress = std::get<DatasetProgressEvent>(events[1]);
    assert(progress.datasetId == DatasetId{2U});
    assert(progress.completedUnits == 2U);
}

void testCriticalOverflowPublishesOneLossEventFirst() {
    EngineEventQueue queue(1U);
    assert(queue.tryPush(MessageEvent{EventSeverity::Info, "occupies-capacity", {}}));
    assert(!queue.tryPush(DatasetLoadedEvent{DatasetId{3U}}));
    assert(!queue.tryPush(FeatureDegradedEvent{"cuda", "unavailable"}));

    const std::vector<EngineEvent> events = queue.poll();
    assert(events.size() == 2U);
    const auto& loss = std::get<ErrorEvent>(events[0]);
    assert(loss.severity == EventSeverity::RecoverableError);
    assert(loss.error.domain == ErrorDomain::Task);
    assert(loss.error.code == static_cast<std::uint32_t>(TaskErrorCode::QueueFull));
    assert(loss.context.datasetId == DatasetId{});
    assert(loss.context.chunkId.value == 0U);
    assert(loss.context.taskId.value == 0U);
    assert(loss.context.frameId.value == 0U);
    assert(std::get<MessageEvent>(events[1]).message == "occupies-capacity");
    assert(queue.poll().empty());
}

} // namespace

int main() {
    testFifoCloseAndDrain();
    testProgressCoalescesAtOriginalPosition();
    testProgressReplacesOldestProgressAndMessagesDrop();
    testCriticalOverflowPublishesOneLossEventFirst();
    return 0;
}
