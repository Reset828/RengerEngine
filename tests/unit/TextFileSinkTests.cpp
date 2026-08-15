#include <diagnostics/TextFileSink.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("dzc-text-file-sink-" + std::to_string(seed));
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

dzc::diagnostics::LogRecord makeRecord(std::string message) {
    dzc::diagnostics::LogRecord record;
    record.level = dzc::diagnostics::LogLevel::Info;
    record.module = "Diagnostics.Test";
    record.errorCode = 7;
    record.message = std::move(message);
    return record;
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input.is_open());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    return lines;
}

void testCreatesAndAppendsUtf8Lines() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "runtime.log";

    {
        dzc::diagnostics::TextFileSink sink(file);
        assert(sink.isOpen());
        assert(sink.write(makeRecord("第一条日志")));
        assert(sink.write(makeRecord("second\nline")));
        assert(sink.close());
        assert(!sink.isOpen());
        assert(!sink.write(makeRecord("after close")));
    }

    {
        dzc::diagnostics::TextFileSink sink(file);
        assert(sink.isOpen());
        assert(sink.write(makeRecord("追加内容")));
        assert(sink.close());
    }

    const auto lines = readLines(file);
    assert(lines.size() == 3);
    assert(lines[0].find("message=\"第一条日志\"") != std::string::npos);
    assert(lines[1].find("message=\"second\\nline\"") != std::string::npos);
    assert(lines[2].find("message=\"追加内容\"") != std::string::npos);
    for (const auto& line : lines) {
        assert(line.find('\r') == std::string::npos);
    }
}

void testNoBomAndSingleLf() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "utf8.log";
    {
        dzc::diagnostics::TextFileSink sink(file);
        assert(sink.write(makeRecord("UTF-8 内容")));
        assert(sink.close());
    }

    std::ifstream input(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    assert(bytes.size() >= 4);
    assert(bytes[0] != static_cast<char>(0xEF));
    assert(bytes.find("\n") != std::string::npos);
    assert(bytes.find("\r\n") == std::string::npos);
}

void testMissingParentDirectoryFailsWithoutCreatingIt() {
    TemporaryDirectory directory;
    const auto missingParent = directory.path() / "missing";
    const auto file = missingParent / "runtime.log";
    dzc::diagnostics::TextFileSink sink(file);
    assert(!sink.isOpen());
    assert(!sink.write(makeRecord("must fail")));
    assert(!std::filesystem::exists(missingParent));
    assert(sink.close());
}

void testDirectoryPathFails() {
    TemporaryDirectory directory;
    dzc::diagnostics::TextFileSink sink(directory.path());
    assert(!sink.isOpen());
    assert(!sink.write(makeRecord("must fail")));
    assert(sink.close());
}

void testConcurrentWritesRemainWholeLines() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "concurrent.log";
    dzc::diagnostics::TextFileSink sink(file);

    constexpr int kThreadCount = 4;
    constexpr int kWritesPerThread = 25;
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

    const auto lines = readLines(file);
    assert(lines.size() == static_cast<std::size_t>(kThreadCount * kWritesPerThread));
    for (const auto& line : lines) {
        assert(line.find("message=\"thread=") != std::string::npos);
    }
}

void testBackgroundFlush() {
    TemporaryDirectory directory;
    const auto file = directory.path() / "timer.log";
    dzc::diagnostics::TextFileSink sink(file);
    assert(sink.write(makeRecord("timer flush")));

    std::this_thread::sleep_for(std::chrono::milliseconds(3500));
    std::ifstream input(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    assert(bytes.find("message=\"timer flush\"") != std::string::npos);
    assert(sink.close());
}

} // namespace

int main() {
    testCreatesAndAppendsUtf8Lines();
    testNoBomAndSingleLf();
    testMissingParentDirectoryFailsWithoutCreatingIt();
    testDirectoryPathFails();
    testConcurrentWritesRemainWholeLines();
    testBackgroundFlush();
    return 0;
}

