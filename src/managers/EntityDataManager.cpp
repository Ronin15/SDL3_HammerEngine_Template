/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/EntityDataManager.hpp"
#include "core/Logger.hpp"
#include "entities/Entity.hpp"  // For AnimationConfig
#include "managers/ResourceTemplateManager.hpp"  // For getMaxStackSize in inventory
#include "managers/TextureManager.hpp"  // For texture lookup in createDataDrivenNPC
#include "managers/WorldResourceManager.hpp"  // For unregister on harvestable destruction
#include "utils/JsonReader.hpp"  // For loading NPC types from JSON
#include "utils/UniqueID.hpp"
#include <SDL3/SDL.h>  // For SDL_GetTextureSize

using HammerEngine::JsonReader;
using HammerEngine::JsonValue;
#include <algorithm>
#include <cassert>
#include <format>

// ============================================================================
// LIFECYCLE
// ============================================================================

bool EntityDataManager::init() {
    if (m_initialized.load(std::memory_order_acquire)) {
        ENTITY_INFO("EntityDataManager already initialized");
        return true;
    }

    try {
        // Pre-allocate storage for expected entity counts
        constexpr size_t INITIAL_CAPACITY = 10000;
        constexpr size_t CHARACTER_CAPACITY = 5000;
        constexpr size_t ITEM_CAPACITY = 2000;
        constexpr size_t PROJECTILE_CAPACITY = 500;
        constexpr size_t EFFECT_CAPACITY = 200;

        m_hotData.reserve(INITIAL_CAPACITY);
        m_entityIds.reserve(INITIAL_CAPACITY);
        m_generations.reserve(INITIAL_CAPACITY);
        m_idToIndex.reserve(INITIAL_CAPACITY);

        // Static entity storage (not part of tier system)
        constexpr size_t STATIC_CAPACITY = 10000;
        m_staticHotData.reserve(STATIC_CAPACITY);
        m_staticEntityIds.reserve(STATIC_CAPACITY);
        m_staticGenerations.reserve(STATIC_CAPACITY);
        m_staticIdToIndex.reserve(STATIC_CAPACITY);

        m_characterData.reserve(CHARACTER_CAPACITY);
        m_npcRenderData.reserve(CHARACTER_CAPACITY);  // Same capacity as CharacterData
        m_itemData.reserve(ITEM_CAPACITY);
        m_itemRenderData.reserve(ITEM_CAPACITY);  // Same capacity as ItemData
        m_projectileData.reserve(PROJECTILE_CAPACITY);
        m_containerData.reserve(100);
        m_containerRenderData.reserve(100);  // Same capacity as ContainerData
        m_harvestableData.reserve(500);
        m_harvestableRenderData.reserve(500);  // Same capacity as HarvestableData
        m_areaEffectData.reserve(EFFECT_CAPACITY);

        // Path data (indexed by edmIndex, sparse for non-AI entities)
        m_pathData.reserve(CHARACTER_CAPACITY);

        // Per-entity waypoint slots (parallel to pathData)
        m_waypointSlots.reserve(CHARACTER_CAPACITY);

        // Behavior data (indexed by edmIndex, pre-allocated alongside hotData)
        m_behaviorData.reserve(CHARACTER_CAPACITY);

        m_activeIndices.reserve(INITIAL_CAPACITY);
        m_backgroundIndices.reserve(INITIAL_CAPACITY);
        m_hibernatedIndices.reserve(INITIAL_CAPACITY);

        for (auto& kindVec : m_kindIndices) {
            kindVec.reserve(1000);
        }

        m_destructionQueue.reserve(100);
        m_destroyBuffer.reserve(100);  // Match destruction queue capacity
        m_freeSlots.reserve(1000);

        // Inventory storage
        constexpr size_t INVENTORY_CAPACITY = 500;
        m_inventoryData.reserve(INVENTORY_CAPACITY);
        m_freeInventorySlots.reserve(INVENTORY_CAPACITY);

        // Initialize NPC type registry
        initializeNPCTypeRegistry();

        // Reset counters
        m_totalEntityCount.store(0, std::memory_order_relaxed);
        for (auto& count : m_countByKind) {
            count.store(0, std::memory_order_relaxed);
        }
        for (auto& count : m_countByTier) {
            count.store(0, std::memory_order_relaxed);
        }

        m_initialized.store(true, std::memory_order_release);
        ENTITY_INFO("EntityDataManager initialized successfully");
        return true;

    } catch (const std::exception& e) {
        ENTITY_ERROR(std::format("Failed to initialize EntityDataManager: {}", e.what()));
        return false;
    }
}

void EntityDataManager::clean() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    ENTITY_INFO("EntityDataManager shutting down...");

    m_initialized.store(false, std::memory_order_release);

    // Clear all entity data (main thread only - no lock needed)
    m_hotData.clear();
    m_entityIds.clear();
    m_generations.clear();
    m_idToIndex.clear();

    // Clear static entity storage
    m_staticHotData.clear();
    m_staticEntityIds.clear();
    m_staticGenerations.clear();
    m_staticIdToIndex.clear();
    m_freeStaticSlots.clear();

    m_characterData.clear();
    m_npcRenderData.clear();
    m_itemData.clear();
    m_itemRenderData.clear();
    m_projectileData.clear();
    m_containerData.clear();
    m_containerRenderData.clear();
    m_harvestableData.clear();
    m_harvestableRenderData.clear();
    m_areaEffectData.clear();
    m_pathData.clear();
    m_waypointSlots.clear();  // Clear per-entity waypoint slots
    m_behaviorData.clear();

    // Clear type-specific free-lists
    m_freeCharacterSlots.clear();
    m_freeItemSlots.clear();
    m_freeProjectileSlots.clear();
    m_freeContainerSlots.clear();
    m_freeHarvestableSlots.clear();
    m_freeAreaEffectSlots.clear();

    // Clear inventory storage
    m_inventoryData.clear();
    m_inventoryOverflow.clear();
    m_freeInventorySlots.clear();
    m_nextOverflowId = 1;

    m_activeIndices.clear();
    m_backgroundIndices.clear();
    m_hibernatedIndices.clear();

    for (auto& kindVec : m_kindIndices) {
        kindVec.clear();
    }

    m_freeSlots.clear();

    {
        std::lock_guard<std::mutex> lock(m_destructionMutex);
        m_destructionQueue.clear();
    }
    m_destroyBuffer.clear();

    m_totalEntityCount.store(0, std::memory_order_relaxed);
    for (auto& count : m_countByKind) {
        count.store(0, std::memory_order_relaxed);
    }
    for (auto& count : m_countByTier) {
        count.store(0, std::memory_order_relaxed);
    }

    ENTITY_INFO("EntityDataManager shutdown complete");
}

void EntityDataManager::prepareForStateTransition() {
    ENTITY_INFO("Preparing EntityDataManager for state transition...");

    // Process any pending destructions first
    processDestructionQueue();

    // Clear all entity data (main thread only - no lock needed)
    m_hotData.clear();
    m_entityIds.clear();
    m_idToIndex.clear();

    // Clear static entity storage
    m_staticHotData.clear();
    m_staticEntityIds.clear();
    m_staticGenerations.clear();
    m_staticIdToIndex.clear();
    m_freeStaticSlots.clear();

    m_characterData.clear();
    m_npcRenderData.clear();
    m_itemData.clear();
    m_itemRenderData.clear();
    m_projectileData.clear();
    m_containerData.clear();
    m_containerRenderData.clear();
    m_harvestableData.clear();
    m_harvestableRenderData.clear();
    m_areaEffectData.clear();
    m_pathData.clear();
    m_waypointSlots.clear();  // Clear per-entity waypoint slots
    m_behaviorData.clear();

    // Clear type-specific free-lists
    m_freeCharacterSlots.clear();
    m_freeItemSlots.clear();
    m_freeProjectileSlots.clear();
    m_freeContainerSlots.clear();
    m_freeHarvestableSlots.clear();
    m_freeAreaEffectSlots.clear();

    // Clear inventory storage
    m_inventoryData.clear();
    m_inventoryOverflow.clear();
    m_freeInventorySlots.clear();
    m_nextOverflowId = 1;

    m_activeIndices.clear();
    m_backgroundIndices.clear();
    m_hibernatedIndices.clear();

    // CRITICAL: Clear ALL cached indices to prevent stale access
    // Each cached index vector can cause crashes if not cleared
    m_activeCollisionIndices.clear();
    m_activeCollisionDirty = true;

    m_triggerDetectionIndices.clear();
    m_triggerDetectionDirty = true;

    for (auto& kindVec : m_kindIndices) {
        kindVec.clear();
    }

    m_freeSlots.clear();
    m_tierIndicesDirty = true;
    markAllKindsDirty();

    m_totalEntityCount.store(0, std::memory_order_relaxed);
    for (auto& count : m_countByKind) {
        count.store(0, std::memory_order_relaxed);
    }
    for (auto& count : m_countByTier) {
        count.store(0, std::memory_order_relaxed);
    }

    ENTITY_INFO("EntityDataManager prepared for state transition");
}

