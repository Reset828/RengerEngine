# src/tasks

## OVERVIEW
Bounded worker execution and flow-control primitives used by engine and data loading.

## TARGET
- CMake target: `dzc_tasks` static library.
- Public include root is the parent `src` directory; consumers include `tasks/...`.
- Links `dzc_engine_api` for shared IDs/types and `Result` contracts.

## CORE TYPES
- `TaskSystem`: priority workers, generic `void`/`Result<void>` submission, dataset association.
- `CancellationSource`/`CancellationToken`: composable cancellation observed by every long task.
- `BoundedQueue<T>`: move-only ring storage with close and nonblocking try-push/pop operations.
- `TaskCompletionQueue`: bounded publication path from workers back to engine/update thread.
- `CommandCoalescer`: bounded engine-command queue with current-segment replacement semantics.
- `ConcurrencyGate`: cancellable permit leases for limited concurrent operations.
- `BackpressureController`: high/low watermark wait/resume gate for producers.
- `ThreadConfiguration`: resolves worker counts and runtime thread settings.

## TASK FLOW
- Submit validates callable signature: accepts `CancellationToken`, returns `void` or `Result<void>`.
- Optional `submitForDataset` stamps `DatasetId` onto completion records.
- Worker catches exceptions, converts them to task errors, and publishes completion.
- Engine drains completion batches through its coordinator; callers inspect `Result` before values.
- Shutdown sequence: stop accepting -> request cancellation -> wait -> close/release queues.

## CONVENTIONS
- Use `TaskPriority` deliberately: `Critical`, `High`, `Normal`, `Low` affect worker selection.
- Make cancellation checks explicit around waits, callbacks, I/O, and allocation-heavy loops.
- Keep queue capacities nonzero; queue-full and closed states are observable outcomes.
- Release `ConcurrencyGate::Lease` by scope; do not hand-roll permit counters.
- Keep backpressure waits cancellable and close-aware.

## ANTI-PATTERNS (THIS DIRECTORY)
- Do not submit tasks that capture GUI objects or assume execution on the submitter thread.
- Do not block forever on a queue/gate while ignoring token cancellation or `close()`.
- Do not publish completions after the completion queue is closed without handling failure.
- Do not call `waitForCompletion` while still accepting new work.
- Do not replace `CommandCoalescer` with an unbounded queue or coalesce order-sensitive commands.
- Do not swallow task exceptions, lose dataset IDs, or convert cancellation into success.
- Do not access task-system internals from engine/data code; use its submission/completion API.
