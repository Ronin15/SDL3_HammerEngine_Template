# Behavior Modes

This page catalogs the current behavior families and the configuration style used on this branch. The important architectural change is that modes are data in EDM-backed config structs, not separate heap-owned behavior instances.

## Behavior Families

- `Idle`
  - modes include stationary, subtle sway, occasional turn, light fidget
- `Wander`
  - broad roaming presets such as small, large, and event-area wandering
- `Chase`
  - pursuit settings for line-of-sight, catch radius, and path refresh
- `Patrol`
  - waypoint or route-driven guard movement
- `Guard`
  - alert, suspicious, and defensive area control
- `Attack`
  - melee/ranged aggression settings plus target engagement rules
- `Flee`
  - panic/retreat behavior with recovery thresholds
- `Follow`
  - formation or distance-preserving follower behavior

## Assignment Pattern

```cpp
VoidLight-Framework::BehaviorConfigData config{};
config.type = BehaviorType::Idle;
config.idle = VoidLight-Framework::IdleBehaviorConfig::createSubtleSway();

AIManager::Instance().assignBehavior(handle, config);
```

Or use a registered behavior name:

```cpp
AIManager::Instance().assignBehavior(handle, "Guard");
```

## Messages and Transitions

Behaviors now react through queued messages and command-bus transitions instead of direct cross-controller mutation.

Important message IDs:

- `ATTACK_TARGET`
- `RETREAT`
- `PANIC`
- `CALM_DOWN`
- `DISTRESS`
- `RAISE_ALERT`

Use:

- `Behaviors::queueBehaviorMessage(...)` from the main thread
- `Behaviors::deferBehaviorMessage(...)` from worker-thread code

## Notes

- persistent behavior state must live in EDM
- per-frame locals must not be used for path/state that should survive updates
- behavior switching is initialized through `Behaviors::init(...)`, not by constructing a new class instance
