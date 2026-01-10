# Data-Driven Resource System Migration Plan

## Overview

Migrate resource entities and inventory into EntityDataManager following the NPCRenderData pattern established in DATA_DRIVEN_NPC_IMPLEMENTATION.md.

**Scope:**
- Make all interactables data-driven: DroppedItem, Container, Harvestable
- Add InventoryData to EDM (entities store inventoryIndex)
- WorldResourceManager queries EDM for resource data

---

## Reference Files (Critical for Implementation)

| File | Purpose |
|------|---------|
| `src/controllers/render/NPCRenderController.cpp` | Complete render controller pattern (lines 1-140) |
| `src/managers/EntityDataManager.cpp:406-470` | createDataDrivenNPC() pattern for data-driven entity creation |
| `src/managers/EntityDataManager.cpp:491-548` | createDroppedItem() existing implementation |
| `src/managers/EntityDataManager.cpp:264-321` | freeSlot() free-list management |
| `src/managers/EntityDataManager.cpp:330-346` | allocateCharacterSlot() parallel array growth |
| `include/managers/EntityDataManager.hpp:191-244` | Existing ItemData, ContainerData, HarvestableData structs |
| `include/managers/EntityDataManager.hpp:270-290` | NPCRenderData struct pattern |
| `include/managers/EntityDataManager.hpp:1254-1278` | Storage vectors and free-lists |
| `src/entities/DroppedItem.cpp` | Current DroppedItem implementation to replace |
| `include/entities/resources/InventoryComponent.hpp:23-37` | InventorySlot struct to adapt |

---

## Phase 1: InventoryData in EntityDataManager

### 1.1 Add InventorySlotData and InventoryData structs

**File:** `include/managers/EntityDataManager.hpp`
**Location:** After HarvestableData struct (line ~244), before NPCRenderData (line ~260)

```cpp
// ============================================================================
// INVENTORY DATA (for Containers, Player, NPCs with inventory)
// ============================================================================

/**
 * @brief Inline inventory slot for EDM (8 bytes)
 * Matches InventorySlot from InventoryComponent.hpp but POD-friendly
 */
struct InventorySlotData {
    HammerEngine::ResourceHandle resourceHandle;  // 4 bytes (32-bit handle)
    int16_t quantity{0};                          // 2 bytes
    int16_t _pad{0};                              // 2 bytes alignment

    [[nodiscard]] bool isEmpty() const noexcept {
        return !resourceHandle.isValid() || quantity <= 0;
    }
    void clear() noexcept {
        resourceHandle = HammerEngine::INVALID_RESOURCE_HANDLE;
        quantity = 0;
    }
};

static_assert(sizeof(InventorySlotData) == 8, "InventorySlotData should be 8 bytes");

/**
 * @brief Fixed-size inventory data for EDM (128 bytes, 2 cache lines)
 *
 * Supports 12 inline slots for most containers (chests, corpses).
 * For larger inventories (Player: 50 slots), use overflow storage via overflowId.
 *
 * Threading: Main-thread structural ops, read-only during render.
 */
struct InventoryData {
    static constexpr size_t INLINE_SLOT_COUNT = 12;

    InventorySlotData slots[INLINE_SLOT_COUNT];   // 96 bytes (12 * 8)
    uint32_t overflowId{0};                       // 4 bytes: ID for extra slots (0 = inline only)
    uint16_t maxSlots{INLINE_SLOT_COUNT};         // 2 bytes: Total capacity
    uint16_t usedSlots{0};                        // 2 bytes: Current used count (cache)
    uint8_t flags{0};                             // 1 byte
    uint8_t ownerKind{0};                         // 1 byte: EntityKind of owner
    uint8_t _padding[22]{};                       // 22 bytes: Align to 128

    static constexpr uint8_t FLAG_WORLD_TRACKED = 0x01;  // Sync with WorldResourceManager
    static constexpr uint8_t FLAG_HAS_OVERFLOW = 0x02;   // Uses overflow storage

    [[nodiscard]] bool hasOverflow() const noexcept { return flags & FLAG_HAS_OVERFLOW; }
    [[nodiscard]] bool isWorldTracked() const noexcept { return flags & FLAG_WORLD_TRACKED; }

    void clear() noexcept {
        for (auto& slot : slots) { slot.clear(); }
        overflowId = 0;
        maxSlots = INLINE_SLOT_COUNT;
        usedSlots = 0;
        flags = 0;
        ownerKind = 0;
    }
};

static_assert(sizeof(InventoryData) == 128, "InventoryData should be 128 bytes (2 cache lines)");

/**
 * @brief Overflow storage for large inventories (Player, large containers)
 * Stored in unordered_map, accessed by overflowId
 */
struct InventoryOverflow {
    std::vector<InventorySlotData> extraSlots;    // Slots beyond INLINE_SLOT_COUNT
    std::string worldId;                           // For WorldResourceManager tracking
};
```

### 1.2 Add storage vectors

**File:** `include/managers/EntityDataManager.hpp`
**Location:** Private section after m_areaEffectData (line ~1260)

```cpp
    // Inventory storage (indexed independently, linked via inventoryIndex in entity data)
    std::vector<InventoryData> m_inventoryData;
    std::vector<uint32_t> m_freeInventorySlots;
    std::unordered_map<uint32_t, InventoryOverflow> m_inventoryOverflow;
    std::atomic<uint32_t> m_nextOverflowId{1};
```

### 1.3 Add public method declarations

**File:** `include/managers/EntityDataManager.hpp`
**Location:** Public section, after entity creation methods

```cpp
    // ========================================================================
    // INVENTORY MANAGEMENT
    // ========================================================================

    /**
     * @brief Create a new inventory with specified capacity
     * @param maxSlots Total slot capacity (12 inline + overflow if needed)
     * @param worldTracked If true, sync changes with WorldResourceManager
     * @return Index into m_inventoryData
     */
    [[nodiscard]] uint32_t createInventory(uint16_t maxSlots, bool worldTracked = false);

    /**
     * @brief Destroy an inventory and free its slot
     */
    void destroyInventory(uint32_t inventoryIndex);

    /**
     * @brief Get inventory data by index
     */
    [[nodiscard]] InventoryData& getInventoryData(uint32_t inventoryIndex);
    [[nodiscard]] const InventoryData& getInventoryData(uint32_t inventoryIndex) const;

    /**
     * @brief Add resource to inventory (handles stacking)
     * @return true if all quantity was added, false if partial or failed
     */
    bool addToInventory(uint32_t inventoryIndex, HammerEngine::ResourceHandle handle, int quantity);

    /**
     * @brief Remove resource from inventory
     * @return true if quantity was removed, false if insufficient
     */
    bool removeFromInventory(uint32_t inventoryIndex, HammerEngine::ResourceHandle handle, int quantity);

    /**
     * @brief Get total quantity of resource in inventory
     */
    [[nodiscard]] int getInventoryQuantity(uint32_t inventoryIndex, HammerEngine::ResourceHandle handle) const;

    /**
     * @brief Check if inventory has at least minQty of resource
     */
    [[nodiscard]] bool hasInInventory(uint32_t inventoryIndex, HammerEngine::ResourceHandle handle, int minQty = 1) const;

    /**
     * @brief Set world tracking for an inventory
     */
    void setInventoryWorldTracking(uint32_t inventoryIndex, const std::string& worldId);
```

### 1.4 Implement inventory methods

**File:** `src/managers/EntityDataManager.cpp`
**Location:** After entity creation methods (after createDroppedItem ~line 548)

```cpp
// ============================================================================
// INVENTORY MANAGEMENT
// ============================================================================

uint32_t EntityDataManager::createInventory(uint16_t maxSlots, bool worldTracked) {
    uint32_t inventoryIndex;
    if (!m_freeInventorySlots.empty()) {
        inventoryIndex = m_freeInventorySlots.back();
        m_freeInventorySlots.pop_back();
        m_inventoryData[inventoryIndex].clear();
    } else {
        inventoryIndex = static_cast<uint32_t>(m_inventoryData.size());
        m_inventoryData.emplace_back();
    }

    auto& inv = m_inventoryData[inventoryIndex];
    inv.maxSlots = maxSlots;
    inv.usedSlots = 0;

    if (worldTracked) {
        inv.flags |= InventoryData::FLAG_WORLD_TRACKED;
    }

    // If needs overflow storage
    if (maxSlots > InventoryData::INLINE_SLOT_COUNT) {
        inv.flags |= InventoryData::FLAG_HAS_OVERFLOW;
        inv.overflowId = m_nextOverflowId.fetch_add(1, std::memory_order_relaxed);
        auto& overflow = m_inventoryOverflow[inv.overflowId];
        overflow.extraSlots.resize(maxSlots - InventoryData::INLINE_SLOT_COUNT);
    }

    return inventoryIndex;
}

void EntityDataManager::destroyInventory(uint32_t inventoryIndex) {
    if (inventoryIndex >= m_inventoryData.size()) {
        return;
    }

    auto& inv = m_inventoryData[inventoryIndex];

    // Remove overflow storage if present
    if (inv.hasOverflow() && inv.overflowId != 0) {
        m_inventoryOverflow.erase(inv.overflowId);
    }

    inv.clear();
    m_freeInventorySlots.push_back(inventoryIndex);
}

InventoryData& EntityDataManager::getInventoryData(uint32_t inventoryIndex) {
    assert(inventoryIndex < m_inventoryData.size() && "Inventory index out of bounds");
    return m_inventoryData[inventoryIndex];
}

const InventoryData& EntityDataManager::getInventoryData(uint32_t inventoryIndex) const {
    assert(inventoryIndex < m_inventoryData.size() && "Inventory index out of bounds");
    return m_inventoryData[inventoryIndex];
}

bool EntityDataManager::addToInventory(uint32_t inventoryIndex, HammerEngine::ResourceHandle handle, int quantity) {
    if (inventoryIndex >= m_inventoryData.size() || quantity <= 0) {
        return false;
    }

    auto& inv = m_inventoryData[inventoryIndex];
    auto& rtm = ResourceTemplateManager::Instance();

    // Get max stack size from template
    int maxStack = rtm.getMaxStackSize(handle);
    if (maxStack <= 0) maxStack = 1;

    int remaining = quantity;

    // Helper lambda to access slot by logical index
    auto getSlot = [&](size_t slotIdx) -> InventorySlotData* {
        if (slotIdx < InventoryData::INLINE_SLOT_COUNT) {
            return &inv.slots[slotIdx];
        }
        if (!inv.hasOverflow()) return nullptr;
        auto it = m_inventoryOverflow.find(inv.overflowId);
        if (it == m_inventoryOverflow.end()) return nullptr;
        size_t overflowIdx = slotIdx - InventoryData::INLINE_SLOT_COUNT;
        if (overflowIdx >= it->second.extraSlots.size()) return nullptr;
        return &it->second.extraSlots[overflowIdx];
    };

    // First pass: stack into existing slots with same resource
    for (size_t i = 0; i < inv.maxSlots && remaining > 0; ++i) {
        auto* slot = getSlot(i);
        if (!slot) break;
        if (slot->resourceHandle == handle && slot->quantity < maxStack) {
            int canAdd = std::min(remaining, maxStack - static_cast<int>(slot->quantity));
            slot->quantity += static_cast<int16_t>(canAdd);
            remaining -= canAdd;
        }
    }

    // Second pass: fill empty slots
    for (size_t i = 0; i < inv.maxSlots && remaining > 0; ++i) {
        auto* slot = getSlot(i);
        if (!slot) break;
        if (slot->isEmpty()) {
            int toAdd = std::min(remaining, maxStack);
            slot->resourceHandle = handle;
            slot->quantity = static_cast<int16_t>(toAdd);
            remaining -= toAdd;
            inv.usedSlots++;
        }
    }

    return remaining == 0;  // True if all added
}

bool EntityDataManager::removeFromInventory(uint32_t inventoryIndex, HammerEngine::ResourceHandle handle, int quantity) {
    if (inventoryIndex >= m_inventoryData.size() || quantity <= 0) {
        return false;
    }

    auto& inv = m_inventoryData[inventoryIndex];

    // Check total quantity first
    if (getInventoryQuantity(inventoryIndex, handle) < quantity) {
        return false;
    }

    int remaining = quantity;

    // Helper lambda (same as addToInventory)
    auto getSlot = [&](size_t slotIdx) -> InventorySlotData* {
        if (slotIdx < InventoryData::INLINE_SLOT_COUNT) {
            return &inv.slots[slotIdx];
        }
        if (!inv.hasOverflow()) return nullptr;
        auto it = m_inventoryOverflow.find(inv.overflowId);
        if (it == m_inventoryOverflow.end()) return nullptr;
        size_t overflowIdx = slotIdx - InventoryData::INLINE_SLOT_COUNT;
        if (overflowIdx >= it->second.extraSlots.size()) return nullptr;
        return &it->second.extraSlots[overflowIdx];
    };

    // Remove from slots (from end first for cleaner compaction)
    for (size_t i = inv.maxSlots; i > 0 && remaining > 0; --i) {
        auto* slot = getSlot(i - 1);
        if (!slot) continue;
        if (slot->resourceHandle == handle && slot->quantity > 0) {
            int toRemove = std::min(remaining, static_cast<int>(slot->quantity));
            slot->quantity -= static_cast<int16_t>(toRemove);
            remaining -= toRemove;
            if (slot->quantity <= 0) {
                slot->clear();
                inv.usedSlots--;
            }
        }
    }

    return remaining == 0;
}

int EntityDataManager::getInventoryQuantity(uint32_t inventoryIndex, HammerEngine::ResourceHandle handle) const {
    if (inventoryIndex >= m_inventoryData.size()) {
        return 0;
    }

    const auto& inv = m_inventoryData[inventoryIndex];
    int total = 0;

    // Count inline slots
    for (size_t i = 0; i < InventoryData::INLINE_SLOT_COUNT && i < inv.maxSlots; ++i) {
        if (inv.slots[i].resourceHandle == handle) {
            total += inv.slots[i].quantity;
        }
    }

    // Count overflow slots
    if (inv.hasOverflow()) {
        auto it = m_inventoryOverflow.find(inv.overflowId);
        if (it != m_inventoryOverflow.end()) {
            for (const auto& slot : it->second.extraSlots) {
                if (slot.resourceHandle == handle) {
                    total += slot.quantity;
                }
            }
        }
    }

    return total;
}

bool EntityDataManager::hasInInventory(uint32_t inventoryIndex, HammerEngine::ResourceHandle handle, int minQty) const {
    return getInventoryQuantity(inventoryIndex, handle) >= minQty;
}

void EntityDataManager::setInventoryWorldTracking(uint32_t inventoryIndex, const std::string& worldId) {
    if (inventoryIndex >= m_inventoryData.size()) {
        return;
    }

    auto& inv = m_inventoryData[inventoryIndex];
    inv.flags |= InventoryData::FLAG_WORLD_TRACKED;

    if (inv.hasOverflow()) {
        auto it = m_inventoryOverflow.find(inv.overflowId);
        if (it != m_inventoryOverflow.end()) {
            it->second.worldId = worldId;
        }
    }

    // TODO: Register with WorldResourceManager
    // WorldResourceManager::Instance().registerEDMInventory(inventoryIndex, worldId);
}
```

