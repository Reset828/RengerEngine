#include <diagnostics/PerformanceCsvWriter.h>

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

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("dzc-performance-csv-" + std::to_string(seed));
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

std::vector<std::string> splitLines(const std::string& bytes) {
    std::vector<std::string> lines;
    std::size_t start = 0U;
    bool quoted = false;
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        const char character = bytes[index];
        if (character == '"') {
            if (quoted && index + 1U < bytes.size() && bytes[index + 1U] == '"') {
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == '\n' && !quoted) {
            lines.push_back(bytes.substr(start, index - start));
            start = index + 1U;
        }
    }
    assert(!quoted);
    assert(start == bytes.size());
    return lines;
}
dzc::diagnostics::PerformanceCsvRow makeCompleteRow() {
    dzc::diagnostics::PerformanceCsvRow row;
    row.utcTime = std::chrono::system_clock::from_time_t(0) + std::chrono::milliseconds(123);
    row.frameId = 42U;
    row.backend = "OpenGL";
    row.width = 1920U;
    row.height = 1080U;
    row.cpuFrameMilliseconds = 1.25;
    row.gpuFrameMilliseconds = 2.5;
    row.framesPerSecond = 60.0;
    row.visiblePoints = 100U;
    row.submittedPoints = 200U;
    row.visibleChunks = 3U;
    row.cpuResidentBytes = 400U;
    row.gpuResidentBytes = 500U;
    row.uploadBytes = 600U;
    row.lodMisses = 7U;
    row.recordingWorkers = 8U;
    return row;
}

void testHeaderAndGoldenRow() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "performance.csv";
    {
        dzc::diagnostics::PerformanceCsvWriter writer(file);
        assert(writer.isOpen());
        assert(writer.write(makeCompleteRow()));
        assert(writer.close());
        assert(!writer.isOpen());
        assert(!writer.write(makeCompleteRow()));
    }

    const std::string bytes = readBytes(file);
    const std::string expected =
        "utcTime,frameId,backend,width,height,cpuFrameMs,gpuFrameMs,fps,"
        "visiblePoints,submittedPoints,visibleChunks,cpuResidentBytes,gpuResidentBytes,"
        "uploadBytes,lodMisses,recordingWorkers\n"
        "1970-01-01T00:00:00.123Z,42,OpenGL,1920,1080,1.250000,2.500000,60.000000,"
        "100,200,3,400,500,600,7,8\n";
    assert(bytes == expected);
    assert(bytes.size() >= 3U);
    assert(static_cast<unsigned char>(bytes[0]) != 0xEFU);
    assert(bytes.find("\r\n") == std::string::npos);
    assert(bytes.find('\r') == std::string::npos);
    assert(bytes.back() == '\n');
    const auto lines = splitLines(bytes);
    assert(lines.size() == 2U);
    assert(lines[0].find(',') != std::string::npos);
    std::size_t headerColumns = 0U;
    for (const char character : lines[0]) {
        if (character == ',') {
            ++headerColumns;
        }
    }
    assert(headerColumns + 1U == 16U);
}

void testMissingValuesAndCsvEscaping() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "escaping.csv";
    dzc::diagnostics::PerformanceCsvWriter writer(file);
    assert(writer.isOpen());

    auto row = makeCompleteRow();
    row.backend = std::string("Vulkan, \"test\"\r\nnext");
    row.width.reset();
    row.cpuFrameMilliseconds.reset();
    row.gpuFrameMilliseconds.reset();
    row.visiblePoints.reset();
    row.recordingWorkers.reset();
    assert(writer.write(row));
    assert(writer.close());

    const auto lines = splitLines(readBytes(file));
    assert(lines.size() == 2U);
    const std::string expectedRow =
        "1970-01-01T00:00:00.123Z,42,\"Vulkan, \"\"test\"\"\r\nnext\",,1080,,,60.000000,,200,3,400,500,600,7,\n";
    assert(lines[1] + "\n" == expectedRow);
}
void testInvalidFloatingPointRejectsWholeRow() {
    const std::vector<double> invalidValues = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };

    for (const double invalid : invalidValues) {
        TemporaryDirectory directory;
        const auto file = directory.path() / "invalid.csv";
        dzc::diagnostics::PerformanceCsvWriter writer(file);
        auto row = makeCompleteRow();
        row.gpuFrameMilliseconds = invalid;
        assert(!writer.write(row));
        assert(writer.close());
        const std::string bytes = readBytes(file);
        assert(bytes.find("42,OpenGL") == std::string::npos);
    }
}

