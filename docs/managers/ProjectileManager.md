# ProjectileManager

## Role

`ProjectileManager` owns **Projectile simulation**:

- Integrates projectile movement (SIMD 4-wide where possible)
- Manages projectile lifetime (timeouts + embedded projectiles)
- Bridges `Collision` → `Damage` via the existing deferred `EventManager` pipeline

Projectiles are regular EDM entities created via `EntityDataManager::createProjectile()` and live in the Active simulation tier.

## Runtime Contract

- Small projectiles are **overlap-driven hit bodies**, not blocking physics bodies.
- `CollisionManager` still detects projectile overlaps and dispatches `CollisionEvent`s for them.
- `CollisionManager::resolve()` skips positional pushback and velocity cancellation when a projectile is involved, so projectile contacts do not block movement.
- `ProjectileManager` applies owner immunity at event-handling time using the stored projectile owner handle.
- Any other health-bearing target can receive projectile damage, regardless of faction.
- Non-health targets (for example world geometry) cause the projectile to embed and stop participating in further hit detection.

## Event Flow (Collision → Damage)

1. `CollisionManager` detects projectile hits during broadphase/narrowphase.
2. A `CollisionEvent` is dispatched through `EventManager` (deferred).
3. `ProjectileManager` subscribes to `EventTypeId::Collision`, ignores owner hits, embeds on non-health targets, and converts valid health-target hits into `DamageEvent` (also deferred).

This keeps projectile hit processing consistent with NPC combat wiring while preserving the non-blocking "small projectile" gameplay rule.

## Threading Model

`ProjectileManager::update(dt)` follows the same adaptive threading pattern as other hot-path managers:

- `WorkerBudget` decides whether to thread (`SystemType::ProjectileSim`)
- Work is split into batches over the active projectile index list
- Each batch uses its own destroy queue, merged on the main thread after futures complete

## Lifecycle

- `init()` requires `EntityDataManager` to already be initialized.
- `prepareForStateTransition()`:
  - waits for any pending batch work to complete
  - destroys all active projectiles
  - clears transient state (destroy queues, futures, buffers) while preserving capacity
  - keeps its collision handler registered (it is a persistent `EventManager` handler)

## Related Docs

- [CollisionManager](CollisionManager.md)
- [EntityDataManager](EntityDataManager.md)
- [EventManager](../events/EventManager.md)
- [WorkerBudget](../core/WorkerBudget.md)
