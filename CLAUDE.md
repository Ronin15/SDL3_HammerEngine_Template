# CLAUDE.md

Claude Code guidance for SDL3 HammerEngine development.

## Build Commands

```bash
# Debug
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build

# Release
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build

# Debug with AddressSanitizer (requires -DUSE_MOLD_LINKER=OFF)
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" -DUSE_MOLD_LINKER=OFF && ninja -C build

# Debug with ThreadSanitizer (requires -DUSE_MOLD_LINKER=OFF)
# Use for data races, deadlocks, thread synchronization issues. Mutually exclusive with ASAN.
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=thread -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" -DUSE_MOLD_LINKER=OFF && ninja -C build

# Run tests with TSAN suppressions (filters benign lock-free races)
export TSAN_OPTIONS="suppressions=$(pwd)/tsan_suppressions.txt"
./bin/debug/thread_safe_ai_manager_tests

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

## UI Positioning Best Practices

**CRITICAL**: Always call `setComponentPositioning()` after creating UI components to ensure proper behavior during fullscreen transitions and window resizing.

### Available Helper Methods (Auto-Positioning)

**Use these for common patterns** (they combine creation + positioning in one call):

```cpp
// Page titles (all states)
ui.createTitleAtTop("title", "My State Title");

// Back buttons (bottom-left)
ui.createButtonAtBottom("back", "Back");

// Centered button stacks (menus)
int buttonStep = 70;
ui.createCenteredButton("play_btn", -135, 300, 50, "Play Game");
ui.createCenteredButton("settings_btn", -135 + buttonStep, 300, 50, "Settings");
ui.createCenteredButton("quit_btn", -135 + 2*buttonStep, 300, 50, "Quit");

// Modal dialogs
ui.createCenteredDialog("dialog", 400, 300);
```

**For everything else, use manual pattern:**

```cpp
// HUD elements - top-left
ui.createProgressBar("health", {20, 150, 200, 25}, 0.0f, 100.0f);
ui.setComponentPositioning("health", {UIPositionMode::TOP_ALIGNED, 20, 150, 200, 25});

// Panels - right-aligned
ui.createPanel("minimap", {ui.getLogicalWidth() - 160, 20, 140, 140});
ui.setComponentPositioning("minimap", {UIPositionMode::RIGHT_ALIGNED, 20, 20, 140, 140});

// Event logs - bottom-right
ui.createEventLog("log", {10, ui.getLogicalHeight() - 200, 730, 180}, 7);
ui.setComponentPositioning("log", {UIPositionMode::BOTTOM_RIGHT, 10, 20, 730, 180});
```

### Manual Positioning Pattern

When not using helper methods, **ALWAYS** call `setComponentPositioning()`:

```cpp
// ❌ WRONG - Will break during fullscreen toggle
ui.createButton("my_btn", {100, 200, 120, 40}, "Click Me");
// Component defaults to ABSOLUTE mode - won't reposition on window resize

// ✅ CORRECT - Fullscreen-safe
ui.createButton("my_btn", {100, 200, 120, 40}, "Click Me");
ui.setComponentPositioning("my_btn", {UIPositionMode::LEFT_ALIGNED, 100, 200, 120, 40});
// Component will reposition correctly during fullscreen transitions
```

### Positioning Modes Reference

```cpp
// Anchored to edges (use baseline coordinates for offsets)
UIPositionMode::LEFT_ALIGNED      // offsetX from left edge
UIPositionMode::RIGHT_ALIGNED     // offsetX from right edge (margin)
UIPositionMode::TOP_ALIGNED       // offsetY from top edge
UIPositionMode::BOTTOM_ALIGNED    // offsetY from bottom edge (margin)
UIPositionMode::BOTTOM_RIGHT      // Both right and bottom edges
UIPositionMode::BOTTOM_CENTERED   // Bottom edge, centered horizontally

