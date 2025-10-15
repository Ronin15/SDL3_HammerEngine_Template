# Changelog: Threading_fixes Branch

**Summary**: Major refactor of threading, collision, and AI systems with significant performance improvements and bug fixes. 95 files changed, 8,093 insertions, 3,632 deletions.

**Branch**: Threading_fixes
**Base**: main
**Commits**: 134 commits over 4 weeks

---

## 🔧 Core System Overhauls

### Collision System - Complete Refactor
- **Replaced SpatialHash with HierarchicalSpatialHash** (`include/collisions/HierarchicalSpatialHash.hpp`, `src/collisions/HierarchicalSpatialHash.cpp`)
  - Two-level spatial partitioning with coarse and fine grids
  - Improved query performance for high entity counts
  - Thread-safe shared mutex for concurrent access
- **Vector pooling** for temporary allocations to reduce memory pressure
- **Deferred command queue** for thread-safe collision updates
- **AABB cache** improvements with state bug fixes
- **Camera culling** integration for collision detection
- **Coarse grid caching** system
- **Building collision architecture** improvements with flood fill for static objects
- Fixed wall bouncing, penetration, and border collision issues
- SIMD optimizations for collision detection

### Threading System Enhancements
- **WorkerBudget refactor** (`include/core/WorkerBudget.hpp`)
  - Consolidated batch processing code
  - Dynamic batch sizing based on queue pressure
  - Optimized batching for AI, Event, and Particle managers
- **ThreadSystem improvements** (`include/core/ThreadSystem.hpp`)
  - Async manager pattern standardization
  - Removed CPU core pinning (reduced efficiency)
  - Buffer request improvements
  - Better task distribution and load balancing
- **Thread-safe deferred command queues** across all managers
- Improved synchronization points between managers

### AI Manager Optimization
- **SIMD vectorization** for collision checks in AI behaviors
- **Batch processing** improvements with optimized thresholds
- **Race condition fixes** with sync points between AIManager and CollisionManager
- **Pathfinding determinism** preservation
- **Wander behavior** performance improvements with world edge detection
- **Request queue** optimizations (`include/ai/internal/RequestQueue.hpp`)
- Removed multi-producer queue in favor of vector batching
- Fixed transition bugs that caused threading to stay enabled
- Cleaned up unused variables and updated function signatures
- Reduced update latency significantly

---

## 🚀 Performance Improvements

### Optimizations
- Camera-aware culling for entities and particles
- Increased culling area size for better entity inclusion
- Static cache implementation for collision detection
- NPC counting logic optimization
- **Fixed player jitter at high entity counts**
- Reduced latency in AIManager updates
- Spatial priority improvements (`include/ai/internal/SpatialPriority.hpp`)
- Optimized batch sizes across all managers
- Improved memory locality through better data structures

### Memory & Safety
- Multiple AddressSanitizer (ASAN) fixes
- Fixed race conditions and segfaults during state transitions
- SDL texture destruction order fix (prevented shutdown segfault)
- Strict runtime validation improvements
- Vector pooling for reduced allocations
- Eliminated undefined behavior in EventDemoState NPCs
- Thread-safe cleanup patterns standardized

---

## 🧪 Testing & Benchmarking

### New Test Infrastructure
- **Split benchmarks**: Separated collision and pathfinding tests
  - `tests/performance/CollisionBenchmark.cpp` (new, 445 lines)
  - `tests/performance/PathfinderBenchmark.cpp` (new, 404 lines)
  - Removed combined `CollisionPathfindingBenchmark.cpp`
- **Test scripts**:
  - `tests/test_scripts/run_collision_benchmark.sh`
  - `tests/test_scripts/run_pathfinder_benchmark.sh`
- **Threading safety tests** for collision detection (`tests/collisions/CollisionSystemTests.cpp`, 537+ lines)
- Enhanced integration tests for collision-pathfinding interaction
- Buffer utilization tests
- Updated test documentation (`tests/TESTING.md`)

### Valgrind & Profiling
- Improved callgrind data parsing/reporting (`tests/valgrind/callgrind_profiling_analysis.sh`)
- Cache performance analysis updates
- Quick memory check improvements (`tests/valgrind/quick_memory_check.sh`)
- Complete suite updates (`tests/valgrind/run_complete_valgrind_suite.sh`)

---

## 🐛 Bug Fixes

### Critical Fixes
- **State transition crashes**: Fixed collision manager and AI manager cleanup patterns
- **Threading race conditions**: Added proper sync points and deferred queues
- **Player movement jitter**: Resolved at high NPC counts through improved synchronization
- **Building collisions**: Fixed penetration and border issues
- **Pathfinding overflow**: Fixed stat tracking to reset per-cycle (prevents overflow errors)
- **EventDemoState NPC undefined behavior**: Cleaned up entity management
- **macOS sound loading**: Fixed AIFF compatibility issues
- **Windows enum conflict**: Changed `Absolute` to `ABSOLUTE_POS`

