# src/data/chunk

## OVERVIEW
Deterministic grid chunking and CPU dataset state, including temporary external-sort runs.

## CORE TYPES
- `PointBatch`: validated point positions plus optional color/intensity attributes.
- `AttributeSchema`: shared attribute presence/shape contract across buckets and chunks.
- `GridCellKey`, `GridParameters`: integer grid identity and spatial quantization inputs.
- `GridBucketStore`: bounded in-memory buckets with resident-byte and point counters.
- `GridCellSplitter`: deterministic oversized-cell partitioning with cancellation checks.
- `GridChunkBuilder`: converts bucket data into chunk metadata/CPU payloads.
- `GridRunFile`: `.dzgrun.pending` -> committed run lifecycle for external sorting.
- `GridRunMerger`: validates sorted inputs, merges cells, preserves source-index order.
- `Chunk`: explicit `MetadataOnly` -> CPU/upload/GPU/eviction/error state machine.
- `Dataset`: validated chunk aggregate with total point count and source metadata.

## DOMAIN FLOW
- Normalize/localize source coordinates using `CoordinateLocalizer` and `RelativeOrigin`.
- Quantize intensity only against a valid finite source range.
- Add points to grid buckets under schema and byte-budget constraints.
- Split oversized cells; optionally write sorted run files for later merge.
- Merge runs, build chunks, then create the validated `Dataset` aggregate.
- Engine/session code drives chunk residency transitions after data events.

## INVARIANTS
- Bounds, origins, ranges, and coordinates must remain finite.
- Attribute vectors must match point count and one shared schema.
- `GridCellKey` input groups are strictly increasing for merge.
- Stable source indices decide ties; axis tie-breaking is deterministic.
- Point counts and byte-size arithmetic must be checked before narrowing.
- Cancellation returns a task-domain cancellation error, not partial success.

## ANTI-PATTERNS (THIS DIRECTORY)
- Do not sort by unstable pointer/object order or rely on hash iteration order.
- Do not append attributes independently of positions or silently change schema mid-stream.
- Do not call `Chunk::completeUpload` without the corresponding queue/upload transition.
- Do not treat a pending `.dzgrun` file as a completed merge input.
- Do not continue allocating after cancellation has been observed in long loops.
- Do not use world-space doubles directly where the local-origin contract is required.
- Do not hide corrupt-data/resource failures behind empty vectors or sentinel chunks.
