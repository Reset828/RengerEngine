#pragma once

#include "dzc/EngineEvent.h"

#include <QString>
#include <cstddef>
#include <memory>

namespace dzc {

class LogPanelModel final {
public:
    // Creates a GUI-thread log history retaining at most 1000 entries.
    LogPanelModel();
    // Releases the private log history.
    ~LogPanelModel();

    LogPanelModel(const LogPanelModel&) = delete;
    LogPanelModel& operator=(const LogPanelModel&) = delete;
    LogPanelModel(LogPanelModel&&) = delete;
    LogPanelModel& operator=(LogPanelModel&&) = delete;

    // Appends supported diagnostic events; returns false for dataset lifecycle events.
    bool append(const EngineEvent& event);
    // Returns retained entries as plain text, or the empty-history placeholder.
    QString text() const;
    // Returns the number of retained events, not the number of text lines.
    std::size_t size() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc
