# src/engine

## OVERVIEW
Thread-facing engine core: lifecycle/state, command consumption, dataset session, and frame coordination.

## TARGETS
- `dzc_engine_api`: interface-only target exposing public headers from `include/dzc`.
- `dzc_engine_core`: static implementation target for `Engine.cpp`, dataset session, queues, commands, state machine, scene, and camera sources.
- `dzc_engine_core` links data/render/compute/task/diagnostics capabilities, never Qt.

## FILE MAP
- `Engine.cpp`: Pimpl-owned lifecycle, backend ownership, command/event/completion plumbing.
- `EngineCommand.*`: variant command validation and application constraints.
- `EngineStateMachine.*`: legal transitions from created through ready/running/loading/shutdown.
- `EngineCoordinator.*`: ordered command, completion, camera, visibility, residency, frame, diagnostics, snapshot stages.
- `EngineQueues.*`: engine-facing bounded event/task queues.
- `DatasetSession.*`: dataset identity, load cancellation, and load event handling.
- `EngineTestAccess.h`: test-only access to lifecycle/snapshot traces; not product API.

## FRAME/LIFECYCLE FLOW
- `init` validates capacities and paired backend injection, initializes resources, then publishes `Ready`.
- Each `update` runs coordinator stages; command stage drains coalesced commands first.
- Task-completion stage consumes dataset load results before later frame stages.
- First valid snapshot transitions `Ready` to `Running`; shutdown command stops the coordinator.
- Render/update/resize calls remain backend-facing lifecycle operations through public abstractions.
- Failures use `Result`/`Error` and retain operation context for UI/diagnostic presentation.

## CONVENTIONS
- Keep implementation details in `.cpp`/Pimpl; public engine contracts live under `include/dzc`.
- Validate every `EngineCommand` before application; queue capacity is a real configuration invariant.
- Preserve stage ordering; later stages must not observe a command/completion that was skipped.
- Treat injected render and compute backends as an all-or-none dependency pair.
- Publish immutable snapshots rather than exposing mutable engine internals.

## ANTI-PATTERNS (THIS DIRECTORY)
- Do not include Qt or make `Engine` a QObject/widget.
- Do not call data readers or UI callbacks synchronously from `enqueueCommand`.
- Do not mutate engine state from outside the engine update/coordinator path.
- Do not return success after a failed stage, invalid transition, or missing backend resource.
- Do not stop workers before task completions/cancellation and backend release are coordinated.
- Do not add backend-specific handles to `include/dzc` types.
- Do not silently ignore queue-full, invalid-command, cancellation, or shutdown signals.
