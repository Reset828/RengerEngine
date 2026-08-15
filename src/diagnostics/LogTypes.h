#pragma once

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <ctime>

namespace dzc::diagnostics {

enum class LogLevel : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warn,
    Error
};

using LogContext = std::map<std::string, std::string>;

struct LogRecord final {
    std::chrono::system_clock::time_point timestamp{};
    LogLevel level{LogLevel::Info};
    std::string module;
    std::uint32_t errorCode{0};
    std::optional<std::uint64_t> dataset;
    std::optional<std::uint64_t> chunk;
    std::optional<std::uint64_t> frame;
    std::string message;
    LogContext context;
};

inline std::string_view logLevelName(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "INFO";
}

inline std::string escapeLogText(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const unsigned char character : text) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (character < 0x20U) {
                std::ostringstream control;
                control << "\\u" << std::hex << std::uppercase << std::setw(4)
                        << std::setfill('0') << static_cast<unsigned int>(character);
                escaped += control.str();
            } else {
                escaped.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return escaped;
}

inline std::string formatLogTimestamp(std::chrono::system_clock::time_point timestamp) {
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(timestamp);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(timestamp - seconds).count();
    const auto time = std::chrono::system_clock::to_time_t(seconds);

    std::tm localTime{};
    std::tm utcTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
    gmtime_s(&utcTime, &time);
#else
    localtime_r(&time, &localTime);
    gmtime_r(&time, &utcTime);
#endif

    const std::time_t localAsUtc =
#ifdef _WIN32
        _mkgmtime(&localTime);
#else
        timegm(&localTime);
#endif
    const std::time_t utcAsUtc =
#ifdef _WIN32
        _mkgmtime(&utcTime);
#else
        timegm(&utcTime);
#endif
    const auto offsetMinutes = static_cast<long long>(
        std::difftime(localAsUtc, utcAsUtc) / 60.0);

    const char sign = offsetMinutes >= 0 ? '+' : '-';
    const auto absoluteOffset = offsetMinutes >= 0 ? offsetMinutes : -offsetMinutes;
    const auto offsetHours = absoluteOffset / 60;
    const auto remainingOffsetMinutes = absoluteOffset % 60;

    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << (localTime.tm_year + 1900) << '-'
           << std::setw(2) << (localTime.tm_mon + 1) << '-' << std::setw(2)
           << localTime.tm_mday << 'T' << std::setw(2) << localTime.tm_hour << ':'
           << std::setw(2) << localTime.tm_min << ':' << std::setw(2)
           << localTime.tm_sec << '.' << std::setw(3) << milliseconds << sign
           << std::setw(2) << offsetHours << ':' << std::setw(2)
           << remainingOffsetMinutes;
    return output.str();
}

inline std::string formatLogRecord(const LogRecord& record) {
    std::ostringstream output;
    output << formatLogTimestamp(record.timestamp) << " [" << logLevelName(record.level)
           << "] [" << escapeLogText(record.module) << "] code=" << record.errorCode;
    if (record.dataset.has_value()) {
        output << " dataset=" << *record.dataset;
    }
    if (record.chunk.has_value()) {
        output << " chunk=" << *record.chunk;
    }
    if (record.frame.has_value()) {
        output << " frame=" << *record.frame;
    }
    for (const auto& [key, value] : record.context) {
        output << ' ' << escapeLogText(key) << "=\"" << escapeLogText(value) << '\"';
    }
    output << " message=\"" << escapeLogText(record.message) << '\"';
    return output.str();
}

} // namespace dzc::diagnostics