### 1.5 Update init() and clean()

**File:** `src/managers/EntityDataManager.cpp`

**In init() (after line 52):**
```cpp
        m_inventoryData.reserve(500);  // Containers, player, NPCs with inventory
```

**In clean() (after m_areaEffectData.clear()):**
```cpp
        m_inventoryData.clear();
        m_inventoryOverflow.clear();
        m_freeInventorySlots.clear();
```

### Checklist Phase 1
- [ ] Add InventorySlotData struct to EntityDataManager.hpp
- [ ] Add InventoryData struct (128 bytes, 12 inline slots)
- [ ] Add InventoryOverflow struct
- [ ] Add m_inventoryData, m_freeInventorySlots, m_inventoryOverflow, m_nextOverflowId storage
- [ ] Add inventory method declarations
- [ ] Implement createInventory() with free-list pattern
- [ ] Implement destroyInventory()
- [ ] Implement addToInventory() with stacking logic
- [ ] Implement removeFromInventory()
- [ ] Implement getInventoryQuantity() and hasInInventory()
- [ ] Implement setInventoryWorldTracking()
- [ ] Add reserve in init()
- [ ] Add clear in clean()
- [ ] Compile and verify no errors

---

## Phase 2: ItemRenderData and DroppedItem Migration

### 2.1 Add ItemRenderData struct

**File:** `include/managers/EntityDataManager.hpp`
**Location:** After NPCRenderData struct (line ~290)

```cpp
/**
 * @brief Render data for dropped items (bobbing animation, visual effects)
 * Indexed parallel to ItemData using typeLocalIndex.
 *
 * Animation: Simple frame cycling from sprite sheet
 * Bobbing: Sinusoidal vertical offset (BOB_AMPLITUDE pixels, BOB_SPEED cycles/sec)
 */
struct ItemRenderData {
    SDL_Texture* cachedTexture{nullptr};  // From ResourceTemplate::worldTextureId
    uint16_t frameWidth{32};              // Single frame width
    uint16_t frameHeight{32};             // Single frame height
    uint16_t animSpeedMs{100};            // Milliseconds per frame
    uint8_t currentFrame{0};              // Current animation frame
    uint8_t numFrames{1};                 // Number of frames in animation
    float animTimer{0.0f};                // Animation accumulator
    float bobPhase{0.0f};                 // Bobbing phase (0 to 2*PI)

    void clear() noexcept {
        cachedTexture = nullptr;
        frameWidth = 32;
        frameHeight = 32;
        animSpeedMs = 100;
        currentFrame = 0;
        numFrames = 1;
        animTimer = 0.0f;
        bobPhase = 0.0f;
    }
};
```

### 2.2 Add storage vector and accessor

**File:** `include/managers/EntityDataManager.hpp`

