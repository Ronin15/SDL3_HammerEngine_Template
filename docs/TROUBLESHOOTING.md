# AI System Troubleshooting Guide

## Common Issues and Solutions

### Issue: NPCs Interfering with Each Other's Behavior

**Symptoms:**
- NPCs with same behavior type move in sync
- Waypoint changes affect multiple NPCs simultaneously
- Patrol routes get confused between NPCs

**Cause:** Using deprecated shared behavior instances

**Solution:** Ensure you're using v2.1+ with individual behavior instances:
```cpp
// ✅ CORRECT (v2.1+): Automatic individual instances
AIManager::Instance().assignBehaviorToEntity(npc, "Patrol");

// ❌ DEPRECATED: Manual shared instance assignment  
// (This pattern is no longer supported)
```

**Verification:**
```cpp
// Check that each NPC has independent behavior
npc1->assignBehavior("Patrol");
npc2->assignBehavior("Patrol");
// npc1 and npc2 should have different waypoint states
```

### Issue: Performance Degradation with Many NPCs

**Symptoms:**
- Frame rate drops with increasing NPC count
- System becomes unresponsive
- Cache invalidation messages in logs

**Cause:** Possible shared state conflicts or missing optimizations

**Solution:**
1. **Verify individual instances:**
   ```cpp
   // Ensure clone() method is implemented
   std::shared_ptr<AIBehavior> MyBehavior::clone() const override {
       return std::make_shared<MyBehavior>(/* params */);
   }
   ```

2. **Enable optimizations:**
   ```cpp
   behavior->setUpdateFrequency(3);  // Update every 3 frames
   behavior->setMaxUpdateDistance(1000.0f);  // Cull distant NPCs
   ```

3. **Use batch processing:**
   ```cpp
   AIManager::Instance().batchProcessEntities("Patrol", npcGroup);
   ```

### Issue: Memory Usage Concerns

**Symptoms:**
- High memory usage with many NPCs
- Memory usage grows with NPC count

**Cause:** Individual behavior instances use more memory than shared instances

**Solution:** This is expected and beneficial:
```cpp
// Memory usage is normal and justified:
// 5000 NPCs = ~2.5MB (vs. system crashes with shared state)
// This is negligible compared to:
// - Textures: 10-100MB each
// - Audio: 1-10MB each  
// - Models: 1-50MB each
```

**Optimization options:**
```cpp
// If memory is critical, implement distance-based culling
if (distanceToPlayer > CULL_DISTANCE) {
    AIManager::Instance().unassignBehaviorFromEntity(npc);
}
```

### Issue: Crashes During NPC Cleanup

**Symptoms:**
- `malloc: double free` errors
- Segmentation faults during state transitions
- Crashes when removing NPCs

**Cause:** Double cleanup or improper memory management

**Solution:** Use defensive cleanup patterns:
```cpp
// ✅ CORRECT: Let AIManager handle cleanup
AIManager::Instance().unassignBehaviorFromEntity(npc);

// ❌ AVOID: Manual behavior cleanup
// npc->getBehavior()->clean();  // Can cause double-free
```

### Issue: Thread Safety Concerns

**Symptoms:**
- Race condition warnings
- Inconsistent behavior states
- Random crashes in multi-threaded mode

**Cause:** Shared data access between behavior instances

**Solution:** Ensure complete instance isolation:
```cpp
// ✅ CORRECT: Each instance has its own data
class MyBehavior : public AIBehavior {
    int m_instanceData;  // Safe: each instance has its own
    
    std::shared_ptr<AIBehavior> clone() const override {
        auto cloned = std::make_shared<MyBehavior>();
        // Don't copy runtime state, only configuration
        return cloned;
    }
};

// ❌ AVOID: Shared static data
class MyBehavior : public AIBehavior {
    static int s_sharedData;  // Unsafe: shared between instances
};
```

### Issue: Clone Method Not Implemented

**Symptoms:**
- Compilation errors about pure virtual function
- Abstract class instantiation errors
- Missing clone method warnings

**Cause:** Custom behaviors missing required clone() method

