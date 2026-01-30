# Data-Driven Resource Migration - Implementation Plan

## Overview

Migrate DroppedItem, Container, and Harvestable entities to be fully data-driven via EntityDataManager, following the NPCRenderData pattern. This eliminates the DroppedItem class and creates a unified ResourceRenderController.

## Architecture Summary

```
┌─────────────────────────────────────────────────────────────────┐
│                    SINGLE SOURCE OF TRUTH                        │
│                                                                  │
│                   EntityDataManager (EDM)                        │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │ InventoryData│  │ItemRenderData│  │ ContainerData/RenderData│ │
│  │ (all entity │  │ (dropped    │  │ (with auto-inventory)   │ │
│  │  inventories)│  │  items)     │  │                         │ │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ HarvestableData/RenderData (world resource nodes)           ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
                              ↑
                    ALL CREATION GOES HERE
                              ↑
    ┌─────────────────────────┼─────────────────────────┐
    │                         │                         │
┌───┴───┐               ┌─────┴─────┐            ┌──────┴──────┐
│Player │               │WorldManager│            │GameStates   │
│creates│               │spawns      │            │spawn items, │
│inventory              │harvestables│            │containers   │
└───────┘               └───────────┘            └─────────────┘

                   WorldResourceManager
            ┌─────────────────────────────────┐
            │ QUERIES EDM (no storage)        │
            │                                 │
            │ - registerInventory(index, world)│
            │ - registerHarvestable(index, world)│
            │ - getResourceQuantity(world, handle)│
            │   └─> Sums from EDM inventories  │
            │   └─> Sums from EDM harvestables │
            └─────────────────────────────────┘
```

**Key Principles:**
1. **EDM is the single source of truth** for all resource data (inventories, items, containers, harvestables)
2. **All creation goes through EDM** - `createInventory()`, `createDroppedItem()`, `createContainer()`, `createHarvestable()`
3. **WorldResourceManager only queries** - no quantity storage, just registry of which inventories/harvestables belong to which world
4. **Validation at creation** - all EDM creation methods validate inputs and return invalid handles on failure

## Clean Break Policy

**NO backward compatibility. NO thin wrappers. NO legacy APIs.**

- InventoryComponent → **DELETED** (not wrapped)
- DroppedItem class → **DELETED** (not wrapped)
- Old WorldResourceManager API → **REPLACED** (not preserved)
- All consuming code → **REWRITTEN** to use EDM directly

Code that previously did:
```cpp
// OLD - DELETE THIS PATTERN
m_inventory->addResource(handle, qty);
WorldResourceManager::Instance().addResource(worldId, handle, qty);
```

Now does:
```cpp
// NEW - CLEAN EDM PATTERN
EntityDataManager::Instance().addToInventory(m_inventoryIndex, handle, qty);
// WorldResourceManager doesn't add - it only queries EDM
```

## Key Decisions

| Decision | Choice |
|----------|--------|
| DroppedItem migration | Hard cutover - remove class entirely |
| Container inventory | Auto-create in createContainer() |
| Player inventory | Migrate to EDM InventoryData with overflow |
| Render architecture | ResourceRenderController for Items/Containers/Harvestables |
| InventoryComponent | DELETE entirely - access EDM directly |
| WorldResourceManager | Keep as registry over EDM (no quantity storage) |

---

## Full Resource System Scope

The migration affects more than just DroppedItem. Here's the complete resource architecture:

### Files That Stay Unchanged (Templates/Factories)
| File | Reason |
|------|--------|
| `Resource.hpp/cpp` | Pure template data, immutable |
| `ItemResources.hpp` | Template definitions (Item, Equipment, Consumable, QuestItem) |
| `MaterialResources.hpp` | Template definitions (Material, CraftingComponent, RawResource) |
| `CurrencyAndGameResources.hpp` | Template definitions (Currency, Gold, Gem, Energy, Mana, etc.) |
| `ResourceHandle.hpp` | Type-safe handles - perfect for EDM field references |
| `ResourceTemplateManager.hpp/cpp` | Read-only template registry |
| `ResourceFactory.hpp/cpp` | JSON deserialization for templates |