**Private section (after m_npcRenderData, line ~1261):**
```cpp
    std::vector<ItemRenderData> m_itemRenderData;    // DroppedItem render data (same index as ItemData)
```

**Public section (after getNPCRenderDataByTypeIndex):**
```cpp
    [[nodiscard]] ItemRenderData& getItemRenderDataByTypeIndex(uint32_t typeLocalIndex);
    [[nodiscard]] const ItemRenderData& getItemRenderDataByTypeIndex(uint32_t typeLocalIndex) const;
```

**Inline implementations (after existing inline accessors, line ~1340):**
```cpp
inline ItemRenderData& EntityDataManager::getItemRenderDataByTypeIndex(uint32_t typeLocalIndex) {
    assert(typeLocalIndex < m_itemRenderData.size() && "Item render data index out of bounds");
    return m_itemRenderData[typeLocalIndex];
}

inline const ItemRenderData& EntityDataManager::getItemRenderDataByTypeIndex(uint32_t typeLocalIndex) const {
    assert(typeLocalIndex < m_itemRenderData.size() && "Item render data index out of bounds");
    return m_itemRenderData[typeLocalIndex];
}
```

### 2.3 Update init(), freeSlot(), and createDroppedItem()

**File:** `src/managers/EntityDataManager.cpp`

**In init() (after m_itemData.reserve, line ~48):**
```cpp
        m_itemRenderData.reserve(ITEM_CAPACITY);  // Same capacity as ItemData
```

**In freeSlot() DroppedItem case (line ~299-301):**
```cpp
        case EntityKind::DroppedItem:
            m_freeItemSlots.push_back(typeIndex);
            // Clear item render data
            if (typeIndex < m_itemRenderData.size()) {
                m_itemRenderData[typeIndex].clear();
            }
            break;
```

**Modify createDroppedItem() (line ~491-548) to add render data:**

After line 533 (`item.flags = 0;`), add:
```cpp
    // Allocate/reuse render data slot (parallel to item data)
    while (m_itemRenderData.size() <= itemIndex) {
        m_itemRenderData.emplace_back();
    }
    auto& renderData = m_itemRenderData[itemIndex];
    renderData.clear();

    // Get resource template for visual properties
    auto resourceTemplate = ResourceTemplateManager::Instance().getResourceTemplate(resourceHandle);
    if (resourceTemplate) {
        // Cache texture from TextureManager
        renderData.cachedTexture = TextureManager::Instance().getTexturePtr(
            resourceTemplate->getWorldTextureId());

        if (renderData.cachedTexture) {
            // Get texture size for frame dimensions
            float texWidth = 0.0f, texHeight = 0.0f;
            SDL_GetTextureSize(renderData.cachedTexture, &texWidth, &texHeight);

            renderData.numFrames = static_cast<uint8_t>(resourceTemplate->getNumFrames());
            if (renderData.numFrames > 0 && texWidth > 0.0f) {
                renderData.frameWidth = static_cast<uint16_t>(texWidth / static_cast<float>(renderData.numFrames));
            }
            renderData.frameHeight = static_cast<uint16_t>(texHeight);
            renderData.animSpeedMs = static_cast<uint16_t>(resourceTemplate->getAnimSpeed());
        }

        // Random initial bob phase for visual variety
        renderData.bobPhase = static_cast<float>(index % 100) * 0.0628f;  // 0 to 2*PI range
    }
```

### 2.4 Create ItemRenderController

**File:** `include/controllers/render/ItemRenderController.hpp` (NEW)

```cpp
/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef ITEM_RENDER_CONTROLLER_HPP
#define ITEM_RENDER_CONTROLLER_HPP

/**
 * @file ItemRenderController.hpp
 * @brief Data-driven dropped item rendering controller
 *
 * Renders DroppedItems using ItemRenderData from EntityDataManager.
 * Handles bobbing animation and sprite frame cycling.
 * No DroppedItem class needed - all data comes from EDM.
 *
 * Usage:
 *   - Add as member in GameState
 *   - Call update(deltaTime) in GameState::update()
 *   - Call renderItems(renderer, camX, camY, alpha) in GameState::render()
 */

#include "controllers/ControllerBase.hpp"
#include "controllers/IUpdatable.hpp"

struct SDL_Renderer;

class ItemRenderController : public ControllerBase, public IUpdatable {
public:
    ItemRenderController() = default;
    ~ItemRenderController() override = default;

    // Non-copyable, non-movable
    ItemRenderController(const ItemRenderController&) = delete;
    ItemRenderController& operator=(const ItemRenderController&) = delete;
    ItemRenderController(ItemRenderController&&) = delete;
    ItemRenderController& operator=(ItemRenderController&&) = delete;

    // ControllerBase interface
    void subscribe() override {}  // No events needed
    [[nodiscard]] std::string_view getName() const override { return "ItemRenderController"; }

    // IUpdatable - advances animation frames and bobbing
    void update(float deltaTime) override;

    /**
     * @brief Render all active dropped items
     * @param renderer SDL renderer from GameState::render()
     * @param cameraX Camera X offset for world-to-screen conversion
     * @param cameraY Camera Y offset for world-to-screen conversion
     * @param alpha Interpolation alpha for smooth rendering (0.0-1.0)
     */
    void renderItems(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha);

    /**
     * @brief Clear all dropped items (cleanup for state transitions)
     */
    void clearSpawnedItems();

private:
    static constexpr float BOB_SPEED = 2.5f;       // Bobbing cycles per second (radians)
    static constexpr float BOB_AMPLITUDE = 4.0f;  // Pixels of vertical bobbing
    static constexpr float TWO_PI = 6.283185307f;
};

#endif // ITEM_RENDER_CONTROLLER_HPP
```

**File:** `src/controllers/render/ItemRenderController.cpp` (NEW)