void testOpenFailures() {
    TemporaryDirectory directory;
    const auto missingParent = directory.path() / "missing" / "performance.csv";
    dzc::diagnostics::PerformanceCsvWriter missing(missingParent);
    assert(!missing.isOpen());
    assert(!missing.write(makeCompleteRow()));
    assert(!std::filesystem::exists(directory.path() / "missing"));

    dzc::diagnostics::PerformanceCsvWriter directoryPath(directory.path());
    assert(!directoryPath.isOpen());
    assert(directoryPath.close());
}

void testCloseIsIdempotentAndDestructorCloses() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "lifetime.csv";
    {
        dzc::diagnostics::PerformanceCsvWriter writer(file);
        assert(writer.write(makeCompleteRow()));
        assert(writer.close());
        assert(writer.close());
        assert(!writer.write(makeCompleteRow()));
    }
    std::ifstream input(file, std::ios::binary);
    assert(input.is_open());
    input.close();
    assert(readBytes(file).find("42,OpenGL") != std::string::npos);
}

void testLodMissesMappingAndNullBackend() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "mapping.csv";
    dzc::diagnostics::PerformanceCsvWriter writer(file);
    auto row = makeCompleteRow();
    row.backend.reset();
    row.lodMisses = 0U; // max(requests - hits, 0) for requests=2 and hits=3.
    assert(writer.write(row));
    assert(writer.close());
    const auto lines = splitLines(readBytes(file));
    assert(lines[1].find(",42,,1920,") != std::string::npos);
    assert(lines[1].find(",600,0,8") != std::string::npos);
}

void testConcurrentWritesAreWholeLines() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "concurrent.csv";
    dzc::diagnostics::PerformanceCsvWriter writer(file);

    constexpr int threadCount = 8;
    constexpr int rowsPerThread = 100;
    std::vector<std::thread> threads;
    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
        threads.emplace_back([&writer, threadIndex, rowsPerThread]() {
            for (int rowIndex = 0; rowIndex < rowsPerThread; ++rowIndex) {
                auto row = makeCompleteRow();
                row.frameId = static_cast<std::uint64_t>(threadIndex * rowsPerThread + rowIndex);
                row.backend = "thread";
                assert(writer.write(row));
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    assert(writer.close());

    const auto lines = splitLines(readBytes(file));
    assert(lines.size() == static_cast<std::size_t>(threadCount * rowsPerThread + 1));
    for (std::size_t index = 1U; index < lines.size(); ++index) {
        assert(lines[index].find("1970-01-01T00:00:00.123Z,") == 0U);
        assert(lines[index].back() != '\r');
    }
}



void testConcurrentCloseRejectsWritesAfterClose() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "close-race.csv";
    dzc::diagnostics::PerformanceCsvWriter writer(file);
    std::vector<std::thread> writers;
    for (int threadIndex = 0; threadIndex < 4; ++threadIndex) {
        writers.emplace_back([&writer, threadIndex]() {
            for (int rowIndex = 0; rowIndex < 1000; ++rowIndex) {
                auto row = makeCompleteRow();
                row.frameId = static_cast<std::uint64_t>(threadIndex * 1000 + rowIndex);
                (void)writer.write(row);
            }
        });
    }
    std::thread closer([&writer]() { assert(writer.close()); });
    closer.join();
    for (auto& thread : writers) {
        thread.join();
    }
    assert(!writer.isOpen());
    assert(!writer.write(makeCompleteRow()));
}

} // namespace

int main() {
    testHeaderAndGoldenRow();
    testMissingValuesAndCsvEscaping();
    testInvalidFloatingPointRejectsWholeRow();
    testOpenFailures();
    testCloseIsIdempotentAndDestructorCloses();
    testLodMissesMappingAndNullBackend();
    testConcurrentWritesAreWholeLines();
    testConcurrentCloseRejectsWritesAfterClose();
    return 0;
}