EntityDataManager::~EntityDataManager() {
    clean();
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

size_t EntityDataManager::allocateSlot() {
    size_t index;

    if (!m_freeSlots.empty()) {
        index = m_freeSlots.back();
        m_freeSlots.pop_back();
    } else {
        index = m_hotData.size();
        m_hotData.emplace_back();
        m_entityIds.emplace_back(0);
        m_generations.emplace_back(0);
        // Pre-allocate PathData, WaypointSlot, BehaviorData to match - avoids concurrent resize during AI processing
        m_pathData.emplace_back();
        m_waypointSlots.emplace_back();  // Per-entity waypoint slot (256 bytes)
        m_behaviorData.emplace_back();
    }

    m_tierIndicesDirty = true;
    // Note: kind dirty flag is marked when entity kind is set, not here
    // (allocateSlot doesn't know the entity kind yet)

    return index;
}

void EntityDataManager::freeSlot(size_t index) {
    if (index >= m_hotData.size()) {
        return;
    }

    // Capture type info BEFORE clearing (for type-specific free-list)
    EntityKind kind = m_hotData[index].kind;
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;

    // Clear path and behavior data for AI entities
    clearPathData(index);
    clearBehaviorData(index);

    // Clear the slot
    m_hotData[index] = EntityHotData{};
    m_entityIds[index] = 0;

    // Increment generation for stale handle detection
    m_generations[index]++;

    // Add to free list
    m_freeSlots.push_back(index);

    // Add type-specific index to appropriate free-list for reuse
    switch (kind) {
        case EntityKind::Player:
            m_freeCharacterSlots.push_back(typeIndex);
            break;
        case EntityKind::NPC:
            m_freeCharacterSlots.push_back(typeIndex);
            // Clear NPC render data (uses same index as CharacterData)
            if (typeIndex < m_npcRenderData.size()) {
                m_npcRenderData[typeIndex].clear();
            }
            break;
        case EntityKind::DroppedItem:
            m_freeItemSlots.push_back(typeIndex);
            // Unregister from WorldResourceManager spatial index
            if (WorldResourceManager::Instance().isInitialized()) {
                WorldResourceManager::Instance().unregisterDroppedItem(index);
            }
            // Clear item render data
            if (typeIndex < m_itemRenderData.size()) {
                m_itemRenderData[typeIndex].clear();
            }
            break;
        case EntityKind::Projectile:
            m_freeProjectileSlots.push_back(typeIndex);
            break;
        case EntityKind::Container:
            // Destroy the associated inventory first
            if (typeIndex < m_containerData.size()) {
                uint32_t invIdx = m_containerData[typeIndex].inventoryIndex;
                if (invIdx != INVALID_INVENTORY_INDEX) {
                    destroyInventory(invIdx);
                }
            }
            m_freeContainerSlots.push_back(typeIndex);
            // Clear container render data
            if (typeIndex < m_containerRenderData.size()) {
                m_containerRenderData[typeIndex].clear();
            }
            break;
        case EntityKind::Harvestable:
            m_freeHarvestableSlots.push_back(typeIndex);
            // Unregister from WorldResourceManager (EDM index, not typeIndex)
            if (WorldResourceManager::Instance().isInitialized()) {
                WorldResourceManager::Instance().unregisterHarvestable(index);
                WorldResourceManager::Instance().unregisterHarvestableSpatial(index);
            }
            // Clear harvestable render data
            if (typeIndex < m_harvestableRenderData.size()) {
                m_harvestableRenderData[typeIndex].clear();
            }
            break;
        case EntityKind::AreaEffect:
            m_freeAreaEffectSlots.push_back(typeIndex);
            break;
        default:
            // StaticObstacle, Prop, Trigger have no type-specific data
            break;
    }

    m_tierIndicesDirty = true;
    markKindDirty(kind);  // Only mark the freed entity's kind dirty
}

uint8_t EntityDataManager::nextGeneration(size_t index) {
    if (index >= m_generations.size()) {
        return 1;
    }
    return static_cast<uint8_t>((m_generations[index] + 1) % 256);
}

uint32_t EntityDataManager::allocateCharacterSlot() {
    uint32_t charIndex;
    if (!m_freeCharacterSlots.empty()) {
        charIndex = m_freeCharacterSlots.back();
        m_freeCharacterSlots.pop_back();
        m_characterData[charIndex] = CharacterData{};
        // Clear render data if slot exists (reusing freed NPC slot)
        if (charIndex < m_npcRenderData.size()) {
            m_npcRenderData[charIndex].clear();
        }
    } else {
        charIndex = static_cast<uint32_t>(m_characterData.size());
        m_characterData.emplace_back();
        m_npcRenderData.emplace_back();  // Always stays in sync
    }
    return charIndex;
}

// ============================================================================
// ENTITY CREATION
// ============================================================================

EntityHandle EntityDataManager::createNPC(const Vector2D& position,
                                          float halfWidth,
                                          float halfHeight) {

    size_t index = allocateSlot();
    EntityHandle::IDType id = HammerEngine::UniqueID::generate();
    uint8_t generation = nextGeneration(index);

    // Initialize hot data
    auto& hot = m_hotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = Vector2D{0.0f, 0.0f};
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = halfWidth;
    hot.halfHeight = halfHeight;
    hot.kind = EntityKind::NPC;
    markKindDirty(EntityKind::NPC);
    hot.tier = SimulationTier::Active;
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;

    // Initialize collision data (NPCs collide with player, environment, projectiles)
    hot.collisionLayers = HammerEngine::CollisionLayer::Layer_Enemy;
    hot.collisionMask = HammerEngine::CollisionLayer::Layer_Player |
                        HammerEngine::CollisionLayer::Layer_Environment |
                        HammerEngine::CollisionLayer::Layer_Projectile |
                        HammerEngine::CollisionLayer::Layer_Enemy;
    hot.collisionFlags = EntityHotData::COLLISION_ENABLED;
    hot.triggerTag = 0;

    // Allocate character data (CharacterData + NPCRenderData stay in sync)
    uint32_t charIndex = allocateCharacterSlot();
    m_characterData[charIndex].stateFlags = CharacterData::STATE_ALIVE;
    hot.typeLocalIndex = charIndex;

    // Store ID and mapping
    m_entityIds[index] = id;
    m_generations[index] = generation;
    m_idToIndex[id] = index;

    // Update counters
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::NPC)].fetch_add(1, std::memory_order_relaxed);
    m_countByTier[static_cast<size_t>(SimulationTier::Active)].fetch_add(1, std::memory_order_relaxed);

    // Mark tier indices dirty so new entity gets added
    m_tierIndicesDirty = true;

    ENTITY_DEBUG(std::format("Created NPC entity {} at ({}, {})",
                            id, position.getX(), position.getY()));

    return EntityHandle{id, EntityKind::NPC, generation};
}

EntityHandle EntityDataManager::createDataDrivenNPC(const Vector2D& position,
                                                     const std::string& npcType) {
    // Look up type in registry
    auto it = m_npcTypeRegistry.find(npcType);
    if (it == m_npcTypeRegistry.end()) {
        ENTITY_ERROR(std::format("Unknown NPC type '{}' - must be registered in npc_types.json", npcType));
        return EntityHandle{};
    }

    const auto& info = it->second;

    // Calculate frame dimensions from atlas region
    int maxFrames = std::max(info.idleAnim.frameCount, info.moveAnim.frameCount);
    uint16_t frameWidth = (maxFrames > 0) ? static_cast<uint16_t>(info.atlasW / maxFrames) : info.atlasW;
    uint16_t frameHeight = info.atlasH;

    // Create NPC with collision dimensions (half-size for AABB)
    EntityHandle handle = createNPC(position,
                                    static_cast<float>(frameWidth) * 0.5f,
                                    static_cast<float>(frameHeight) * 0.5f);

    if (!handle.isValid()) {
        ENTITY_ERROR("createDataDrivenNPC: Failed to create NPC entity");
        return INVALID_ENTITY_HANDLE;
    }

    // Get render data (created by createNPC alongside CharacterData)
    size_t index = getIndex(handle);
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    auto& renderData = m_npcRenderData[typeIndex];

    // Use atlas texture
    renderData.cachedTexture = TextureManager::Instance().getTexturePtr("atlas");
    if (!renderData.cachedTexture) {
        ENTITY_ERROR("createDataDrivenNPC: Atlas texture not loaded");
    }

    // Set atlas coordinates and frame dimensions
    renderData.atlasX = info.atlasX;
    renderData.atlasY = info.atlasY;
    renderData.frameWidth = frameWidth;
    renderData.frameHeight = frameHeight;

    // Animation config
    renderData.idleSpeedMs = static_cast<uint16_t>(std::max(1, info.idleAnim.speed));
    renderData.moveSpeedMs = static_cast<uint16_t>(std::max(1, info.moveAnim.speed));
    renderData.numIdleFrames = static_cast<uint8_t>(std::max(1, info.idleAnim.frameCount));
    renderData.numMoveFrames = static_cast<uint8_t>(std::max(1, info.moveAnim.frameCount));
    renderData.idleRow = static_cast<uint8_t>(info.idleAnim.row);
    renderData.moveRow = static_cast<uint8_t>(info.moveAnim.row);
    renderData.currentFrame = 0;
    renderData.animationAccumulator = 0.0f;
    renderData.flipMode = 0;

    ENTITY_DEBUG(std::format("Created NPC '{}' at ({},{}) atlas({},{}) {}x{}",
                            npcType, position.getX(), position.getY(),
                            info.atlasX, info.atlasY, frameWidth, frameHeight));

    return handle;
}

const NPCTypeInfo* EntityDataManager::getNPCTypeInfo(const std::string& npcType) const {
    auto it = m_npcTypeRegistry.find(npcType);
    return (it != m_npcTypeRegistry.end()) ? &it->second : nullptr;
}

EntityHandle EntityDataManager::createDroppedItem(const Vector2D& position,
                                                  HammerEngine::ResourceHandle resourceHandle,
                                                  int quantity,
                                                  const std::string& worldId) {
    // Validation: Invalid resource handle
    if (!resourceHandle.isValid()) {
        ENTITY_ERROR("createDroppedItem: Invalid resource handle");
        return EntityHandle{};
    }

    // Validation: Quantity must be positive
    if (quantity <= 0) {
        ENTITY_ERROR(std::format("createDroppedItem: Invalid quantity {}", quantity));
        return EntityHandle{};
    }

    // Validation: Clamp to reasonable max (will be refined when RTM integration is added)
    static constexpr int MAX_STACK_SIZE = 999;
    if (quantity > MAX_STACK_SIZE) {
        ENTITY_WARN(std::format("createDroppedItem: Clamping quantity {} to max {}", quantity, MAX_STACK_SIZE));
        quantity = MAX_STACK_SIZE;
    }

    // Allocate in STATIC pool (resources don't move, not in tier system)
    size_t index;
    if (!m_freeStaticSlots.empty()) {
        index = m_freeStaticSlots.back();
        m_freeStaticSlots.pop_back();
    } else {
        index = m_staticHotData.size();
        m_staticHotData.emplace_back();
        m_staticEntityIds.push_back(0);
        m_staticGenerations.push_back(0);
    }

    EntityHandle::IDType id = HammerEngine::UniqueID::generate();
    uint8_t generation = ++m_staticGenerations[index];

    // Initialize hot data in STATIC pool
    auto& hot = m_staticHotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = Vector2D{0.0f, 0.0f};
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = 8.0f;
    hot.halfHeight = 8.0f;
    hot.kind = EntityKind::DroppedItem;
    markKindDirty(EntityKind::DroppedItem);
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;

    // DroppedItems use WRM spatial index for pickup detection, not collision system
    hot.collisionLayers = 0;  // No collision layers
    hot.collisionMask = 0;    // No collision mask
    hot.collisionFlags = 0;   // Collision disabled
    hot.triggerTag = 0;

    // Allocate item data and render data (keep indices in sync)
    uint32_t itemIndex;
    if (!m_freeItemSlots.empty()) {
        itemIndex = m_freeItemSlots.back();
        m_freeItemSlots.pop_back();
    } else {
        itemIndex = static_cast<uint32_t>(m_itemData.size());
        m_itemData.emplace_back();
        m_itemRenderData.emplace_back();  // Keep in sync with ItemData
    }

    // Initialize ItemData
    auto& item = m_itemData[itemIndex];
    item = ItemData{};  // Reset to default
    item.resourceHandle = resourceHandle;
    item.quantity = quantity;
    item.pickupTimer = 0.5f;
    item.bobTimer = 0.0f;
    item.flags = 0;
    hot.typeLocalIndex = itemIndex;

    // Initialize ItemRenderData
    // Ensure render data vector is sized correctly if reusing freed slot
    if (itemIndex >= m_itemRenderData.size()) {
        m_itemRenderData.resize(itemIndex + 1);
    }
    auto& renderData = m_itemRenderData[itemIndex];
    renderData.clear();

    // Get atlas texture (single texture for all items)
    renderData.cachedTexture = TextureManager::Instance().getTexturePtr("atlas");

    // Get atlas coords and animation data from resource template
    auto& rtm = ResourceTemplateManager::Instance();
    ResourcePtr resource = rtm.getResourceTemplate(resourceHandle);
    if (resource) {
        renderData.atlasX = static_cast<uint16_t>(resource->getAtlasX());
        renderData.atlasY = static_cast<uint16_t>(resource->getAtlasY());
        renderData.frameWidth = static_cast<uint16_t>(resource->getAtlasW());
        renderData.frameHeight = static_cast<uint16_t>(resource->getAtlasH());
        if (resource->getNumFrames() > 0) {
            renderData.numFrames = static_cast<uint8_t>(resource->getNumFrames());
        }
        if (resource->getAnimSpeed() > 0) {
            renderData.animSpeedMs = static_cast<uint16_t>(resource->getAnimSpeed());
        }
    }

    // Fallback if no atlas texture
    if (!renderData.cachedTexture) {
        ENTITY_WARN(std::format("createDroppedItem: Atlas texture not found for resource {}",
                                resourceHandle.toString()));
    }

    // Store ID and mapping in STATIC pool structures
    m_staticEntityIds[index] = id;
    m_staticIdToIndex[id] = index;

    // Update counters (NO tier count - statics not in tier system)
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::DroppedItem)].fetch_add(1, std::memory_order_relaxed);

    ENTITY_DEBUG(std::format("Created DroppedItem (static) entity {} with resource {} qty {} at ({}, {})",
                             id, resourceHandle.getId(), quantity, position.getX(), position.getY()));

    // Auto-register with WorldResourceManager for spatial queries
    auto& wrm = WorldResourceManager::Instance();
    const std::string& targetWorld = worldId.empty() ? wrm.getActiveWorld() : worldId;
    if (!targetWorld.empty()) {
        wrm.registerDroppedItem(index, position, targetWorld);
    } else {
        ENTITY_WARN("createDroppedItem: No active world set, item not registered for spatial queries");
    }

    return EntityHandle{id, EntityKind::DroppedItem, generation};
}