```cpp
/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/render/ItemRenderController.hpp"
#include "entities/EntityHandle.hpp"
#include "managers/EntityDataManager.hpp"
#include <SDL3/SDL.h>
#include <cmath>

void ItemRenderController::update(float deltaTime) {
    auto& edm = EntityDataManager::Instance();
    auto activeIndices = edm.getActiveIndices();

    for (size_t idx : activeIndices) {
        const auto& hotData = edm.getHotDataByIndex(idx);

        // Filter to DroppedItems only
        if (hotData.kind != EntityKind::DroppedItem || !hotData.isAlive()) {
            continue;
        }

        // Get render data via typeLocalIndex
        auto& renderData = edm.getItemRenderDataByTypeIndex(hotData.typeLocalIndex);

        // Skip if no texture cached
        if (!renderData.cachedTexture) {
            continue;
        }

        // Update bobbing phase
        renderData.bobPhase += deltaTime * BOB_SPEED;
        if (renderData.bobPhase > TWO_PI) {
            renderData.bobPhase -= TWO_PI;
        }

        // Update animation frame
        if (renderData.numFrames > 1 && renderData.animSpeedMs > 0) {
            float frameTime = static_cast<float>(renderData.animSpeedMs) / 1000.0f;
            renderData.animTimer += deltaTime;
            if (renderData.animTimer >= frameTime) {
                renderData.currentFrame = (renderData.currentFrame + 1) % renderData.numFrames;
                renderData.animTimer -= frameTime;
            }
        }
    }
}

void ItemRenderController::renderItems(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha) {
    if (!renderer) {
        return;
    }

    auto& edm = EntityDataManager::Instance();
    auto activeIndices = edm.getActiveIndices();

    for (size_t idx : activeIndices) {
        const auto& hotData = edm.getHotDataByIndex(idx);

        // Filter to DroppedItems only
        if (hotData.kind != EntityKind::DroppedItem || !hotData.isAlive()) {
            continue;
        }

        // Get item data for quantity check
        const auto& itemData = edm.getItemDataByTypeIndex(hotData.typeLocalIndex);
        if (itemData.quantity <= 0) {
            continue;  // Don't render empty stacks
        }

        // Get render data via typeLocalIndex
        const auto& renderData = edm.getItemRenderDataByTypeIndex(hotData.typeLocalIndex);
        if (!renderData.cachedTexture) {
            continue;  // Skip if texture not loaded
        }

        // Interpolate position for smooth rendering
        const auto& transform = hotData.transform;
        float interpX = transform.previousPosition.getX() +
            (transform.position.getX() - transform.previousPosition.getX()) * alpha;
        float interpY = transform.previousPosition.getY() +
            (transform.position.getY() - transform.previousPosition.getY()) * alpha;

        // Apply bobbing offset
        float bobOffset = std::sin(renderData.bobPhase) * BOB_AMPLITUDE;

        // Source rect (from sprite sheet)
        SDL_FRect srcRect = {
            static_cast<float>(renderData.currentFrame * renderData.frameWidth),
            0.0f,  // Single row sprite sheet
            static_cast<float>(renderData.frameWidth),
            static_cast<float>(renderData.frameHeight)
        };

        // Destination rect (screen position, centered on entity)
        SDL_FRect destRect = {
            interpX - cameraX - static_cast<float>(renderData.frameWidth) / 2.0f,
            interpY - cameraY - static_cast<float>(renderData.frameHeight) / 2.0f + bobOffset,
            static_cast<float>(renderData.frameWidth),
            static_cast<float>(renderData.frameHeight)
        };

        // Render with no rotation or flip
        SDL_RenderTextureRotated(
            renderer,
            renderData.cachedTexture,
            &srcRect,
            &destRect,
            0.0,
            nullptr,
            SDL_FLIP_NONE
        );
    }
}

void ItemRenderController::clearSpawnedItems() {
    auto& edm = EntityDataManager::Instance();

    // Get all DroppedItem indices
    auto itemIndices = edm.getIndicesByKind(EntityKind::DroppedItem);

    // Destroy via EDM
    for (size_t idx : itemIndices) {
        EntityHandle handle = edm.getHandle(idx);
        if (handle.isValid()) {
            edm.destroyEntity(handle);
        }
    }
}
```

### 2.5 Add getItemDataByTypeIndex accessor

**File:** `include/managers/EntityDataManager.hpp`

**Public section (if not already present):**
```cpp
    [[nodiscard]] ItemData& getItemDataByTypeIndex(uint32_t typeLocalIndex);
    [[nodiscard]] const ItemData& getItemDataByTypeIndex(uint32_t typeLocalIndex) const;
```

**Inline implementations:**
```cpp
inline ItemData& EntityDataManager::getItemDataByTypeIndex(uint32_t typeLocalIndex) {
    assert(typeLocalIndex < m_itemData.size() && "Item data index out of bounds");
    return m_itemData[typeLocalIndex];
}

inline const ItemData& EntityDataManager::getItemDataByTypeIndex(uint32_t typeLocalIndex) const {
    assert(typeLocalIndex < m_itemData.size() && "Item data index out of bounds");
    return m_itemData[typeLocalIndex];
}
```

### 2.6 Update CMakeLists.txt

**File:** `CMakeLists.txt`

Add to CONTROLLER_SOURCES (around line with other controllers):
```cmake
    src/controllers/render/ItemRenderController.cpp
```

### Checklist Phase 2
- [ ] Add ItemRenderData struct to EntityDataManager.hpp
- [ ] Add m_itemRenderData vector
- [ ] Add getItemRenderDataByTypeIndex() accessor and inline implementation
- [ ] Add getItemDataByTypeIndex() accessor and inline implementation
- [ ] Reserve m_itemRenderData in init()
- [ ] Clear m_itemRenderData in freeSlot() for DroppedItem kind
- [ ] Update createDroppedItem() to populate ItemRenderData from ResourceTemplate
- [ ] Create include/controllers/render/ItemRenderController.hpp
- [ ] Create src/controllers/render/ItemRenderController.cpp
- [ ] Add ItemRenderController.cpp to CMakeLists.txt
- [ ] Compile and verify no errors
- [ ] Test dropped item rendering in EventDemoState

---

## Phase 3: Container Migration

### 3.1 Add ContainerRenderData struct

**File:** `include/managers/EntityDataManager.hpp`
**Location:** After ItemRenderData struct