**Solution:** Implement clone() in all custom behaviors:
```cpp
class CustomBehavior : public AIBehavior {
public:
    std::shared_ptr<AIBehavior> clone() const override {
        auto cloned = std::make_shared<CustomBehavior>(/* constructor params */);
        
        // Copy configuration
        cloned->setActive(m_active);
        cloned->setPriority(m_priority);
        cloned->setUpdateFrequency(m_updateFrequency);
        cloned->setUpdateDistances(m_maxUpdateDistance, m_mediumUpdateDistance, m_minUpdateDistance);
        
        // Copy behavior-specific settings
        cloned->setMyCustomParameter(m_myCustomParameter);
        
        // DON'T copy runtime state
        // cloned->m_currentState = m_currentState;  // ❌ Don't do this
        
        return cloned;
    }
};
```

### Issue: EventDemoState NPC Spawning Crashes

**Symptoms:**
- Crashes when pressing key 4 repeatedly
- Memory corruption errors during NPC creation
- System instability with increasing NPC count

**Cause:** Multiple issues combined (resolved in v2.1+)

**Solution:** Ensure you have all fixes:
1. **Individual behavior instances** (automatic in v2.1+)
2. **NPC spawn limits** (5000 NPC capacity)
3. **Removed debug output** (no console spam)
4. **Defensive cleanup** (proper memory management)

**Verification:**
```cpp
// Test NPC spawning stability
for (int i = 0; i < 100; i++) {
    AIManager::Instance().assignBehaviorToEntity(createNPC(), "Patrol");
}
// Should create 100 independent patrol behaviors without conflicts
```

## Debugging Tools

### 1. Performance Monitoring
```cpp
// Check AI performance statistics
auto stats = AIManager::Instance().getPerformanceStats();
std::cout << "AI update time: " << stats.averageUpdateTimeMs << "ms" << std::endl;
```

### 2. Memory Analysis
```cpp
// Monitor memory usage
size_t behaviorCount = AIManager::Instance().getBehaviorCount();
size_t entityCount = AIManager::Instance().getManagedEntityCount();
std::cout << "Behaviors: " << behaviorCount << ", Entities: " << entityCount << std::endl;
```

### 3. State Verification
```cpp
// Verify individual instances
bool hasIndependentState = 
    (npc1->getBehaviorState() != npc2->getBehaviorState());
assert(hasIndependentState && "NPCs should have independent behavior state");
```

### 4. Threading Diagnostics
```cpp
// Check threading configuration
bool isThreadingEnabled = AIManager::Instance().isThreadingEnabled();
int threadCount = AIManager::Instance().getThreadCount();
std::cout << "Threading: " << (isThreadingEnabled ? "enabled" : "disabled") 
          << ", Threads: " << threadCount << std::endl;
```

### 5. Behavior Instance Verification
```cpp
// Verify each NPC has its own behavior instance
void verifyIndependentBehaviors() {
    auto npc1 = createNPC();
    auto npc2 = createNPC();
    
    AIManager::Instance().assignBehaviorToEntity(npc1, "Patrol");
    AIManager::Instance().assignBehaviorToEntity(npc2, "Patrol");
    
    // Get behavior instances (implementation-specific)
    auto behavior1 = getBehaviorInstance(npc1);
    auto behavior2 = getBehaviorInstance(npc2);
    
    assert(behavior1 != behavior2 && "Each NPC should have unique behavior instance");
    std::cout << "✅ NPCs have independent behavior instances" << std::endl;
}
```

## Error Messages and Solutions

### "Behavior 'X' not registered"
**Cause:** Trying to assign unregistered behavior
**Solution:** 
```cpp
// Register the behavior first
auto behaviorTemplate = std::make_shared<MyBehavior>();
AIManager::Instance().registerBehavior("MyBehavior", behaviorTemplate);
```

### "Failed to clone behavior 'X'"
**Cause:** Clone method throws exception or returns nullptr
**Solution:** Fix clone method implementation:
```cpp
std::shared_ptr<AIBehavior> MyBehavior::clone() const override {
    try {
        auto cloned = std::make_shared<MyBehavior>(/* valid params */);
        // Copy configuration properly
        return cloned;
    } catch (const std::exception& e) {
        std::cerr << "Clone failed: " << e.what() << std::endl;
        return nullptr;  // Will be caught by AIManager
    }
}
```

### "Cache invalidation pending"
**Cause:** Rapid behavior assignments causing cache thrashing (should be resolved in v2.1+)
**Solution:** This should not occur with individual instances. If it does:
```cpp
// Force cache rebuild
AIManager::Instance().ensureOptimizationCachesValid();
```

### "Thread safety violation"
**Cause:** Shared data access between behaviors
**Solution:** Eliminate all shared state:
```cpp
// ❌ WRONG: Shared static variables
static Vector2D s_sharedTarget;

// ✅ CORRECT: Instance variables
Vector2D m_instanceTarget;
```

