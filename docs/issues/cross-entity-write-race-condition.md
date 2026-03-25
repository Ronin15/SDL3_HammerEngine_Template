# Cross-Entity Write Race Condition in AttackBehavior

**Status:** Open
**Priority:** Medium (rare in practice, but architecturally incorrect)
**File:** `src/ai/behaviors/AttackBehavior.cpp` lines 916-980

## Problem

During parallel batch processing in `AIManager::processBatch()`, `AttackBehavior::applyDamageToTarget()` directly writes to the **victim's** EDM data from the **attacker's** batch thread. This is a race condition.

## The 3 Problematic Writes

```cpp
// Line 937 - Writing to victim's CharacterData
charData.health = std::max(0.0f, charData.health - damage);

// Line 938 - Writing to victim's TransformData
hotData.transform.velocity = hotData.transform.velocity + scaledKnockback;

// Lines 947-949 - Writing to victim's MemoryData
memData.lastAttacker = attackerHandle;
memData.lastCombatTime = 0.0f;
```

## What is NOT a Race Condition

These calls are already thread-safe and do NOT need to be changed:
- `AIManager::assignBehavior()` - uses internal locking
- `AIManager::broadcastMessage()` - uses lock-free message queue
- `EntityDataManager::destroyEntity()` - queues for deferred destruction

## Minimal Fix

1. Create a simple `DamageRecord` struct with only:
   - `victimIdx` (size_t)
   - `attackerIdx` (size_t)
   - `damage` (float)
   - `knockbackX`, `knockbackY` (float)

2. Add a thread-local damage buffer to `processBatch()`

3. Modify `applyDamageToTarget()` to append to buffer instead of direct writes

4. After sync point (where futures are awaited), apply all deferred damage:
   - Apply health changes (can use SIMD for batches of 4)
   - Apply knockback
   - Update memory

5. Keep flee/broadcast/death logic in `applyDamageToTarget()` - just move it to execute AFTER the deferred writes are applied

## Why This Was Considered "Safe" Before

The original code comment states:
> "Safe because: victim entities in attack range are typically not in the same batch as their attackers (different spatial positions). Concurrent writes to the same memData from different batches would be a race, but this is extremely rare in practice."

This is a known architectural compromise, not a proper solution.

## Files to Modify

1. `include/ai/AIBehavior.hpp` - Add DamageRecord struct
2. `include/managers/AIManager.hpp` - Update processBatch signature if needed
3. `src/managers/AIManager.cpp` - Add damage buffer, apply after sync
4. `src/ai/behaviors/AttackBehavior.cpp` - Use buffer instead of direct writes