```cpp
/**
 * @brief Render data for containers (chests, barrels)
 * Indexed parallel to ContainerData using typeLocalIndex.
 */
struct ContainerRenderData {
    SDL_Texture* closedTexture{nullptr};
    SDL_Texture* openTexture{nullptr};
    uint16_t frameWidth{32};
    uint16_t frameHeight{32};
    uint8_t animState{0};       // 0=closed, 1=opening, 2=open, 3=closing
    uint8_t animFrame{0};       // Current frame in open/close animation
    uint8_t openAnimFrames{4};  // Frames in open animation
    float stateTimer{0.0f};     // Time in current state

    void clear() noexcept {
        closedTexture = nullptr;
        openTexture = nullptr;
        frameWidth = 32;
        frameHeight = 32;
        animState = 0;
        animFrame = 0;
        openAnimFrames = 4;
        stateTimer = 0.0f;
    }
};
```

### 3.2 Enhance ContainerData

**File:** `include/managers/EntityDataManager.hpp`
**Modify existing ContainerData struct (line ~225):**

```cpp
/**
 * @brief Container data for Container entities (chests, barrels)
 */
struct ContainerData {
    uint32_t inventoryIndex{0};     // Index into m_inventoryData (EDM inventory storage)
    uint16_t maxSlots{20};          // For reference (actual storage in InventoryData)
    uint8_t containerType{0};       // Chest=0, Barrel=1, Corpse=2, Crate=3
    uint8_t lockLevel{0};           // 0=unlocked, 1-10=lock difficulty
    uint8_t flags{0};               // isOpen, isLocked, etc.

    static constexpr uint8_t FLAG_IS_OPEN = 0x01;
    static constexpr uint8_t FLAG_IS_LOCKED = 0x02;
    static constexpr uint8_t FLAG_WAS_LOOTED = 0x04;

    [[nodiscard]] bool isOpen() const noexcept { return flags & FLAG_IS_OPEN; }
    [[nodiscard]] bool isLocked() const noexcept { return flags & FLAG_IS_LOCKED; }

    void setOpen(bool open) noexcept {
        if (open) flags |= FLAG_IS_OPEN;
        else flags &= ~FLAG_IS_OPEN;
    }
};
```

### 3.3 Add Container type registry

**File:** `include/managers/EntityDataManager.hpp`
**Private section:**

```cpp
    // Container Type Registry
    struct ContainerTypeInfo {
        std::string closedTextureID;
        std::string openTextureID;
        uint16_t defaultMaxSlots;
        uint8_t containerType;
        uint8_t defaultLockLevel;
    };
    std::unordered_map<std::string, ContainerTypeInfo> m_containerTypeRegistry;
    void initializeContainerTypeRegistry();
```

**File:** `include/managers/EntityDataManager.hpp`
**Public section:**

```cpp
    // Container creation
    [[nodiscard]] EntityHandle createContainer(const Vector2D& position,
                                                uint8_t containerType,
                                                uint16_t maxSlots,
                                                uint8_t lockLevel = 0);
    [[nodiscard]] EntityHandle createContainer(const Vector2D& position,
                                                const std::string& containerTypeName);
    void registerContainerType(const std::string& typeName, const ContainerTypeInfo& info);
```

### 3.4 Add storage vectors

**File:** `include/managers/EntityDataManager.hpp`
**Private section (after m_itemRenderData):**

```cpp
    std::vector<ContainerRenderData> m_containerRenderData;  // Same index as ContainerData
```

### 3.5 Implement container creation

**File:** `src/managers/EntityDataManager.cpp`

```cpp
void EntityDataManager::initializeContainerTypeRegistry() {
    m_containerTypeRegistry = {
        {"WoodenChest", {"chest_closed", "chest_open", 20, 0, 0}},
        {"IronChest", {"iron_chest_closed", "iron_chest_open", 30, 0, 3}},
        {"Barrel", {"barrel", "barrel_open", 10, 1, 0}},
        {"Corpse", {"corpse", "corpse_looted", 15, 2, 0}},
        {"Crate", {"crate", "crate_open", 12, 3, 0}},
    };
}

EntityHandle EntityDataManager::createContainer(const Vector2D& position,
                                                  uint8_t containerType,
                                                  uint16_t maxSlots,
                                                  uint8_t lockLevel) {
    size_t index = allocateSlot();
    EntityHandle::IDType id = HammerEngine::UniqueID::generate();
    uint8_t generation = nextGeneration(index);

    // Initialize hot data
    auto& hot = m_hotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = Vector2D{0.0f, 0.0f};
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = 16.0f;
    hot.halfHeight = 16.0f;
    hot.kind = EntityKind::Container;
    hot.tier = SimulationTier::Active;
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;

    // Collision (interact trigger, not physical)
    hot.collisionLayers = HammerEngine::CollisionLayer::Layer_Default;
    hot.collisionMask = HammerEngine::CollisionLayer::Layer_Player;
    hot.collisionFlags = EntityHotData::IS_TRIGGER;

    // Allocate container data
    uint32_t containerIndex;
    if (!m_freeContainerSlots.empty()) {
        containerIndex = m_freeContainerSlots.back();
        m_freeContainerSlots.pop_back();
    } else {
        containerIndex = static_cast<uint32_t>(m_containerData.size());
        m_containerData.emplace_back();
        m_containerRenderData.emplace_back();  // Keep in sync
    }
    auto& container = m_containerData[containerIndex];
    container = ContainerData{};
    container.containerType = containerType;
    container.maxSlots = maxSlots;
    container.lockLevel = lockLevel;
    if (lockLevel > 0) {
        container.flags |= ContainerData::FLAG_IS_LOCKED;
    }

    // Create inventory for this container
    container.inventoryIndex = createInventory(maxSlots, false);

    hot.typeLocalIndex = containerIndex;

    // Store ID and mapping
    m_entityIds[index] = id;
    m_generations[index] = generation;
    m_idToIndex[id] = index;

    // Update counters
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::Container)].fetch_add(1, std::memory_order_relaxed);
    m_countByTier[static_cast<size_t>(SimulationTier::Active)].fetch_add(1, std::memory_order_relaxed);
    m_tierIndicesDirty = true;

    return EntityHandle{id, EntityKind::Container, generation};
}

EntityHandle EntityDataManager::createContainer(const Vector2D& position,
                                                  const std::string& containerTypeName) {
    auto it = m_containerTypeRegistry.find(containerTypeName);
    if (it != m_containerTypeRegistry.end()) {
        const auto& info = it->second;
        EntityHandle handle = createContainer(position, info.containerType,
                                              info.defaultMaxSlots, info.defaultLockLevel);
        if (handle.isValid()) {
            // Populate render data from registry
            size_t index = getIndex(handle);
            uint32_t typeIndex = m_hotData[index].typeLocalIndex;
            auto& renderData = m_containerRenderData[typeIndex];
            renderData.closedTexture = TextureManager::Instance().getTexturePtr(info.closedTextureID);
            renderData.openTexture = TextureManager::Instance().getTexturePtr(info.openTextureID);
            // Get frame dimensions from texture if available
            if (renderData.closedTexture) {
                float w, h;
                SDL_GetTextureSize(renderData.closedTexture, &w, &h);
                renderData.frameWidth = static_cast<uint16_t>(w);
                renderData.frameHeight = static_cast<uint16_t>(h);
            }
        }
        return handle;
    }
    ENTITY_ERROR(std::format("Unknown container type '{}'", containerTypeName));
    return EntityHandle{};
}

void EntityDataManager::registerContainerType(const std::string& typeName,
                                               const ContainerTypeInfo& info) {
    m_containerTypeRegistry[typeName] = info;
}
```