EntityHandle EntityDataManager::createContainer(const Vector2D& position,
                                                ContainerType containerType,
                                                uint16_t maxSlots,
                                                uint8_t lockLevel,
                                                const std::string& worldId) {
    // Validation: Valid container type
    if (static_cast<uint8_t>(containerType) >= static_cast<uint8_t>(ContainerType::COUNT)) {
        ENTITY_ERROR(std::format("createContainer: Invalid container type {}",
                                 static_cast<int>(containerType)));
        return EntityHandle{};
    }

    // Validation: Valid slot count
    if (maxSlots == 0 || maxSlots > 100) {
        ENTITY_ERROR(std::format("createContainer: Invalid slot count {}", maxSlots));
        return EntityHandle{};
    }

    // Validation: Clamp lock level
    if (lockLevel > 10) {
        ENTITY_WARN(std::format("createContainer: Clamping lock level {} to 10", lockLevel));
        lockLevel = 10;
    }

    // Auto-create inventory for this container
    uint32_t inventoryIndex = createInventory(maxSlots, false);  // Containers not world-tracked by default
    if (inventoryIndex == INVALID_INVENTORY_INDEX) {
        ENTITY_ERROR("createContainer: Failed to create inventory");
        return EntityHandle{};
    }

    // Allocate in STATIC pool (resources don't move, not in tier system)
    size_t index;
    if (!m_freeStaticSlots.empty()) {
        index = m_freeStaticSlots.back();
        m_freeStaticSlots.pop_back();
    } else {
        index = m_staticHotData.size();
        m_staticHotData.emplace_back();
        m_staticEntityIds.push_back(0);
        m_staticGenerations.push_back(0);
    }

    EntityHandle::IDType id = HammerEngine::UniqueID::generate();
    uint8_t generation = ++m_staticGenerations[index];

    // Initialize hot data in STATIC pool
    auto& hot = m_staticHotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = Vector2D{0.0f, 0.0f};
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = 16.0f;
    hot.halfHeight = 16.0f;
    hot.kind = EntityKind::Container;
    markKindDirty(EntityKind::Container);
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;

    // Container - no collision (use WRM spatial queries for interaction)
    hot.collisionLayers = 0;
    hot.collisionMask = 0;
    hot.collisionFlags = 0;
    hot.triggerTag = 0;

    // Allocate container data (reuse freed slot if available)
    uint32_t containerIndex;
    if (!m_freeContainerSlots.empty()) {
        containerIndex = m_freeContainerSlots.back();
        m_freeContainerSlots.pop_back();
    } else {
        containerIndex = static_cast<uint32_t>(m_containerData.size());
        m_containerData.emplace_back();
        m_containerRenderData.emplace_back();  // Keep in sync
    }

    // Initialize ContainerData
    auto& container = m_containerData[containerIndex];
    container.inventoryIndex = inventoryIndex;
    container.maxSlots = maxSlots;
    container.containerType = static_cast<uint8_t>(containerType);
    container.lockLevel = lockLevel;
    container.flags = (lockLevel > 0) ? ContainerData::FLAG_IS_LOCKED : 0;
    hot.typeLocalIndex = containerIndex;

    // Initialize ContainerRenderData
    if (containerIndex >= m_containerRenderData.size()) {
        m_containerRenderData.resize(containerIndex + 1);
    }
    auto& renderData = m_containerRenderData[containerIndex];
    renderData.clear();
    renderData.frameWidth = 32;
    renderData.frameHeight = 32;

    // Store ID and mapping in STATIC pool structures
    m_staticEntityIds[index] = id;
    m_staticIdToIndex[id] = index;

    // Update counters (NO tier count - statics not in tier system)
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::Container)].fetch_add(1, std::memory_order_relaxed);

    ENTITY_DEBUG(std::format("Created Container (static) entity {} type {} with {} slots at ({}, {})",
                             id, static_cast<int>(containerType), maxSlots,
                             position.getX(), position.getY()));

    // Auto-register container's inventory with WorldResourceManager
    auto& wrm = WorldResourceManager::Instance();
    const std::string& targetWorld = worldId.empty() ? wrm.getActiveWorld() : worldId;
    if (!targetWorld.empty()) {
        wrm.registerInventory(inventoryIndex, targetWorld);
        // Also register container spatially for rendering queries
        wrm.registerContainerSpatial(index, position, targetWorld);
    } else {
        ENTITY_WARN("createContainer: No active world set, inventory not registered with WRM");
    }

    return EntityHandle{id, EntityKind::Container, generation};
}

EntityHandle EntityDataManager::createHarvestable(const Vector2D& position,
                                                  HammerEngine::ResourceHandle yieldResource,
                                                  int yieldMin,
                                                  int yieldMax,
                                                  float respawnTime,
                                                  const std::string& worldId) {
    // Validation: Valid yield resource
    if (!yieldResource.isValid()) {
        ENTITY_ERROR("createHarvestable: Invalid yield resource handle");
        return EntityHandle{};
    }

    // Validation: Yield range sanity
    if (yieldMin < 0 || yieldMax < yieldMin) {
        ENTITY_ERROR(std::format("createHarvestable: Invalid yield range [{}, {}]",
                                 yieldMin, yieldMax));
        return EntityHandle{};
    }

    // Validation: Respawn time
    if (respawnTime < 0.0f) {
        ENTITY_WARN("createHarvestable: Negative respawn time, setting to 0");
        respawnTime = 0.0f;
    }

    // Allocate in STATIC pool (resources don't move, not in tier system)
    size_t index;
    if (!m_freeStaticSlots.empty()) {
        index = m_freeStaticSlots.back();
        m_freeStaticSlots.pop_back();
    } else {
        index = m_staticHotData.size();
        m_staticHotData.emplace_back();
        m_staticEntityIds.push_back(0);
        m_staticGenerations.push_back(0);
    }

    EntityHandle::IDType id = HammerEngine::UniqueID::generate();
    uint8_t generation = ++m_staticGenerations[index];

    // Initialize hot data in STATIC pool
    auto& hot = m_staticHotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = Vector2D{0.0f, 0.0f};
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = 16.0f;
    hot.halfHeight = 16.0f;
    hot.kind = EntityKind::Harvestable;
    markKindDirty(EntityKind::Harvestable);
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;

    // Harvestable - no collision (use WRM spatial queries for interaction)
    hot.collisionLayers = 0;
    hot.collisionMask = 0;
    hot.collisionFlags = 0;
    hot.triggerTag = 0;

    // Allocate harvestable data (reuse freed slot if available)
    uint32_t harvestableIndex;
    if (!m_freeHarvestableSlots.empty()) {
        harvestableIndex = m_freeHarvestableSlots.back();
        m_freeHarvestableSlots.pop_back();
    } else {
        harvestableIndex = static_cast<uint32_t>(m_harvestableData.size());
        m_harvestableData.emplace_back();
        m_harvestableRenderData.emplace_back();  // Keep in sync
    }

    // Initialize HarvestableData
    auto& harvestable = m_harvestableData[harvestableIndex];
    harvestable.yieldResource = yieldResource;
    harvestable.yieldMin = yieldMin;
    harvestable.yieldMax = yieldMax;
    harvestable.respawnTime = respawnTime;
    harvestable.currentRespawn = 0.0f;
    harvestable.harvestType = 0;  // Will be set by caller if needed
    harvestable.isDepleted = false;
    hot.typeLocalIndex = harvestableIndex;

    // Initialize HarvestableRenderData
    if (harvestableIndex >= m_harvestableRenderData.size()) {
        m_harvestableRenderData.resize(harvestableIndex + 1);
    }
    auto& renderData = m_harvestableRenderData[harvestableIndex];
    renderData.clear();
    renderData.frameWidth = 32;
    renderData.frameHeight = 32;

    // Store ID and mapping in STATIC pool structures
    m_staticEntityIds[index] = id;
    m_staticIdToIndex[id] = index;

    // Update counters (NO tier count - statics not in tier system)
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::Harvestable)].fetch_add(1, std::memory_order_relaxed);

    ENTITY_DEBUG(std::format("Created Harvestable (static) entity {} yielding {} [{}-{}] at ({}, {})",
                             id, yieldResource.getId(), yieldMin, yieldMax,
                             position.getX(), position.getY()));

    // Auto-register with WorldResourceManager for both registry and spatial queries
    auto& wrm = WorldResourceManager::Instance();
    const std::string& targetWorld = worldId.empty() ? wrm.getActiveWorld() : worldId;
    if (!targetWorld.empty()) {
        wrm.registerHarvestable(index, targetWorld);
        wrm.registerHarvestableSpatial(index, position, targetWorld);
    } else {
        ENTITY_WARN("createHarvestable: No active world set, harvestable not registered with WRM");
    }

    return EntityHandle{id, EntityKind::Harvestable, generation};
}

EntityHandle EntityDataManager::createProjectile(const Vector2D& position,
                                                 const Vector2D& velocity,
                                                 EntityHandle owner,
                                                 float damage,
                                                 float lifetime) {

    size_t index = allocateSlot();
    EntityHandle::IDType id = HammerEngine::UniqueID::generate();
    uint8_t generation = nextGeneration(index);

    // Initialize hot data
    auto& hot = m_hotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = velocity;
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = 4.0f;
    hot.halfHeight = 4.0f;
    hot.kind = EntityKind::Projectile;
    markKindDirty(EntityKind::Projectile);
    hot.tier = SimulationTier::Active;  // Projectiles always active
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;

    // Initialize collision data (Projectiles collide with enemies and environment)
    hot.collisionLayers = HammerEngine::CollisionLayer::Layer_Projectile;
    hot.collisionMask = HammerEngine::CollisionLayer::Layer_Enemy |
                        HammerEngine::CollisionLayer::Layer_Environment;
    hot.collisionFlags = EntityHotData::COLLISION_ENABLED;
    hot.triggerTag = 0;

    // Allocate projectile data (reuse freed slot if available)
    uint32_t projIndex;
    if (!m_freeProjectileSlots.empty()) {
        projIndex = m_freeProjectileSlots.back();
        m_freeProjectileSlots.pop_back();
    } else {
        projIndex = static_cast<uint32_t>(m_projectileData.size());
        m_projectileData.emplace_back();
    }
    auto& proj = m_projectileData[projIndex];
    proj = ProjectileData{};  // Reset to default
    proj.owner = owner;
    proj.damage = damage;
    proj.lifetime = lifetime;
    proj.speed = velocity.length();
    proj.damageType = 0;
    proj.flags = 0;
    hot.typeLocalIndex = projIndex;

    // Store ID and mapping
    m_entityIds[index] = id;
    m_generations[index] = generation;
    m_idToIndex[id] = index;

    // Update counters
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::Projectile)].fetch_add(1, std::memory_order_relaxed);
    m_countByTier[static_cast<size_t>(SimulationTier::Active)].fetch_add(1, std::memory_order_relaxed);
    m_tierIndicesDirty = true;

    return EntityHandle{id, EntityKind::Projectile, generation};
}

