# src/diagnostics

## OVERVIEW
Structured runtime observability: asynchronous logs, frame statistics, metrics, and performance files.

## TARGET
- CMake target: `dzc_diagnostics` static library.
- Public include root is the parent `src` directory; consumers include `diagnostics/...`.
- Current sources: `MetricsRegistry`, `FrameStatistics`, `Logger`, file sinks, CSV, and summary writers.

## DATA CONTRACTS
- `LogRecord` carries timestamp, level, module, error code, optional dataset/chunk/frame IDs, message, and ordered context.
- `formatLogRecord` emits escaped structured text; preserve context keys and error codes.
- `MetricsRegistry` groups performance, geometry, transfer, memory, LOD, runtime, recording, and compute counters.
- `FrameStatistics` maintains frame-window and time-window FPS/frame-time snapshots using an injectable clock.
- `PerformanceCsvWriter` writes per-frame optional columns; `PerformanceSummaryWriter` writes aggregate results.

## RUNTIME FLOW
- Producers call `Logger::write`; logger queues records and uses primary/fallback `ILogSink` paths.
- `TextFileSink` flushes periodically on its own thread and joins it during close.
- `RotatingFileSink` enforces file-size/file-count rotation and can delegate failures to a fallback sink.
- Engine frame stages update `MetricsRegistry`/`FrameStatistics`; writers consume snapshots/rows later.
- Close sinks and writers before their dependent logger/worker objects disappear.

## CONVENTIONS
- Sink implementations own synchronization; callers should only rely on `bool write` acceptance.
- Use `std::optional` to represent unavailable GPU/size/backend measurements, not magic zeroes.
- Validate finite metric values where setters return `bool`; do not publish NaN/inf measurements.
- Keep timestamps/context deterministic enough for tests; use the shared formatting helpers.
- Preserve `ErrorDomain`/code context when adapting engine errors into `LogRecord` values.

## ANTI-PATTERNS (THIS DIRECTORY)
- Do not write directly to a shared `ofstream` from engine or UI call sites.
- Do not assume a sink is open after construction; check `isOpen`/write results and use fallback behavior.
- Do not call `close` from a sink worker in a way that joins the current thread.
- Do not update metrics from multiple paths with conflicting frame IDs or double-counted points.
- Do not fabricate GPU timings when timer queries are unavailable; retain the optional state.
- Do not format log messages with unescaped quotes, control characters, or unordered context.
- Do not turn diagnostics failure into engine success when a required performance artifact cannot be written.
