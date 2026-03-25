# Behavior Quick Reference

## Current Model

- configs live in `BehaviorConfigData`
- state lives in EDM `BehaviorData`
- execution uses `Behaviors::execute(...)`
- transitions/messages are mediated by `AICommandBus`

## AIManager Calls

```cpp
registerDefaultBehaviors();
hasBehavior(name);
assignBehavior(handle, name);
assignBehavior(handle, config);
unassignBehavior(handle);
hasBehavior(handle);
```

## Query Helpers

```cpp
scanActiveHandlesInRadius(...)
scanActiveIndicesInRadius(...)
scanGuardsInRadius(...)
scanFactionInRadius(...)
```

## Behavior Messages

```cpp
BehaviorMessage::ATTACK_TARGET
BehaviorMessage::RETREAT
BehaviorMessage::PANIC
BehaviorMessage::CALM_DOWN
BehaviorMessage::DISTRESS
BehaviorMessage::RAISE_ALERT
```

## Do Not Use

- removed clone-based behavior ownership
- old registration flows built around `registerBehavior(...)`
- string broadcast helpers from the previous messaging model