### Stability Improvements
- Multiple ASAN crash fixes (addressed memory safety issues)
- Thread correctness improvements across all managers
- State cleanup pattern standardization (consistent with EventDemoState pattern)
- Removed static variables in threaded code
- Fixed AIManager transition bug
- Corrected system update order
- Eliminated capture variable issues

---

## 🔨 Build System

### CMake Improvements
- **Mold linker support** (`CMakeLists.txt`)
  - Automatically uses mold if available for faster linking
  - Significant build time improvements
- **AddressSanitizer configuration** support with proper flags
- CPP check suppressions (`tests/cppcheck/cppcheck_suppressions.txt`, 42 suppressions)
- Build type optimizations for Debug and Release
- Improved cross-platform compatibility

---

## 📚 Documentation

### Major Updates
- **CLAUDE.md**: Complete rewrite and streamlining (`CLAUDE.md`, 85 lines)
  - Moved from `.claude/CLAUDE.md` to root directory
  - Updated architecture documentation
  - Added threading best practices
  - Improved build commands with examples
  - Enhanced testing guidelines
- **Collision system docs** (`docs/collisions/CollisionSystem.md`, 158+ lines)
  - Documented new HierarchicalSpatialHash
  - Explained vector pooling and caching strategies
- **ThreadSystem docs** (`docs/core/ThreadSystem.md`, 242+ lines)
  - Updated for new WorkerBudget patterns
  - Documented deferred command queues
- **Manager documentation** updates across AIManager, CollisionManager, PathfinderManager
- **Moved AGENTS.md** to `docs/` directory for better organization
- **AI optimization summary** updates (`docs/ai/AIManager_Optimization_Summary.md`)

---

## 🎨 Assets & Resources

### Audio
- **Fixed macOS sound format issues**
  - Converted WAV files to AIFF for compatibility
  - Created Python tools for audio conversion:
    - `tests/tools/convert_wav.py` (184 lines) - WAV to AIFF converter
    - `tests/tools/test_wav.py` (42 lines) - WAV file validator
  - Preserved original WAV files as `.wav.original`
  - Files affected:
    - `res/sfx/level_complete.aiff` (new)
    - `res/sfx/logo.aiff` (new)

### Graphics
- **HammerEngine.png**: Fixed alpha channel (sRGB → sRGBA)
  - Resolved rendering issues on certain platforms
  - File size: 17751 → 19305 bytes

---

## 🎮 Game State Updates

### AIDemoState Improvements (`src/gameStates/AIDemoState.cpp`, `include/gameStates/AIDemoState.hpp`)
- Adjusted entity count for testing (configurable for various scenarios)
- Updated to serve as extreme stress test case for engine capabilities
- Performance tuning for high entity scenarios
- Fixed undefined behavior in NPC spawning
- Improved usability and controls
- Better integration with collision and AI systems

### Other States
- **EventDemoState**: Cleanup pattern improvements, NPC management fixes
- **LogoState**: Sound toggle for testing (disabled by default)
- **AdvancedAIDemoState**: Updates for new AI manager patterns
- **GamePlayState**: Collision integration improvements

---

## 🔄 API Changes

### Manager Interfaces
- **Threading threshold getters** added to:
  - `EventManager::getThreadingThreshold()`
  - `ParticleManager::getThreadingThreshold()`
  - `CollisionManager::getThreadingThreshold()`
- **Behavior parameter adjustments** for improved responsiveness:
  - `FleeBehavior`, `FollowBehavior`, `GuardBehavior`, `IdleBehavior` parameter updates
  - WanderBehavior with double collision check
- **CollisionBody** interface updates (`include/collisions/CollisionBody.hpp`)
  - Additional collision info fields
  - Better integration with HierarchicalSpatialHash
- **NPC/Player** entity updates:
  - `include/entities/NPC.hpp`, `src/entities/NPC.cpp`
  - `include/entities/Player.hpp`, `src/entities/Player.cpp`
  - Improved collision handling and movement

### Internal API Changes
- Moved AI internal headers from `src/` to `include/` for better organization:
  - `include/ai/internal/Crowd.hpp`
  - `include/ai/internal/SpatialPriority.hpp`
- Updated function signatures across managers
- Standardized async patterns

---

## 📦 File Structure Changes

### Organization
- **Moved files**:
  - `.claude/CLAUDE.md` → `CLAUDE.md` (root)
  - `AGENTS.md` → `docs/AGENTS.md`
  - `src/ai/internal/Crowd.hpp` → `include/ai/internal/Crowd.hpp`
  - `src/ai/internal/SpatialPriority.hpp` → `include/ai/internal/SpatialPriority.hpp`

