# Data-Driven NPC Entity System Implementation

## Overview

Remove the NPC class and make NPCs data-driven entities with:
- **Moving/Idle animations only** (velocity-based, no state machine)
- **Player class stays dynamic** (keeps full animation system)
- **AttackBehavior simplified** to velocity-based lunge attacks

---

## Architecture Summary

### Current State
```
NPC Class → EntityPtr → EntityStateManager → Animation States
                     → TextureID, Cached Texture
                     → Frame dimensions, Animation maps
```

### Target State
```
EntityDataManager → NPCRenderData (texture, frame, flip)
NPCRenderController → Velocity-based animation (render from GameState)
                   → Active tier filtering (only render on-screen NPCs)
AttackBehavior → Lunge velocity burst (combat event triggered)
```

---

## Phase 1: NPCRenderData in EntityDataManager

### File References
- `include/managers/EntityDataManager.hpp`
- `src/managers/EntityDataManager.cpp`
- `include/entities/Entity.hpp` (AnimationConfig struct location)

### Reusing AnimationConfig (Existing Struct)

**AnimationConfig already exists in `Entity.hpp`:**
```cpp
struct AnimationConfig {
    int row;           // Sprite sheet row (0-based)
    int frameCount;    // Number of frames in animation
    int speed;         // Milliseconds per frame
    bool loop;         // Whether animation loops
};
```

**Current NPC usage:**
```cpp
m_animationMap["idle"] = AnimationConfig{0, 2, 150, true};     // Row 0, 2 frames
m_animationMap["walking"] = AnimationConfig{1, 4, 100, true};  // Row 1, 4 frames
```

**Data-driven usage:** Pass AnimationConfig at spawn time → values extracted into NPCRenderData.

### NPCRenderData Struct (32 bytes, cache-friendly)

```cpp
struct NPCRenderData {
    SDL_Texture* cachedTexture{nullptr};  // 8 bytes - Cached at spawn, used for rendering
    uint16_t frameWidth{32};              // 2 bytes - Single frame width (from texture)
    uint16_t frameHeight{32};             // 2 bytes - Single frame height (from texture)
    uint8_t currentFrame{0};              // 1 byte - Current animation frame index
    uint8_t numIdleFrames{2};             // 1 byte - FROM AnimationConfig (idleConfig.frameCount)
    uint8_t numMoveFrames{4};             // 1 byte - FROM AnimationConfig (moveConfig.frameCount)
    uint8_t idleSpeed{150};               // 1 byte - FROM AnimationConfig (idleConfig.speed)
    uint8_t moveSpeed{100};               // 1 byte - FROM AnimationConfig (moveConfig.speed)
    uint8_t flipMode{0};                  // 1 byte - SDL_FLIP_NONE or SDL_FLIP_HORIZONTAL
    uint8_t padding[6]{};                 // 6 bytes - Align to 32 bytes
    float animationAccumulator{0.0f};     // 4 bytes - Time accumulator for frame advancement
    std::string textureID;                // For debugging only - NOT used in render
};
```

**NOTE:** Default values are fallbacks only. Actual values populated from AnimationConfig at spawn time.

**Key point:** `cachedTexture` is set once at spawn from TextureManager, then used directly by NPCRenderController for SDL rendering. No TextureManager calls during render loop.

### Implementation Checklist

- [ ] **1.1** Add `NPCRenderData` struct to EntityDataManager.hpp (before class declaration)
- [ ] **1.2** Add storage vector: `std::vector<NPCRenderData> m_npcRenderData;`
- [ ] **1.3** Add accessor: `NPCRenderData& getNPCRenderData(size_t index);`
- [ ] **1.4** Add const accessor: `const NPCRenderData& getNPCRenderData(size_t index) const;`
- [ ] **1.5** Reserve capacity in constructor to match entity capacity
- [ ] **1.6** Add `createDataDrivenNPC(position, textureID, idleConfig, moveConfig)` method
- [ ] **1.7** Implement texture caching in createDataDrivenNPC (lookup from TextureManager)
- [ ] **1.8** Add `destroyDataDrivenNPC(EntityHandle handle)` method for cleanup
- [ ] **1.9** Compile and verify no errors

---

## Phase 2: NPCRenderController

### File References
- `include/controllers/render/NPCRenderController.hpp` (NEW)
- `src/controllers/render/NPCRenderController.cpp` (NEW)
- `include/controllers/ControllerBase.hpp` (reference pattern)
- `include/controllers/IUpdatable.hpp` (reference pattern)

### Key Design Points

- **Direct member** in GameState (not through ControllerRegistry)
- **Renderer passed in** from GameState::render() (not stored)
- **Uses getActiveIndices()** for active tier filtering
- **Velocity threshold** of 15.0f for Moving/Idle determination

### Header Implementation

```cpp
#ifndef NPC_RENDER_CONTROLLER_HPP
#define NPC_RENDER_CONTROLLER_HPP

#include "controllers/ControllerBase.hpp"
#include "controllers/IUpdatable.hpp"
#include <SDL3/SDL.h>

class NPCRenderController : public ControllerBase, public IUpdatable {
public:
    NPCRenderController() = default;

    // ControllerBase interface
    void subscribe() override {}  // No events needed
    std::string_view getName() const override { return "NPCRenderController"; }

    // IUpdatable - advances animation frames
    void update(float deltaTime) override;

    // Called from GameState::render() - renderer passed in
    void renderNPCs(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha);

private:
    static constexpr float MOVEMENT_THRESHOLD = 15.0f;
};

#endif
```

