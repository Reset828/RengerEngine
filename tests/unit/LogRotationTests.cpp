#include <diagnostics/RotatingFileSink.h>
#include <diagnostics/TextFileSink.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("dzc-log-rotation-" + std::to_string(seed));
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

dzc::diagnostics::LogRecord makeRecord(std::string message,
                                        dzc::diagnostics::LogLevel level =
                                            dzc::diagnostics::LogLevel::Info) {
    dzc::diagnostics::LogRecord record;
    record.level = level;
    record.module = "Diagnostics.RotationTest";
    record.errorCode = 7;
    record.message = std::move(message);
    return record;
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    return lines;
}

std::string readAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void testMultipleRotationsUseNewestFirstOrderAndCleanup() {
    TemporaryDirectory directory;
    const auto active = directory.path() / "render.log";
    dzc::diagnostics::RotatingFileSink sink(active, 120U, 3U);

    for (int index = 1; index <= 6; ++index) {
        assert(sink.write(makeRecord("record-" + std::to_string(index))));
    }
    assert(sink.close());

    assert(std::filesystem::exists(active));
    assert(std::filesystem::exists(active.string() + ".1"));
    assert(std::filesystem::exists(active.string() + ".2"));
    assert(!std::filesystem::exists(active.string() + ".3"));

    assert(readAll(active).find("record-6") != std::string::npos);
    assert(readAll(active.string() + ".1").find("record-5") != std::string::npos);
    assert(readAll(active.string() + ".2").find("record-4") != std::string::npos);
}

void testExistingOversizedFileRotatesBeforeNextWrite() {
    TemporaryDirectory directory;
    const auto active = directory.path() / "existing.log";
    {
        std::ofstream output(active, std::ios::binary);
        output << "existing-content";
    }

    dzc::diagnostics::RotatingFileSink sink(active, 8U, 3U);
    assert(sink.write(makeRecord("after-existing")));
    assert(sink.close());

    assert(readAll(active.string() + ".1").find("existing-content") != std::string::npos);
    assert(readAll(active).find("after-existing") != std::string::npos);
}

void testOversizedSingleRecordIsNotDiscarded() {
    TemporaryDirectory directory;
    const auto active = directory.path() / "oversized.log";
    dzc::diagnostics::RotatingFileSink sink(active, 8U, 3U);

    assert(sink.write(makeRecord("this record is larger than the configured limit")));
    assert(sink.close());
    assert(readAll(active).find("this record is larger") != std::string::npos);
}

void testRotationFailureKeepsWritingCurrentFile() {
    TemporaryDirectory directory;
    const auto active = directory.path() / "current.log";
    const auto collision = std::filesystem::path(active.string() + ".1");
    const auto oldestCollision = std::filesystem::path(active.string() + ".2");
    std::filesystem::create_directories(collision);
    std::filesystem::create_directories(oldestCollision);
    std::ofstream(collision / "blocker") << "keep-rotation-from-renaming";
    std::ofstream(oldestCollision / "blocker") << "keep-rotation-from-removing";
    std::ofstream(active) << "before-failure\n";

    dzc::diagnostics::RotatingFileSink sink(active, 8U, 3U);
    assert(sink.write(makeRecord("after-failed-rotation")));
    assert(sink.close());

    assert(readAll(active).find("after-failed-rotation") != std::string::npos);
    assert(std::filesystem::is_directory(collision));
}

void testFallbackReceivesRecordsWhenActiveFileCannotOpen() {
    TemporaryDirectory directory;
    const auto missingParent = directory.path() / "missing";
    const auto active = missingParent / "primary.log";
    const auto fallbackPath = directory.path() / "fallback.log";
    auto fallback = std::make_shared<dzc::diagnostics::TextFileSink>(fallbackPath);

    dzc::diagnostics::RotatingFileSink sink(active, 32U, 3U, fallback);
    assert(!sink.isOpen());
    assert(sink.write(makeRecord("fallback message", dzc::diagnostics::LogLevel::Error)));
    assert(sink.close());
    assert(fallback->close());
    assert(readAll(fallbackPath).find("fallback message") != std::string::npos);
}

void testErrorRecordsSurviveRotations() {
    TemporaryDirectory directory;
    const auto active = directory.path() / "errors.log";
    dzc::diagnostics::RotatingFileSink sink(active, 100U, 10U);

    assert(sink.write(makeRecord("first", dzc::diagnostics::LogLevel::Info)));
    assert(sink.write(makeRecord("important-error", dzc::diagnostics::LogLevel::Error)));
    for (int index = 0; index < 5; ++index) {
        assert(sink.write(makeRecord("filler-" + std::to_string(index))));
    }
    assert(sink.close());

    bool foundError = false;
    for (std::size_t index = 0; index < 10U; ++index) {
        const auto path = index == 0U
                              ? active
                              : std::filesystem::path(active.string() + "." +
                                                      std::to_string(index));
        if (std::filesystem::exists(path) && readAll(path).find("important-error") !=
                                                std::string::npos) {
            foundError = true;
        }
    }
    assert(foundError);
}

void testConcurrentWritesRemainWholeLines() {
    TemporaryDirectory directory;
    const auto active = directory.path() / "concurrent.log";
    dzc::diagnostics::RotatingFileSink sink(active, 1000U, 10U);

    constexpr int kThreadCount = 4;
    constexpr int kWritesPerThread = 20;
    std::vector<std::thread> writers;
    for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex) {
        writers.emplace_back([&sink, threadIndex, kWritesPerThread] {
            for (int writeIndex = 0; writeIndex < kWritesPerThread; ++writeIndex) {
                assert(sink.write(makeRecord("thread=" + std::to_string(threadIndex) +
                                             ",write=" + std::to_string(writeIndex))));
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }
    assert(sink.close());

    std::size_t lineCount = 0;
    for (std::size_t index = 0; index < 10U; ++index) {
        const auto path = index == 0U
                              ? active
                              : std::filesystem::path(active.string() + "." +
                                                      std::to_string(index));
        if (!std::filesystem::exists(path)) {
            continue;
        }
        for (const auto& line : readLines(path)) {
            assert(line.find("message=\"thread=") != std::string::npos);
            ++lineCount;
        }
    }
    assert(lineCount == static_cast<std::size_t>(kThreadCount * kWritesPerThread));
}

} // namespace

int main() {
    testMultipleRotationsUseNewestFirstOrderAndCleanup();
    testExistingOversizedFileRotatesBeforeNextWrite();
    testOversizedSingleRecordIsNotDiscarded();
    testRotationFailureKeepsWritingCurrentFile();
    testFallbackReceivesRecordsWhenActiveFileCannotOpen();
    testErrorRecordsSurviveRotations();
    testConcurrentWritesRemainWholeLines();
    return 0;
}







