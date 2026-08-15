#include "diagnostics/PerformanceCsvWriter.h"

#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <locale>
#include <mutex>
#include <sstream>
#include <string>

namespace dzc::diagnostics {
namespace {

constexpr const char* kHeader =
    "utcTime,frameId,backend,width,height,cpuFrameMs,gpuFrameMs,fps,"
    "visiblePoints,submittedPoints,visibleChunks,cpuResidentBytes,gpuResidentBytes,"
    "uploadBytes,lodMisses,recordingWorkers\n";

bool isValidDouble(const std::optional<double>& value) noexcept {
    return !value.has_value() || std::isfinite(*value);
}

std::string formatUtcTime(const std::chrono::system_clock::time_point time) {
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        time.time_since_epoch());
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(millis);
    auto remainder = millis - seconds;
    if (remainder.count() < 0) {
        --seconds;
        remainder += std::chrono::seconds(1);
    }

    const std::time_t timeValue = static_cast<std::time_t>(seconds.count());
    std::tm utc{};
#if defined(_WIN32)
    if (gmtime_s(&utc, &timeValue) != 0) {
        return {};
    }
#else
    if (gmtime_r(&timeValue, &utc) == nullptr) {
        return {};
    }
#endif

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setfill('0') << std::setw(4) << (utc.tm_year + 1900) << '-'
           << std::setw(2) << (utc.tm_mon + 1) << '-'
           << std::setw(2) << utc.tm_mday << 'T'
           << std::setw(2) << utc.tm_hour << ':'
           << std::setw(2) << utc.tm_min << ':'
           << std::setw(2) << utc.tm_sec << '.'
           << std::setw(3) << remainder.count() << 'Z';
    return stream.str();
}

template <typename T>
std::string formatOptionalInteger(const std::optional<T>& value) {
    return value.has_value() ? std::to_string(*value) : std::string{};
}

std::string formatOptionalDouble(const std::optional<double>& value) {
    if (!value.has_value()) {
        return {};
    }

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(6) << *value;
    return stream.str();
}

std::string escapeCsv(const std::optional<std::string>& value) {
    if (!value.has_value()) {
        return {};
    }

    const std::string& text = *value;
    if (text.find_first_of(",\"\r\n") == std::string::npos) {
        return text;
    }

    std::string escaped;
    escaped.reserve(text.size() + 2U);
    escaped.push_back('"');
    for (const char character : text) {
        if (character == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

} // namespace

class PerformanceCsvWriter::Impl final {
public:
    explicit Impl(const std::filesystem::path& path) {
        m_stream.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!m_stream.is_open()) {
            return;
        }

        m_stream.write(kHeader, static_cast<std::streamsize>(std::char_traits<char>::length(kHeader)));
        if (m_stream.good()) {
            m_isOpen = true;
        } else {
            m_stream.close();
        }
    }

    ~Impl() {
        close();
    }

    bool write(const PerformanceCsvRow& row) {
        if (!isValidDouble(row.cpuFrameMilliseconds) ||
            !isValidDouble(row.gpuFrameMilliseconds) ||
            !isValidDouble(row.framesPerSecond)) {
            return false;
        }

        const std::string utcTime = formatUtcTime(row.utcTime);
        if (utcTime.empty()) {
            return false;
        }

        std::ostringstream line;
        line.imbue(std::locale::classic());
        line << utcTime << ','
             << row.frameId << ','
             << escapeCsv(row.backend) << ','
             << formatOptionalInteger(row.width) << ','
             << formatOptionalInteger(row.height) << ','
             << formatOptionalDouble(row.cpuFrameMilliseconds) << ','
             << formatOptionalDouble(row.gpuFrameMilliseconds) << ','
             << formatOptionalDouble(row.framesPerSecond) << ','
             << formatOptionalInteger(row.visiblePoints) << ','
             << formatOptionalInteger(row.submittedPoints) << ','
             << formatOptionalInteger(row.visibleChunks) << ','
             << formatOptionalInteger(row.cpuResidentBytes) << ','
             << formatOptionalInteger(row.gpuResidentBytes) << ','
             << formatOptionalInteger(row.uploadBytes) << ','
             << formatOptionalInteger(row.lodMisses) << ','
             << formatOptionalInteger(row.recordingWorkers) << '\n';
        const std::string output = line.str();

        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isOpen || !m_stream.good()) {
            return false;
        }

        m_stream.write(output.data(), static_cast<std::streamsize>(output.size()));
        if (!m_stream.good()) {
            m_isOpen = false;
            return false;
        }
        return true;
    }

    bool close() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_stream.is_open()) {
            m_isOpen = false;
            return true;
        }

        bool success = true;
        m_stream.flush();
        if (!m_stream.good()) {
            success = false;
        }
        m_stream.close();
        if (m_stream.fail()) {
            success = false;
        }
        m_isOpen = false;
        return success;
    }

    bool isOpen() const noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_isOpen;
    }

private:
    mutable std::mutex m_mutex;
    std::ofstream m_stream;
    bool m_isOpen = false;
};

PerformanceCsvWriter::PerformanceCsvWriter(const std::filesystem::path& path)
    : m_impl(std::make_unique<Impl>(path)) {}

PerformanceCsvWriter::~PerformanceCsvWriter() {
    if (m_impl) {
        m_impl->close();
    }
}

bool PerformanceCsvWriter::write(const PerformanceCsvRow& row) {
    return m_impl != nullptr && m_impl->write(row);
}

bool PerformanceCsvWriter::close() noexcept {
    return m_impl == nullptr || m_impl->close();
}

bool PerformanceCsvWriter::isOpen() const noexcept {
    return m_impl != nullptr && m_impl->isOpen();
}

} // namespace dzc::diagnostics