### Files That Need Migration/Deletion
| File | Action |
|------|--------|
| `InventoryComponent.hpp/cpp` | **DELETE** - replaced by EDM inventory methods |
| `DroppedItem.hpp/cpp` | **DELETE** - replaced by EDM + ResourceRenderController |
| `WorldResourceManager.hpp/cpp` | **REFACTOR** - remove quantity storage, become registry over EDM |
| `ResourceChangeEvent.hpp` | Keep - already uses EntityHandle |
| `HarvestResourceEvent.hpp` | Keep - triggers EDM inventory updates |

### WorldResourceManager Complete Rewrite

**Old API (DELETE ALL):**
```cpp
// REMOVE - these methods are deleted
addResource(worldId, handle, qty);      // GONE
removeResource(worldId, handle, qty);   // GONE
setResource(worldId, handle, qty);      // GONE
getResourceQuantity(worldId, handle);   // REPLACED
transferResource(...);                   // REPLACED
m_worldResources map;                   // DELETED
```

**New API (Query-Only):**
```cpp
class WorldResourceManager {
    // Registry only - NO quantity storage
    std::unordered_map<WorldId, std::vector<uint32_t>> m_inventoryIndices;
    std::unordered_map<WorldId, std::vector<size_t>> m_harvestableIndices;

    // Registration (called by EDM or game code)
    void registerInventory(uint32_t inventoryIndex, const WorldId& worldId);
    void unregisterInventory(uint32_t inventoryIndex);
    void registerHarvestable(size_t edmIndex, const WorldId& worldId);
    void unregisterHarvestable(size_t edmIndex);

    // Query-only methods (no mutation)
    Quantity queryInventoryTotal(const WorldId& worldId, ResourceHandle handle) const;
    Quantity queryHarvestableTotal(const WorldId& worldId, ResourceHandle handle) const;
    Quantity queryWorldTotal(const WorldId& worldId, ResourceHandle handle) const;

    // World lifecycle
    void createWorld(const WorldId& worldId);
    void destroyWorld(const WorldId& worldId);
};
```

### WorldManager Changes (Critical)

WorldManager currently calls `WorldResourceManager::addResource()` to add world-level resources. This changes to spawning Harvestable entities:

```cpp
// BEFORE (WorldManager.cpp):
WorldResourceManager::Instance().addResource(m_currentWorld->worldId, woodHandle, woodAmount);

// AFTER:
// Spawn harvestable entities throughout the world
for (int i = 0; i < woodAmount; ++i) {
    Vector2D pos = getRandomForestPosition();  // Find forest tile
    EntityHandle h = EntityDataManager::Instance().createHarvestable(
        pos, HarvestType::Chopping, woodHandle, 1, 3, 60.0f);  // yields 1-3, respawns in 60s
    WorldResourceManager::Instance().registerHarvestable(
        EntityDataManager::Instance().getIndex(h), m_currentWorld->worldId);
}
```

This fundamentally changes world resource initialization to create actual harvestable entities instead of abstract pools.

---

## Validation Guards for Creation Methods

All EDM creation methods MUST validate input and return invalid handles on failure:

### createDroppedItem() Validation
```cpp
EntityHandle createDroppedItem(const Vector2D& position,
                               HammerEngine::ResourceHandle resourceHandle,
                               int quantity) {
    // Guard 1: Invalid resource handle
    if (!resourceHandle.isValid()) {
        ENTITY_ERROR("createDroppedItem: Invalid resource handle");
        return INVALID_ENTITY_HANDLE;
    }

    // Guard 2: Resource template must exist
    auto& rtm = ResourceTemplateManager::Instance();
    if (!rtm.hasResourceTemplate(resourceHandle)) {
        ENTITY_ERROR(std::format("createDroppedItem: Resource handle {} not registered",
                                 resourceHandle.getId()));
        return INVALID_ENTITY_HANDLE;
    }

    // Guard 3: Quantity must be positive
    if (quantity <= 0) {
        ENTITY_ERROR(std::format("createDroppedItem: Invalid quantity {}", quantity));
        return INVALID_ENTITY_HANDLE;
    }

    // Guard 4: Quantity must not exceed stack size
    int maxStack = rtm.getMaxStackSize(resourceHandle);
    if (quantity > maxStack) {
        ENTITY_WARN(std::format("createDroppedItem: Clamping quantity {} to max stack {}",
                                quantity, maxStack));
        quantity = maxStack;
    }

    // ... proceed with creation
}
```

