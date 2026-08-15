#include <diagnostics/PerformanceSummaryWriter.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <mutex>
#include <sstream>
#include <string>

namespace dzc::diagnostics {
namespace {

constexpr const char* kPerformanceSummaryPrefix =
    "# Performance Summary\n"
    "\n"
    "## Environment\n"
    "\n"
    "| Field | Value |\n"
    "| --- | --- |\n";

constexpr const char* kDatasetPrefix =
    "\n"
    "## Dataset\n"
    "\n"
    "| Field | Value |\n"
    "| --- | --- |\n";

constexpr const char* kConfigurationPrefix =
    "\n"
    "## Configuration\n"
    "\n"
    "| Field | Value |\n"
    "| --- | --- |\n";

constexpr const char* kStatisticsPrefix =
    "\n"
    "## Statistics\n"
    "\n"
    "| Field | Value |\n"
    "| --- | --- |\n";

constexpr const char* kErrorsPrefix =
    "\n"
    "## Errors\n"
    "\n"
    "| Field | Value |\n"
    "| --- | --- |\n";

bool isValidDouble(const std::optional<double>& value) noexcept {
    return !value.has_value() || std::isfinite(*value);
}

std::string escapeMarkdownCell(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '|':
            escaped += "\\|";
            break;
        case '\r':
        case '\n':
            escaped.push_back(' ');
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }
    return escaped;
}

std::string formatOptionalString(const std::optional<std::string>& value) {
    return value.has_value() ? escapeMarkdownCell(*value) : std::string{};
}

std::string formatTbdString(const std::optional<std::string>& value) {
    return value.has_value() ? escapeMarkdownCell(*value) : "TBD";
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

std::string formatTbdDouble(const std::optional<double>& value) {
    return value.has_value() ? formatOptionalDouble(value) : "TBD";
}

void appendField(std::string& output, const char* name, const std::string& value) {
    output += "| ";
    output += name;
    output += " | ";
    output += value;
    output += " |\n";
}

std::string buildSummary(const PerformanceSummary& summary) {
    std::string output;
    output.reserve(2048U);
    output += kPerformanceSummaryPrefix;
    appendField(output, "projectVersion", formatOptionalString(summary.projectVersion));
    appendField(output, "buildType", formatOptionalString(summary.buildType));
    appendField(output, "operatingSystem", formatOptionalString(summary.operatingSystem));
    appendField(output, "cpu", formatOptionalString(summary.cpu));
    appendField(output, "gpu", formatOptionalString(summary.gpu));
    appendField(output, "driver", formatOptionalString(summary.driver));
    appendField(output, "memory", formatOptionalString(summary.memory));
    appendField(output, "gpuMemory", formatOptionalString(summary.gpuMemory));
    appendField(output, "cuda", formatOptionalString(summary.cuda));
    appendField(output, "benchmarkHardware", formatTbdString(summary.benchmarkHardware));
    appendField(output, "cameraPath", formatTbdString(summary.cameraPath));

    output += kDatasetPrefix;
    appendField(output, "datasetIdentity", formatOptionalString(summary.datasetIdentity));
    appendField(output, "pointCount", formatOptionalInteger(summary.pointCount));

    output += kConfigurationPrefix;
    appendField(output, "width", formatOptionalInteger(summary.width));
    appendField(output, "height", formatOptionalInteger(summary.height));
    appendField(output, "backend", formatOptionalString(summary.backend));
    appendField(output, "parameters", formatOptionalString(summary.parameters));

    output += kStatisticsPrefix;
    appendField(output, "sampleFrameCount", formatOptionalInteger(summary.sampleFrameCount));
    appendField(output, "averageFps", formatOptionalDouble(summary.averageFps));
    appendField(output, "averageCpuFrameMilliseconds",
                formatOptionalDouble(summary.averageCpuFrameMilliseconds));
    appendField(output, "averageGpuFrameMilliseconds",
                formatOptionalDouble(summary.averageGpuFrameMilliseconds));
    appendField(output, "lowFrameRatePercentile",
                formatTbdDouble(summary.lowFrameRatePercentile));

    output += kErrorsPrefix;
    appendField(output, "errorCount", formatOptionalInteger(summary.errorCount));
    return output;
}

} // namespace

class PerformanceSummaryWriter::Impl final {
public:
    explicit Impl(const std::filesystem::path& path) {
        m_stream.open(path, std::ios::binary | std::ios::out | std::ios::trunc);
        if (m_stream.is_open() && m_stream.good()) {
            m_isOpen = true;
        }
    }

    ~Impl() {
        close();
    }

    bool write(const PerformanceSummary& summary) {
        if (!isValidDouble(summary.averageFps) ||
            !isValidDouble(summary.averageCpuFrameMilliseconds) ||
            !isValidDouble(summary.averageGpuFrameMilliseconds) ||
            !isValidDouble(summary.lowFrameRatePercentile)) {
            return false;
        }

        const std::string output = buildSummary(summary);
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_isOpen || m_hasWritten || !m_stream.good()) {
            return false;
        }

        m_stream.write(output.data(), static_cast<std::streamsize>(output.size()));
        if (!m_stream.good()) {
            m_isOpen = false;
            return false;
        }
        m_hasWritten = true;
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
    bool m_hasWritten = false;
};

PerformanceSummaryWriter::PerformanceSummaryWriter(const std::filesystem::path& path)
    : m_impl(std::make_unique<Impl>(path)) {}

PerformanceSummaryWriter::~PerformanceSummaryWriter() {
    if (m_impl) {
        m_impl->close();
    }
}

bool PerformanceSummaryWriter::write(const PerformanceSummary& summary) {
    return m_impl != nullptr && m_impl->write(summary);
}

bool PerformanceSummaryWriter::close() noexcept {
    return m_impl == nullptr || m_impl->close();
}

bool PerformanceSummaryWriter::isOpen() const noexcept {
    return m_impl != nullptr && m_impl->isOpen();
}

} // namespace dzc::diagnostics