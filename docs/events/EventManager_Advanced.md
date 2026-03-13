# EventManager Advanced

## Deferred Queue Semantics

Deferred events are processed in FIFO order by `EventManager::update()`. The queue is the synchronization boundary between worker-thread producers and main-thread handler execution.

Implications:

- handlers observe a stable main-thread dispatch point
- producers can emit many events without re-entering subscribers immediately
- tests can force determinism with `drainAllDeferredEvents()`

## Worker-Thread Batching

The intended high-throughput pattern is:

1. collect `EventManager::DeferredEvent` values in a thread-local vector
2. finish the worker batch
3. call `enqueueBatch(std::move(events))` once

This avoids lock-per-event contention in AI, combat, and other heavily parallel code.

## Handler Lifecycle

Handlers are usually state scoped, not global. Typical ownership:

- `ControllerBase` stores tokens and removes them automatically
- GameStates register tokens in `enter()` and remove them in `exit()`
- managers remove or reset handlers during `prepareForStateTransition()`

Avoid leaving handlers registered across state teardown unless the subscriber has true process-wide lifetime.

## Threading Notes

- event production can happen off-thread through deferred batches
- handler execution should be treated as main-thread work unless the caller explicitly uses immediate dispatch in a safe context
- `prepareForStateTransition()` should be part of teardown for AI/world-heavy states

## Testing Guidance

- prefer deferred dispatch in tests when validating the real runtime path
- call `drainAllDeferredEvents()` before assertions that depend on subscriber side effects
- if a test uses immediate dispatch, make that choice explicit in the test body
