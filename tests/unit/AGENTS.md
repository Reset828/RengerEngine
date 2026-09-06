# Unit Test Guide

## OVERVIEW
- Deterministic executable-per-contract coverage for library and internal components.
- Configure each executable in `tests/unit/CMakeLists.txt`.
- Most tests use `<cassert>` and a local `main()`; no external test framework.
- Link the narrowest production target(s) needed by the contract.

## Target map: public/engine
- `EngineTypesTests.cpp` -> `dzc_engine_types` -> `dzc_engine_api`.
- `EngineCommandTests.cpp` -> `dzc_engine_command` -> `dzc_engine_api`.
- `EngineEventTests.cpp` -> `dzc_engine_event` -> `dzc_engine_api`.
- `EngineSnapshotTests.cpp` -> `dzc_engine_snapshot` -> `dzc_engine_api`.
- `ResultTests.cpp` -> `dzc_result` -> `dzc_engine_api`.
- `EngineConfigTests.cpp` -> `dzc_engine_config` -> `dzc_engine_api`.
- `EnginePublicApiTests.cpp` -> `dzc_engine_public_api`.
- `EngineStateMachineTests.cpp`, `EngineCoordinatorTests.cpp` -> engine core coverage.
- `EngineQueuePolicyTests.cpp`, `SnapshotPublicationTests.cpp` -> queue/snapshot rules.
- `EngineLifecycleRollbackTests.cpp` -> failed initialization rollback.
- `DatasetReplacementTests.cpp` -> dataset transition behavior.

## Target map: data/camera/tasks
- Geometry: `Bounds3dTests.cpp`, `ViewFrustumTests.cpp`, `FrustumCullingTests.cpp`.
- Point schema/batches: `PointAttributesTests.cpp`, `PointBatchTests.cpp`, `IntensityQuantizerTests.cpp`.
- Chunk/dataset/localization: `ChunkStateTests.cpp`, `DatasetTests.cpp`, `CoordinateLocalizerTests.cpp`.
- Reader contracts: `ReaderContractTests.cpp`, `ReaderRegistryTests.cpp`.
- Grid contracts: parameter, key, bucket, run, merger, splitter, builder tests.
- Task/concurrency: cancellation, queue, coalescer, configuration, system, completion, gate, backpressure, shutdown.
- Camera/input: type, event, controller-contract, and orbit-controller tests.
- Diagnostics: logger, sinks, rotation, frame statistics, metrics, CSV/summary writers.
- App seams: command-line, settings, and `EngineUiAdapter` tests add selected app `.cpp` files.

## Test construction
- Name one behavior-focused test function per scenario.
- Invoke every test explicitly from `main()`.
- Use fakes for clocks, readers, controllers, queues, and backends.
- Make fake state/call counts observable rather than inferring hidden behavior.
- Include internal headers through private `${PROJECT_SOURCE_DIR}/src` only when needed.
- Use `Result` correctly: test success and failure before `value()`/`error()` access.

## Invariants worth preserving
- Engine enum/state-machine transitions are stable contracts.
- Queue capacities must be nonzero and bounded behavior is intentional.
- Readers preserve lifecycle: open, batched reads, repeated EOF, close/reopen.
- Cancellation must not advance a cancelled read unexpectedly.
- Spatial values reject nonfinite or invalid bounds.
- Grid partition tests must preserve every point and nonoverlapping partitions.
- Clock-based tests inject time; do not sleep.
- Concurrent tests assert synchronization contracts without nondeterministic races.

## OpenGL-adjacent exception
- `ShaderLayoutTests.cpp` is conditional on `DZC_ENABLE_OPENGL`.
- It links `dzc_render_opengl` and uses `DZC_SOURCE_DIR` for shader fixtures.
- Its real-context variant returns `77` for CTest skipping.

## ANTI-PATTERNS
- Do not make a unit test require PCL file parsing, Qt widgets, or hardware GL.
- Do not test a public default by duplicating implementation internals.
- Do not omit regression coverage for failure cleanup, move state, or idempotence.
- Do not use wall-clock sleeps to prove task or synchronization behavior.
- Do not broaden links merely to avoid adding the exact required source/target dependency.