### createInventory() Validation
```cpp
uint32_t createInventory(uint16_t maxSlots, bool worldTracked) {
    // Guard 1: Zero slots makes no sense
    if (maxSlots == 0) {
        ENTITY_ERROR("createInventory: maxSlots cannot be 0");
        return INVALID_INVENTORY_INDEX;
    }

    // Guard 2: Reasonable upper bound
    static constexpr uint16_t MAX_REASONABLE_SLOTS = 1000;
    if (maxSlots > MAX_REASONABLE_SLOTS) {
        ENTITY_WARN(std::format("createInventory: Clamping {} slots to max {}",
                                maxSlots, MAX_REASONABLE_SLOTS));
        maxSlots = MAX_REASONABLE_SLOTS;
    }

    // ... proceed with creation
}
```

### addToInventory() Validation
```cpp
bool addToInventory(uint32_t inventoryIndex,
                    HammerEngine::ResourceHandle handle,
                    int quantity) {
    // Guard 1: Valid inventory index
    if (inventoryIndex >= m_inventoryData.size()) {
        ENTITY_ERROR(std::format("addToInventory: Invalid index {}", inventoryIndex));
        return false;
    }

    // Guard 2: Valid resource handle
    if (!handle.isValid()) {
        ENTITY_ERROR("addToInventory: Invalid resource handle");
        return false;
    }

    // Guard 3: Positive quantity
    if (quantity <= 0) {
        ENTITY_ERROR(std::format("addToInventory: Invalid quantity {}", quantity));
        return false;
    }

    // Guard 4: Overflow protection
    auto& rtm = ResourceTemplateManager::Instance();
    int maxStack = rtm.getMaxStackSize(handle);
    if (maxStack <= 0) {
        ENTITY_WARN("addToInventory: Resource has invalid maxStackSize, defaulting to 1");
        maxStack = 1;
    }

    // ... proceed with stacking logic
}
```

### createContainer() Validation
```cpp
EntityHandle createContainer(const Vector2D& position,
                             uint8_t containerType,
                             uint16_t maxSlots,
                             uint8_t lockLevel) {
    // Guard 1: Valid container type
    static constexpr uint8_t MAX_CONTAINER_TYPE = 3; // Chest, Barrel, Corpse, Crate
    if (containerType > MAX_CONTAINER_TYPE) {
        ENTITY_ERROR(std::format("createContainer: Invalid type {}", containerType));
        return INVALID_ENTITY_HANDLE;
    }

    // Guard 2: Valid slot count
    if (maxSlots == 0 || maxSlots > 100) {
        ENTITY_ERROR(std::format("createContainer: Invalid slot count {}", maxSlots));
        return INVALID_ENTITY_HANDLE;
    }

    // Guard 3: Lock level range
    if (lockLevel > 10) {
        ENTITY_WARN(std::format("createContainer: Clamping lock level {} to 10", lockLevel));
        lockLevel = 10;
    }

    // ... proceed with creation
}
```

### createHarvestable() Validation
```cpp
EntityHandle createHarvestable(const Vector2D& position,
                               HammerEngine::ResourceHandle yieldResource,
                               int yieldMin, int yieldMax,
                               float respawnTime) {
    // Guard 1: Valid yield resource
    if (!yieldResource.isValid()) {
        ENTITY_ERROR("createHarvestable: Invalid yield resource handle");
        return INVALID_ENTITY_HANDLE;
    }

    // Guard 2: Yield range sanity
    if (yieldMin < 0 || yieldMax < yieldMin) {
        ENTITY_ERROR(std::format("createHarvestable: Invalid yield range [{}, {}]",
                                 yieldMin, yieldMax));
        return INVALID_ENTITY_HANDLE;
    }

    // Guard 3: Respawn time
    if (respawnTime < 0.0f) {
        ENTITY_WARN("createHarvestable: Negative respawn time, setting to 0");
        respawnTime = 0.0f;
    }

    // ... proceed with creation
}
```

---

## Notes from Original Plan Review