### 3.6 Create ContainerRenderController

**File:** `include/controllers/render/ContainerRenderController.hpp` (NEW)

```cpp
/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef CONTAINER_RENDER_CONTROLLER_HPP
#define CONTAINER_RENDER_CONTROLLER_HPP

#include "controllers/ControllerBase.hpp"
#include "controllers/IUpdatable.hpp"

struct SDL_Renderer;

class ContainerRenderController : public ControllerBase, public IUpdatable {
public:
    ContainerRenderController() = default;
    ~ContainerRenderController() override = default;

    ContainerRenderController(const ContainerRenderController&) = delete;
    ContainerRenderController& operator=(const ContainerRenderController&) = delete;

    void subscribe() override {}
    [[nodiscard]] std::string_view getName() const override { return "ContainerRenderController"; }

    void update(float deltaTime) override;
    void renderContainers(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha);
    void clearSpawnedContainers();

private:
    static constexpr float OPEN_ANIM_SPEED = 0.1f;  // Seconds per frame
};

#endif // CONTAINER_RENDER_CONTROLLER_HPP
```

**File:** `src/controllers/render/ContainerRenderController.cpp` (NEW)

Similar pattern to ItemRenderController - render open/closed state based on ContainerData::isOpen().

### 3.7 Update freeSlot for Container

**File:** `src/managers/EntityDataManager.cpp**
**In freeSlot(), Container case:**

```cpp
        case EntityKind::Container:
            {
                // Destroy associated inventory
                if (typeIndex < m_containerData.size()) {
                    uint32_t invIndex = m_containerData[typeIndex].inventoryIndex;
                    destroyInventory(invIndex);
                }
            }
            m_freeContainerSlots.push_back(typeIndex);
            if (typeIndex < m_containerRenderData.size()) {
                m_containerRenderData[typeIndex].clear();
            }
            break;
```

### Checklist Phase 3
- [ ] Add ContainerRenderData struct
- [ ] Enhance ContainerData with inventoryIndex and flags
- [ ] Add ContainerTypeInfo struct and registry
- [ ] Add m_containerRenderData vector
- [ ] Add registerContainerType() declaration
- [ ] Implement initializeContainerTypeRegistry()
- [ ] Implement createContainer() (both overloads)
- [ ] Update freeSlot() to destroy container inventory
- [ ] Create ContainerRenderController.hpp
- [ ] Create ContainerRenderController.cpp
- [ ] Add to CMakeLists.txt
- [ ] Test container creation and inventory access

---

## Phase 4: Harvestable Migration

### 4.1 Add HarvestableRenderData struct

**File:** `include/managers/EntityDataManager.hpp`

```cpp
/**
 * @brief Render data for harvestable resource nodes
 */
struct HarvestableRenderData {
    SDL_Texture* normalTexture{nullptr};
    SDL_Texture* depletedTexture{nullptr};
    uint16_t frameWidth{32};
    uint16_t frameHeight{32};
    uint8_t harvestAnimFrames{4};
    uint8_t currentFrame{0};
    float animTimer{0.0f};
    bool isAnimating{false};

    void clear() noexcept {
        normalTexture = nullptr;
        depletedTexture = nullptr;
        frameWidth = 32;
        frameHeight = 32;
        harvestAnimFrames = 4;
        currentFrame = 0;
        animTimer = 0.0f;
        isAnimating = false;
    }
};
```

### 4.2 Add Harvestable type registry

**File:** `include/managers/EntityDataManager.hpp`

```cpp
    // Harvestable Type Registry
    struct HarvestableTypeInfo {
        std::string normalTextureID;
        std::string depletedTextureID;
        HammerEngine::ResourceHandle yieldResource;
        int yieldMin;
        int yieldMax;
        float respawnTime;
        uint8_t harvestType;  // Mining=0, Chopping=1, Gathering=2, Fishing=3
    };
    std::unordered_map<std::string, HarvestableTypeInfo> m_harvestableTypeRegistry;
    void initializeHarvestableTypeRegistry();