EntityHandle EntityDataManager::createAreaEffect(const Vector2D& position,
                                                 float radius,
                                                 EntityHandle owner,
                                                 float damage,
                                                 float duration) {

    size_t index = allocateSlot();
    EntityHandle::IDType id = HammerEngine::UniqueID::generate();
    uint8_t generation = nextGeneration(index);

    // Initialize hot data
    auto& hot = m_hotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = Vector2D{0.0f, 0.0f};
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = radius;
    hot.halfHeight = radius;
    hot.kind = EntityKind::AreaEffect;
    markKindDirty(EntityKind::AreaEffect);
    hot.tier = SimulationTier::Active;
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;

    // Allocate area effect data (reuse freed slot if available)
    uint32_t effectIndex;
    if (!m_freeAreaEffectSlots.empty()) {
        effectIndex = m_freeAreaEffectSlots.back();
        m_freeAreaEffectSlots.pop_back();
    } else {
        effectIndex = static_cast<uint32_t>(m_areaEffectData.size());
        m_areaEffectData.emplace_back();
    }
    auto& effect = m_areaEffectData[effectIndex];
    effect = AreaEffectData{};  // Reset to default
    effect.owner = owner;
    effect.radius = radius;
    effect.damage = damage;
    effect.tickInterval = 0.5f;
    effect.duration = duration;
    effect.elapsed = 0.0f;
    effect.lastTick = 0.0f;
    effect.effectType = 0;
    hot.typeLocalIndex = effectIndex;

    // Store ID and mapping
    m_entityIds[index] = id;
    m_generations[index] = generation;
    m_idToIndex[id] = index;

    // Update counters
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::AreaEffect)].fetch_add(1, std::memory_order_relaxed);
    m_countByTier[static_cast<size_t>(SimulationTier::Active)].fetch_add(1, std::memory_order_relaxed);
    m_tierIndicesDirty = true;

    return EntityHandle{id, EntityKind::AreaEffect, generation};
}

EntityHandle EntityDataManager::createStaticBody(const Vector2D& position,
                                                  float halfWidth,
                                                  float halfHeight) {

    // Allocate slot in static storage (separate from dynamic m_hotData)
    size_t index;
    if (!m_freeStaticSlots.empty()) {
        index = m_freeStaticSlots.back();
        m_freeStaticSlots.pop_back();
    } else {
        index = m_staticHotData.size();
        m_staticHotData.emplace_back();
        m_staticEntityIds.push_back(0);
        m_staticGenerations.push_back(0);
    }

    EntityHandle::IDType id = HammerEngine::UniqueID::generate();
    uint8_t generation = ++m_staticGenerations[index];

    // Initialize static hot data
    auto& hot = m_staticHotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = Vector2D{0.0f, 0.0f};
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = halfWidth;
    hot.halfHeight = halfHeight;
    hot.kind = EntityKind::StaticObstacle;
    markKindDirty(EntityKind::StaticObstacle);
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;
    hot.typeLocalIndex = 0;

    // Store ID and mapping (separate from dynamic entities)
    m_staticEntityIds[index] = id;
    m_staticIdToIndex[id] = index;

    // Update counters (only kind count, no tier count - statics not in tier system)
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::StaticObstacle)].fetch_add(1, std::memory_order_relaxed);

    return EntityHandle{id, EntityKind::StaticObstacle, generation};
}

EntityHandle EntityDataManager::createTrigger(const Vector2D& position,
                                               float halfWidth,
                                               float halfHeight,
                                               HammerEngine::TriggerTag tag,
                                               HammerEngine::TriggerType type) {
    // Allocate slot in static storage (triggers don't move)
    size_t index;
    if (!m_freeStaticSlots.empty()) {
        index = m_freeStaticSlots.back();
        m_freeStaticSlots.pop_back();
    } else {
        index = m_staticHotData.size();
        m_staticHotData.emplace_back();
        m_staticEntityIds.push_back(0);
        m_staticGenerations.push_back(0);
    }

    EntityHandle::IDType id = HammerEngine::UniqueID::generate();
    uint8_t generation = ++m_staticGenerations[index];

    // Initialize trigger hot data
    auto& hot = m_staticHotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = Vector2D{0.0f, 0.0f};
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = halfWidth;
    hot.halfHeight = halfHeight;
    hot.kind = EntityKind::Trigger;
    markKindDirty(EntityKind::Trigger);
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;
    hot.typeLocalIndex = 0;

    // Set collision data for trigger
    hot.collisionLayers = HammerEngine::CollisionLayer::Layer_Environment;
    hot.collisionMask = 0xFFFF;  // Collides with all layers
    hot.setCollisionEnabled(true);
    hot.setTrigger(true);
    hot.triggerTag = static_cast<uint8_t>(tag);
    hot.triggerType = static_cast<uint8_t>(type);

    // Store ID and mapping
    m_staticEntityIds[index] = id;
    m_staticIdToIndex[id] = index;

    // Update counters
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::Trigger)].fetch_add(1, std::memory_order_relaxed);

    return EntityHandle{id, EntityKind::Trigger, generation};
}

// ============================================================================
// PHASE 1: REGISTRATION OF EXISTING ENTITIES (Parallel Storage)
// ============================================================================

EntityHandle EntityDataManager::registerPlayer(EntityHandle::IDType entityId,
                                               const Vector2D& position,
                                               float halfWidth,
                                               float halfHeight) {
    if (entityId == 0) {
        ENTITY_ERROR("registerPlayer: Invalid entity ID (0)");
        return INVALID_ENTITY_HANDLE;
    }


    // Check if already registered
    if (m_idToIndex.find(entityId) != m_idToIndex.end()) {
        ENTITY_WARN(std::format("registerPlayer: Entity {} already registered", entityId));
        size_t existingIndex = m_idToIndex[entityId];
        return EntityHandle{entityId, EntityKind::Player, m_generations[existingIndex]};
    }

    size_t index = allocateSlot();
    uint8_t generation = nextGeneration(index);

    // Initialize hot data
    auto& hot = m_hotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = Vector2D{0.0f, 0.0f};
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = halfWidth;
    hot.halfHeight = halfHeight;
    hot.kind = EntityKind::Player;
    markKindDirty(EntityKind::Player);
    hot.tier = SimulationTier::Active;  // Player always active
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;

    // Initialize collision data (Player collides with enemies, environment, triggers)
    hot.collisionLayers = HammerEngine::CollisionLayer::Layer_Player;
    hot.collisionMask = HammerEngine::CollisionLayer::Layer_Enemy |
                        HammerEngine::CollisionLayer::Layer_Environment |
                        HammerEngine::CollisionLayer::Layer_Trigger |
                        HammerEngine::CollisionLayer::Layer_Default;
    hot.collisionFlags = EntityHotData::COLLISION_ENABLED | EntityHotData::NEEDS_TRIGGER_DETECTION;
    hot.triggerTag = 0;

    // Allocate character data (CharacterData + NPCRenderData stay in sync)
    uint32_t charIndex = allocateCharacterSlot();
    auto& charData = m_characterData[charIndex];
    charData.health = 100.0f;
    charData.maxHealth = 100.0f;
    charData.stamina = 100.0f;
    charData.maxStamina = 100.0f;
    charData.attackDamage = 25.0f;
    charData.attackRange = 50.0f;
    charData.stateFlags = CharacterData::STATE_ALIVE;
    hot.typeLocalIndex = charIndex;

    // Store ID and mapping
    m_entityIds[index] = entityId;
    m_generations[index] = generation;
    m_idToIndex[entityId] = index;

    // Update counters
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::Player)].fetch_add(1, std::memory_order_relaxed);
    m_countByTier[static_cast<size_t>(SimulationTier::Active)].fetch_add(1, std::memory_order_relaxed);
    m_tierIndicesDirty = true;

    ENTITY_INFO(std::format("Registered Player entity {} at ({}, {})",
                           entityId, position.getX(), position.getY()));

    return EntityHandle{entityId, EntityKind::Player, generation};
}

EntityHandle EntityDataManager::registerDroppedItem(EntityHandle::IDType entityId,
                                                    const Vector2D& position,
                                                    HammerEngine::ResourceHandle resourceHandle,
                                                    int quantity) {
    if (entityId == 0) {
        ENTITY_ERROR("registerDroppedItem: Invalid entity ID (0)");
        return INVALID_ENTITY_HANDLE;
    }

    // Check if already registered in static pool
    if (m_staticIdToIndex.find(entityId) != m_staticIdToIndex.end()) {
        ENTITY_WARN(std::format("registerDroppedItem: Entity {} already registered", entityId));
        size_t existingIndex = m_staticIdToIndex[entityId];
        return EntityHandle{entityId, EntityKind::DroppedItem, m_staticGenerations[existingIndex]};
    }

    // Allocate in STATIC pool (resources are static entities)
    size_t index;
    if (!m_freeStaticSlots.empty()) {
        index = m_freeStaticSlots.back();
        m_freeStaticSlots.pop_back();
    } else {
        index = m_staticHotData.size();
        m_staticHotData.emplace_back();
        m_staticEntityIds.push_back(0);
        m_staticGenerations.push_back(0);
    }

    uint8_t generation = m_staticGenerations[index];
    ++generation;
    if (generation == 0) generation = 1;
    m_staticGenerations[index] = generation;

    // Initialize hot data in static pool
    auto& hot = m_staticHotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = Vector2D{0.0f, 0.0f};
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = 8.0f;
    hot.halfHeight = 8.0f;
    hot.kind = EntityKind::DroppedItem;
    hot.tier = SimulationTier::Active;  // Not used for static, but set for consistency
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;

    // Allocate item data and render data (reuse freed slot if available)
    uint32_t itemIndex;
    if (!m_freeItemSlots.empty()) {
        itemIndex = m_freeItemSlots.back();
        m_freeItemSlots.pop_back();
    } else {
        itemIndex = static_cast<uint32_t>(m_itemData.size());
        m_itemData.emplace_back();
        m_itemRenderData.emplace_back();
    }
    auto& item = m_itemData[itemIndex];
    item = ItemData{};
    item.resourceHandle = resourceHandle;
    item.quantity = quantity;
    item.pickupTimer = 0.5f;
    item.bobTimer = 0.0f;
    item.flags = 0;
    hot.typeLocalIndex = itemIndex;

    // Ensure render data vector is sized correctly
    if (itemIndex >= m_itemRenderData.size()) {
        m_itemRenderData.resize(itemIndex + 1);
    }
    m_itemRenderData[itemIndex].clear();

    // Store ID and mapping in static pool
    m_staticEntityIds[index] = entityId;
    m_staticIdToIndex[entityId] = index;

    // Update counters (NO tier count for static entities)
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::DroppedItem)].fetch_add(1, std::memory_order_relaxed);

    ENTITY_DEBUG(std::format("Registered DroppedItem (static) entity {} at ({}, {})",
                            entityId, position.getX(), position.getY()));

    return EntityHandle{entityId, EntityKind::DroppedItem, generation};
}

