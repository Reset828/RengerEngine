# src/data

## OVERVIEW
Point-cloud ingestion and dataset representation; produces validated CPU-side chunks for engine use.

## TARGETS
- `dzc_data_core` is a static library containing chunk algorithms and reader orchestration.
- `dzc_data_core` links `dzc_engine_api`, `dzc_tasks`, `dzc_diagnostics`, and `glm::glm`.
- `src/data/io/pcl` is a subdirectory and the concrete PCL target is `dzc_data_pcl`.

## STRUCTURE
- `chunk/`: bounds, grid bucketing/splitting, run files/merge, `Chunk`, `Dataset`.
- `io/`: reader interface/registry and worker-side `PointCloudLoadTask`.
- `cache/`, `lod/`: adjacent data-domain extension points; not part of the current data CMake source list.

## DOMAIN FLOW
- `PointCloudReaderRegistry` chooses a reader from the final lowercase `.pcd`/`.ply` extension.
- `PointCloudLoadTask` opens the reader, reports metadata/progress, and emits validated `PointBatch` values.
- Chunk algorithms bucket/split/merge batches while preserving schema, ordering, and source identity.
- `Dataset::create` validates metadata plus chunks and computes total point count.
- Engine dataset-session code consumes data events and publishes state/snapshot changes.

## WHERE TO LOOK
- Start reader lifecycle questions in `io/IPointCloudReader.h`.
- Start ingestion scheduling in `io/PointCloudLoadTask.*`.
- Start deterministic partitioning in `chunk/GridCellSplitter.*` and `GridChunkBuilder.*`.
- Start external-sort persistence in `chunk/GridRunFile.*` and `GridRunMerger.*`.
- Start residency/state transitions in `chunk/Chunk.*` and aggregate invariants in `Dataset.*`.
- Use `PointAttributes.h`, `CoordinateLocalizer`, and `RelativeOrigin` for coordinate/schema rules.

## CONVENTIONS
- Return `dzc::Result<T>` for validation, I/O, cancellation, and resource failures.
- Propagate `tasks::CancellationToken` through long-running chunk and reader operations.
- Preserve deterministic order; tests rely on stable source-index and `GridCellKey` behavior.
- Keep PCL implementation types private to `dzc_data_pcl`; core headers use project data types only.

## ANTI-PATTERNS (THIS DIRECTORY)
- Do not couple chunk algorithms to Qt, OpenGL/Vulkan, CUDA, or PCL headers.
- Do not construct datasets from unchecked batches, non-finite coordinates, or mismatched attributes.
- Do not turn a load callback into a GUI callback; callbacks run on task workers.
- Do not ignore backpressure/cancellation while accumulating or writing intermediate runs.
- Do not use unordered iteration where output order is part of the file or merge contract.
- Do not make `dzc_data_core` discover PCL merely to support a new reader format.
