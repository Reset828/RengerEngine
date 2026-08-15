#include <diagnostics/ILogSink.h>

#include <cassert>
#include <chrono>
#include <string>
#include <vector>

namespace {

class MemorySink final : public dzc::diagnostics::ILogSink {
public:
    bool write(const dzc::diagnostics::LogRecord& record) override {
        records.push_back(dzc::diagnostics::formatLogRecord(record));
        return true;
    }

    std::vector<std::string> records;
};

void testDefaults() {
    const dzc::diagnostics::LogRecord record;
    assert(record.level == dzc::diagnostics::LogLevel::Info);
    assert(record.module.empty());
    assert(record.errorCode == 0);
    assert(!record.dataset.has_value());
    assert(!record.chunk.has_value());
    assert(!record.frame.has_value());
    assert(record.message.empty());
    assert(record.context.empty());
}

void testLevelNames() {
    using dzc::diagnostics::LogLevel;
    assert(dzc::diagnostics::logLevelName(LogLevel::Trace) == "TRACE");
    assert(dzc::diagnostics::logLevelName(LogLevel::Debug) == "DEBUG");
    assert(dzc::diagnostics::logLevelName(LogLevel::Info) == "INFO");
    assert(dzc::diagnostics::logLevelName(LogLevel::Warn) == "WARN");
    assert(dzc::diagnostics::logLevelName(LogLevel::Error) == "ERROR");
}

void testMemorySinkFormatsCompleteRecord() {
    dzc::diagnostics::LogRecord record;
    record.timestamp = std::chrono::system_clock::time_point{};
    record.level = dzc::diagnostics::LogLevel::Warn;
    record.module = "Render.Vulkan";
    record.errorCode = 42;
    record.dataset = 7;
    record.chunk = 18;
    record.frame = 42;
    record.message = "点云\n加载\"失败\"\\";
    record.context.emplace("datasetName", "示例数据集");
    record.context.emplace("path", "C:\\data\\chunk\t01");

    MemorySink sink;
    assert(sink.write(record));
    assert(sink.records.size() == 1);
    const std::string& line = sink.records.front();
    assert(line.find(" [WARN] [Render.Vulkan] code=42 dataset=7 chunk=18 frame=42") !=
           std::string::npos);
    assert(line.find("datasetName=\"示例数据集\"") != std::string::npos);
    assert(line.find("path=\"C:\\\\data\\\\chunk\\t01\"") != std::string::npos);
    assert(line.find("message=\"点云\\n加载\\\"失败\\\"\\\\\"") != std::string::npos);
    assert(line.find('\n') == std::string::npos);
    assert(line.find('\r') == std::string::npos);
}

void testMemorySinkFormatsMissingOptionalFields() {
    dzc::diagnostics::LogRecord record;
    record.timestamp = std::chrono::system_clock::time_point{};
    record.module = "Loader";
    record.message = "ok";

    MemorySink sink;
    assert(sink.write(record));
    const std::string& line = sink.records.front();
    assert(line.find("dataset=") == std::string::npos);
    assert(line.find("chunk=") == std::string::npos);
    assert(line.find("frame=") == std::string::npos);
    const std::string suffix = "message=\"ok\"";
    assert(line.size() >= suffix.size());
    assert(line.compare(line.size() - suffix.size(), suffix.size(), suffix) == 0);
}

} // namespace

int main() {
    testDefaults();
    testLevelNames();
    testMemorySinkFormatsCompleteRecord();
    testMemorySinkFormatsMissingOptionalFields();
    return 0;
}