// Centered positioning
UIPositionMode::CENTERED_H        // Horizontal center + offsetX
UIPositionMode::CENTERED_V        // Vertical center + offsetY
UIPositionMode::CENTERED_BOTH     // Both axes centered

// Fixed positioning (avoid unless truly needed)
UIPositionMode::ABSOLUTE          // No auto-repositioning - breaks in fullscreen
```

### Common Patterns

```cpp
// Centered menu buttons (MainMenuState pattern)
ui.createButton("play", {960, 300, 200, 50}, "Play Game");
ui.setComponentPositioning("play", {UIPositionMode::CENTERED_BOTH, 0, 0, 200, 50});

// Top-left HUD info (EventDemoState pattern)
ui.createLabel("fps", {10, 55, 200, 36}, "FPS: 60");
ui.setComponentPositioning("fps", {UIPositionMode::TOP_ALIGNED, 10, 55, 200, 36});

// Bottom-right event log (UIDemoState pattern)
ui.createEventLog("log", {1180, 900, 730, 180}, 5);
ui.setComponentPositioning("log", {UIPositionMode::BOTTOM_RIGHT, 10, 20, 730, 180});

// Left column form elements
ui.createSlider("volume", {50, 140, 200, 30}, 0.0f, 1.0f);
ui.setComponentPositioning("volume", {UIPositionMode::LEFT_ALIGNED, 50, 140, 200, 30});
```

### Why This Matters

**Without proper positioning**: Toggling fullscreen before entering a state causes misaligned UI because components calculate positions using the current window size at creation time but never recalculate during resize.

**With proper positioning**: Components automatically reposition when window size changes (fullscreen toggle, manual resize, state transitions), using the positioning rules you specify.

**Rule of thumb**: If you're calculating positions using `ui.getLogicalWidth()` or `ui.getLogicalHeight()`, you MUST call `setComponentPositioning()` afterward.

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

**Platforms**: x86-64 (SSE2/AVX2) + ARM64 (NEON). See `include/utils/SIMDMath.hpp` for cross-platform abstraction layer.

**Optimized Systems**: AIManager (distances: 3-4x faster) | CollisionManager (bounds/masks: 2-3x faster) | ParticleManager

**Pattern**: Process 4 elements/iteration (SSE2/NEON width) with scalar tail loop for remainder. Always provide scalar fallback. See `AIManager::calculateDistancesSIMD()` for reference implementation.

**Rules**: Scalar fallback required | Handle non-multiple-of-4 with tail loop | `alignas(16)` for aligned data | Test both x86-64 and ARM64 | Release builds (`-O3 -march=native`) for full utilization

## GameEngine Update/Render Flow

**Pattern**: VSync-based lock-free double-buffering. Update thread (mutex-locked) runs game logic concurrently with main thread rendering.

**Core Loop** (HammerMain.cpp):
```cpp
if (gameEngine.hasNewFrameToRender()) gameEngine.swapBuffers();  // Atomic check + swap
gameEngine.update(deltaTime);  // Concurrent with render
```

**Frame Pacing**: VSync blocks `SDL_RenderPresent()` (default) | Software fallback uses `SDL_Delay()` on Wayland (auto-configured via `setSoftwareFrameLimiting()`).

**Coordination**: Atomic flags (`m_bufferReady[]`) + buffer indices (`m_currentBufferIndex`/`m_renderBufferIndex`) enable lock-free buffer swaps. No explicit wait/signal synchronization.

**Rules**: Update/render concurrent | VSync provides timing | No background rendering | NEVER static vars in threaded code

## Rendering Rules

**Critical for SDL3_Renderer**: Requires **exactly one Present() per frame** through unified render path.

**NEVER Manual Rendering in GameStates**:
- NEVER call `SDL_RenderClear()` or `SDL_RenderPresent()` directly in GameState classes
- ALL rendering MUST go through: `GameEngine::render()` → `GameStateManager::render()` → `GameState::render()`
- Multiple Present() calls break frame pacing and cause rendering artifacts

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
