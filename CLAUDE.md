# CLAUDE.md

Claude Code guidance for SDL3 HammerEngine development.

## Build Commands

```bash
# Debug
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build

# Release
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build

# Debug with AddressSanitizer (requires -DUSE_MOLD_LINKER=OFF)
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=address" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" -DUSE_MOLD_LINKER=OFF && ninja -C build

# Filter warnings/errors
ninja -C build -v 2>&1 | grep -E "(warning|unused|error)" | head -n 100

# Clean: ninja -C build clean | Full rebuild: rm -rf build/
# Reconfigure: rm build/CMakeCache.txt (optional, re-running cmake updates settings)
```

**Outputs**: `bin/debug/` or `bin/release/`
**Run**: `./bin/debug/SDL3_Template` | `timeout 60s ./bin/debug/SDL3_Template > /tmp/app_log.txt 2>&1`

## Testing

**Framework**: Boost.Test (68+ test executables in `bin/debug/`). Use targeted tests for specific systems; avoid full suite during development.

```bash
# All tests
./run_all_tests.sh --core-only --errors-only

# Targeted tests
./tests/test_scripts/run_save_tests.sh --verbose
./tests/test_scripts/run_ai_optimization_tests.sh
./tests/test_scripts/run_thread_tests.sh
./bin/debug/SaveManagerTests --run_test="TestSaveAndLoad*"
```

## Architecture

**Core**: GameEngine (double-buffered, central coordinator) | GameLoop (fixed timestep, separate update/render threads) | ThreadSystem (WorkerBudget priorities) | Logger (thread-safe)

**Systems**: AIManager (10K+ entities @ 60+ FPS, batch-processed) | EventManager (thread-safe, batch processing) | CollisionManager (spatial hash, pathfinding integration) | ParticleManager (camera-aware, batched) | WorldManager (tile-based, procedural) | UIManager (theming, DPI-aware) | ResourceManager (JSON + handles) | InputManager (keyboard/mouse/gamepad)

**Utils**: Camera (world↔screen, zoom) | Vector2D (2D math) | JsonReader | BinarySerializer (cross-platform save/load)

```
src/{core, managers, gameStates, entities, events, ai, collisions, utils, world}
include/  # Headers mirror src/
tests/    # Boost.Test scripts
res/      # Assets
```

## Development Workflow

**ALWAYS Check Established Patterns First**: Before implementing new features, search the codebase for existing patterns. Don't reinvent solutions that already exist.

**Pattern Discovery**:
```bash
# UI positioning/components
grep -rn "createEventLog\|BOTTOM_\|createOverlay" src/gameStates/
grep -n "enum class UIPositionMode" include/managers/UIManager.hpp

# Reference states: EventDemoState (event log, bottom-left), UIDemoState (full UI showcase),
# SettingsMenuState (centered buttons, tabs), MainMenuState (simple menus)

# Manager patterns
grep -rn "class.*Manager" include/managers/
```

**When to Add New Patterns**: Only when (1) existing patterns don't solve the use case, (2) new pattern is a logical extension of existing ones. Mirror established implementations whenever possible.

## Standards

**C++20** | 4-space indent, Allman braces | RAII + smart pointers | ThreadSystem (not raw std::thread) | Exceptions for critical errors, codes for expected failures | Logger macros | Cross-platform guards | STL algorithms > manual loops

**Parameter Passing**: **ALWAYS prefer references over copies**. Use `const T&` for read-only access to non-trivial objects. Use `T&` for mutation. Pass by value only for primitives (int, float, bool) or intentional ownership transfer (move semantics). NEVER copy when a reference suffices.

**Naming**: UpperCamelCase (classes/enums) | lowerCamelCase (functions/vars) | `m_` prefix (members), `mp_` (pointers) | ALL_CAPS (constants)

**Headers**: `.hpp` for C++, `.h` for C | Minimal interface, forward declarations | Non-trivial logic in .cpp | Inline only for trivial 1-2 line accessors

**Threading**: Update (mutex-locked, fixed timestep) | Render (main thread only, double-buffered) | Background (ThreadSystem + WorkerBudget) | **NEVER static vars in threaded code** (use instance vars, thread_local, or atomics)

**Copyright** (all files):
```cpp
/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/
```

## Memory Management

