#pragma once

#include "diagnostics/LogTypes.h"

namespace dzc::diagnostics {

class ILogSink {
public:
    virtual ~ILogSink() = default;

    // Implementations own their synchronization policy. A false result reports
    // that the record was not accepted by this sink.
    virtual bool write(const LogRecord& record) = 0;
};

} // namespace dzc::diagnostics
