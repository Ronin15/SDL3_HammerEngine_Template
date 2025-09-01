# Workflow Execution Examples

## Example 1: New Feature Implementation - Weapon System

### Initial Request
**User:** "I need to implement a weapon system that handles different weapon types with damage, range, and fire rate properties. The system should integrate with the existing combat and AI systems."

### Workflow Execution

#### Phase 1: cpp-coder Implementation
**Agent Task:** Implement core weapon system following HammerEngine patterns

**Expected Actions:**
1. Create `WeaponManager` singleton with shutdown guards
2. Implement `Weapon` base class and derived types (`MeleeWeapon`, `RangedWeapon`)
3. Add weapon components to existing `Player` and `NPC` entities
4. Create weapon-related events (`WeaponAttackEvent`, `WeaponEquipEvent`)
5. Build and test basic functionality

**Handoff Report:**
```markdown
# Implementation Handoff Report

## Components Modified
- **Files Changed:** 
  - `src/managers/WeaponManager.cpp` (new)
  - `include/managers/WeaponManager.hpp` (new)
  - `src/entities/weapons/Weapon.cpp` (new)
  - `src/entities/Player.cpp` (modified for weapon integration)
  - `src/events/WeaponEvent.cpp` (new)

## Build Status
- **Debug Build:** ✅ Clean compilation
- **Test Results:** Basic weapon creation/destruction tests pass
- **Compilation Warnings:** 0

## Integration Points
- **Manager Dependencies:** EventManager, EntityStateManager
- **Event System Impact:** 3 new weapon events added
- **Threading Considerations:** WeaponManager uses ThreadSystem for damage calculations
- **Rendering Pipeline:** Weapon sprites integrated with Entity::render()

## Performance Considerations
- **Memory Allocations:** Weapon instances use object pooling
- **CPU Hotpaths:** Damage calculation optimized for batch processing
- **Entity Count Impact:** Minimal - weapons are components, not separate entities
- **Threading Safety:** All weapon state changes go through EventManager

## Next Steps for system-optimizer
- [ ] Optimize weapon-AI integration for combat behaviors
- [ ] Batch weapon update processing with other managers
- [ ] Ensure weapon events integrate efficiently with existing event queues
```

#### Phase 2: system-optimizer Integration
**Agent Task:** Optimize cross-system integration between weapon, AI, and collision systems

**Expected Actions:**
1. Integrate weapon range calculations with CollisionManager spatial hash
2. Optimize AI behavior patterns to consider weapon capabilities
3. Batch weapon damage processing with existing update loops
4. Ensure thread-safe weapon state management

**Handoff Report:**
```markdown
# Integration Optimization Report

## System-Level Changes
- **Cross-Manager Optimizations:** Weapon range queries use CollisionManager spatial hash
- **Batch Processing Updates:** Weapon damage calculation batched with AI decision cycle
- **Memory Access Patterns:** Weapon data aligned with entity data for cache efficiency
- **Threading Optimizations:** Weapon updates scheduled through WorkerBudget system

## Performance Targets
- **Entity Count:** Maintain 10K+ entities with weapons at 60+ FPS
- **Memory Usage:** <5MB additional for weapon system
- **CPU Budget:** Weapon processing <1% additional CPU
- **Frame Time:** No frame time impact

## Benchmarks Required
- [ ] AI combat behavior performance with 1000 armed NPCs
- [ ] Collision detection with weapon range queries
- [ ] Event processing throughput with weapon events
- [ ] Memory allocation patterns during combat scenarios

## Next Steps for performance-analyst
- [ ] Validate weapon system performance under combat load
- [ ] Measure AI behavior efficiency with weapon integration
- [ ] Test memory stability during extended combat
```

#### Phase 3: performance-analyst Validation
**Agent Task:** Validate performance requirements and identify bottlenecks

**Expected Actions:**
1. Run combat scenarios with 1000+ armed entities
2. Measure weapon system CPU/memory usage
3. Test AI pathfinding performance with combat avoidance
4. Validate event system throughput with weapon events

**Handoff Report:**
```markdown
# Performance Validation Report

## Benchmark Results
### Combat System Performance
- **Entity Count:** 10,500 entities with weapons at 61 FPS ✅
- **Weapon System CPU:** 0.8% usage ✅
- **Memory Usage:** 4.2MB additional, stable ✅
- **Event Throughput:** 15,000 weapon events/second ✅

### Integration Benchmarks
- **AI Combat Decisions:** 12ms average per 1000 entities
- **Collision + Weapon Range:** 8ms per frame with spatial optimization
- **Memory Allocation:** No leaks detected in 120-second test

## Performance Requirements Status
- [ ] ✅ 10K+ entities at 60+ FPS maintained
- [ ] ✅ Weapon system <1% CPU overhead
- [ ] ✅ Memory growth stable
- [ ] ✅ No frame drops during combat

## Recommendations
- **Immediate Fixes:** None required
- **Optimization Opportunities:** Weapon sprite batching could improve rendering 5%
- **Architecture Concerns:** None identified

## Next Steps for senior-developer-reviewer
- [ ] Review weapon manager singleton pattern
- [ ] Validate RAII compliance in weapon lifecycle
- [ ] Assess integration with existing architecture
```

