#include <diagnostics/PerformanceSummaryWriter.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("dzc-performance-summary-" + std::to_string(seed));
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    const std::filesystem::path& path() const noexcept {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input.is_open());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

dzc::diagnostics::PerformanceSummary makeCompleteSummary() {
    dzc::diagnostics::PerformanceSummary summary;
    summary.projectVersion = "1.2.3";
    summary.buildType = "Debug";
    summary.operatingSystem = "Windows";
    summary.cpu = "CPU 16-Core";
    summary.gpu = "GPU";
    summary.driver = "Driver 1.0";
    summary.memory = "16 GiB";
    summary.gpuMemory = "8 GiB";
    summary.cuda = "12.4";
    summary.datasetIdentity = "dataset-001";
    summary.backend = "OpenGL";
    summary.parameters = "width=1920,height=1080";
    summary.benchmarkHardware = "NVIDIA Rig";
    summary.cameraPath = "camera/default";
    summary.pointCount = 123456U;
    summary.sampleFrameCount = 100U;
    summary.errorCount = 2U;
    summary.width = 1920U;
    summary.height = 1080U;
    summary.averageFps = 60.0;
    summary.averageCpuFrameMilliseconds = 1.25;
    summary.averageGpuFrameMilliseconds = 2.5;
    summary.lowFrameRatePercentile = 1.234567;
    return summary;
}

std::string expectedCompleteSummary() {
    return
        "# Performance Summary\n"
        "\n"
        "## Environment\n"
        "\n"
        "| Field | Value |\n"
        "| --- | --- |\n"
        "| projectVersion | 1.2.3 |\n"
        "| buildType | Debug |\n"
        "| operatingSystem | Windows |\n"
        "| cpu | CPU 16-Core |\n"
        "| gpu | GPU |\n"
        "| driver | Driver 1.0 |\n"
        "| memory | 16 GiB |\n"
        "| gpuMemory | 8 GiB |\n"
        "| cuda | 12.4 |\n"
        "| benchmarkHardware | NVIDIA Rig |\n"
        "| cameraPath | camera/default |\n"
        "\n"
        "## Dataset\n"
        "\n"
        "| Field | Value |\n"
        "| --- | --- |\n"
        "| datasetIdentity | dataset-001 |\n"
        "| pointCount | 123456 |\n"
        "\n"
        "## Configuration\n"
        "\n"
        "| Field | Value |\n"
        "| --- | --- |\n"
        "| width | 1920 |\n"
        "| height | 1080 |\n"
        "| backend | OpenGL |\n"
        "| parameters | width=1920,height=1080 |\n"
        "\n"
        "## Statistics\n"
        "\n"
        "| Field | Value |\n"
        "| --- | --- |\n"
        "| sampleFrameCount | 100 |\n"
        "| averageFps | 60.000000 |\n"
        "| averageCpuFrameMilliseconds | 1.250000 |\n"
        "| averageGpuFrameMilliseconds | 2.500000 |\n"
        "| lowFrameRatePercentile | 1.234567 |\n"
        "\n"
        "## Errors\n"
        "\n"
        "| Field | Value |\n"
        "| --- | --- |\n"
        "| errorCount | 2 |\n";
}

void testCompleteGoldenFileAndEncoding() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "summary.md";
    dzc::diagnostics::PerformanceSummaryWriter writer(file);
    assert(writer.isOpen());
    assert(writer.write(makeCompleteSummary()));
    assert(writer.close());
    assert(!writer.isOpen());

    const std::string bytes = readBytes(file);
    assert(bytes == expectedCompleteSummary());
    assert(bytes.size() < 3U || bytes.substr(0U, 3U) != std::string("\xEF\xBB\xBF"));
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        if (bytes[index] == '\n') {
            assert(index == 0U || bytes[index - 1U] != '\r');
        }
    }
}

void testMissingValuesAndTbd() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "missing.md";
    dzc::diagnostics::PerformanceSummaryWriter writer(file);
    dzc::diagnostics::PerformanceSummary summary;
    summary.gpu = "GPU";
    summary.pointCount = 0U;
    summary.averageFps = 0.0;
    assert(writer.write(summary));
    assert(writer.close());

    const std::string bytes = readBytes(file);
    assert(bytes.find("| projectVersion |  |\n") != std::string::npos);
    assert(bytes.find("| gpu | GPU |\n") != std::string::npos);
    assert(bytes.find("| pointCount | 0 |\n") != std::string::npos);
    assert(bytes.find("| benchmarkHardware | TBD |\n") != std::string::npos);
    assert(bytes.find("| cameraPath | TBD |\n") != std::string::npos);
    assert(bytes.find("| averageGpuFrameMilliseconds |  |\n") != std::string::npos);
    assert(bytes.find("| lowFrameRatePercentile | TBD |\n") != std::string::npos);
}

