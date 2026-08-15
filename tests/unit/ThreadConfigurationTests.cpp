#include "tasks/ThreadConfiguration.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

using dzc::ThreadConfig;
using dzc::tasks::ResolvedThreadConfig;
using dzc::tasks::ThreadConfiguration;

void assertEqual(const ResolvedThreadConfig& actual,
                 std::uint32_t phase1,
                 std::uint32_t phase2,
                 std::uint32_t io) {
    assert(actual.phase1WorkerThreads == phase1);
    assert(actual.phase2RecordingThreads == phase2);
    assert(actual.maxConcurrentIoTasks == io);
}

void testAutomaticValues() {
    const ThreadConfig requested{};
    struct Case final {
        std::uint32_t hardwareConcurrency;
        std::uint32_t phase1;
        std::uint32_t phase2;
    };
    const std::vector<Case> cases{
        {0U, 3U, 2U},
        {1U, 2U, 2U},
        {4U, 3U, 2U},
        {16U, 8U, 8U},
    };

    for (const Case& testCase : cases) {
        assertEqual(ThreadConfiguration::resolve(requested, testCase.hardwareConcurrency),
                    testCase.phase1,
                    testCase.phase2,
                    2U);
    }
}

void testConfiguredValuesOverrideAutomaticValues() {
    ThreadConfig requested;
    requested.workerThreads = 1U;
    requested.commandRecordingThreads = 7U;
    requested.maxConcurrentIoTasks = 5U;

    assertEqual(ThreadConfiguration::resolve(requested, 16U), 1U, 7U, 5U);
}

void testConfiguredValuesAreClampedToSafetyRange() {
    ThreadConfig requested;
    requested.workerThreads = 100U;
    requested.commandRecordingThreads = 9U;
    requested.maxConcurrentIoTasks = 99U;

    assertEqual(ThreadConfiguration::resolve(requested, 4U), 8U, 8U, 8U);
}

void testZeroIoConfigurationUsesDefault() {
    ThreadConfig requested;
    requested.maxConcurrentIoTasks = 0U;

    const auto resolved = ThreadConfiguration::resolve(requested, 4U);
    assert(resolved.maxConcurrentIoTasks == 2U);
}

void testInputIsNotModified() {
    ThreadConfig requested;
    requested.workerThreads = 12U;
    requested.commandRecordingThreads = 3U;
    requested.maxConcurrentIoTasks = 11U;
    const ThreadConfig before = requested;

    static_cast<void>(ThreadConfiguration::resolve(requested, 16U));

    assert(requested.workerThreads == before.workerThreads);
    assert(requested.commandRecordingThreads == before.commandRecordingThreads);
    assert(requested.maxConcurrentIoTasks == before.maxConcurrentIoTasks);
}

void testRepeatedResolutionIsDeterministic() {
    ThreadConfig requested;
    requested.workerThreads = 0U;
    requested.commandRecordingThreads = 6U;
    requested.maxConcurrentIoTasks = 0U;

    const auto first = ThreadConfiguration::resolve(requested, 12U);
    const auto second = ThreadConfiguration::resolve(requested, 12U);
    assert(first.phase1WorkerThreads == second.phase1WorkerThreads);
    assert(first.phase2RecordingThreads == second.phase2RecordingThreads);
    assert(first.maxConcurrentIoTasks == second.maxConcurrentIoTasks);
}

} // namespace

int main() {
    testAutomaticValues();
    testConfiguredValuesOverrideAutomaticValues();
    testConfiguredValuesAreClampedToSafetyRange();
    testZeroIoConfigurationUsesDefault();
    testInputIsNotModified();
    testRepeatedResolutionIsDeterministic();
    return 0;
}