**Avoid Per-Frame Allocations**: Heap allocations cause periodic frame dips due to allocator overhead (defragmentation, OS paging). Reuse buffers across frames.

**Buffer Reuse Pattern**:
```cpp
// BAD: Allocates/deallocates 128KB every frame
void update() {
    std::vector<Data> buffer;  // Fresh allocation
    buffer.reserve(entityCount);
    // ... use buffer
}  // Deallocation

// GOOD: Reuses capacity across frames
class Manager {
    std::vector<Data> m_reusableBuffer;  // Member variable

    void update() {
        m_reusableBuffer.clear();  // Keeps capacity, no dealloc
        // ... use m_reusableBuffer
    }
};
```

**Pre-allocation**: Always `reserve()` when size is known to avoid incremental reallocations:
```cpp
std::vector<Entity> entities;
entities.reserve(expectedCount);  // Single allocation
for (size_t i = 0; i < expectedCount; ++i) {
    entities.push_back(data[i]);  // No reallocs
}
```

**Rules**: Member vars for hot-path buffers | `clear()` over reconstruction | `reserve()` before loops | Avoid `push_back()` without reserve | Profile allocations with -fsanitize=address

## Cross-Platform SIMD Optimizations

**Supported Platforms**: x86-64 (SSE2/AVX2) + ARM64 (NEON). Intel Macs are NOT supported.

**SIMD-Optimized Systems**:
- **AIManager**: Distance calculations (3-4x speedup on 10K+ entities)
- **CollisionManager**: Bounds calculation, layer mask filtering (2-3x speedup on Apple Silicon)
- **ParticleManager**: Various particle operations

**Platform Detection** (automatic):
```cpp
#ifdef AI_SIMD_SSE2        // x86-64 with SSE2
#ifdef AI_SIMD_AVX2        // x86-64 with AVX2
#ifdef AI_SIMD_NEON        // ARM64 (Apple Silicon)
```

**SIMD Abstraction Layer**: `include/utils/SIMDMath.hpp` provides cross-platform SIMD utilities:
```cpp
using namespace HammerEngine::SIMD;

// Platform-agnostic SIMD operations
Float4 a = load4(ptr);              // Load 4 floats
Float4 b = broadcast(value);         // Broadcast scalar to all lanes
Float4 c = add(a, b);               // Add vectors
Float4 d = mul(c, broadcast(2.0f)); // Multiply by scalar
store4(ptr, d);                      // Store results
```

**Implementation Pattern**:
```cpp
void processData(const std::vector<Data>& input) {
#if defined(AI_SIMD_SSE2)
    // x86-64 SSE2 path - process 4 elements at once
    for (size_t i = 0; i + 3 < input.size(); i += 4) {
        __m128 data = _mm_loadu_ps(&input[i].value);
        // ... SIMD processing ...
    }
    // Scalar tail for remaining elements
#elif defined(AI_SIMD_NEON)
    // ARM NEON path - process 4 elements at once
    for (size_t i = 0; i + 3 < input.size(); i += 4) {
        float32x4_t data = vld1q_f32(&input[i].value);
        // ... SIMD processing ...
    }
    // Scalar tail for remaining elements
#else
    // Scalar fallback - always works
    for (size_t i = 0; i < input.size(); ++i) {
        // ... scalar processing ...
    }
#endif
}
```

**Best Practices**:
- Always provide scalar fallback path (portability + debugging)
- Process 4 elements per iteration (SSE2/NEON native width)
- Handle non-multiple-of-4 counts with scalar tail loop
- Use aligned loads/stores when possible (`alignas(16)`)
- Test on both x86-64 and ARM64 platforms

**Performance Notes**:
- SIMD provides 2-4x speedup for arithmetic-heavy operations
- Memory bandwidth can be bottleneck (ensure cache-friendly access)
- Branch prediction matters - minimize conditionals in SIMD loops
- Release builds (`-O3 -march=native`) enable full SIMD utilization