void EntityDataManager::unregisterEntity(EntityHandle::IDType entityId) {
    if (entityId == 0) {
        return;
    }


    auto it = m_idToIndex.find(entityId);
    if (it == m_idToIndex.end()) {
        ENTITY_DEBUG(std::format("unregisterEntity: Entity {} not found", entityId));
        return;
    }

    size_t index = it->second;
    if (index >= m_hotData.size()) {
        return;
    }

    EntityKind kind = m_hotData[index].kind;
    SimulationTier tier = m_hotData[index].tier;

    // Update counters
    m_totalEntityCount.fetch_sub(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(kind)].fetch_sub(1, std::memory_order_relaxed);
    m_countByTier[static_cast<size_t>(tier)].fetch_sub(1, std::memory_order_relaxed);
    m_tierIndicesDirty = true;  // Remove destroyed entity from indices

    // Remove from ID mapping
    m_idToIndex.erase(it);

    // Free the slot
    freeSlot(index);

    ENTITY_DEBUG(std::format("Unregistered entity {}", entityId));
}

void EntityDataManager::destroyEntity(EntityHandle handle) {
    if (!handle.isValid()) {
        return;
    }

    // Route static resources to static destruction path (immediate, not queued)
    if (handle.kind == EntityKind::DroppedItem ||
        handle.kind == EntityKind::Container ||
        handle.kind == EntityKind::Harvestable) {
        destroyStaticResource(handle);
        return;
    }

    // Dynamic pool destruction (deferred queue)
    std::lock_guard<std::mutex> lock(m_destructionMutex);
    m_destructionQueue.push_back(handle);
}

void EntityDataManager::destroyStaticResource(EntityHandle handle) {
    // Find entity in static pool
    auto it = m_staticIdToIndex.find(handle.id);
    if (it == m_staticIdToIndex.end()) {
        ENTITY_WARN(std::format("destroyStaticResource: Entity {} not found in static pool", handle.id));
        return;
    }

    size_t index = it->second;
    if (index >= m_staticHotData.size()) {
        ENTITY_ERROR(std::format("destroyStaticResource: Invalid static index {} for entity {}", index, handle.id));
        return;
    }

    // Verify generation matches
    if (m_staticGenerations[index] != handle.generation) {
        ENTITY_DEBUG(std::format("destroyStaticResource: Stale handle for entity {}", handle.id));
        return;
    }

    // Unregister from WorldResourceManager
    auto& wrm = WorldResourceManager::Instance();
    switch (handle.kind) {
        case EntityKind::DroppedItem:
            wrm.unregisterDroppedItem(index);
            m_freeItemSlots.push_back(m_staticHotData[index].typeLocalIndex);
            break;
        case EntityKind::Harvestable:
            wrm.unregisterHarvestableSpatial(index);
            wrm.unregisterHarvestable(index);
            m_freeHarvestableSlots.push_back(m_staticHotData[index].typeLocalIndex);
            break;
        case EntityKind::Container:
            wrm.unregisterContainerSpatial(index);
            m_freeContainerSlots.push_back(m_staticHotData[index].typeLocalIndex);
            break;
        default:
            break;
    }

    // Mark slot free
    m_staticHotData[index].flags = 0;  // Clear FLAG_ALIVE
    m_freeStaticSlots.push_back(index);
    m_staticIdToIndex.erase(it);
    markKindDirty(handle.kind);

    // Update counters
    m_totalEntityCount.fetch_sub(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(handle.kind)].fetch_sub(1, std::memory_order_relaxed);

    ENTITY_DEBUG(std::format("Destroyed static resource entity {} (kind {})", handle.id, static_cast<int>(handle.kind)));
}

void EntityDataManager::processDestructionQueue() {
    // Use member buffer to avoid per-frame allocation
    m_destroyBuffer.clear();

    {
        std::lock_guard<std::mutex> lock(m_destructionMutex);
        std::swap(m_destroyBuffer, m_destructionQueue);
    }

    if (m_destroyBuffer.empty()) {
        return;
    }


    for (const auto& handle : m_destroyBuffer) {
        auto it = m_idToIndex.find(handle.id);
        if (it == m_idToIndex.end()) {
            continue;
        }

        size_t index = it->second;
        if (index >= m_hotData.size()) {
            continue;
        }

        // Verify generation matches
        if (m_generations[index] != handle.generation) {
            continue;
        }

        EntityKind kind = m_hotData[index].kind;
        SimulationTier tier = m_hotData[index].tier;

        // Update counters
        m_totalEntityCount.fetch_sub(1, std::memory_order_relaxed);
        m_countByKind[static_cast<size_t>(kind)].fetch_sub(1, std::memory_order_relaxed);
        m_countByTier[static_cast<size_t>(tier)].fetch_sub(1, std::memory_order_relaxed);

        // Remove from ID mapping
        m_idToIndex.erase(it);

        // Free the slot
        freeSlot(index);
    }

    // Mark tier indices dirty if any entities were destroyed
    if (!m_destroyBuffer.empty()) {
        m_tierIndicesDirty = true;
    }

    ENTITY_DEBUG(std::format("Processed {} entity destructions", m_destroyBuffer.size()));
}

// ============================================================================
// INVENTORY MANAGEMENT
// ============================================================================

uint32_t EntityDataManager::createInventory(uint16_t maxSlots, bool worldTracked) {
    // Validation: maxSlots must be positive
    if (maxSlots == 0) {
        ENTITY_ERROR("createInventory: maxSlots cannot be 0");
        return INVALID_INVENTORY_INDEX;
    }

    // Validation: reasonable upper bound
    static constexpr uint16_t MAX_REASONABLE_SLOTS = 1000;
    if (maxSlots > MAX_REASONABLE_SLOTS) {
        ENTITY_WARN(std::format("createInventory: Clamping {} slots to max {}",
                                maxSlots, MAX_REASONABLE_SLOTS));
        maxSlots = MAX_REASONABLE_SLOTS;
    }

    // Allocate slot from free-list or grow vector
    uint32_t inventoryIndex;
    if (!m_freeInventorySlots.empty()) {
        inventoryIndex = m_freeInventorySlots.back();
        m_freeInventorySlots.pop_back();
    } else {
        inventoryIndex = static_cast<uint32_t>(m_inventoryData.size());
        m_inventoryData.emplace_back();
    }

    auto& inv = m_inventoryData[inventoryIndex];
    inv.clear();
    inv.maxSlots = maxSlots;
    inv.setValid(true);
    inv.setWorldTracked(worldTracked);

    // Allocate overflow if needed
    if (inv.needsOverflow()) {
        inv.overflowId = m_nextOverflowId++;
        auto& overflow = m_inventoryOverflow[inv.overflowId];
        overflow.extraSlots.resize(maxSlots - InventoryData::INLINE_SLOT_COUNT);
    }

    ENTITY_DEBUG(std::format("Created inventory {} with {} slots (overflow: {})",
                             inventoryIndex, maxSlots, inv.overflowId > 0));
    return inventoryIndex;
}

void EntityDataManager::destroyInventory(uint32_t inventoryIndex) {
    if (!isValidInventoryIndex(inventoryIndex)) {
        return;
    }

    auto& inv = m_inventoryData[inventoryIndex];

    // Remove overflow if present
    if (inv.overflowId > 0) {
        m_inventoryOverflow.erase(inv.overflowId);
    }

    // Clear and mark invalid
    inv.clear();

    // Add to free-list for reuse
    m_freeInventorySlots.push_back(inventoryIndex);

    ENTITY_DEBUG(std::format("Destroyed inventory {}", inventoryIndex));
}

bool EntityDataManager::addToInventory(uint32_t inventoryIndex,
                                       HammerEngine::ResourceHandle handle,
                                       int quantity) {
    // Validation: valid inventory index (outside lock for quick fail)
    if (!isValidInventoryIndex(inventoryIndex)) {
        ENTITY_ERROR(std::format("addToInventory: Invalid inventory index {}", inventoryIndex));
        return false;
    }

    // Validation: valid resource handle
    if (!handle.isValid()) {
        ENTITY_ERROR("addToInventory: Invalid resource handle");
        return false;
    }

    // Validation: positive quantity
    if (quantity <= 0) {
        ENTITY_ERROR(std::format("addToInventory: Invalid quantity {}", quantity));
        return false;
    }

    // Get max stack size from ResourceTemplateManager (outside lock)
    auto& rtm = ResourceTemplateManager::Instance();
    int maxStack = rtm.isInitialized() ? rtm.getMaxStackSize(handle) : 99;
    if (maxStack <= 0) {
        maxStack = 99;  // Fallback for invalid stack size
    }

    // Lock for thread-safe inventory modification
    std::lock_guard<std::mutex> lock(m_inventoryMutex);

    auto& inv = m_inventoryData[inventoryIndex];
    InventoryOverflow* overflow = (inv.overflowId > 0) ? &m_inventoryOverflow[inv.overflowId] : nullptr;

    int remaining = quantity;

    // First pass: try to stack with existing slots of same type
    // Check inline slots
    for (size_t i = 0; i < InventoryData::INLINE_SLOT_COUNT && remaining > 0; ++i) {
        auto& slot = inv.slots[i];
        if (!slot.isEmpty() && slot.resourceHandle == handle) {
            int canAdd = maxStack - slot.quantity;
            if (canAdd > 0) {
                int toAdd = std::min(canAdd, remaining);
                slot.quantity += static_cast<int16_t>(toAdd);
                remaining -= toAdd;
            }
        }
    }

    // Check overflow slots
    if (overflow) {
        for (auto& slot : overflow->extraSlots) {
            if (remaining <= 0) break;
            if (!slot.isEmpty() && slot.resourceHandle == handle) {
                int canAdd = maxStack - slot.quantity;
                if (canAdd > 0) {
                    int toAdd = std::min(canAdd, remaining);
                    slot.quantity += static_cast<int16_t>(toAdd);
                    remaining -= toAdd;
                }
            }
        }
    }

    // Second pass: fill empty slots
    // Check inline slots
    for (size_t i = 0; i < InventoryData::INLINE_SLOT_COUNT && remaining > 0; ++i) {
        auto& slot = inv.slots[i];
        if (slot.isEmpty()) {
            int toAdd = std::min(maxStack, remaining);
            slot.resourceHandle = handle;
            slot.quantity = static_cast<int16_t>(toAdd);
            remaining -= toAdd;
            ++inv.usedSlots;
        }
    }

    // Check overflow slots
    if (overflow) {
        for (auto& slot : overflow->extraSlots) {
            if (remaining <= 0) break;
            if (slot.isEmpty()) {
                int toAdd = std::min(maxStack, remaining);
                slot.resourceHandle = handle;
                slot.quantity = static_cast<int16_t>(toAdd);
                remaining -= toAdd;
                ++inv.usedSlots;
            }
        }
    }

    if (remaining > 0) {
        ENTITY_WARN(std::format("addToInventory: Could not add {} items (inventory full)", remaining));
        return false;  // Couldn't fit everything
    }

    inv.flags |= InventoryData::FLAG_DIRTY;
    return true;
}