### Implementation Checklist

- [ ] **2.1** Create directory: `include/controllers/render/`
- [ ] **2.2** Create directory: `src/controllers/render/`
- [ ] **2.3** Create `NPCRenderController.hpp` with header above
- [ ] **2.4** Create `NPCRenderController.cpp`
- [ ] **2.5** Implement `update(float deltaTime)`:
  - [ ] Get `getActiveIndices()` from EntityDataManager
  - [ ] Filter by `EntityKind::NPC`
  - [ ] Update animation frame based on velocity
  - [ ] Update flip mode based on velocity.getX()
- [ ] **2.6** Implement `renderNPCs(renderer, camX, camY, alpha)`:
  - [ ] Get `getActiveIndices()` from EntityDataManager
  - [ ] Filter by `EntityKind::NPC`
  - [ ] Interpolate position using alpha
  - [ ] Compute row from velocity (0=Idle, 1=Moving)
  - [ ] Render using SDL_RenderTextureRotated
- [ ] **2.7** Add to CMakeLists.txt (src files)
- [ ] **2.8** Compile and verify no errors

### Animation Logic Reference

```cpp
// In update():
float velocityMag = hotData.velocity.length();
bool isMoving = velocityMag > MOVEMENT_THRESHOLD;
uint8_t targetFrames = isMoving ? renderData.numMoveFrames : renderData.numIdleFrames;
float speed = (isMoving ? renderData.moveSpeed : renderData.idleSpeed) / 1000.0f;

renderData.animationAccumulator += deltaTime;
if (renderData.animationAccumulator >= speed) {
    renderData.currentFrame = (renderData.currentFrame + 1) % targetFrames;
    renderData.animationAccumulator -= speed;
}

// Flip based on velocity X
if (std::abs(hotData.velocity.getX()) > MOVEMENT_THRESHOLD) {
    renderData.flipMode = (hotData.velocity.getX() < 0) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
}
```

### Render Logic Reference

```cpp
// In renderNPCs():
// Interpolate position
Vector2D pos = hotData.previousPosition +
    (hotData.position - hotData.previousPosition) * alpha;

// Velocity determines row: 0=Idle, 1=Moving
int row = (hotData.velocity.length() > MOVEMENT_THRESHOLD) ? 1 : 0;

SDL_FRect srcRect = {
    static_cast<float>(renderData.currentFrame * renderData.frameWidth),
    static_cast<float>(row * renderData.frameHeight),
    static_cast<float>(renderData.frameWidth),
    static_cast<float>(renderData.frameHeight)
};

SDL_FRect destRect = {
    pos.getX() - cameraX - renderData.frameWidth / 2.0f,
    pos.getY() - cameraY - renderData.frameHeight / 2.0f,
    static_cast<float>(renderData.frameWidth),
    static_cast<float>(renderData.frameHeight)
};

SDL_RenderTextureRotated(renderer, renderData.cachedTexture,
    &srcRect, &destRect, 0.0, nullptr,
    static_cast<SDL_FlipMode>(renderData.flipMode));
```

---

## Phase 3: AttackBehavior Simplification

### File References
- `include/ai/behaviors/AttackBehavior.hpp`
- `src/ai/behaviors/AttackBehavior.cpp`

### Design: Lunge Attack

- **Velocity burst** toward target (2x normal speed)
- **Existing cooldown** system remains
- **CollisionManager** handles bump-back naturally
- **Moving animation** plays during lunge (no special animation)

### Lunge Implementation

```cpp
void AttackBehavior::executeLungeAttack(BehaviorContext& ctx, const Vector2D& targetPos) {
    auto& state = m_entityStates[ctx.entityId];
    if (state.attackTimer > 0) return;  // Use existing cooldown

    // Calculate lunge direction
    Vector2D direction = (targetPos - ctx.transform.position).normalized();

    // Apply velocity burst
    ctx.transform.velocity = direction * m_movementSpeed * 2.0f;

    // Start cooldown
    state.attackTimer = m_attackCooldown;
    state.isLunging = true;
}
```

### Implementation Checklist

- [ ] **3.1** Add `bool isLunging{false};` to EntityState struct
- [ ] **3.2** Add `float m_lungeSpeed{4.0f};` member variable
- [ ] **3.3** Simplify `executeAttack()` to just call lunge
- [ ] **3.4** Remove `notifyAnimationStateChange()` calls
- [ ] **3.5** Remove EntityPtr dependencies where possible
- [ ] **3.6** Keep existing cooldown and damage calculation
- [ ] **3.7** Compile and verify AI tests pass

---

## Phase 4: GameState Integration

### File References
- `include/gameStates/EventDemoState.hpp`
- `src/gameStates/EventDemoState.cpp`

### Changes Required

**Header:**
```cpp
// Add include:
#include "controllers/render/NPCRenderController.hpp"

// Add member (direct, not pointer):
NPCRenderController m_npcRenderCtrl;

// Change spawned NPCs storage:
// OLD: std::vector<std::shared_ptr<NPC>> m_spawnedNPCs;
// NEW: std::vector<EntityHandle> m_spawnedNPCHandles;
```