## Migration Checklist

### Upgrading from v2.0 to v2.1+

- [ ] Add `clone()` method to all custom behaviors
- [ ] Remove any manual behavior sharing code
- [ ] Test with multiple NPCs of same behavior type
- [ ] Verify performance improvements
- [ ] Check memory usage is reasonable
- [ ] Confirm thread safety in multi-threaded mode
- [ ] Update documentation for team members

### Custom Behavior Clone Implementation Template
```cpp
// Template for clone() method
std::shared_ptr<AIBehavior> MyBehavior::clone() const override {
    auto cloned = std::make_shared<MyBehavior>(/* constructor parameters */);
    
    // Copy configuration (do copy these)
    cloned->setActive(m_active);
    cloned->setPriority(m_priority);
    cloned->setUpdateFrequency(m_updateFrequency);
    cloned->setUpdateDistances(m_maxUpdateDistance, m_mediumUpdateDistance, m_minUpdateDistance);
    
    // Copy behavior-specific settings
    cloned->setCustomSetting(m_customSetting);
    
    // DON'T copy runtime state (let each instance start fresh)
    // cloned->m_currentWaypoint = m_currentWaypoint;  // ❌ Don't do this
    // cloned->m_targetPosition = m_targetPosition;    // ❌ Don't do this
    // cloned->m_timeElapsed = m_timeElapsed;          // ❌ Don't do this
    
    return cloned;
}
```

## Performance Optimization Checklist

### For Large NPC Counts (1000+)

- [ ] Implement distance-based update frequency
- [ ] Use batch processing for similar behaviors
- [ ] Enable threading with appropriate priorities
- [ ] Set reasonable update distances
- [ ] Monitor memory usage patterns
- [ ] Profile CPU usage during peak loads

### Code Examples for Optimization
```cpp
// Distance-based optimization
behavior->setUpdateDistances(800.0f, 1600.0f, 2400.0f);

// Priority-based processing
behavior->setPriority(5);  // Lower priority for background NPCs

// Batch processing
std::vector<EntityPtr> guards;
for (auto& guard : allGuards) {
    guards.push_back(guard);
}
AIManager::Instance().batchProcessEntities("Patrol", guards);

// Threading configuration
AIManager::Instance().configureThreading(true, 4, Forge::TaskPriority::Normal);
```

## Getting Help

If you encounter issues not covered here:

1. **Check the logs** for specific error messages
2. **Run the benchmarks** to isolate performance issues:
   ```bash
   ./bin/debug/ai_scaling_benchmark
   ./bin/debug/ai_optimization_tests
   ```
3. **Verify individual instances** are being created correctly
4. **Test with minimal NPC counts** to isolate the problem
5. **Review the documentation** for correct usage patterns:
   - [`AIManager.md`](ai/AIManager.md)
   - [`SHARED_BEHAVIOR_ISSUE_RESOLVED.md`](ai/SHARED_BEHAVIOR_ISSUE_RESOLVED.md)
6. **Enable debug logging** by defining `AI_DEBUG_LOGGING`

## Common Anti-Patterns to Avoid

### ❌ Shared State Between Behaviors
```cpp
// DON'T DO THIS
class BadBehavior : public AIBehavior {
    static std::vector<Vector2D> s_sharedWaypoints;  // Shared between all instances
    static Entity* s_sharedTarget;                   // Race condition waiting to happen
};
```

### ❌ Manual Memory Management
```cpp
// DON'T DO THIS
AIBehavior* behavior = new MyBehavior();  // Manual allocation
delete behavior;                          // Manual deletion - use smart pointers!
```

### ❌ Copying Runtime State in Clone
```cpp
// DON'T DO THIS
std::shared_ptr<AIBehavior> BadBehavior::clone() const override {
    auto cloned = std::make_shared<BadBehavior>();
    cloned->m_currentWaypoint = m_currentWaypoint;  // Copies runtime state!
    cloned->m_timeInState = m_timeInState;          // All instances start at same point!
    return cloned;
}
```

### ❌ Ignoring Clone Method
```cpp
// DON'T DO THIS
class IncompleteB ehavior : public AIBehavior {
    // Missing clone() method - will cause compilation errors
};
```

The individual behavior instances architecture should resolve most stability and performance issues while providing predictable, scalable performance. When in doubt, ensure each NPC has its own independent behavior state and avoid any shared data between behavior instances.