bool EntityDataManager::removeFromInventory(uint32_t inventoryIndex,
                                            HammerEngine::ResourceHandle handle,
                                            int quantity) {
    if (!isValidInventoryIndex(inventoryIndex)) {
        return false;
    }

    if (!handle.isValid() || quantity <= 0) {
        return false;
    }

    // Lock for thread-safe inventory modification
    std::lock_guard<std::mutex> lock(m_inventoryMutex);

    // Check if we have enough (under lock to avoid TOCTOU race)
    int available = getInventoryQuantityLocked(inventoryIndex, handle);
    if (available < quantity) {
        return false;
    }

    auto& inv = m_inventoryData[inventoryIndex];
    InventoryOverflow* overflow = (inv.overflowId > 0) ? &m_inventoryOverflow[inv.overflowId] : nullptr;

    int remaining = quantity;

    // Remove from inline slots first
    for (size_t i = 0; i < InventoryData::INLINE_SLOT_COUNT && remaining > 0; ++i) {
        auto& slot = inv.slots[i];
        if (!slot.isEmpty() && slot.resourceHandle == handle) {
            int toRemove = std::min(static_cast<int>(slot.quantity), remaining);
            slot.quantity -= static_cast<int16_t>(toRemove);
            remaining -= toRemove;
            if (slot.quantity <= 0) {
                slot.clear();
                --inv.usedSlots;
            }
        }
    }

    // Remove from overflow slots
    if (overflow) {
        for (auto& slot : overflow->extraSlots) {
            if (remaining <= 0) break;
            if (!slot.isEmpty() && slot.resourceHandle == handle) {
                int toRemove = std::min(static_cast<int>(slot.quantity), remaining);
                slot.quantity -= static_cast<int16_t>(toRemove);
                remaining -= toRemove;
                if (slot.quantity <= 0) {
                    slot.clear();
                    --inv.usedSlots;
                }
            }
        }
    }

    inv.flags |= InventoryData::FLAG_DIRTY;
    return true;
}

int EntityDataManager::getInventoryQuantity(uint32_t inventoryIndex,
                                            HammerEngine::ResourceHandle handle) const {
    if (!isValidInventoryIndex(inventoryIndex)) {
        return 0;
    }

    if (!handle.isValid()) {
        return 0;
    }

    // Lock for thread-safe read
    std::lock_guard<std::mutex> lock(m_inventoryMutex);
    return getInventoryQuantityLocked(inventoryIndex, handle);
}

int EntityDataManager::getInventoryQuantityLocked(uint32_t inventoryIndex,
                                                   HammerEngine::ResourceHandle handle) const {
    // Note: Caller MUST hold m_inventoryMutex
    // No validation here - caller already validated before acquiring lock

    const auto& inv = m_inventoryData[inventoryIndex];
    int total = 0;

    // Sum inline slots
    for (size_t i = 0; i < InventoryData::INLINE_SLOT_COUNT; ++i) {
        const auto& slot = inv.slots[i];
        if (!slot.isEmpty() && slot.resourceHandle == handle) {
            total += slot.quantity;
        }
    }

    // Sum overflow slots
    if (inv.overflowId > 0) {
        auto it = m_inventoryOverflow.find(inv.overflowId);
        if (it != m_inventoryOverflow.end()) {
            for (const auto& slot : it->second.extraSlots) {
                if (!slot.isEmpty() && slot.resourceHandle == handle) {
                    total += slot.quantity;
                }
            }
        }
    }

    return total;
}

bool EntityDataManager::hasInInventory(uint32_t inventoryIndex,
                                       HammerEngine::ResourceHandle handle,
                                       int quantity) const {
    return getInventoryQuantity(inventoryIndex, handle) >= quantity;
}

std::unordered_map<HammerEngine::ResourceHandle, int>
EntityDataManager::getInventoryResources(uint32_t inventoryIndex) const {
    std::unordered_map<HammerEngine::ResourceHandle, int> result;

    if (!isValidInventoryIndex(inventoryIndex)) {
        return result;
    }

    // Lock for thread-safe read
    std::lock_guard<std::mutex> lock(m_inventoryMutex);

    const auto& inv = m_inventoryData[inventoryIndex];

    // Sum inline slots
    for (size_t i = 0; i < InventoryData::INLINE_SLOT_COUNT; ++i) {
        const auto& slot = inv.slots[i];
        if (!slot.isEmpty()) {
            result[slot.resourceHandle] += slot.quantity;
        }
    }

    // Sum overflow slots
    if (inv.overflowId > 0) {
        auto it = m_inventoryOverflow.find(inv.overflowId);
        if (it != m_inventoryOverflow.end()) {
            for (const auto& slot : it->second.extraSlots) {
                if (!slot.isEmpty()) {
                    result[slot.resourceHandle] += slot.quantity;
                }
            }
        }
    }

    return result;
}

InventoryData& EntityDataManager::getInventoryData(uint32_t inventoryIndex) {
    assert(inventoryIndex < m_inventoryData.size() && "Inventory index out of bounds");
    return m_inventoryData[inventoryIndex];
}

const InventoryData& EntityDataManager::getInventoryData(uint32_t inventoryIndex) const {
    assert(inventoryIndex < m_inventoryData.size() && "Inventory index out of bounds");
    return m_inventoryData[inventoryIndex];
}

InventoryOverflow* EntityDataManager::getInventoryOverflow(uint32_t overflowId) {
    auto it = m_inventoryOverflow.find(overflowId);
    return (it != m_inventoryOverflow.end()) ? &it->second : nullptr;
}

const InventoryOverflow* EntityDataManager::getInventoryOverflow(uint32_t overflowId) const {
    auto it = m_inventoryOverflow.find(overflowId);
    return (it != m_inventoryOverflow.end()) ? &it->second : nullptr;
}

// ============================================================================
// HANDLE VALIDATION
// ============================================================================

bool EntityDataManager::isValidHandle(EntityHandle handle) const {
    if (!handle.isValid()) {
        return false;
    }

    // Check if this is a static pool entity (resources)
    if (handle.kind == EntityKind::DroppedItem ||
        handle.kind == EntityKind::Container ||
        handle.kind == EntityKind::Harvestable) {
        auto it = m_staticIdToIndex.find(handle.id);
        if (it == m_staticIdToIndex.end()) {
            return false;
        }

        size_t index = it->second;
        if (index >= m_staticHotData.size()) {
            return false;
        }

        return m_staticGenerations[index] == handle.generation &&
               m_staticHotData[index].isAlive();
    }

    // Dynamic pool lookup
    auto it = m_idToIndex.find(handle.id);
    if (it == m_idToIndex.end()) {
        return false;
    }

    size_t index = it->second;
    if (index >= m_hotData.size()) {
        return false;
    }

    return m_generations[index] == handle.generation &&
           m_hotData[index].isAlive();
}

size_t EntityDataManager::getIndex(EntityHandle handle) const {
    if (!handle.isValid()) {
        return SIZE_MAX;
    }

    // Route to correct pool based on entity kind
    if (handle.kind == EntityKind::DroppedItem ||
        handle.kind == EntityKind::Container ||
        handle.kind == EntityKind::Harvestable) {
        // Static pool lookup
        auto it = m_staticIdToIndex.find(handle.id);
        if (it == m_staticIdToIndex.end()) {
            return SIZE_MAX;
        }

        size_t index = it->second;
        if (index >= m_staticHotData.size() ||
            m_staticGenerations[index] != handle.generation) {
            return SIZE_MAX;
        }

        return index;
    }

    // Dynamic pool lookup
    auto it = m_idToIndex.find(handle.id);
    if (it == m_idToIndex.end()) {
        return SIZE_MAX;
    }

    size_t index = it->second;
    if (index >= m_hotData.size() || m_generations[index] != handle.generation) {
        return SIZE_MAX;
    }

    return index;
}

// ============================================================================
// TRANSFORM ACCESS
// ============================================================================

// For dynamic pool entities only (Player, NPC, Projectile, AreaEffect)
// Resources use getStaticHotDataByIndex().transform
TransformData& EntityDataManager::getTransform(EntityHandle handle) {
    assert(handle.kind != EntityKind::DroppedItem &&
           handle.kind != EntityKind::Container &&
           handle.kind != EntityKind::Harvestable &&
           "Resources use getStaticHotDataByIndex().transform");
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    return m_hotData[index].transform;
}

const TransformData& EntityDataManager::getTransform(EntityHandle handle) const {
    assert(handle.kind != EntityKind::DroppedItem &&
           handle.kind != EntityKind::Container &&
           handle.kind != EntityKind::Harvestable &&
           "Resources use getStaticHotDataByIndex().transform");
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    return m_hotData[index].transform;
}

// getTransformByIndex() is now inline in EntityDataManager.hpp

const TransformData& EntityDataManager::getStaticTransformByIndex(size_t index) const {
    assert(index < m_staticHotData.size() && "Static index out of bounds");
    return m_staticHotData[index].transform;
}

// ============================================================================
// HOT DATA ACCESS
// ============================================================================

EntityHotData& EntityDataManager::getHotData(EntityHandle handle) {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");

    // Route to correct pool based on entity kind
    if (handle.kind == EntityKind::DroppedItem ||
        handle.kind == EntityKind::Container ||
        handle.kind == EntityKind::Harvestable) {
        return m_staticHotData[index];
    }
    return m_hotData[index];
}

const EntityHotData& EntityDataManager::getHotData(EntityHandle handle) const {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");

    // Route to correct pool based on entity kind
    if (handle.kind == EntityKind::DroppedItem ||
        handle.kind == EntityKind::Container ||
        handle.kind == EntityKind::Harvestable) {
        return m_staticHotData[index];
    }
    return m_hotData[index];
}

// getHotDataByIndex() is now inline in EntityDataManager.hpp

std::span<const EntityHotData> EntityDataManager::getHotDataArray() const {
    return std::span<const EntityHotData>(m_hotData);
}

std::span<const EntityHotData> EntityDataManager::getStaticHotDataArray() const {
    return std::span<const EntityHotData>(m_staticHotData);
}

const EntityHotData& EntityDataManager::getStaticHotDataByIndex(size_t index) const {
    assert(index < m_staticHotData.size() && "Static index out of bounds");
    return m_staticHotData[index];
}

size_t EntityDataManager::getStaticIndex(EntityHandle handle) const {
    if (handle.kind != EntityKind::StaticObstacle) {
        return SIZE_MAX;
    }
    auto it = m_staticIdToIndex.find(handle.id);
    if (it == m_staticIdToIndex.end()) {
        return SIZE_MAX;
    }
    size_t index = it->second;
    if (index >= m_staticGenerations.size() || m_staticGenerations[index] != handle.generation) {
        return SIZE_MAX;
    }
    return index;
}

