# EventManager Advanced

## Deferred Queue Semantics

Deferred events are processed during `EventManager::update()`. The queue boundary is still the synchronization point between worker-thread producers and main-thread handler execution, but combat events now take a dedicated fast path inside that drain.

Implications:

- handlers observe a stable main-thread dispatch point after any built-in event processing completes
- producers can emit many events without re-entering subscribers immediately
- tests can force determinism with `drainAllDeferredEvents()`

Current ordering model:

- combat and non-combat deferred events are stored separately internally
- each deferred event receives a sequence number when enqueued
- drain logic merges both queues back into original enqueue order before dispatching mixed workloads

Combat-specific processing model:

- `DamageEvent` payloads are prepared first, optionally in WorkerBudget-guided batches
- commit still happens on the main thread
- combat handlers run after the damage result has been applied to EDM

## Worker-Thread Batching

The intended high-throughput pattern is:

1. collect `EventManager::DeferredEvent` values in a thread-local vector
2. finish the worker batch
3. call `enqueueBatch(std::move(events))` once

This avoids lock-per-event contention in AI, combat, and other heavily parallel code.

If a batch would overflow the queue cap, the manager drops the oldest pending events first. If the incoming batch itself is larger than capacity, only the newest tail is retained.

## Handler Lifecycle

Handlers are usually state scoped, not global. Typical ownership:

- `ControllerBase` stores tokens and removes them automatically
- GameStates register tokens in `enter()` and remove them in `exit()`
- managers remove or reset handlers during `prepareForStateTransition()`

Avoid leaving handlers registered across state teardown unless the subscriber has true process-wide lifetime.

## Threading Notes

- event production can happen off-thread through deferred batches
- handler execution should be treated as main-thread work unless the caller explicitly uses immediate dispatch in a safe context
- immediate combat dispatch still performs built-in combat mutation before handlers run
- `prepareForStateTransition()` should be part of teardown for AI/world-heavy states

## Testing Guidance

- prefer deferred dispatch in tests when validating the real runtime path
- call `drainAllDeferredEvents()` before assertions that depend on subscriber side effects
- if a test uses immediate dispatch, make that choice explicit in the test body