The original `DATA_DRIVEN_RESOURCE_IMPLEMENTATION.md` had outdated line numbers and missed some cleanup files. Key corrections:
- ContainerData exists but needs `inventoryId` renamed to `inventoryIndex` with flags
- Additional files to update: `BackgroundSimulationManager.cpp`, test files, documentation
- NPCRenderController pattern is the reference for new render code

---

## Final Implementation Plan

### Phase 1: InventoryData in EDM

**Files**: `EntityDataManager.hpp`, `EntityDataManager.cpp`

1. Add `InventorySlotData` struct (8 bytes) after HarvestableData
2. Add `InventoryData` struct (128 bytes, 12 inline slots)
3. Add `InventoryOverflow` struct for large inventories
4. Add storage: `m_inventoryData`, `m_freeInventorySlots`, `m_inventoryOverflow`, `m_nextOverflowId`
5. Implement inventory methods:
   - `createInventory(maxSlots, worldTracked)` - allocate from free-list
   - `destroyInventory(index)` - cleanup overflow, add to free-list
   - `addToInventory()` - stacking logic with ResourceTemplateManager lookup
   - `removeFromInventory()` - quantity tracking
   - `getInventoryQuantity()`, `hasInInventory()` - queries
6. Update `init()` - reserve 500 slots
7. Update `clean()` and `prepareForStateTransition()` - clear all inventory data

### Phase 2: Item/Container/Harvestable RenderData

**Files**: `EntityDataManager.hpp`, `EntityDataManager.cpp`

1. Add `ItemRenderData` struct (bobbing, animation):
   ```cpp
   struct ItemRenderData {
       SDL_Texture* cachedTexture{nullptr};
       uint16_t frameWidth{32}, frameHeight{32};
       uint16_t animSpeedMs{100};
       uint8_t currentFrame{0}, numFrames{1};
       float animTimer{0.0f}, bobPhase{0.0f};
   };
   ```

2. Add `ContainerRenderData` struct (open/closed textures)
3. Add `HarvestableRenderData` struct (normal/depleted textures)
4. Add storage vectors: `m_itemRenderData`, `m_containerRenderData`, `m_harvestableRenderData`
5. Add inline accessor: `getItemDataByTypeIndex()`
6. Update `createDroppedItem()` to populate ItemRenderData from ResourceTemplate
7. Update `freeSlot()` to clear render data for each kind

### Phase 3: ResourceRenderController

**New Files**:
- `include/controllers/render/ResourceRenderController.hpp`
- `src/controllers/render/ResourceRenderController.cpp`

```cpp
class ResourceRenderController : public ControllerBase, public IUpdatable {
public:
    void update(float deltaTime) override;  // Update all resource animations

    // Render methods (call from GameState::render)
    void renderDroppedItems(SDL_Renderer*, float camX, float camY, float alpha);
    void renderContainers(SDL_Renderer*, float camX, float camY, float alpha);
    void renderHarvestables(SDL_Renderer*, float camX, float camY, float alpha);

    // Cleanup
    void clearAll();

private:
    void updateItemBobbing(float dt);    // Sine-wave bobbing
    void updateItemAnimation(float dt);   // Frame cycling
    void updateContainerStates(float dt); // Open/close animation
    void updateHarvestableStates(float dt); // Depletion visuals
};
```

### Phase 4: Container Creation

**Files**: `EntityDataManager.hpp`, `EntityDataManager.cpp`

1. Enhance `ContainerData`:
   - Change `inventoryId` to `inventoryIndex`
   - Add flags: `FLAG_IS_OPEN`, `FLAG_IS_LOCKED`, `FLAG_WAS_LOOTED`
   - Add helper methods: `isOpen()`, `isLocked()`, `setOpen()`

2. Add `ContainerTypeInfo` registry:
   ```cpp
   struct ContainerTypeInfo {
       std::string closedTextureID, openTextureID;
       uint16_t defaultMaxSlots;
       uint8_t containerType, defaultLockLevel;
   };
   ```

3. Implement `createContainer(position, containerType, maxSlots, lockLevel)`:
   - Allocate slot via `allocateSlot()`
   - Setup hot data with collision (Layer_Default, trigger)
   - Create inventory via `createInventory(maxSlots)`
   - Link `inventoryIndex` in ContainerData