#### Phase 4: senior-developer-reviewer Decision
**Agent Task:** Architectural review and final approval

**Expected Actions:**
1. Review code quality and architectural compliance
2. Validate long-term maintainability
3. Check integration with existing patterns
4. Make approval/refinement decision

**Final Decision:**
```markdown
# Architectural Review & Decision

## Code Quality Assessment
- **C++20 Standards:** ✅ Modern C++ patterns used correctly
- **Coding Conventions:** ✅ Follows project style guide
- **RAII Principles:** ✅ Smart pointers, automatic cleanup
- **Thread Safety:** ✅ Proper EventManager integration

## Architectural Compliance
- **Manager Pattern:** ✅ WeaponManager follows singleton + shutdown pattern
- **Module Organization:** ✅ Proper src/include structure maintained
- **Header Minimalism:** ✅ Implementation details in .cpp files
- **Dependency Management:** ✅ Clean interfaces, forward declarations

## Final Decision: **APPROVED** ✅
Feature ready for production use. Excellent integration with existing systems while maintaining performance standards.
```

---

## Example 2: Bug Fix - Memory Leak in Particle System

### Initial Request
**User:** "There's a memory leak in the particle system that's causing crashes after 30 minutes of gameplay."

### Workflow Execution

#### Phase 1: cpp-coder Investigation & Fix
**Agent Task:** Debug and fix memory management in ParticleManager

**Expected Actions:**
1. Use AddressSanitizer build to identify leak location
2. Analyze particle lifecycle and cleanup patterns
3. Fix memory management issues
4. Validate fix with extended testing

**Handoff Report:**
```markdown
# Implementation Handoff Report

## Issue Analysis
- **Root Cause:** Particle textures not released when particles expire
- **Location:** `ParticleManager::cleanupExpiredParticles()`
- **Impact:** 50MB/hour memory growth during gameplay

## Fix Applied
- **Files Changed:** `src/managers/ParticleManager.cpp`
- **Solution:** Added texture reference cleanup in particle destructor
- **Pattern:** Followed RAII principles with smart_ptr texture management

## Build Status
- **AddressSanitizer Build:** ✅ No leaks detected
- **Extended Test:** 2-hour gameplay test shows stable memory usage
- **Memory Growth:** <1MB over 2 hours (within normal bounds)

## Next Steps for system-optimizer
- [ ] Review particle system memory patterns across all managers
- [ ] Check for similar issues in other resource cleanup paths
```

#### Phase 2: system-optimizer System Review
**Agent Task:** Ensure fix doesn't create other issues and optimize memory patterns

**Handoff Report:**
```markdown
# Integration Optimization Report

## System-Wide Analysis
- **Memory Patterns:** Reviewed all manager cleanup patterns
- **Resource Management:** Verified consistent RAII usage
- **No Similar Issues:** Other managers follow proper cleanup patterns

## Performance Impact
- **CPU Overhead:** Cleanup optimization reduces CPU by 0.2%
- **Memory Efficiency:** Particle pool reuse improved by 15%

## Validation Required
- [ ] Extended memory stability testing
- [ ] Performance impact under high particle load
```

#### Phase 3: performance-analyst Extended Validation
**Handoff Report:**
```markdown
# Performance Validation Report

## Extended Testing Results
- **Memory Stability:** 6-hour test shows <2MB growth ✅
- **Performance Impact:** No FPS regression ✅
- **High Load Test:** 5000 particles simultaneously - stable ✅

## Final Decision: Memory leak resolved, performance maintained
```

#### Phase 4: senior-developer-reviewer Approval
```markdown
# Architectural Review & Decision

## Fix Quality Assessment
- **Root Cause Resolution:** ✅ Proper RAII implementation
- **Code Quality:** ✅ Follows established patterns
- **Testing Coverage:** ✅ Extended validation performed

## Final Decision: **APPROVED** ✅
Memory leak resolved with proper architectural compliance.
```

---

## Usage Instructions

### Starting a Workflow
1. Identify task complexity (simple fix vs major feature)
2. Invoke workflow-orchestrator with clear requirements
3. Orchestrator will route to appropriate agent chain
4. Monitor progress through handoff reports

### Monitoring Progress
- Each phase produces a structured handoff report
- Quality gates ensure standards before proceeding
- Iteration loops handle refinements automatically

### Emergency Stops
- Use "WORKFLOW HALT" if critical issues arise
- Agents will pause and await manual intervention
- Context preserved for workflow resumption

This workflow system ensures systematic, high-quality development while maintaining the SDL3 HammerEngine's performance and architectural standards.