```

### 4.3 Add creation methods and storage

Similar pattern to Container - implement createHarvestable() and HarvestableRenderController.

### Checklist Phase 4
- [ ] Add HarvestableRenderData struct
- [ ] Add HarvestableTypeInfo struct and registry
- [ ] Add m_harvestableRenderData vector
- [ ] Implement createHarvestable() methods
- [ ] Implement harvest() method that yields resources to player inventory
- [ ] Update respawn timer in update loop
- [ ] Create HarvestableRenderController.hpp
- [ ] Create HarvestableRenderController.cpp
- [ ] Add to CMakeLists.txt
- [ ] Test harvestable creation and depletion

---

## Phase 5: WorldResourceManager EDM Integration

### 5.1 Add EDM query methods

**File:** `include/managers/WorldResourceManager.hpp`

```cpp
    // EDM integration methods
    int64_t getResourceQuantityFromEDM(const WorldId& worldId,
                                        HammerEngine::ResourceHandle handle) const;

    std::unordered_map<HammerEngine::ResourceHandle, int64_t>
    calculateWorldTotalsFromEDM(const WorldId& worldId) const;

    void registerEDMInventory(uint32_t inventoryIndex, const WorldId& worldId);
    void unregisterEDMInventory(uint32_t inventoryIndex);

private:
    std::unordered_map<WorldId, std::vector<uint32_t>> m_edmInventoriesByWorld;
```

### 5.2 Implement EDM queries

**File:** `src/managers/WorldResourceManager.cpp`

```cpp
int64_t WorldResourceManager::getResourceQuantityFromEDM(
    const WorldId& worldId,
    HammerEngine::ResourceHandle handle) const {

    auto it = m_edmInventoriesByWorld.find(worldId);
    if (it == m_edmInventoriesByWorld.end()) {
        return 0;
    }

    int64_t total = 0;
    auto& edm = EntityDataManager::Instance();
    for (uint32_t invIndex : it->second) {
        total += edm.getInventoryQuantity(invIndex, handle);
    }
    return total;
}

void WorldResourceManager::registerEDMInventory(uint32_t inventoryIndex,
                                                 const WorldId& worldId) {
    m_edmInventoriesByWorld[worldId].push_back(inventoryIndex);
}

void WorldResourceManager::unregisterEDMInventory(uint32_t inventoryIndex) {
    for (auto& [worldId, indices] : m_edmInventoriesByWorld) {
        auto it = std::find(indices.begin(), indices.end(), inventoryIndex);
        if (it != indices.end()) {
            indices.erase(it);
            break;
        }
    }
}
```

### Checklist Phase 5
- [ ] Add EDM query method declarations to WorldResourceManager.hpp
- [ ] Add m_edmInventoriesByWorld tracking map
- [ ] Implement getResourceQuantityFromEDM()
- [ ] Implement calculateWorldTotalsFromEDM()
- [ ] Implement registerEDMInventory() and unregisterEDMInventory()
- [ ] Call registerEDMInventory() in EDM::setInventoryWorldTracking()
- [ ] Test world resource aggregation

---

## Phase 6: Cleanup and Removal

### 6.1 Files to delete

```
include/entities/DroppedItem.hpp
src/entities/DroppedItem.cpp
```

### 6.2 Files to update

| File | Changes |
|------|---------|
| `tests/resources/ResourceArchitectureTests.cpp` | Remove `#include "entities/DroppedItem.hpp"`, use EDM createDroppedItem |
| `CMakeLists.txt` | Remove DroppedItem.cpp, add render controllers |

### 6.3 CMakeLists.txt updates

**Remove:**
```cmake
src/entities/DroppedItem.cpp
```

**Add:**
```cmake
src/controllers/render/ItemRenderController.cpp
src/controllers/render/ContainerRenderController.cpp
src/controllers/render/HarvestableRenderController.cpp
```

### Checklist Phase 6
- [ ] grep for all DroppedItem includes: `grep -r "DroppedItem.hpp"`
- [ ] Update ResourceArchitectureTests.cpp to use EDM
- [ ] Delete DroppedItem.hpp and DroppedItem.cpp
- [ ] Update CMakeLists.txt
- [ ] Full rebuild: `cmake -B build -G Ninja && ninja -C build`
- [ ] Run EDM tests: `./bin/debug/entity_data_manager_tests`
- [ ] Run resource tests: `./bin/debug/inventory_component_tests`

---

## Phase 7: Testing & Validation

### Test Commands

```bash
# Build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build

# Run specific tests
./bin/debug/entity_data_manager_tests
./bin/debug/inventory_component_tests
./bin/debug/resource_architecture_tests

# Run all core tests
./tests/test_scripts/run_all_tests.sh --core-only --errors-only
```

### Test Scenarios

1. **Inventory Operations**
   - Create 12-slot inventory (inline only)
   - Create 50-slot inventory (with overflow)
   - Add stackable resources, verify stacking
   - Add non-stackable resources, verify separate slots
   - Remove resources, verify quantities
   - Transfer between inventories

2. **DroppedItem**
   - Create via createDroppedItem(), verify ItemRenderData populated
   - Verify bobbing animation in ItemRenderController
   - Destroy item, verify free-list reuse

3. **Container**
   - Create container with inventory
   - Add/remove items from container inventory
   - Open/close state changes in render

4. **Harvestable**
   - Create harvestable, verify render data
   - Harvest, verify depletion
   - Wait respawn timer, verify restoration

5. **WorldResourceManager**
   - Create world-tracked inventory
   - Add resources, verify world totals
   - Multiple inventories in same world

---

## Summary of New Files

| File | Type |
|------|------|
| `include/controllers/render/ItemRenderController.hpp` | NEW |
| `src/controllers/render/ItemRenderController.cpp` | NEW |
| `include/controllers/render/ContainerRenderController.hpp` | NEW |
| `src/controllers/render/ContainerRenderController.cpp` | NEW |
| `include/controllers/render/HarvestableRenderController.hpp` | NEW |
| `src/controllers/render/HarvestableRenderController.cpp` | NEW |

## Summary of Modified Files

| File | Changes |
|------|---------|
| `include/managers/EntityDataManager.hpp` | Add InventoryData, ItemRenderData, ContainerRenderData, HarvestableRenderData structs; type registries; methods |
| `src/managers/EntityDataManager.cpp` | Implement inventory methods, creation functions, type registrations |
| `include/managers/WorldResourceManager.hpp` | Add EDM query methods |
| `src/managers/WorldResourceManager.cpp` | Implement EDM integration |
| `CMakeLists.txt` | Add render controllers, remove DroppedItem |

## Files to Delete

| File |
|------|
| `include/entities/DroppedItem.hpp` |
| `src/entities/DroppedItem.cpp` |