**Spawning:**
```cpp
// OLD:
auto npc = NPC::create(textureID, position);
npc->setNavRadius(18.0f);
npc->setBehavior(behaviorName);
m_spawnedNPCs.push_back(npc);

// NEW:
AnimationConfig idleConfig{0, 2, 150, true};
AnimationConfig moveConfig{1, 4, 100, true};

EntityHandle handle = EntityDataManager::Instance().createDataDrivenNPC(
    position, textureID, idleConfig, moveConfig);

// Collision halfSize derived from frame dimensions (frameWidth/2, frameHeight/2)
Vector2D halfSize(16.0f, 16.0f);  // Or compute from renderData.frameWidth/2
CollisionManager::Instance().addCollisionBodySOA(
    handle.getId(), position, halfSize, CollisionType::DYNAMIC, false);

// Register with AI (navRadius now set via behavior or AIManager)
AIManager::Instance().registerEntity(handle, behaviorName);

m_spawnedNPCHandles.push_back(handle);
```

**Update:**
```cpp
// Add animation update:
m_npcRenderCtrl.update(deltaTime);
```

**Render:**
```cpp
// OLD:
for (const auto& npc : m_spawnedNPCs) {
    if (npc && npc->isInActiveTier()) {
        npc->render(renderer, renderCamX, renderCamY, interpolationAlpha);
    }
}

// NEW:
m_npcRenderCtrl.renderNPCs(renderer, renderCamX, renderCamY, interpolationAlpha);
```

### Implementation Checklist

- [ ] **4.1** Add NPCRenderController include to EventDemoState.hpp
- [ ] **4.2** Add `NPCRenderController m_npcRenderCtrl;` member
- [ ] **4.3** Change `m_spawnedNPCs` to `std::vector<EntityHandle> m_spawnedNPCHandles`
- [ ] **4.4** Update spawning code to use `createDataDrivenNPC()`
- [ ] **4.5** Add `m_npcRenderCtrl.update(deltaTime)` in update()
- [ ] **4.6** Replace NPC render loop with `m_npcRenderCtrl.renderNPCs(...)`
- [ ] **4.7** Update cleanup code for EntityHandles (see Cleanup section below)
- [ ] **4.8** Test: Spawn 10 NPCs, verify rendering and animation
- [ ] **4.9** Test: Spawn 100 NPCs, verify performance

### NPC Cleanup (Destruction)

```cpp
// OLD (NPC class cleanup):
m_spawnedNPCs.clear();  // shared_ptr destructor handles everything

// NEW (Data-driven cleanup):
for (auto& handle : m_spawnedNPCHandles) {
    if (handle.isValid()) {
        // Unregister from managers (order matters)
        AIManager::Instance().unregisterEntity(handle);
        CollisionManager::Instance().removeBody(handle.getId());
        EntityDataManager::Instance().destroyEntity(handle);
        // NPCRenderData cleared automatically when entity destroyed
    }
}
m_spawnedNPCHandles.clear();
```

### NavRadius Handling

**Current:** NPC class stores navRadius per-instance, passed to PathfinderManager.

**Data-driven options:**
1. **Store in behavior config** - Behaviors already have navRadius in AIBehaviorState
2. **Store in EntityHotData** - Add navRadius field if needed globally
3. **Use constant** - Most NPCs use same navRadius (18.0f)

**Recommended:** Behaviors already handle this via `AIBehaviorState::navRadius`:
```cpp
// In AttackBehavior, WanderBehavior, etc.:
struct EntityState {
    AIBehaviorState baseState;  // Contains navRadius = 18.0f
    // ...
};
```

No changes needed - behaviors already store navRadius per-entity.

### In-Game NPC Death (During Gameplay)

When an NPC dies during gameplay (not state cleanup):

```cpp
// Via CombatController or EventManager:
void onNPCDeath(EntityHandle handle) {
    // 1. Mark as dead in EDM (stops AI updates, render skips dead entities)
    auto& hotData = EntityDataManager::Instance().getHotData(handle);
    hotData.isAlive = false;

    // 2. Remove from collision (no more physics)
    CollisionManager::Instance().removeBody(handle.getId());

    // 3. Unregister from AI (no more behavior updates)
    AIManager::Instance().unregisterEntity(handle);

    // 4. Optionally: spawn death particle, drop loot, etc.

    // 5. Entity data remains until GameState cleanup
    // (or implement pooling for entity reuse)
}
```

**Key point:** Setting `isAlive = false` causes:
- `NPCRenderController::renderNPCs()` skips the entity (checks `hotData.isAlive`)
- `NPCRenderController::update()` skips animation updates
- AIManager skips behavior updates for dead entities

---

## Phase 5: Convert All GameStates and Events

### Files That Include NPC.hpp (Must Update)

```
src/gameStates/GamePlayState.cpp       ← Primary gameplay state
src/gameStates/EventDemoState.cpp      ← Demo state (covered in Phase 4)
src/controllers/combat/CombatController.cpp  ← Return EntityHandle
src/events/NPCSpawnEvent.cpp           ← Update spawn event
src/ai/behaviors/AttackBehavior.cpp    ← Already simplified (Phase 3)
src/ai/behaviors/PatrolBehavior.cpp    ← Check for NPC dependencies
```

### Implementation Checklist

