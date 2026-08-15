#include "tasks/Cancellation.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <thread>
#include <utility>
#include <vector>

namespace {

using dzc::tasks::CancellationSource;
using dzc::tasks::CancellationToken;

void testDefaultTokenIsNotCancelled() {
    const CancellationToken token;
    assert(!token.isCancellationRequested());
}

void testSourceTokensShareCancellationState() {
    CancellationSource source;
    const CancellationToken first = source.token();
    const CancellationToken second = source.token();
    const CancellationToken copy = first;

    assert(!first.isCancellationRequested());
    assert(!second.isCancellationRequested());
    assert(!copy.isCancellationRequested());

    assert(source.requestCancellation());
    assert(first.isCancellationRequested());
    assert(second.isCancellationRequested());
    assert(copy.isCancellationRequested());
}

void testCancellationIsIdempotent() {
    CancellationSource source;
    const CancellationToken token = source.token();

    assert(source.requestCancellation());
    assert(!source.requestCancellation());
    assert(token.isCancellationRequested());
}

void testSourceDestructionCancelsLiveToken() {
    CancellationToken token;
    {
        CancellationSource source;
        token = source.token();
        assert(!token.isCancellationRequested());
    }
    assert(token.isCancellationRequested());
}

void testMoveConstructionTransfersCancellationControl() {
    CancellationSource source;
    const CancellationToken token = source.token();

    CancellationSource moved(std::move(source));
    assert(!token.isCancellationRequested());
    assert(!source.requestCancellation());
    assert(moved.requestCancellation());
    assert(token.isCancellationRequested());
}

void testMoveAssignmentCancelsReplacedSource() {
    CancellationSource replacement;
    const CancellationToken replacedToken = replacement.token();

    CancellationSource source;
    const CancellationToken sourceToken = source.token();

    replacement = std::move(source);
    assert(replacedToken.isCancellationRequested());
    assert(!sourceToken.isCancellationRequested());
    assert(!source.requestCancellation());

    assert(replacement.requestCancellation());
    assert(sourceToken.isCancellationRequested());
}

void testConcurrentCancellationHasOneWinner() {
    CancellationSource source;
    constexpr std::size_t threadCount = 16U;
    std::atomic_bool start{false};
    std::atomic_uint successCount{0U};
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (std::size_t index = 0U; index < threadCount; ++index) {
        threads.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
            }
            if (source.requestCancellation()) {
                successCount.fetch_add(1U, std::memory_order_relaxed);
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (std::thread& thread : threads) {
        thread.join();
    }

    assert(successCount.load(std::memory_order_relaxed) == 1U);
    assert(source.token().isCancellationRequested());
}

void testConcurrentQueriesObserveCancellationSafely() {
    CancellationSource source;
    constexpr std::size_t observerCount = 12U;
    std::vector<CancellationToken> tokens;
    tokens.reserve(observerCount);
    for (std::size_t index = 0U; index < observerCount; ++index) {
        tokens.push_back(source.token());
    }

    std::atomic_bool start{false};
    std::atomic_bool cancellationIssued{false};
    std::atomic_bool failed{false};
    std::vector<std::thread> observers;
    observers.reserve(observerCount);

    for (const CancellationToken& token : tokens) {
        observers.emplace_back([&, token] {
            while (!start.load(std::memory_order_acquire)) {
            }
            while (!cancellationIssued.load(std::memory_order_acquire)) {
                static_cast<void>(token.isCancellationRequested());
            }
            if (!token.isCancellationRequested()) {
                failed.store(true, std::memory_order_release);
            }
        });
    }

    std::thread canceller([&] {
        while (!start.load(std::memory_order_acquire)) {
        }
        assert(source.requestCancellation());
        cancellationIssued.store(true, std::memory_order_release);
    });

    start.store(true, std::memory_order_release);
    canceller.join();
    for (std::thread& observer : observers) {
        observer.join();
    }

    assert(!failed.load(std::memory_order_acquire));
    for (const CancellationToken& token : tokens) {
        assert(token.isCancellationRequested());
    }
}

} // namespace

int main() {
    testDefaultTokenIsNotCancelled();
    testSourceTokensShareCancellationState();
    testCancellationIsIdempotent();
    testSourceDestructionCancelsLiveToken();
    testMoveConstructionTransfersCancellationControl();
    testMoveAssignmentCancelsReplacedSource();
    testConcurrentCancellationHasOneWinner();
    testConcurrentQueriesObserveCancellationSafely();
    return 0;
}