### New Files
- `include/collisions/HierarchicalSpatialHash.hpp`
- `src/collisions/HierarchicalSpatialHash.cpp`
- `tests/performance/CollisionBenchmark.cpp`
- `tests/performance/PathfinderBenchmark.cpp`
- `tests/test_scripts/run_collision_benchmark.sh`
- `tests/test_scripts/run_pathfinder_benchmark.sh`
- `tests/tools/convert_wav.py`
- `tests/tools/test_wav.py`
- `tests/cppcheck/cppcheck_suppressions.txt`

### Removed Files
- `include/collisions/SpatialHash.hpp`
- `src/collisions/SpatialHash.cpp`
- `tests/CollisionPathfindingBenchmark.cpp`
- `tests/test_scripts/run_collision_pathfinding_benchmark.bat`
- `.claude/CLAUDE.md`

---

## 🎯 Key Metrics

- **Performance**: Significant improvements for high entity count scenarios
- **Code Quality**: Multiple CPP check passes with documented suppressions
- **Testing**: 68+ test executables with focused system tests
- **Memory Safety**: All ASAN tests passing
- **Thread Safety**: Deferred queues and proper synchronization throughout
- **Lines Changed**: 8,093 insertions, 3,632 deletions
- **Files Modified**: 95 files across all subsystems

---

## 🔍 Technical Highlights

### Collision Detection
- **Hierarchical spatial partitioning** reduces broad-phase collision checks
- **Coarse/fine grid system** adapts to entity density
- **Vector pooling** eliminates allocation overhead in hot paths
- **AABB caching** for frequently queried static objects

### Threading Architecture
- **Deferred command queues** prevent race conditions during state transitions
- **Dynamic batch sizing** adapts to workload pressure
- **Deterministic synchronization** points between AI and collision systems
- **WorkerBudget priorities** ensure critical tasks complete first

### AI Performance
- **SIMD acceleration** for proximity checks and collision avoidance
- **Spatial priority sorting** focuses computation on visible/relevant entities
- **Batched pathfinding requests** amortize overhead
- **Crowd behavior** optimizations for large groups

---

## 🚀 Migration Notes

When merging to `main`:

1. **Collision System API**: The collision system API has changed significantly
   - `SpatialHash` → `HierarchicalSpatialHash`
   - Update any custom collision handling code
   - Test all collision integration points

2. **Threading Patterns**: Threading patterns are now standardized
   - Review any custom thread usage
   - Use `ThreadSystem::enqueueAsync()` instead of custom threads
   - Adopt deferred command queue pattern for state modifications

3. **Audio Files**: Audio files now use AIFF format on macOS
   - Tools provided for conversion if needed
   - Original WAV files preserved as `.wav.original`

4. **Documentation**: CLAUDE.md moved to root directory
   - Update any references to `.claude/CLAUDE.md`
   - Review updated build and testing procedures

5. **Test Scripts**: Test scripts renamed and reorganized
   - Update CI/CD pipelines if applicable
   - Use new split benchmark scripts

6. **Build System**: Mold linker integration
   - Install mold for faster builds (optional but recommended)
   - CMake will auto-detect and use if available

---

## 📝 Commit Timeline

### Week 1 (4 weeks ago)
- Initial threading unification changes
- AIManager and ParticleManager batch processing refactor
- Collision tuning for high entity counts
- AI behavior parameter adjustments

### Week 2 (3 weeks ago)
- Collision system refactor begins
- HierarchicalSpatialHash implementation
- Threading safety tests added
- Vector pooling implementation
- Static cache and AABB improvements

### Week 3 (2 weeks ago)
- Deferred command queue implementation
- macOS sound loading fixes
- Multiple ASAN fixes
- State cleanup pattern standardization
- Coarse grid caching

### Week 4 (Recent)
- Final optimizations and tuning
- Building collision fixes
- SIMD integration
- Documentation updates
- CPP check cleanup
- Final testing and validation

---

## 🎉 Conclusion

The Threading_fixes branch represents a comprehensive modernization of the SDL3 HammerEngine's core systems. Key achievements include:

- **Scalability**: Engine now handles large entity counts with improved performance
- **Stability**: Eliminated race conditions and memory safety issues
- **Performance**: Significant improvements through SIMD, caching, and batching
- **Maintainability**: Cleaner code, better documentation, comprehensive tests
- **Cross-platform**: Improved macOS compatibility, Windows enum fixes

This branch is production-ready and recommended for merge to main after final integration testing.

---

**Generated**: 2025-10-14
**Author**: RoninXV / Hammer Forged Games
**License**: MIT License