EntityHandle EntityDataManager::getStaticHandle(size_t staticIndex) const {
    if (staticIndex >= m_staticHotData.size()) {
        return EntityHandle{};  // Invalid
    }

    const auto& hot = m_staticHotData[staticIndex];
    if (!hot.isAlive()) {
        return EntityHandle{};
    }

    return EntityHandle{
        m_staticEntityIds[staticIndex],
        hot.kind,
        hot.generation
    };
}

// ============================================================================
// TYPE-SPECIFIC DATA ACCESS
// ============================================================================

CharacterData& EntityDataManager::getCharacterData(EntityHandle handle) {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.hasHealth() && "Entity does not have character data");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_characterData.size() && "Type index out of bounds");
    return m_characterData[typeIndex];
}

const CharacterData& EntityDataManager::getCharacterData(EntityHandle handle) const {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.hasHealth() && "Entity does not have character data");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_characterData.size() && "Type index out of bounds");
    return m_characterData[typeIndex];
}

// getCharacterDataByIndex() is now inline in EntityDataManager.hpp

ItemData& EntityDataManager::getItemData(EntityHandle handle) {
    assert(handle.isItem() && "Entity is not an item");

    // Items are in static pool
    auto it = m_staticIdToIndex.find(handle.id);
    assert(it != m_staticIdToIndex.end() && "Invalid item handle");
    size_t index = it->second;
    assert(index < m_staticHotData.size() && "Static index out of bounds");

    uint32_t typeIndex = m_staticHotData[index].typeLocalIndex;
    assert(typeIndex < m_itemData.size() && "Type index out of bounds");
    return m_itemData[typeIndex];
}

const ItemData& EntityDataManager::getItemData(EntityHandle handle) const {
    assert(handle.isItem() && "Entity is not an item");

    // Items are in static pool
    auto it = m_staticIdToIndex.find(handle.id);
    assert(it != m_staticIdToIndex.end() && "Invalid item handle");
    size_t index = it->second;
    assert(index < m_staticHotData.size() && "Static index out of bounds");

    uint32_t typeIndex = m_staticHotData[index].typeLocalIndex;
    assert(typeIndex < m_itemData.size() && "Type index out of bounds");
    return m_itemData[typeIndex];
}

ProjectileData& EntityDataManager::getProjectileData(EntityHandle handle) {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.isProjectile() && "Entity is not a projectile");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_projectileData.size() && "Type index out of bounds");
    return m_projectileData[typeIndex];
}

const ProjectileData& EntityDataManager::getProjectileData(EntityHandle handle) const {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.isProjectile() && "Entity is not a projectile");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_projectileData.size() && "Type index out of bounds");
    return m_projectileData[typeIndex];
}

ContainerData& EntityDataManager::getContainerData(EntityHandle handle) {
    assert(handle.getKind() == EntityKind::Container && "Entity is not a container");

    // Containers are in static pool
    auto it = m_staticIdToIndex.find(handle.id);
    assert(it != m_staticIdToIndex.end() && "Invalid container handle");
    size_t index = it->second;
    assert(index < m_staticHotData.size() && "Static index out of bounds");

    uint32_t typeIndex = m_staticHotData[index].typeLocalIndex;
    assert(typeIndex < m_containerData.size() && "Type index out of bounds");
    return m_containerData[typeIndex];
}

const ContainerData& EntityDataManager::getContainerData(EntityHandle handle) const {
    assert(handle.getKind() == EntityKind::Container && "Entity is not a container");

    // Containers are in static pool
    auto it = m_staticIdToIndex.find(handle.id);
    assert(it != m_staticIdToIndex.end() && "Invalid container handle");
    size_t index = it->second;
    assert(index < m_staticHotData.size() && "Static index out of bounds");

    uint32_t typeIndex = m_staticHotData[index].typeLocalIndex;
    assert(typeIndex < m_containerData.size() && "Type index out of bounds");
    return m_containerData[typeIndex];
}

HarvestableData& EntityDataManager::getHarvestableData(EntityHandle handle) {
    assert(handle.getKind() == EntityKind::Harvestable && "Entity is not harvestable");

    // Harvestables are in static pool
    auto it = m_staticIdToIndex.find(handle.id);
    assert(it != m_staticIdToIndex.end() && "Invalid harvestable handle");
    size_t index = it->second;
    assert(index < m_staticHotData.size() && "Static index out of bounds");

    uint32_t typeIndex = m_staticHotData[index].typeLocalIndex;
    assert(typeIndex < m_harvestableData.size() && "Type index out of bounds");
    return m_harvestableData[typeIndex];
}

const HarvestableData& EntityDataManager::getHarvestableData(EntityHandle handle) const {
    assert(handle.getKind() == EntityKind::Harvestable && "Entity is not harvestable");

    // Harvestables are in static pool
    auto it = m_staticIdToIndex.find(handle.id);
    assert(it != m_staticIdToIndex.end() && "Invalid harvestable handle");
    size_t index = it->second;
    assert(index < m_staticHotData.size() && "Static index out of bounds");

    uint32_t typeIndex = m_staticHotData[index].typeLocalIndex;
    assert(typeIndex < m_harvestableData.size() && "Type index out of bounds");
    return m_harvestableData[typeIndex];
}

AreaEffectData& EntityDataManager::getAreaEffectData(EntityHandle handle) {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.getKind() == EntityKind::AreaEffect && "Entity is not an area effect");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_areaEffectData.size() && "Type index out of bounds");
    return m_areaEffectData[typeIndex];
}

const AreaEffectData& EntityDataManager::getAreaEffectData(EntityHandle handle) const {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.getKind() == EntityKind::AreaEffect && "Entity is not an area effect");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_areaEffectData.size() && "Type index out of bounds");
    return m_areaEffectData[typeIndex];
}

// ============================================================================
// NPC RENDER DATA ACCESS
// ============================================================================

NPCRenderData& EntityDataManager::getNPCRenderData(EntityHandle handle) {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.getKind() == EntityKind::NPC && "Entity is not an NPC");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_npcRenderData.size() && "NPC render data type index out of bounds");
    return m_npcRenderData[typeIndex];
}

const NPCRenderData& EntityDataManager::getNPCRenderData(EntityHandle handle) const {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.getKind() == EntityKind::NPC && "Entity is not an NPC");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_npcRenderData.size() && "NPC render data type index out of bounds");
    return m_npcRenderData[typeIndex];
}

// ============================================================================
// PATH DATA ACCESS
// ============================================================================

// getPathData() is now inline in EntityDataManager.hpp

bool EntityDataManager::hasPathData(size_t index) const noexcept {
    return index < m_pathData.size();
}

void EntityDataManager::ensurePathData(size_t index) {
    // PathData is pre-allocated in allocateSlot() to avoid concurrent resize issues.
    // This check should never trigger during normal operation.
    assert(index < m_pathData.size() && "PathData not pre-allocated for index - allocation bug");
}

void EntityDataManager::clearPathData(size_t index) {
    if (index < m_pathData.size()) {
        m_pathData[index].clear();
        m_pathData[index].pathRequestPending.store(0, std::memory_order_relaxed);
    }
}

void EntityDataManager::finalizePath(size_t index, uint16_t length) noexcept {
    auto& pd = m_pathData[index];
    if (length == 0) {
        pd.clear();
        return;
    }
    pd.pathLength = std::min(length, static_cast<uint16_t>(FixedWaypointSlot::MAX_WAYPOINTS_PER_ENTITY));
    pd.navIndex = 0;
    pd.hasPath = true;
    pd.pathUpdateTimer = 0.0f;
    pd.progressTimer = 0.0f;
    pd.lastNodeDistance = std::numeric_limits<float>::max();
    pd.stallTimer = 0.0f;
    pd.pathRequestPending.store(0, std::memory_order_release);
    pd.currentWaypoint = m_waypointSlots[index][0];
}

void EntityDataManager::advanceWaypointWithCache(size_t index) {
    if (index >= m_pathData.size()) return;
    auto& pd = m_pathData[index];
    pd.advanceWaypoint();
    // Update cached waypoint from per-entity slot
    if (pd.navIndex < pd.pathLength) {
        pd.currentWaypoint = m_waypointSlots[index][pd.navIndex];
    }
}

// ============================================================================
// BEHAVIOR DATA ACCESS
// ============================================================================

// getBehaviorData() is now inline in EntityDataManager.hpp

bool EntityDataManager::hasBehaviorData(size_t index) const noexcept {
    return index < m_behaviorData.size() && m_behaviorData[index].isValid();
}

void EntityDataManager::initBehaviorData(size_t index, BehaviorType behaviorType) {
    assert(index < m_behaviorData.size() && "BehaviorData index out of bounds");
    auto& data = m_behaviorData[index];
    data.clear();
    data.behaviorType = behaviorType;
    data.setValid(true);
}

void EntityDataManager::clearBehaviorData(size_t index) {
    if (index < m_behaviorData.size()) {
        m_behaviorData[index].clear();
    }
}

// ============================================================================
// SIMULATION TIER MANAGEMENT
// ============================================================================

void EntityDataManager::setSimulationTier(EntityHandle handle, SimulationTier tier) {

    auto it = m_idToIndex.find(handle.id);
    if (it == m_idToIndex.end()) {
        return;
    }

    size_t index = it->second;
    if (index >= m_hotData.size() || m_generations[index] != handle.generation) {
        return;
    }

    SimulationTier oldTier = m_hotData[index].tier;
    if (oldTier == tier) {
        return;
    }

    m_hotData[index].tier = tier;

    // Update counters
    m_countByTier[static_cast<size_t>(oldTier)].fetch_sub(1, std::memory_order_relaxed);
    m_countByTier[static_cast<size_t>(tier)].fetch_add(1, std::memory_order_relaxed);

    m_tierIndicesDirty = true;
}

void EntityDataManager::updateSimulationTiers(const Vector2D& referencePoint,
                                              float activeRadius,
                                              float backgroundRadius) {

    const float activeRadiusSq = activeRadius * activeRadius;
    const float backgroundRadiusSq = backgroundRadius * backgroundRadius;

    for (size_t i = 0; i < m_hotData.size(); ++i) {
        auto& hot = m_hotData[i];
        if (!hot.isAlive()) {
            continue;
        }

        // Player always stays active
        if (hot.kind == EntityKind::Player) {
            continue;
        }

        // Calculate distance squared
        float dx = hot.transform.position.getX() - referencePoint.getX();
        float dy = hot.transform.position.getY() - referencePoint.getY();
        float distSq = dx * dx + dy * dy;

        SimulationTier newTier;
        if (distSq <= activeRadiusSq) {
            newTier = SimulationTier::Active;
        } else if (distSq <= backgroundRadiusSq) {
            newTier = SimulationTier::Background;
        } else {
            newTier = SimulationTier::Hibernated;
        }

        if (hot.tier != newTier) {
            m_countByTier[static_cast<size_t>(hot.tier)].fetch_sub(1, std::memory_order_relaxed);
            m_countByTier[static_cast<size_t>(newTier)].fetch_add(1, std::memory_order_relaxed);
            hot.tier = newTier;
            m_tierIndicesDirty = true;
        }
    }

    // Rebuild tier indices if dirty - single pass builds all derived indices
    if (m_tierIndicesDirty) {
        rebuildTierIndicesFromHotData();

#ifndef NDEBUG
        // Rolling log every 60 seconds using time-based check
        static auto lastLogTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime).count() >= 60) {
            lastLogTime = now;
            size_t tierTotal = m_activeIndices.size() + m_backgroundIndices.size() + m_hibernatedIndices.size();
            size_t dynamicCount = m_hotData.size();  // Only dynamic entities (statics in separate vector)

            // Count static entities by kind
            size_t resourceCount = 0, itemCount = 0, containerCount = 0, obstacleCount = 0;
            for (const auto& hot : m_staticHotData) {
                if (!hot.isAlive()) continue;
                switch (hot.kind) {
                    case EntityKind::Harvestable: ++resourceCount; break;
                    case EntityKind::DroppedItem: ++itemCount; break;
                    case EntityKind::Container: ++containerCount; break;
                    case EntityKind::StaticObstacle: ++obstacleCount; break;
                    default: break;
                }
            }

            ENTITY_DEBUG(std::format(
                "Tiers: Active={}, Background={}, Hibernated={} (Total={}, Dynamic={}, Statics={} [Res={}, Items={}, Cont={}, Obst={}])",
                m_activeIndices.size(), m_backgroundIndices.size(), m_hibernatedIndices.size(),
                tierTotal, dynamicCount, m_staticHotData.size(),
                resourceCount, itemCount, containerCount, obstacleCount));
        }
