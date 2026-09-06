# src/data/io

## OVERVIEW
Reader abstraction and worker-side point-cloud loading; PCL stays behind the concrete reader target.

## TARGETS
- Core reader/registry/load-task sources compile into `dzc_data_core`.
- `io/pcl/CMakeLists.txt` defines `dzc_data_pcl`; it is the only PCL integration boundary.
- Supported registry extensions are final, case-insensitive `.pcd` and `.ply`.

## READER CONTRACT
- `IPointCloudReader::open(path)` returns `PointCloudSourceInfo` metadata.
- `readNext(maximumPoints, token)` returns a bounded optional `PointBatch`; empty means EOF.
- `readProgress()` reports consumed points and optional total source points.
- `close()` is noexcept and resets opened-reader state.
- `PcdReader` and `PlyReader` keep PCL types in private Pimpl implementations.

## LOAD FLOW
- Registry creates an injected reader from the source extension.
- `PointCloudLoadTask::submit` validates request dependencies and associates a `DatasetId`.
- Worker opens reader, invokes `onOpened`, then loops progress -> batch -> `onBatch`.
- `ConcurrencyGate` limits active reader work; `BackpressureController` gates downstream pressure.
- Events report progress, loaded, cancelled, or structured error outcomes.
- Reader close is guarded even when callbacks or PCL operations throw.

## CONVENTIONS
- Reader callbacks execute on the `TaskSystem` worker, never the submitter/GUI thread.
- Validate batch shape and reader progress before forwarding downstream.
- Check cancellation before and after blocking/expensive reader and callback steps.
- Preserve callback errors; do not replace them with generic EOF or success.

## ANTI-PATTERNS (THIS DIRECTORY)
- Do not include PCL headers from `IPointCloudReader`, registry, or load-task interfaces.
- Do not read an entire source into memory when bounded batches are requested.
- Do not omit `close()` on EOF, cancellation, callback failure, or exception.
- Do not report progress that moves backward or exceeds a declared total.
- Do not invoke UI work directly from `onOpened`, `onBatch`, or `onEvent`.
- Do not bypass concurrency/backpressure controls to increase ingestion throughput.
- Do not accept unsupported extensions by guessing a reader from arbitrary text.
