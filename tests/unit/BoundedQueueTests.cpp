#include "tasks/BoundedQueue.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <exception>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

namespace {

using dzc::tasks::BoundedQueue;

struct MoveOnly final {
    explicit MoveOnly(int valueIn) : value(valueIn) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& other) noexcept : value(other.value) { other.value = -1; }
    MoveOnly& operator=(MoveOnly&& other) noexcept {
        value = other.value;
        other.value = -1;
        return *this;
    }

    int value;
};

void testDefaultCapacity() {
    BoundedQueue<int> queue;
    for (int value = 0; value < 1024; ++value) {
        assert(queue.tryPush(value));
    }
    assert(!queue.tryPush(1024));
}

void testCustomCapacity() {
    BoundedQueue<int> queue(2U);
    assert(queue.tryPush(1));
    assert(queue.tryPush(2));
    assert(!queue.tryPush(3));
}

void testZeroCapacityThrows() {
    bool threw = false;
    try {
        BoundedQueue<int> queue(0U);
        static_cast<void>(queue);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

void testFifoAndEmptyPop() {
    BoundedQueue<int> queue(3U);
    assert(!queue.tryPop().has_value());
    assert(queue.tryPush(10));
    assert(queue.tryPush(20));
    assert(queue.tryPush(30));

    const auto first = queue.tryPop();
    const auto second = queue.tryPop();
    const auto third = queue.tryPop();
    assert(first.has_value() && *first == 10);
    assert(second.has_value() && *second == 20);
    assert(third.has_value() && *third == 30);
    assert(!queue.tryPop().has_value());
}

void testBatchPopPreservesFifoAndLimit() {
    BoundedQueue<int> queue(5U);
    for (int value = 1; value <= 5; ++value) {
        assert(queue.tryPush(value));
    }

    const auto firstBatch = queue.tryPopBatch(3U);
    assert((firstBatch == std::vector<int>{1, 2, 3}));
    const auto secondBatch = queue.tryPopBatch(10U);
    assert((secondBatch == std::vector<int>{4, 5}));
    assert(queue.tryPopBatch(0U).empty());
    assert(queue.tryPopBatch(1U).empty());
}

void testCloseRejectsPushAndDrainsAcceptedItems() {
    BoundedQueue<int> queue(3U);
    assert(queue.tryPush(1));
    assert(queue.tryPush(2));
    queue.close();
    queue.close();

    assert(!queue.tryPush(3));
    const auto values = queue.tryPopBatch(10U);
    assert((values == std::vector<int>{1, 2}));
    assert(!queue.tryPop().has_value());
    queue.close();
    assert(!queue.tryPush(4));
}

void testDestructorClosesQueue() {
    {
        BoundedQueue<int> queue(1U);
        assert(queue.tryPush(7));
    }
    assert(true);
}

void testMoveOnlyElements() {
    BoundedQueue<MoveOnly> queue(2U);
    assert(queue.tryPush(MoveOnly(11)));
    assert(queue.tryPush(MoveOnly(22)));

    auto first = queue.tryPop();
    assert(first.has_value() && first->value == 11);
    auto batch = queue.tryPopBatch(2U);
    assert(batch.size() == 1U && batch.front().value == 22);
}

void testMultipleProducersSingleConsumer() {
    constexpr std::size_t producerCount = 4U;
    constexpr std::size_t valuesPerProducer = 250U;
    constexpr std::size_t totalValues = producerCount * valuesPerProducer;
    BoundedQueue<std::size_t> queue(totalValues);
    std::atomic_bool start{false};
    std::vector<std::thread> producers;
    producers.reserve(producerCount);

    for (std::size_t producer = 0U; producer < producerCount; ++producer) {
        producers.emplace_back([&, producer] {
            while (!start.load(std::memory_order_acquire)) {
            }
            for (std::size_t value = 0U; value < valuesPerProducer; ++value) {
                assert(queue.tryPush(producer * valuesPerProducer + value));
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (std::thread& producer : producers) {
        producer.join();
    }

    std::vector<std::size_t> values;
    while (const auto value = queue.tryPop()) {
        values.push_back(*value);
    }
    assert(values.size() == totalValues);
    std::sort(values.begin(), values.end());
    for (std::size_t index = 0U; index < totalValues; ++index) {
        assert(values[index] == index);
    }
}

void testConcurrentCloseAndPush() {
    BoundedQueue<int> queue(64U);
    std::atomic_bool start{false};
    std::vector<std::thread> producers;
    producers.reserve(4U);

    for (int producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&, producer] {
            while (!start.load(std::memory_order_acquire)) {
            }
            for (int index = 0; index < 2000; ++index) {
                static_cast<void>(queue.tryPush(producer * 2000 + index));
            }
        });
    }

    std::thread closer([&] {
        while (!start.load(std::memory_order_acquire)) {
        }
        queue.close();
    });

    start.store(true, std::memory_order_release);
    closer.join();
    for (std::thread& producer : producers) {
        producer.join();
    }

    assert(!queue.tryPush(1));
    queue.close();
    while (queue.tryPop().has_value()) {
    }
}

} // namespace

int main() {
    testDefaultCapacity();
    testCustomCapacity();
    testZeroCapacityThrows();
    testFifoAndEmptyPop();
    testBatchPopPreservesFifoAndLimit();
    testCloseRejectsPushAndDrainsAcceptedItems();
    testDestructorClosesQueue();
    testMoveOnlyElements();
    testMultipleProducersSingleConsumer();
    testConcurrentCloseAndPush();
    return 0;
}