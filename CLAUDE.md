# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Debug build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build

# Release build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build

# Run the engine
./bin/debug/SDL3_Template
./bin/release/SDL3_Template

# Reconfigure (required when switching sanitizers or build options)
rm build/CMakeCache.txt && cmake -B build/ ...
```

**Sanitizers** (mutually exclusive, require CMakeCache removal to switch):
```bash
# AddressSanitizer
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=address -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" -DUSE_MOLD_LINKER=OFF && ninja -C build

# ThreadSanitizer
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=thread -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" -DUSE_MOLD_LINKER=OFF && ninja -C build
export TSAN_OPTIONS="suppressions=$(pwd)/tests/tsan_suppressions.txt"
```

## Testing

Uses **Boost.Test** framework with 78+ test executables.

```bash
# Run a single test executable
./bin/debug/<test_executable>

# List available test cases
./bin/debug/<test_executable> --list_content

# Run specific test by name (use BOOST_AUTO_TEST_CASE name directly)
./bin/debug/<test_executable> --run_test="TestCase*"

# Examples
./bin/debug/entity_data_manager_tests
./bin/debug/behavior_functionality_tests --run_test="FleeFromAttacker*"

# Run all tests (slow, 7-20 min)
./tests/test_scripts/run_all_tests.sh --core-only --errors-only
```

Prefer direct test executables over wrapper scripts for speed.

## Architecture

**C++20 game engine** using SDL3, built with CMake/Ninja. Data-oriented design supporting 10K+ entities at 60+ FPS.

### Dependency Direction
```
Core → Managers → GameStates → Entities/Controllers
```

### Source Layout
- `src/` and `include/` mirror each other: `{core, managers, controllers, gameStates, entities, events, ai, collisions, utils, world, gpu}`
- `tests/` — Boost.Test suites, scripts in `tests/test_scripts/`
- `res/` — images, shaders, fonts, data, sprites
- `docs/` — comprehensive subsystem documentation

### Key Systems

- **EntityDataManager (EDM)**: Single source of truth. Structure-of-Arrays storage for all entity state. Pure data storage — no decision logic.
- **AIManager**: Batch-processes behaviors across worker threads. Behaviors (`BehaviorExecutors.hpp/.cpp`) operate on pre-fetched context from EDM.
- **EventManager**: Central event hub with deferred main-thread draining. Separate combat/non-combat queues.
- **ThreadSystem**: Hardware-aware worker pool (`hardware_concurrency - 1`). All threading goes through this, never raw `std::thread`.
- **WorkerBudget**: Adaptive threading policy with threshold learning. Managers use `shouldUseThreading()` → batch → `reportExecution()` pattern.
- **ControllerRegistry**: State-scoped controller lifecycle. Controllers added in `enter()`, cleared in `exit()`.

### Rendering (SDL3 GPU only)
- Flow: scene pass → composite to swapchain → UI pass. Platform-native shaders (Metal/macOS, DXIL/Windows, SPIR-V/Linux).
- Game states implement `renderGPUScene()` and `renderGPUUI()`. The engine ends the frame outside the state.
- Scene texture = viewport dimensions (1x). Zoom handled in composite shader, not by scaling tile positions.
- Composite shader uses LINEAR sampler for sub-pixel scrolling: `uv = fragTexCoord / zoom + subPixelOffset`.

**Rule**: Exactly one present per frame. `GameEngine::render()` handles scene+UI, `GameEngine::present()` does the present.

### AI and Controller Boundaries
- Decision logic belongs in `Behaviors::` namespace (`BehaviorExecutors.hpp/.cpp`), not in EDM.
- Controllers must never directly mutate AI behavior state in EDM.
  - Main thread: `Behaviors::queueBehaviorMessage(idx, BehaviorMessage::X)`
  - Worker threads: `Behaviors::deferBehaviorMessage()`
- Cross-frame state (paths, timers) belongs in EDM, not local variables.
- `switchBehavior()` calls `clearBehaviorData()` → `setBehaviorConfig()` → `init()`. State set before switch is wiped — always set state after.

### State Transitions
Cleanup order for AI-heavy states when all managers are initialized:
AIManager → BackgroundSimulationManager → WorldResourceManager → EventManager → CollisionManager → PathfinderManager → EntityDataManager → WorkerBudgetManager → ParticleManager

`ControllerRegistry::clear()` (not just `unsubscribeAll()`) must be called in `GamePlayState::exit()`.

## Coding Conventions

- C++20, 4-space indent, Allman braces
- **Naming**: UpperCamelCase types, lowerCamelCase functions/variables, `m_`/`mp_` members, `ALL_CAPS` constants
- `.hpp` headers, non-trivial logic in `.cpp`
- RAII and smart pointers; no raw `new/delete`
- `std::format()` for logs; never concatenate with `+`
- Use `const T&` for read-only non-trivial inputs, value for primitives
- Avoid per-frame allocations; reuse buffers with `clear()`, `reserve()` when size is known
- Copyright: `/* Copyright (c) 2025 Hammer Forged Games ... MIT License */`

## Repo-Specific Traps

- Demo states are for testing, not production. `GamePlayState` is the pristine official gameplay state.
- File/class names don't always match runtime state names: `EventDemoState` → "EventDemo", `UIDemoState.hpp` → `UIExampleState`
- EDM render data stores `shared_ptr<SDL_Texture>` handles — call `.get()` only at the final SDL draw site, never copy `shared_ptr` in visible-entity loops
- `WorldResourceManager` is a spatial index over EDM, not a quantity store
- Do not wire `subscribeWorldEvents()` in init() — `WorldManager` fires deferred events that arrive after new world is populated

## Working Principles

See `AGENTS.md` for detailed task checklists and the full priority order. Key points:
- Read the exact code path before editing. Search for matching patterns in the same subsystem.
- Fix root causes in production code; never bypass failing tests by changing expectations.
- Keep production code and tests aligned in the same change.
- Prefer minimal direct fixes over new abstractions.
- Run the most targeted test executable after changes when feasible.