- [ ] **5.1** Search for all `#include "entities/NPC.hpp"` references
- [ ] **5.2** Convert `GamePlayState.cpp` following Phase 4 pattern
- [ ] **5.3** Update `NPCSpawnEvent.cpp` to use data-driven spawning
- [ ] **5.4** Update `PatrolBehavior.cpp` if it has NPC-specific code
- [ ] **5.5** Update `CombatController.cpp` to use EntityHandle (Phase 6)
- [ ] **5.6** Verify each file compiles
- [ ] **5.7** Test each state manually

---

## Phase 6: Remove NPC Class

### Files to Delete

```
include/entities/NPC.hpp
src/entities/NPC.cpp

# NPC State Classes (6 states)
include/entities/npcStates/NPCIdleState.hpp
include/entities/npcStates/NPCWalkingState.hpp
include/entities/npcStates/NPCAttackingState.hpp
include/entities/npcStates/NPCHurtState.hpp
include/entities/npcStates/NPCRecoveringState.hpp
include/entities/npcStates/NPCDyingState.hpp

src/entities/npcStates/NPCIdleState.cpp
src/entities/npcStates/NPCWalkingState.cpp
src/entities/npcStates/NPCAttackingState.cpp
src/entities/npcStates/NPCHurtState.cpp
src/entities/npcStates/NPCRecoveringState.cpp
src/entities/npcStates/NPCDyingState.cpp

# Related files to update/check
src/events/NPCSpawnEvent.cpp     ← Update or remove
```

### Implementation Checklist