void testMarkdownEscaping() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "escaping.md";
    dzc::diagnostics::PerformanceSummaryWriter writer(file);
    auto summary = makeCompleteSummary();
    summary.projectVersion = std::string("a|b\\c\r\nd");
    assert(writer.write(summary));
    assert(writer.close());

    const std::string bytes = readBytes(file);
    assert(bytes.find(R"(| projectVersion | a\|b\\c  d |)" ) != std::string::npos);
}

void testInvalidFloatingPointDoesNotWrite() {
    const std::vector<double> invalidValues = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };

    for (const double invalidValue : invalidValues) {
        TemporaryDirectory directory;
        const auto file = directory.path() / "invalid.md";
        dzc::diagnostics::PerformanceSummaryWriter writer(file);
        auto summary = makeCompleteSummary();
        summary.averageFps = invalidValue;
        assert(!writer.write(summary));
        assert(readBytes(file).empty());
        assert(writer.write(makeCompleteSummary()));
        assert(writer.close());
    }
}

void testOpenFailures() {
    TemporaryDirectory directory;
    const auto missingParent = directory.path() / "missing" / "summary.md";
    dzc::diagnostics::PerformanceSummaryWriter missing(missingParent);
    assert(!missing.isOpen());
    assert(!missing.write(makeCompleteSummary()));
    assert(!std::filesystem::exists(directory.path() / "missing"));

    dzc::diagnostics::PerformanceSummaryWriter directoryPath(directory.path());
    assert(!directoryPath.isOpen());
    assert(directoryPath.close());
}

void testSingleWriteAndCloseLifecycle() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "lifetime.md";
    {
        dzc::diagnostics::PerformanceSummaryWriter writer(file);
        auto first = makeCompleteSummary();
        auto second = makeCompleteSummary();
        second.projectVersion = "second";
        assert(writer.write(first));
        assert(!writer.write(second));
        assert(writer.close());
        const std::string afterClose = readBytes(file);
        assert(writer.close());
        assert(!writer.write(second));
        assert(afterClose == expectedCompleteSummary());
    }
    assert(readBytes(file) == expectedCompleteSummary());
}

void testConcurrentOperations() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "concurrent.md";
    dzc::diagnostics::PerformanceSummaryWriter writer(file);
    constexpr int threadCount = 8;
    std::atomic<int> successfulWrites{0};
    std::atomic<bool> stopObserver{false};
    std::vector<std::thread> writers;
    for (int index = 0; index < threadCount; ++index) {
        writers.emplace_back([&writer, &successfulWrites, index]() {
            auto summary = makeCompleteSummary();
            summary.projectVersion = "thread-" + std::to_string(index);
            if (writer.write(summary)) {
                ++successfulWrites;
            }
        });
    }
    std::thread observer([&writer, &stopObserver]() {
        while (!stopObserver.load()) {
            (void)writer.isOpen();
        }
    });
    std::thread closer([&writer]() { assert(writer.close()); });

    for (auto& thread : writers) {
        thread.join();
    }
    closer.join();
    stopObserver.store(true);
    observer.join();

    assert(successfulWrites.load() <= 1);
    assert(!writer.isOpen());
    assert(!writer.write(makeCompleteSummary()));
}

void testDestructorCloses() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "destructor.md";
    {
        dzc::diagnostics::PerformanceSummaryWriter writer(file);
        assert(writer.write(makeCompleteSummary()));
    }
    assert(readBytes(file) == expectedCompleteSummary());
}

} // namespace

int main() {
    testCompleteGoldenFileAndEncoding();
    testMissingValuesAndTbd();
    testMarkdownEscaping();
    testInvalidFloatingPointDoesNotWrite();
    testOpenFailures();
    testSingleWriteAndCloseLifecycle();
    testConcurrentOperations();
    testDestructorCloses();
    return 0;
}