4. Update `freeSlot()` for Container kind:
   - Call `destroyInventory(containerData.inventoryIndex)`
   - Clear ContainerRenderData

### Phase 5: Harvestable Creation

**Files**: `EntityDataManager.hpp`, `EntityDataManager.cpp`

1. Add `HarvestableTypeInfo` registry with yield resources, respawn time
2. Implement `createHarvestable(position, harvestType, yieldResource, ...)`
3. Implement `harvest(handle)` - yields resources, sets depleted state
4. Update timer logic in game loop for respawn

### Phase 6: WorldResourceManager Complete Rewrite

**Files**: `WorldResourceManager.hpp`, `WorldResourceManager.cpp`

**DELETE all old methods and storage:**
1. Delete `m_worldResources` map entirely
2. Delete `addResource()`, `removeResource()`, `setResource()`
3. Delete `transferResource()`, `transferAllResources()`
4. Delete `m_resourceCache`, `m_aggregateCache` (no longer needed)

**Implement new query-only API:**
1. Add registry storage:
   ```cpp
   std::unordered_map<WorldId, std::vector<uint32_t>> m_inventoryIndices;
   std::unordered_map<WorldId, std::vector<size_t>> m_harvestableIndices;
   ```
2. Add registration methods:
   - `registerInventory(uint32_t, WorldId)`
   - `unregisterInventory(uint32_t)`
   - `registerHarvestable(size_t, WorldId)`
   - `unregisterHarvestable(size_t)`
3. Add query methods (read EDM directly):
   - `queryInventoryTotal(WorldId, ResourceHandle)` - sum from EDM inventories
   - `queryHarvestableTotal(WorldId, ResourceHandle)` - sum harvestable yields
   - `queryWorldTotal(WorldId, ResourceHandle)` - combined total
4. Keep world lifecycle:
   - `createWorld(WorldId)` - creates empty registry
   - `destroyWorld(WorldId)` - clears registry (EDM handles entity cleanup)

### Phase 7: Player Inventory Migration

**Files**: `Player.hpp`, `Player.cpp`

1. Remove `#include "InventoryComponent.hpp"`
2. Change `std::unique_ptr<InventoryComponent> m_inventory` to `uint32_t m_inventoryIndex`
3. In constructor:
   ```cpp
   m_inventoryIndex = EntityDataManager::Instance().createInventory(50, true);  // 50 slots, world tracked
   ```
4. Replace `getInventory()->` calls with direct EDM calls:
   ```cpp
   // Before: m_inventory->addResource(handle, qty);
   // After:  EntityDataManager::Instance().addToInventory(m_inventoryIndex, handle, qty);
   ```
5. Register with WorldResourceManager:
   ```cpp
   WorldResourceManager::Instance().registerInventory(m_inventoryIndex, m_worldId);
   ```
6. In destructor/clean:
   ```cpp
   WorldResourceManager::Instance().unregisterInventory(m_inventoryIndex);
   EntityDataManager::Instance().destroyInventory(m_inventoryIndex);
   ```

### Phase 8: Cleanup & File Deletion

1. **Delete files**:
   - `include/entities/DroppedItem.hpp`
   - `src/entities/DroppedItem.cpp`
   - `include/entities/resources/InventoryComponent.hpp`
   - `src/entities/resources/InventoryComponent.cpp`

2. **Update CMakeLists.txt**:
   - Remove: `DroppedItem.cpp`, `InventoryComponent.cpp`
   - Add: `ResourceRenderController.cpp`

3. **Update/Delete tests**:
   - `tests/resources/ResourceArchitectureTests.cpp` - use EDM createDroppedItem
   - `tests/resources/InventoryComponentTests.cpp` - **DELETE** or rewrite as EDM inventory tests
   - `tests/managers/EntityDataManagerTests.cpp` - add new inventory tests

4. **Grep and fix all references**:
   ```bash
   grep -r "DroppedItem" --include="*.cpp" --include="*.hpp" --include="*.md"
   grep -r "InventoryComponent" --include="*.cpp" --include="*.hpp" --include="*.md"
   ```

