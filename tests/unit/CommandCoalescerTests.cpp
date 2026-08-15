#include "tasks/CommandCoalescer.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <exception>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using dzc::ColorRgba;
using dzc::DatasetId;
using dzc::OptionalFeatureMode;
using dzc::RenderSize;
using dzc::ShadingMode;
using dzc::tasks::CancelDatasetLoadCommand;
using dzc::tasks::CommandCoalescer;
using dzc::tasks::EngineCommand;
using dzc::tasks::LoadDatasetCommand;
using dzc::tasks::ResetViewCommand;
using dzc::tasks::ResizeCommand;
using dzc::tasks::SetBackgroundColorCommand;
using dzc::tasks::SetCudaModeCommand;
using dzc::tasks::SetFixedColorCommand;
using dzc::tasks::SetPointSizeCommand;
using dzc::tasks::SetShadingModeCommand;
using dzc::tasks::ShutdownCommand;
using dzc::tasks::UnloadDatasetCommand;

void testDefaultCapacity() {
    CommandCoalescer coalescer;
    for (std::size_t index = 0U; index < 1024U; ++index) {
        assert(coalescer.push(ResetViewCommand{}));
    }
    assert(!coalescer.push(ResetViewCommand{}));
}

void testCustomCapacityAndZeroCapacity() {
    CommandCoalescer coalescer(2U);
    assert(coalescer.push(LoadDatasetCommand{"first"}));
    assert(coalescer.push(LoadDatasetCommand{"second"}));
    assert(!coalescer.push(LoadDatasetCommand{"third"}));

    bool threw = false;
    try {
        CommandCoalescer invalid(0U);
        static_cast<void>(invalid);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

void testFifoAndBatchPop() {
    CommandCoalescer coalescer(4U);
    assert(coalescer.push(LoadDatasetCommand{"dataset-a"}));
    assert(coalescer.push(CancelDatasetLoadCommand{DatasetId{3U}}));
    assert(coalescer.push(ShutdownCommand{}));

    const auto first = coalescer.pop();
    assert(first.has_value());
    assert(std::get<LoadDatasetCommand>(*first).path == "dataset-a");

    const std::vector<EngineCommand> remaining = coalescer.popBatch(10U);
    assert(remaining.size() == 2U);
    assert(std::holds_alternative<CancelDatasetLoadCommand>(remaining[0]));
    assert(std::get<CancelDatasetLoadCommand>(remaining[0]).datasetId == DatasetId{3U});
    assert(std::holds_alternative<ShutdownCommand>(remaining[1]));
    assert(coalescer.popBatch(0U).empty());
    assert(!coalescer.pop().has_value());
}

void testCoalescingKeepsOriginalPositionAndLastValue() {
    CommandCoalescer coalescer(8U);
    assert(coalescer.push(SetPointSizeCommand{2.0F}));
    assert(coalescer.push(SetShadingModeCommand{ShadingMode::Height}));
    assert(coalescer.push(SetPointSizeCommand{7.0F}));
    assert(coalescer.push(SetFixedColorCommand{ColorRgba{0.1F, 0.2F, 0.3F, 1.0F}}));
    assert(coalescer.push(SetCudaModeCommand{OptionalFeatureMode::On}));
    assert(coalescer.push(ResizeCommand{RenderSize{800U, 600U, 1.0F}}));
    assert(coalescer.push(SetShadingModeCommand{ShadingMode::Intensity}));
    assert(coalescer.push(SetFixedColorCommand{ColorRgba{0.8F, 0.7F, 0.6F, 0.5F}}));
    assert(coalescer.push(SetCudaModeCommand{OptionalFeatureMode::Off}));
    assert(coalescer.push(ResizeCommand{RenderSize{1920U, 1080U, 2.0F}}));

    const std::vector<EngineCommand> commands = coalescer.popBatch(10U);
    assert(commands.size() == 5U);
    assert(std::get<SetPointSizeCommand>(commands[0]).pixels == 7.0F);
    assert(std::get<SetShadingModeCommand>(commands[1]).mode == ShadingMode::Intensity);
    assert(std::get<SetFixedColorCommand>(commands[2]).color == (ColorRgba{0.8F, 0.7F, 0.6F, 0.5F}));
    assert(std::get<SetCudaModeCommand>(commands[3]).mode == OptionalFeatureMode::Off);
    assert(std::get<ResizeCommand>(commands[4]).size == (RenderSize{1920U, 1080U, 2.0F}));
}

void testBarriersSplitCoalescingSegments() {
    CommandCoalescer coalescer(16U);
    assert(coalescer.push(SetPointSizeCommand{1.0F}));
    assert(coalescer.push(LoadDatasetCommand{"dataset"}));
    assert(coalescer.push(SetPointSizeCommand{2.0F}));
    assert(coalescer.push(SetPointSizeCommand{3.0F}));
    assert(coalescer.push(SetBackgroundColorCommand{ColorRgba{0.2F, 0.3F, 0.4F, 1.0F}}));
    assert(coalescer.push(SetPointSizeCommand{4.0F}));
    assert(coalescer.push(CancelDatasetLoadCommand{DatasetId{4U}}));
    assert(coalescer.push(SetPointSizeCommand{5.0F}));
    assert(coalescer.push(UnloadDatasetCommand{DatasetId{5U}}));
    assert(coalescer.push(SetPointSizeCommand{6.0F}));
    assert(coalescer.push(ResetViewCommand{}));
    assert(coalescer.push(SetPointSizeCommand{7.0F}));
    assert(coalescer.push(ShutdownCommand{}));
    assert(coalescer.push(SetPointSizeCommand{8.0F}));

    const std::vector<EngineCommand> commands = coalescer.popBatch(20U);
    assert(commands.size() == 13U);
    assert(std::get<SetPointSizeCommand>(commands[0]).pixels == 1.0F);
    assert(std::holds_alternative<LoadDatasetCommand>(commands[1]));
    assert(std::get<SetPointSizeCommand>(commands[2]).pixels == 3.0F);
    assert(std::holds_alternative<SetBackgroundColorCommand>(commands[3]));
    assert(std::get<SetPointSizeCommand>(commands[4]).pixels == 4.0F);
    assert(std::holds_alternative<CancelDatasetLoadCommand>(commands[5]));
    assert(std::get<SetPointSizeCommand>(commands[6]).pixels == 5.0F);
    assert(std::holds_alternative<UnloadDatasetCommand>(commands[7]));
    assert(std::get<SetPointSizeCommand>(commands[8]).pixels == 6.0F);
    assert(std::holds_alternative<ResetViewCommand>(commands[9]));
    assert(std::get<SetPointSizeCommand>(commands[10]).pixels == 7.0F);
    assert(std::holds_alternative<ShutdownCommand>(commands[11]));
    assert(std::get<SetPointSizeCommand>(commands[12]).pixels == 8.0F);
}

void testFullQueueStillUpdatesCurrentSegment() {
    CommandCoalescer coalescer(2U);
    assert(coalescer.push(LoadDatasetCommand{"dataset"}));
    assert(coalescer.push(SetPointSizeCommand{2.0F}));
    assert(coalescer.push(SetPointSizeCommand{9.0F}));
    assert(!coalescer.push(ResizeCommand{RenderSize{10U, 20U, 1.0F}}));

    const std::vector<EngineCommand> commands = coalescer.popBatch(3U);
    assert(commands.size() == 2U);
    assert(std::holds_alternative<LoadDatasetCommand>(commands[0]));
    assert(std::get<SetPointSizeCommand>(commands[1]).pixels == 9.0F);
}

void testCloseDrainsAndShutdownDoesNotClose() {
    CommandCoalescer coalescer(3U);
    assert(coalescer.push(ShutdownCommand{}));
    assert(coalescer.push(SetPointSizeCommand{4.0F}));

    const auto shutdown = coalescer.pop();
    assert(shutdown.has_value() && std::holds_alternative<ShutdownCommand>(*shutdown));
    const auto pointSize = coalescer.pop();
    assert(pointSize.has_value() && std::get<SetPointSizeCommand>(*pointSize).pixels == 4.0F);

    assert(coalescer.push(LoadDatasetCommand{"pending"}));
    coalescer.close();
    coalescer.close();
    assert(!coalescer.push(LoadDatasetCommand{"rejected"}));
    const auto pending = coalescer.pop();
    assert(pending.has_value() && std::get<LoadDatasetCommand>(*pending).path == "pending");
    assert(!coalescer.pop().has_value());
}

void testConcurrentProducersAndSingleConsumer() {
    constexpr std::size_t producerCount = 4U;
    constexpr std::size_t commandsPerProducer = 200U;
    constexpr std::size_t totalCommandCount = producerCount * commandsPerProducer;
    CommandCoalescer coalescer(totalCommandCount);
    std::atomic_bool start{false};
    std::atomic<std::size_t> completedProducers{0U};
    std::vector<std::thread> producers;
    producers.reserve(producerCount);

    for (std::size_t producer = 0U; producer < producerCount; ++producer) {
        producers.emplace_back([&, producer] {
            while (!start.load(std::memory_order_acquire)) {
            }
            for (std::size_t index = 0U; index < commandsPerProducer; ++index) {
                assert(coalescer.push(LoadDatasetCommand{
                    "producer-" + std::to_string(producer) + "-" + std::to_string(index)}));
            }
            completedProducers.fetch_add(1U, std::memory_order_release);
        });
    }

    std::vector<std::string> consumedPaths;
    consumedPaths.reserve(totalCommandCount);
    std::thread consumer([&] {
        while (completedProducers.load(std::memory_order_acquire) != producerCount ||
               consumedPaths.size() != totalCommandCount) {
            if (const auto command = coalescer.pop()) {
                consumedPaths.push_back(std::get<LoadDatasetCommand>(*command).path);
            }
        }
    });

    start.store(true, std::memory_order_release);
    for (std::thread& producer : producers) {
        producer.join();
    }
    consumer.join();

    assert(consumedPaths.size() == totalCommandCount);
    std::sort(consumedPaths.begin(), consumedPaths.end());
    for (std::size_t producer = 0U; producer < producerCount; ++producer) {
        for (std::size_t index = 0U; index < commandsPerProducer; ++index) {
            const std::string expected =
                "producer-" + std::to_string(producer) + "-" + std::to_string(index);
            assert(std::binary_search(consumedPaths.begin(), consumedPaths.end(), expected));
        }
    }
}

void testConcurrentCloseAndPush() {
    CommandCoalescer coalescer(128U);
    std::atomic_bool start{false};
    std::vector<std::thread> producers;
    producers.reserve(4U);

    for (std::size_t producer = 0U; producer < 4U; ++producer) {
        producers.emplace_back([&, producer] {
            while (!start.load(std::memory_order_acquire)) {
            }
            for (std::size_t index = 0U; index < 2000U; ++index) {
                static_cast<void>(coalescer.push(LoadDatasetCommand{
                    "concurrent-" + std::to_string(producer) + "-" + std::to_string(index)}));
            }
        });
    }

    std::thread closer([&] {
        while (!start.load(std::memory_order_acquire)) {
        }
        coalescer.close();
    });

    start.store(true, std::memory_order_release);
    for (std::thread& producer : producers) {
        producer.join();
    }
    closer.join();

    const std::vector<EngineCommand> accepted = coalescer.popBatch(1000U);
    assert(accepted.size() <= 128U);
    assert(!coalescer.push(LoadDatasetCommand{"after-close"}));
}

void testDestructorCloses() {
    {
        CommandCoalescer coalescer(1U);
        assert(coalescer.push(LoadDatasetCommand{"dataset"}));
    }
    assert(true);
}

} // namespace

int main() {
    testDefaultCapacity();
    testCustomCapacityAndZeroCapacity();
    testFifoAndBatchPop();
    testCoalescingKeepsOriginalPositionAndLastValue();
    testBarriersSplitCoalescingSegments();
    testFullQueueStillUpdatesCurrentSegment();
    testCloseDrainsAndShutdownDoesNotClose();
    testConcurrentProducersAndSingleConsumer();
    testConcurrentCloseAndPush();
    testDestructorCloses();
    return 0;
}