#endif
    }
}

void EntityDataManager::rebuildTierIndicesFromHotData() {
    m_activeIndices.clear();
    m_backgroundIndices.clear();
    m_hibernatedIndices.clear();
    // Build collision/trigger indices in same pass (avoid separate lazy rebuilds)
    m_activeCollisionIndices.clear();
    m_triggerDetectionIndices.clear();

    for (size_t i = 0; i < m_hotData.size(); ++i) {
        const auto& hot = m_hotData[i];
        if (!hot.isAlive()) {
            continue;
        }

        switch (hot.tier) {
            case SimulationTier::Active:
                m_activeIndices.push_back(i);
                // Build collision/trigger indices while iterating active entities
                if (hot.hasCollision()) {
                    m_activeCollisionIndices.push_back(i);
                }
                if (hot.needsTriggerDetection()) {
                    m_triggerDetectionIndices.push_back(i);
                }
                break;
            case SimulationTier::Background:
                m_backgroundIndices.push_back(i);
                break;
            case SimulationTier::Hibernated:
                m_hibernatedIndices.push_back(i);
                break;
        }
    }

    m_tierIndicesDirty = false;
    m_activeCollisionDirty = false;    // Built in same pass
    m_triggerDetectionDirty = false;   // Built in same pass
}

std::span<const size_t> EntityDataManager::getActiveIndices() const {
    if (m_tierIndicesDirty) {
        auto& self = const_cast<EntityDataManager&>(*this);
        self.rebuildTierIndicesFromHotData();
    }
    return std::span<const size_t>(m_activeIndices);
}

std::span<const size_t> EntityDataManager::getActiveIndicesWithCollision() const {
    if (m_tierIndicesDirty) {
        auto& self = const_cast<EntityDataManager&>(*this);
        self.rebuildTierIndicesFromHotData();
    }
    // Lazy rebuild when dirty (tier changes or collision flag changes)
    if (m_activeCollisionDirty) {
        m_activeCollisionIndices.clear();
        m_activeCollisionIndices.reserve(m_activeIndices.size() / 4); // ~25% have collision

        for (size_t idx : m_activeIndices) {
            if (m_hotData[idx].hasCollision()) {
                m_activeCollisionIndices.push_back(idx);
            }
        }
        m_activeCollisionDirty = false;
    }
    return std::span<const size_t>(m_activeCollisionIndices);
}

std::span<const size_t> EntityDataManager::getTriggerDetectionIndices() const {
    if (m_tierIndicesDirty) {
        auto& self = const_cast<EntityDataManager&>(*this);
        self.rebuildTierIndicesFromHotData();
    }
    // Lazy rebuild when dirty (tier changes or trigger detection flag changes)
    if (m_triggerDetectionDirty) {
        m_triggerDetectionIndices.clear();
        // Only Player has this flag by default, but NPCs can enable it too
        m_triggerDetectionIndices.reserve(16);

        for (size_t idx : m_activeIndices) {
            if (m_hotData[idx].needsTriggerDetection()) {
                m_triggerDetectionIndices.push_back(idx);
            }
        }
        m_triggerDetectionDirty = false;
    }
    return std::span<const size_t>(m_triggerDetectionIndices);
}

std::span<const size_t> EntityDataManager::getBackgroundIndices() const {
    if (m_tierIndicesDirty) {
        auto& self = const_cast<EntityDataManager&>(*this);
        self.rebuildTierIndicesFromHotData();
    }
    return std::span<const size_t>(m_backgroundIndices);
}

std::span<const size_t> EntityDataManager::getIndicesByKind(EntityKind kind) const {
    const size_t kindIdx = static_cast<size_t>(kind);

    // Only rebuild the requested kind if its dirty flag is set
    if (m_kindIndicesDirty[kindIdx]) {
        // Rebuild only this specific kind's indices (const_cast for lazy rebuild)
        auto& kindVec = const_cast<std::vector<size_t>&>(m_kindIndices[kindIdx]);
        kindVec.clear();

        for (size_t i = 0; i < m_hotData.size(); ++i) {
            if (m_hotData[i].isAlive() && m_hotData[i].kind == kind) {
                kindVec.push_back(i);
            }
        }

        m_kindIndicesDirty[kindIdx] = false;
    }

    return std::span<const size_t>(m_kindIndices[kindIdx]);
}

// ============================================================================
// QUERIES
// ============================================================================

void EntityDataManager::queryEntitiesInRadius(const Vector2D& center,
                                              float radius,
                                              std::vector<EntityHandle>& outHandles,
                                              EntityKind kindFilter) const {
    outHandles.clear();


    const float radiusSq = radius * radius;

    for (size_t i = 0; i < m_hotData.size(); ++i) {
        const auto& hot = m_hotData[i];
        if (!hot.isAlive()) {
            continue;
        }

        // Filter by kind if specified
        if (kindFilter != EntityKind::COUNT && hot.kind != kindFilter) {
            continue;
        }

        // Check distance
        float dx = hot.transform.position.getX() - center.getX();
        float dy = hot.transform.position.getY() - center.getY();
        float distSq = dx * dx + dy * dy;

        if (distSq <= radiusSq) {
            outHandles.emplace_back(m_entityIds[i], hot.kind, hot.generation);
        }
    }
}

size_t EntityDataManager::getEntityCount() const noexcept {
    return m_totalEntityCount.load(std::memory_order_relaxed);
}

size_t EntityDataManager::getEntityCount(EntityKind kind) const noexcept {
    return m_countByKind[static_cast<size_t>(kind)].load(std::memory_order_relaxed);
}

size_t EntityDataManager::getEntityCount(SimulationTier tier) const noexcept {
    return m_countByTier[static_cast<size_t>(tier)].load(std::memory_order_relaxed);
}

// ============================================================================
// ENTITY ID LOOKUP
// ============================================================================

EntityHandle::IDType EntityDataManager::getEntityId(size_t index) const {

    if (index >= m_entityIds.size()) {
        return 0;
    }
    return m_entityIds[index];
}

EntityHandle EntityDataManager::getHandle(size_t index) const {

    if (index >= m_hotData.size() || !m_hotData[index].isAlive()) {
        return INVALID_ENTITY_HANDLE;
    }

    return EntityHandle{
        m_entityIds[index],
        m_hotData[index].kind,
        m_hotData[index].generation
    };
}

// ============================================================================
// NPC TYPE REGISTRY
// ============================================================================

void EntityDataManager::initializeNPCTypeRegistry() {
    // Try to load NPC types from JSON (data-driven)
    const std::string jsonPath = "res/data/npc_types.json";
    JsonReader reader;

    if (reader.loadFromFile(jsonPath)) {
        const JsonValue& root = reader.getRoot();

        if (root.isObject() && root.hasKey("npcTypes") && root["npcTypes"].isArray()) {
            const JsonValue& npcTypes = root["npcTypes"];

            for (size_t i = 0; i < npcTypes.size(); ++i) {
                const JsonValue& npc = npcTypes[i];

                if (!npc.hasKey("id") || !npc["id"].isString()) {
                    ENTITY_WARN(std::format("NPC type at index {} missing 'id'", i));
                    continue;
                }

                std::string id = npc["id"].asString();
                std::string textureId = npc.hasKey("textureId") ? npc["textureId"].asString() : id;

                // Parse idle animation config
                AnimationConfig idleAnim{0, 1, 150};  // defaults
                if (npc.hasKey("idleAnim") && npc["idleAnim"].isObject()) {
                    const JsonValue& idle = npc["idleAnim"];
                    idleAnim.row = idle.hasKey("row") ? idle["row"].asInt() : 0;
                    idleAnim.frameCount = idle.hasKey("frameCount") ? idle["frameCount"].asInt() : 1;
                    idleAnim.speed = idle.hasKey("speed") ? idle["speed"].asInt() : 150;
                }

                // Parse move animation config
                AnimationConfig moveAnim{0, 2, 100};  // defaults
                if (npc.hasKey("moveAnim") && npc["moveAnim"].isObject()) {
                    const JsonValue& move = npc["moveAnim"];
                    moveAnim.row = move.hasKey("row") ? move["row"].asInt() : 0;
                    moveAnim.frameCount = move.hasKey("frameCount") ? move["frameCount"].asInt() : 2;
                    moveAnim.speed = move.hasKey("speed") ? move["speed"].asInt() : 100;
                }

                // Parse atlas coordinates
                uint16_t atlasX = npc.hasKey("atlasX") ? static_cast<uint16_t>(npc["atlasX"].asInt()) : 0;
                uint16_t atlasY = npc.hasKey("atlasY") ? static_cast<uint16_t>(npc["atlasY"].asInt()) : 0;
                uint16_t atlasW = npc.hasKey("atlasW") ? static_cast<uint16_t>(npc["atlasW"].asInt()) : 32;
                uint16_t atlasH = npc.hasKey("atlasH") ? static_cast<uint16_t>(npc["atlasH"].asInt()) : 32;

                m_npcTypeRegistry[id] = {textureId, idleAnim, moveAnim, atlasX, atlasY, atlasW, atlasH};
                ENTITY_DEBUG(std::format("Loaded NPC type '{}' atlas ({},{}) {}x{}", id, atlasX, atlasY, atlasW, atlasH));
            }

            ENTITY_INFO(std::format("Loaded {} NPC types from {}", m_npcTypeRegistry.size(), jsonPath));
            return;
        }
    }

    // Fallback to hardcoded defaults if JSON loading fails
    ENTITY_WARN(std::format("Failed to load NPC types from {}, using defaults", jsonPath));
    AnimationConfig idle{0, 1, 150};
    AnimationConfig move{0, 2, 100};

    // Fallback atlas coords from npc_types.json defaults
    m_npcTypeRegistry["Guard"]    = {"guard", idle, move, 165, 1126, 64, 32};
    m_npcTypeRegistry["Villager"] = {"villager", idle, move, 163, 1159, 64, 32};
    m_npcTypeRegistry["Merchant"] = {"merchant", idle, move, 230, 1126, 64, 32};
    m_npcTypeRegistry["Warrior"]  = {"warrior", idle, move, 228, 1159, 64, 32};

    ENTITY_INFO(std::format("Initialized NPC type registry with {} types (fallback)", m_npcTypeRegistry.size()));
}