5. **Update documentation**:
   - `docs/entities/README.md`
   - `docs/managers/EntityDataManager.md`
   - `docs/ARCHITECTURE.md`

---

## Files to Create

| File | Purpose |
|------|---------|
| `include/controllers/render/ResourceRenderController.hpp` | Unified resource rendering (items, containers, harvestables) |
| `src/controllers/render/ResourceRenderController.cpp` | Implementation |

## Files to Modify

| File | Changes |
|------|---------|
| `include/managers/EntityDataManager.hpp` | Add InventoryData, ItemRenderData, ContainerRenderData, HarvestableRenderData, type registries, INVALID_INVENTORY_INDEX constant |
| `src/managers/EntityDataManager.cpp` | Implement inventory methods with validation guards, createContainer, createHarvestable, update init/clean/prepareForStateTransition |
| `include/entities/Player.hpp` | Remove InventoryComponent include, change m_inventory to m_inventoryIndex |
| `src/entities/Player.cpp` | Create EDM inventory on construction, direct EDM calls |
| `include/managers/WorldResourceManager.hpp` | **COMPLETE REWRITE** - delete old API, new query-only interface |
| `src/managers/WorldResourceManager.cpp` | **COMPLETE REWRITE** - delete all mutation methods, implement EDM queries |
| `include/core/Logger.hpp` | Update INVENTORY_* macros category from "InventoryComponent" to "EDMInventory" |
| `include/events/ResourceChangeEvent.hpp` | Update comment at line 18 (references InventoryComponent) |
| `include/entities/Entity.hpp` | Update comment at line 35 (mentions DroppedItem - still valid, just clarify it's data-driven) |
| `src/managers/WorldManager.cpp` | Replace addResource() calls with createHarvestable() spawning |
| `tests/world/WorldManagerTests.cpp` | Update for new harvestable-based world resources |
| `CMakeLists.txt` | Add ResourceRenderController.cpp, remove DroppedItem.cpp, InventoryComponent.cpp |
| `tests/resources/ResourceArchitectureTests.cpp` | Remove DroppedItem.hpp include, use EDM createDroppedItem |
| `tests/resources/ResourceEdgeCaseTests.cpp` | Remove InventoryComponent.hpp include, use EDM inventory methods |
| `tests/resources/ResourceIntegrationTests.cpp` | Remove InventoryComponent.hpp include, use EDM inventory methods |
| `tests/managers/EntityDataManagerTests.cpp` | Add inventory and resource render tests |
| `tests/resources/WorldResourceManagerTests.cpp` | Update for new registry-over-EDM API |
| `tests/performance/PathfinderBenchmark.cpp` | Minor updates if it uses removed WRM methods |

## Files That Stay Unchanged (No Action Required)

| File | Reason |
|------|--------|
| `include/entities/EntityHandle.hpp` | EntityKind::DroppedItem stays (concept remains, just data-driven now) |
| `include/core/GameEngine.hpp` | WorldResourceManager pointer stays (WRM still exists) |
| `src/core/GameEngine.cpp` | WRM init/clean calls stay (WRM still exists) |
| `src/managers/BackgroundSimulationManager.cpp` | Uses EntityKind::DroppedItem enum (stays), simulateItem() works with EDM |

## Files to Delete

| File | Reason |
|------|--------|
| `include/entities/DroppedItem.hpp` | Replaced by EDM data-driven approach |
| `src/entities/DroppedItem.cpp` | Replaced by EDM + ResourceRenderController |
| `include/entities/resources/InventoryComponent.hpp` | Replaced by EDM InventoryData |
| `src/entities/resources/InventoryComponent.cpp` | Replaced by EDM inventory methods |
| `tests/resources/InventoryComponentTests.cpp` | Tests deleted class (or rewrite as EDM tests) |

---

## Verification Steps

1. **Build**: `cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build`
2. **EDM tests**: `./bin/debug/entity_data_manager_tests`
3. **Resource tests**: `./bin/debug/resource_architecture_tests`
4. **WorldResourceManager tests**: `./bin/debug/world_resource_manager_tests`
5. **Full suite**: `./tests/test_scripts/run_all_tests.sh --core-only --errors-only`
6. **Manual test**: Run game, drop items, open containers, harvest nodes - verify rendering

