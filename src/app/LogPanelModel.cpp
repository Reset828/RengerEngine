#include "LogPanelModel.h"

#include <QStringList>
#include <deque>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace dzc {

struct LogPanelModel::Impl final {
    std::deque<QString> entries;
};

namespace {

constexpr std::size_t kMaximumEntries = 1000U;

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

QString severityPrefix(EventSeverity value) {
    switch (value) {
    case EventSeverity::Info: return QStringLiteral("[Info]");
    case EventSeverity::Warning: return QStringLiteral("[Warning]");
    case EventSeverity::RecoverableError: return QStringLiteral("[RecoverableError]");
    case EventSeverity::FatalError: return QStringLiteral("[FatalError]");
    }
    return QStringLiteral("[Unknown]");
}

QString eventContextText(const EventContext& context) {
    QStringList fields;
    if (context.datasetId.value != 0U) {
        fields.append(QStringLiteral("DatasetId=%1").arg(static_cast<qulonglong>(context.datasetId.value)));
    }
    if (context.chunkId.value != 0U) {
        fields.append(QStringLiteral("ChunkId=%1").arg(static_cast<qulonglong>(context.chunkId.value)));
    }
    if (context.taskId.value != 0U) {
        fields.append(QStringLiteral("TaskId=%1").arg(static_cast<qulonglong>(context.taskId.value)));
    }
    if (context.frameId.value != 0U) {
        fields.append(QStringLiteral("FrameId=%1").arg(static_cast<qulonglong>(context.frameId.value)));
    }
    return fields.isEmpty() ? QString{} : QStringLiteral(" [") + fields.join(QStringLiteral(" ")) + QStringLiteral("]");
}

} // namespace

LogPanelModel::LogPanelModel() : m_impl(std::make_unique<Impl>()) {}
LogPanelModel::~LogPanelModel() = default;

bool LogPanelModel::append(const EngineEvent& event) {
    std::optional<QString> entry;
    std::visit([&entry](const auto& value) {
        using Event = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Event, MessageEvent>) {
            entry = severityPrefix(value.severity) + QStringLiteral(" ") + fromUtf8(value.message)
                + eventContextText(value.context);
        } else if constexpr (std::is_same_v<Event, ErrorEvent>) {
            QString line = severityPrefix(value.severity) + QStringLiteral(" ") + fromUtf8(value.error.userMessage);
            if (!value.error.diagnosticMessage.empty()) {
                line += QStringLiteral(" | ") + fromUtf8(value.error.diagnosticMessage);
            }
            if (!value.error.context.empty()) {
                line += QStringLiteral(" [") + fromUtf8(value.error.context) + QStringLiteral("]");
            }
            line += QStringLiteral(" [Domain=%1 Code=%2]")
                .arg(static_cast<unsigned int>(value.error.domain)).arg(value.error.code);
            entry = line + eventContextText(value.context);
        } else if constexpr (std::is_same_v<Event, FeatureDegradedEvent>) {
            entry = QStringLiteral("[Warning] ") + fromUtf8(value.feature) + QStringLiteral(": ") + fromUtf8(value.reason);
        }
    }, event);
    if (!entry) {
        return false;
    }
    m_impl->entries.push_back(std::move(*entry));
    if (m_impl->entries.size() > kMaximumEntries) {
        m_impl->entries.pop_front();
    }
    return true;
}

QString LogPanelModel::text() const {
    if (m_impl->entries.empty()) {
        return QStringLiteral("No log entries.");
    }
    QStringList lines;
    lines.reserve(static_cast<int>(m_impl->entries.size()));
    for (const auto& entry : m_impl->entries) {
        lines.append(entry);
    }
    return lines.join(QStringLiteral("\n"));
}

std::size_t LogPanelModel::size() const noexcept {
    return m_impl->entries.size();
}

} // namespace dzc