**Real-World SIMD Usage Example** (AIManager distance calculation):
```cpp
void calculateDistancesSIMD(size_t start, size_t end,
                            const Vector2D& playerPos,
                            const EntityStorage& storage,
                            std::vector<float>& outDistances) {
    using namespace HammerEngine::SIMD;

    Float4 playerPosX = broadcast(playerPos.x);
    Float4 playerPosY = broadcast(playerPos.y);

    // Process 4 entities per iteration (SSE2/NEON native width)
    for (size_t i = start; i + 3 < end; i += 4) {
        // Load 4 entity positions at once
        Float4 entityX = load4(&storage.hotData[i].position.x);
        Float4 entityY = load4(&storage.hotData[i].position.y);

        // Calculate distance squared (4 entities in parallel)
        Float4 dx = sub(entityX, playerPosX);
        Float4 dy = sub(entityY, playerPosY);
        Float4 distSq = add(mul(dx, dx), mul(dy, dy));

        // Store results
        store4(&outDistances[i], distSq);
    }

    // Scalar tail for remaining elements (handles non-multiple-of-4 counts)
    for (size_t i = (end / 4) * 4; i < end; ++i) {
        float dx = storage.hotData[i].position.x - playerPos.x;
        float dy = storage.hotData[i].position.y - playerPos.y;
        outDistances[i] = dx * dx + dy * dy;
    }
}
```

**Key SIMD Patterns**:
- Always provide scalar tail loop for remaining elements
- Use `broadcast()` to replicate scalar values across SIMD lanes
- `load4()` and `store4()` handle unaligned memory access safely
- The abstraction layer handles platform differences (SSE2/AVX2/NEON) automatically
- Batch processing reduces function call overhead and improves cache locality

## GameEngine Update/Render Flow

**GameLoop** (configured in `HammerMain.cpp`): Drives events (main thread) → fixed-timestep update → render callbacks.

**Update** (thread-safe, mutex-locked): `GameEngine::update(deltaTime)` updates global systems (AIManager, EventManager, ParticleManager) → delegates to `GameStateManager::update`. Completes before render starts.

**Double Buffer Synchronization**: `m_currentBufferIndex` (update) + `m_renderBufferIndex` (render) + `m_bufferReady[]` control frame synchronization. Buffer indices select which `m_bufferReady[]` flag indicates a complete update frame. Entity data is single-buffered with mutex protection during update; atomic flags ensure render reads stable data. Main loop calls `hasNewFrameToRender()` + `swapBuffers()` before update using lock-free atomic operations.

**Render** (main thread only): `GameEngine::render()` clears renderer → `GameStateManager::render()` → world/entities/particles/UI (deterministic order, current camera).

**Rules**: No background thread rendering (all drawing in `GameEngine::render()`) | No extra manager sync (rely on mutexed update + buffer swap) | Snapshot camera once per render | NEVER static vars in threaded code

## Rendering Rules

**Critical for SDL3_GPU Compatibility**: SDL3_GPU uses command buffer architecture requiring **exactly one Present() per frame** through unified render path.

**NEVER Manual Rendering in GameStates**:
- NEVER call `SDL_RenderClear()` or `SDL_RenderPresent()` directly in GameState classes
- ALL rendering MUST go through: `GameEngine::render()` → `GameStateManager::render()` → `GameState::render()`
- Multiple Present() calls break SDL3_GPU's command buffer system

**Loading Screens**: Use `LoadingState` with async operations (never blocking with manual rendering):
```cpp
// Configure LoadingState before transition
auto* loadingState = dynamic_cast<LoadingState*>(gameStateManager->getState("LoadingState").get());
loadingState->configure("TargetStateName", worldConfig);
gameStateManager->changeState("LoadingState");

// LoadingState handles async world generation on ThreadSystem
// Progress bar renders through normal GameEngine::render() flow
// No manual SDL_RenderClear/Present calls needed
```

**Deferred State Transitions**: State changes from `enter()` cause timing issues. Use deferred pattern:
```cpp
bool GameState::enter() {
    if (!m_worldLoaded) {
        m_needsLoading = true;    // Set flag
        m_worldLoaded = true;      // Prevent loop
        return true;               // Exit early
    }
    // Normal initialization when world loaded
}

void GameState::update(float deltaTime) {
    if (m_needsLoading) {
        m_needsLoading = false;
        // Configure and transition to LoadingState here
        return;
    }
    // Normal update
}
```

**Rules**: One Present per frame | Use LoadingState for async loading | Deferred transitions from update() | No manual SDL rendering calls in GameStates