- [ ] **6.1** Verify all GameStates converted (no NPC.hpp includes remain)
- [ ] **6.2** Update CombatController to return EntityHandle instead of NPCPtr
- [ ] **6.3** Remove `NPCPtr` type alias from Entity.hpp or related files
- [ ] **6.4** Delete NPC.hpp and NPC.cpp
- [ ] **6.5** Delete all npcStates/*.hpp and *.cpp files
- [ ] **6.6** Update CMakeLists.txt to remove deleted files
- [ ] **6.7** Full rebuild to verify clean compilation
- [ ] **6.8** Run all tests: `./tests/test_scripts/run_all_tests.sh --core-only`

---

## Phase 7: Testing & Validation

### Implementation Checklist

- [ ] **7.1** Spawn 10 NPCs, verify animation switches at velocity threshold
- [ ] **7.2** Spawn 100 NPCs, verify performance stable
- [ ] **7.3** Spawn 1000 NPCs, verify no frame drops
- [ ] **7.4** Test lunge attacks work via AttackBehavior
- [ ] **7.5** Test collision bump-back feels natural
- [ ] **7.6** Test off-screen NPCs don't render (active tier filtering)
- [ ] **7.7** Run AI benchmarks: `./tests/test_scripts/run_ai_benchmark.sh`
- [ ] **7.8** Full 10K NPC stress test

---

## Key File Paths Summary

| File | Action |
|------|--------|
| `include/managers/EntityDataManager.hpp` | Add NPCRenderData struct, storage, accessor |
| `src/managers/EntityDataManager.cpp` | Implement createDataDrivenNPC(), clearNPCRenderData() |
| `include/controllers/render/NPCRenderController.hpp` | NEW - velocity-based animation + render |
| `src/controllers/render/NPCRenderController.cpp` | NEW - implements update() and renderNPCs() |
| `include/ai/behaviors/AttackBehavior.hpp` | Simplify to lunge-only |
| `src/ai/behaviors/AttackBehavior.cpp` | Lunge implementation |
| `include/gameStates/EventDemoState.hpp` | Add NPCRenderController member |
| `src/gameStates/EventDemoState.cpp` | Use data-driven spawning, use controller |
| `src/gameStates/GamePlayState.cpp` | Convert to data-driven NPCs |
| `src/controllers/combat/CombatController.cpp` | Return EntityHandle instead of NPCPtr |
| `src/events/NPCSpawnEvent.cpp` | Update to use data-driven spawning |
| `CMakeLists.txt` | Add NPCRenderController, remove NPC files |
| `include/entities/NPC.hpp` | DELETE |
| `src/entities/NPC.cpp` | DELETE |
| `include/entities/npcStates/*.hpp` | DELETE all 6 state files |
| `src/entities/npcStates/*.cpp` | DELETE all 6 state files |

---

## Notes

### Player Class Unchanged
Player keeps full class implementation:
- PlayerIdleState, PlayerRunningState, PlayerAttackingState, etc.
- Full animation system with EntityStateManager
- Input handling
- Equipment/inventory systems

### Velocity Threshold
- 15.0f is the threshold for Moving vs Idle
- Matches existing animation patterns in the codebase

### Sprite Sheet Layout
- Row 0: Idle frames
- Row 1: Moving frames
- Flip determined by velocity.getX() direction

---

## Critical Context for Implementation

### What NPC Class Currently Does (Complete Reference)

**File: `src/entities/NPC.cpp`**

| NPC Functionality | Data-Driven Replacement |
|-------------------|------------------------|
| `NPC::create()` - Factory method | `EntityDataManager::createDataDrivenNPC()` |
| `m_textureID` storage | `NPCRenderData::textureID` (debug only) |
| `mp_cachedTexture` pointer | `NPCRenderData::cachedTexture` |
| `m_frameWidth/Height` | `NPCRenderData::frameWidth/Height` |
| `m_animationMap` (idle, walking, etc) | `AnimationConfig` passed at spawn |
| `initializeAnimationMap()` | Config values passed to `createDataDrivenNPC()` |
| `m_currentAnimation` state | `NPCRenderData::currentFrame` + velocity-based row |
| `playAnimation()` / `stopAnimation()` | Velocity-based (no explicit calls needed) |
| `setAnimationSpeed()` | `NPCRenderData::idleSpeed/moveSpeed` |
| `render()` | `NPCRenderController::renderNPCs()` |
| `update()` | AIManager + `NPCRenderController::update()` |
| `setBehavior()` / `m_behaviorName` | `AIManager::registerEntity()` |
| `setNavRadius()` | `AIBehaviorState::navRadius` in behaviors |
| `isInActiveTier()` | `getActiveIndices()` filtering |
| `getPosition()` / `setPosition()` | `EntityHotData::position` |
| `getVelocity()` / `setVelocity()` | `EntityHotData::velocity` |
| `m_entityStateManager` | **REMOVED** - velocity-based animation only |
| NPC state classes (Idle, Wandering, etc) | **REMOVED** - behaviors handle all states |
| `takeDamage()` / combat | `CombatController` + events |
| `die()` | `EntityDataManager::destroyEntity()` |

### TextureManager Role (Loading Only)

**Location:** `include/managers/TextureManager.hpp`

TextureManager is ONLY used to load textures into EntityDataManager at spawn time.
NPCRenderController does NOT use TextureManager for rendering - it renders directly with SDL.

```cpp
// TextureData struct - what's cached per texture
struct TextureData {
    std::shared_ptr<SDL_Texture> texture;
    float width{0.0f};   // Full spritesheet width
    float height{0.0f};  // Full spritesheet height
};

// Method used at spawn time to get texture pointer:
SDL_Texture* getTexturePtr(const std::string& textureID);  // Raw pointer for caching
bool isTextureInMap(const std::string& textureID);         // Check existence
```

**Flow:**
1. At spawn: `createDataDrivenNPC()` calls `TextureManager::getTexturePtr()`
2. Texture pointer cached in `NPCRenderData::cachedTexture`
3. At render: `NPCRenderController` uses cached pointer directly with SDL calls
4. No TextureManager involvement during render loop

### AIManager & Behavior Flow

**Location:** `include/managers/AIManager.hpp`, `src/managers/AIManager.cpp`

AIManager already handles all movement:
1. `updateBehaviors(deltaTime)` called each frame
2. Behaviors (WanderBehavior, ChaseBehavior, FleeBehavior, AttackBehavior) set velocity
3. Velocity stored in `EntityHotData::velocity`

**NPCRenderController does NOT calculate movement** - it reads velocity that AI already set.

### EntityHotData Struct

**Location:** `include/managers/EntityDataManager.hpp` (around line 50-80)

```cpp
struct EntityHotData {
    Vector2D position{0, 0};           // Current position
    Vector2D previousPosition{0, 0};   // For interpolation
    Vector2D velocity{0, 0};           // Set by AIManager behaviors
    EntityKind kind{EntityKind::UNKNOWN};
    SimulationTier tier{SimulationTier::Active};
    bool isAlive{true};
    // ... other fields
};
```

### EntityDataManager Key APIs

**Location:** `include/managers/EntityDataManager.hpp`

```cpp
// Get active tier entity indices (pre-computed, efficient)
[[nodiscard]] std::span<const size_t> getActiveIndices() const;

// Access hot data by index
[[nodiscard]] const EntityHotData& getHotDataByIndex(size_t index) const;

// Create entity
[[nodiscard]] EntityHandle createEntity(EntityKind kind, const Vector2D& position);

// Get index from handle
[[nodiscard]] size_t getIndexFromHandle(EntityHandle handle) const;
```

### GameState Update/Render Flow

**Location:** `src/gameStates/EventDemoState.cpp`

```cpp
void EventDemoState::update(float deltaTime) {
    // ... other updates ...

    // AIManager updates behaviors, sets velocities
    AIManager::Instance().updateBehaviors(deltaTime);

    // NPCRenderController reads velocities, advances animation frames
    m_npcRenderCtrl.update(deltaTime);
}

void EventDemoState::render(SDL_Renderer* renderer, float interpolationAlpha) {
    // ... camera setup, world render ...

    // NPCRenderController reads velocity, renders with correct animation
    m_npcRenderCtrl.renderNPCs(renderer, renderCamX, renderCamY, interpolationAlpha);

    // ... UI render ...
}
```

### Simulation Tier Filtering

**Location:** `include/entities/EntityHandle.hpp`

```cpp
enum class SimulationTier : uint8_t {
    Active = 0,      // Full update: AI, collision, render (on-screen)
    Background = 1,  // Simplified updates (off-screen but nearby)
    Hibernated = 2   // No updates (far away)
};
```

`getActiveIndices()` returns only Active tier entities - this is the same filtering
that `NPC::isInActiveTier()` currently does.

### Current NPC Spawning Pattern

**Location:** `src/gameStates/EventDemoState.cpp` (~line 400-450)

```cpp
// Current pattern:
auto npc = NPC::create(textureID, position);
npc->setNavRadius(18.0f);
npc->setBehavior(behaviorName);
m_spawnedNPCs.push_back(npc);

// What this does internally:
// 1. Creates Entity with EntityDataManager
// 2. Sets up texture/animation
// 3. Registers with CollisionManager
// 4. Registers with AIManager
```

### Current NPC Render Pattern

**Location:** `src/gameStates/EventDemoState.cpp` (~line 700-710)

```cpp
// Current pattern to replace:
for (const auto &npc : m_spawnedNPCs) {
    if (npc && npc->isInActiveTier()) {
        npc->render(renderer, renderCamX, renderCamY, interpolationAlpha);
    }
}
```

---

## Complete Implementation Code

### NPCRenderController.cpp (Full)

**Key insight:** AIManager already calculates velocity via behaviors (WanderBehavior, ChaseBehavior, etc.).
NPCRenderController just reads velocity and renders - no movement logic needed.

```cpp
/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/render/NPCRenderController.hpp"
#include "managers/EntityDataManager.hpp"
#include "entities/EntityHandle.hpp"
#include <cmath>
// NOTE: No TextureManager include - controller uses cached texture pointer directly

void NPCRenderController::update(float deltaTime) {
    auto& edm = EntityDataManager::Instance();
    auto activeIndices = edm.getActiveIndices();

    for (size_t idx : activeIndices) {
        const auto& hotData = edm.getHotDataByIndex(idx);

        // Filter to NPCs only
        if (hotData.kind != EntityKind::NPC || !hotData.isAlive) {
            continue;
        }

        auto& renderData = edm.getNPCRenderData(idx);

        // AIManager already set velocity via behaviors - just read it
        float velocityMag = hotData.velocity.length();
        bool isMoving = velocityMag > MOVEMENT_THRESHOLD;

        // Select animation parameters based on velocity
        uint8_t targetFrames = isMoving ? renderData.numMoveFrames : renderData.numIdleFrames;
        float speed = static_cast<float>(isMoving ? renderData.moveSpeed : renderData.idleSpeed) / 1000.0f;

        // Advance frame
        renderData.animationAccumulator += deltaTime;
        if (renderData.animationAccumulator >= speed) {
            renderData.currentFrame = (renderData.currentFrame + 1) % targetFrames;
            renderData.animationAccumulator -= speed;
        }

        // Update flip based on velocity X (already calculated by AI)
        if (std::abs(hotData.velocity.getX()) > MOVEMENT_THRESHOLD) {
            renderData.flipMode = (hotData.velocity.getX() < 0)
                ? static_cast<uint8_t>(SDL_FLIP_HORIZONTAL)
                : static_cast<uint8_t>(SDL_FLIP_NONE);
        }
    }
}

void NPCRenderController::renderNPCs(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha) {
    auto& edm = EntityDataManager::Instance();
    auto activeIndices = edm.getActiveIndices();

    for (size_t idx : activeIndices) {
        const auto& hotData = edm.getHotDataByIndex(idx);

        // Filter to NPCs only
        if (hotData.kind != EntityKind::NPC || !hotData.isAlive) {
            continue;
        }

        const auto& renderData = edm.getNPCRenderData(idx);
        if (!renderData.cachedTexture) {
            continue;  // Skip if texture not loaded
        }

        // Interpolate position for smooth rendering
        Vector2D pos = hotData.previousPosition +
            (hotData.position - hotData.previousPosition) * alpha;

        // Velocity determines animation row: 0=Idle, 1=Moving
        int row = (hotData.velocity.length() > MOVEMENT_THRESHOLD) ? 1 : 0;

        // Source rect (from sprite sheet)
        SDL_FRect srcRect = {
            static_cast<float>(renderData.currentFrame * renderData.frameWidth),
            static_cast<float>(row * renderData.frameHeight),
            static_cast<float>(renderData.frameWidth),
            static_cast<float>(renderData.frameHeight)
        };

        // Destination rect (screen position, centered on entity)
        SDL_FRect destRect = {
            pos.getX() - cameraX - static_cast<float>(renderData.frameWidth) / 2.0f,
            pos.getY() - cameraY - static_cast<float>(renderData.frameHeight) / 2.0f,
            static_cast<float>(renderData.frameWidth),
            static_cast<float>(renderData.frameHeight)
        };

        // Direct SDL rendering - controller owns render logic
        SDL_RenderTextureRotated(
            renderer,
            renderData.cachedTexture,  // Cached from TextureManager at spawn
            &srcRect,
            &destRect,
            0.0,
            nullptr,
            static_cast<SDL_FlipMode>(renderData.flipMode)
        );
    }
}
```

### EntityDataManager Additions

**In EntityDataManager.hpp - Add near other structs:**

```cpp
// Forward declaration for SDL
struct SDL_Texture;

struct NPCRenderData {
    SDL_Texture* cachedTexture{nullptr};  // Cached at spawn from TextureManager - used for rendering
    uint16_t frameWidth{32};              // Single frame width
    uint16_t frameHeight{32};             // Single frame height
    uint8_t currentFrame{0};              // Current animation frame index
    uint8_t numIdleFrames{2};             // Frames in idle animation (row 0)
    uint8_t numMoveFrames{4};             // Frames in move animation (row 1)
    uint8_t idleSpeed{150};               // ms per frame for idle
    uint8_t moveSpeed{100};               // ms per frame for moving
    uint8_t flipMode{0};                  // SDL_FLIP_NONE or SDL_FLIP_HORIZONTAL
    uint8_t padding[6]{};                 // Align to 32 bytes
    float animationAccumulator{0.0f};     // Time accumulator for frame advancement
    std::string textureID;                // For debugging/logging only - not used in render
};
```

**In EntityDataManager class - Private section:**

```cpp
std::vector<NPCRenderData> m_npcRenderData;
```

**In EntityDataManager class - Public section:**

```cpp
// NPC render data accessors
[[nodiscard]] NPCRenderData& getNPCRenderData(size_t index);
[[nodiscard]] const NPCRenderData& getNPCRenderData(size_t index) const;

// Create data-driven NPC (returns handle for tracking)
// Uses AnimationConfig from Entity.hpp - same struct NPC class uses
[[nodiscard]] EntityHandle createDataDrivenNPC(
    const Vector2D& position,
    const std::string& textureID,
    const AnimationConfig& idleConfig,   // {0, 2, 150, true} - row 0
    const AnimationConfig& moveConfig    // {1, 4, 100, true} - row 1
);

// Cleanup NPC render data when entity destroyed
void clearNPCRenderData(size_t index);
```

**In EntityDataManager.cpp - Implementation:**

```cpp
#include "managers/TextureManager.hpp"  // Add to includes
#include "entities/Entity.hpp"          // For AnimationConfig

NPCRenderData& EntityDataManager::getNPCRenderData(size_t index) {
    assert(index < m_npcRenderData.size() && "NPC render data index out of bounds");
    return m_npcRenderData[index];
}

const NPCRenderData& EntityDataManager::getNPCRenderData(size_t index) const {
    assert(index < m_npcRenderData.size() && "NPC render data index out of bounds");
    return m_npcRenderData[index];
}

EntityHandle EntityDataManager::createDataDrivenNPC(
    const Vector2D& position,
    const std::string& textureID,
    const AnimationConfig& idleConfig,
    const AnimationConfig& moveConfig)
{
    // Create entity via existing mechanism (sets up EntityHotData)
    EntityHandle handle = createEntity(EntityKind::NPC, position);
    size_t index = getIndexFromHandle(handle);

    // Ensure render data vector is sized properly
    if (index >= m_npcRenderData.size()) {
        m_npcRenderData.resize(index + 1);
    }

    // Cache texture pointer from TextureManager (one-time lookup at spawn)
    SDL_Texture* texture = TextureManager::Instance().getTexturePtr(textureID);
    if (!texture) {
        LOG_WARN("EntityDataManager", "Texture not found: {}", textureID);
    }

    // Calculate frame dimensions from texture size
    int texWidth = 0, texHeight = 0;
    if (texture) {
        float w, h;
        SDL_GetTextureSize(texture, &w, &h);
        texWidth = static_cast<int>(w);
        texHeight = static_cast<int>(h);
    }

    // Setup render data - VALUES COME FROM AnimationConfig
    NPCRenderData& renderData = m_npcRenderData[index];
    renderData.textureID = textureID;
    renderData.cachedTexture = texture;

    // Frame dimensions from texture (divide by frame count for width, rows for height)
    renderData.frameWidth = static_cast<uint16_t>(texWidth / moveConfig.frameCount);
    renderData.frameHeight = static_cast<uint16_t>(texHeight / 2);  // 2 rows: idle, move

    // Animation parameters FROM AnimationConfig (same source as NPC class)
    renderData.numIdleFrames = static_cast<uint8_t>(idleConfig.frameCount);
    renderData.numMoveFrames = static_cast<uint8_t>(moveConfig.frameCount);
    renderData.idleSpeed = static_cast<uint8_t>(idleConfig.speed);
    renderData.moveSpeed = static_cast<uint8_t>(moveConfig.speed);

    // Reset runtime state
    renderData.currentFrame = 0;
    renderData.animationAccumulator = 0.0f;
    renderData.flipMode = 0;

    return handle;
}

void EntityDataManager::clearNPCRenderData(size_t index) {
    if (index < m_npcRenderData.size()) {
        // Reset to default state (texture pointer becomes stale but won't be used)
        m_npcRenderData[index] = NPCRenderData{};
    }
}
```

**Note:** Call `clearNPCRenderData()` from `destroyEntity()` when entity kind is NPC, or integrate into existing entity destruction flow.

### Complete NPC Spawning Replacement

**What NPC::create() currently does (must replicate):**

```cpp
// Current NPC::create() does approximately:
1. EntityDataManager::createEntity(EntityKind::NPC, position)
2. CollisionManager::addCollisionBodySOA(entityId, position, halfSize, ...)
3. AIManager::registerEntity(handle, behaviorName)
4. Setup texture/animation via initializeAnimationMap()
```

**New spawning pattern in GameState using AnimationConfig:**

```cpp
// In EventDemoState::spawnNPC() or similar:

// Define animation configs - same values NPC class uses in initializeAnimationMap()
AnimationConfig idleConfig{0, 2, 150, true};   // Row 0, 2 frames, 150ms, loop
AnimationConfig moveConfig{1, 4, 100, true};   // Row 1, 4 frames, 100ms, loop

// Create data-driven NPC with animation config
EntityHandle handle = EntityDataManager::Instance().createDataDrivenNPC(
    position,
    textureID,
    idleConfig,
    moveConfig
);

// Register with CollisionManager (NPC::create did this internally)
CollisionManager::Instance().addCollisionBodySOA(
    handle.getId(),
    position,
    Vector2D(16.0f, 16.0f),  // halfSize
    CollisionType::DYNAMIC,
    false  // not trigger
);

// Register with AIManager (NPC::setBehavior did this)
AIManager::Instance().registerEntity(handle, behaviorName);

// Store handle for tracking
m_spawnedNPCHandles.push_back(handle);
```

**Alternative: Predefined NPC Types (for cleaner code):**

```cpp
// Define common NPC animation configs as constants
namespace NPCAnimations {
    // Standard humanoid NPC
    static constexpr AnimationConfig IDLE{0, 2, 150, true};
    static constexpr AnimationConfig WALK{1, 4, 100, true};

    // Fast enemy (faster animation)
    static constexpr AnimationConfig FAST_IDLE{0, 2, 100, true};
    static constexpr AnimationConfig FAST_WALK{1, 6, 60, true};
}

// Usage:
EntityHandle handle = EntityDataManager::Instance().createDataDrivenNPC(
    position, textureID,
    NPCAnimations::IDLE,
    NPCAnimations::WALK
);
```

### CMakeLists.txt Addition

Add to the appropriate section in CMakeLists.txt:

```cmake
# In src file list:
src/controllers/render/NPCRenderController.cpp
```

---

## Existing Code References

### EntityDataManager Patterns to Follow

**Location:** `include/managers/EntityDataManager.hpp`

```cpp
// Existing accessor pattern (follow this style):
[[nodiscard]] EntityHotData& getHotData(EntityHandle handle);
[[nodiscard]] const EntityHotData& getHotData(EntityHandle handle) const;

// Existing index-based accessor:
[[nodiscard]] const EntityHotData& getHotDataByIndex(size_t index) const;

// Existing active tier query:
[[nodiscard]] std::span<const size_t> getActiveIndices() const;
```

### Controller Pattern to Follow

**Location:** `include/controllers/world/DayNightController.hpp`

```cpp
class DayNightController : public ControllerBase {
public:
    void subscribe() override;
    std::string_view getName() const override { return "DayNightController"; }
    // ...
};
```

**Location:** `include/controllers/combat/CombatController.hpp`

```cpp
class CombatController : public ControllerBase, public IUpdatable {
public:
    void subscribe() override;
    std::string_view getName() const override { return "CombatController"; }
    void update(float deltaTime) override;
    // ...
};
```

### GameState Render Pattern

**Location:** `src/gameStates/EventDemoState.cpp` (~line 700)

```cpp
// Current pattern to replace:
for (const auto &npc : m_spawnedNPCs) {
    if (npc && npc->isInActiveTier()) {
        npc->render(renderer, renderCamX, renderCamY, interpolationAlpha);
    }
}
```

### Simulation Tier System

**Location:** `include/entities/EntityHandle.hpp`

```cpp
enum class SimulationTier : uint8_t {
    Active = 0,      // Full update: AI, collision, render
    Background = 1,  // Simplified: position only
    Hibernated = 2   // Minimal: data stored only
};
```

**Location:** `src/managers/EntityDataManager.cpp` (~line 1201)

```cpp
void EntityDataManager::updateSimulationTiers(const Vector2D& referencePoint, ...);
// Rebuilds m_activeIndices when tiers change
```

---

## Error Handling

### Assertions to Add

```cpp
// In getNPCRenderData():
assert(index < m_npcRenderData.size() && "NPC render data index out of bounds");

// In renderNPCs():
if (!renderData.cachedTexture) {
    continue;  // Skip NPCs with missing textures
}
```

### Edge Cases

1. **Missing texture:** Skip rendering, log warning on first occurrence
2. **Invalid handle:** Return early from spawn function
3. **Zero frames:** Use minimum of 1 frame to avoid division by zero

---

## Performance Considerations

### Cache Efficiency

- NPCRenderData is 32 bytes (fits in half a cache line)
- Iterating activeIndices is O(active NPCs), not O(all NPCs)
- Texture pointer cached, no per-frame lookup

### Threading

- `update()` called from main thread update loop
- `renderNPCs()` called from main thread render loop
- No thread safety concerns (single-threaded access)

### Memory

- `m_npcRenderData` grows with entity count
- Reserve capacity in constructor for expected NPC count
- Clear render data when entity destroyed

---

## Future Direction

### Render Controller Pattern

If NPCRenderController works well, the pattern extends to:

- **TileRenderController** - World tile rendering
- **ParticleRenderController** - Particle system rendering
- **UIRenderController** - UI component rendering

Each controller:
- Fetches data from its respective manager
- Receives renderer from GameState::render()
- Handles its specific rendering logic

### TextureManager Evolution

TextureManager may simplify to just texture loading:
- Load textures from disk
- Cache SDL_Texture pointers
- Remove rendering logic (moved to controllers)

Controllers handle all rendering, TextureManager just provides textures.

---

## Rollback Plan

If issues arise, revert in this order:

1. Re-enable NPC class in CMakeLists.txt
2. Restore NPC.hpp includes in GameStates
3. Remove NPCRenderController from GameStates
4. Delete NPCRenderController files
5. Remove NPCRenderData from EntityDataManager
