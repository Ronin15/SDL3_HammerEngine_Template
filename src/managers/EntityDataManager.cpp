/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/EntityDataManager.hpp"
#include "core/Logger.hpp"
#include "entities/Entity.hpp"  // For AnimationConfig
#include "managers/AIManager.hpp"  // For auto-registering NPCs with AI
#include "managers/ResourceTemplateManager.hpp"  // For getMaxStackSize in inventory
#include "managers/TextureManager.hpp"  // For texture lookup in creature creation
#include "managers/WorldResourceManager.hpp"  // For unregister on harvestable destruction
#include "utils/JsonReader.hpp"  // For loading NPC types from JSON
#include "utils/ResourcePath.hpp"  // For path resolution in JSON loading
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

        // NPC Memory data (indexed by edmIndex, pre-allocated alongside hotData)
        m_memoryData.reserve(CHARACTER_CAPACITY);

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

        // Initialize creature composition registries
        initializeRaceRegistry();
        initializeClassRegistry();
        initializeMonsterTypeRegistry();
        initializeMonsterVariantRegistry();
        initializeSpeciesRegistry();
        initializeAnimalRoleRegistry();

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
    m_memoryData.clear();
    m_memoryOverflow.clear();
    m_nextMemoryOverflowId = 1;

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
    m_memoryData.clear();
    m_memoryOverflow.clear();
    m_nextMemoryOverflowId = 1;

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
        // Pre-allocate PathData, WaypointSlot, BehaviorData, MemoryData to match - avoids concurrent resize during AI processing
        m_pathData.emplace_back();
        m_waypointSlots.emplace_back();  // Per-entity waypoint slot (256 bytes)
        m_behaviorData.emplace_back();
        m_memoryData.emplace_back();     // NPC memory data
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

    // Clear path, behavior, and memory data for AI entities
    clearPathData(index);
    clearBehaviorData(index);
    clearMemoryData(index);

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
            // Destroy the associated inventory first
            if (typeIndex < m_characterData.size()) {
                uint32_t invIdx = m_characterData[typeIndex].inventoryIndex;
                if (invIdx != INVALID_INVENTORY_INDEX) {
                    destroyInventory(invIdx);
                    m_characterData[typeIndex].inventoryIndex = INVALID_INVENTORY_INDEX;
                }
            }
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

    // Allocate character data first (needed for faction-based collision setup)
    uint32_t charIndex = allocateCharacterSlot();
    m_characterData[charIndex].stateFlags = CharacterData::STATE_ALIVE;
    // faction defaults to 0 (Friendly) in CharacterData

    // All NPCs get an inventory (20 slots, not world-tracked)
    uint32_t invIdx = createInventory(20, false);
    m_characterData[charIndex].inventoryIndex = invIdx;

    // Initialize collision data based on faction
    // Friendly/Neutral NPCs: Layer_Default, don't collide with other NPCs
    // Enemy NPCs: Layer_Enemy, can collide with other enemies
    uint8_t faction = m_characterData[charIndex].faction;
    if (faction == 1) {  // Enemy
        hot.collisionLayers = HammerEngine::CollisionLayer::Layer_Enemy;
        hot.collisionMask = HammerEngine::CollisionLayer::Layer_Player |
                            HammerEngine::CollisionLayer::Layer_Environment |
                            HammerEngine::CollisionLayer::Layer_Projectile |
                            HammerEngine::CollisionLayer::Layer_Enemy;
    } else {  // Friendly (0) or Neutral (2)
        hot.collisionLayers = HammerEngine::CollisionLayer::Layer_Default;
        hot.collisionMask = HammerEngine::CollisionLayer::Layer_Player |
                            HammerEngine::CollisionLayer::Layer_Environment |
                            HammerEngine::CollisionLayer::Layer_Projectile;
    }
    hot.collisionFlags = EntityHotData::COLLISION_ENABLED;
    hot.triggerTag = 0;
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

// ============================================================================
// CREATURE COMPOSITION CREATION
// ============================================================================

EntityHandle EntityDataManager::createNPCWithRaceClass(const Vector2D& position,
                                                        const std::string& race,
                                                        const std::string& charClass,
                                                        Sex sex,
                                                        uint8_t factionOverride) {
    // Look up race and class in registries
    auto raceIt = m_raceRegistry.find(race);
    if (raceIt == m_raceRegistry.end()) {
        ENTITY_ERROR(std::format("Unknown race '{}' - must be registered in races.json", race));
        return EntityHandle{};
    }

    auto classIt = m_classRegistry.find(charClass);
    if (classIt == m_classRegistry.end()) {
        ENTITY_ERROR(std::format("Unknown class '{}' - must be registered in classes.json", charClass));
        return EntityHandle{};
    }

    const RaceInfo& raceInfo = raceIt->second;
    const ClassInfo& classInfo = classIt->second;

    // Calculate frame dimensions from atlas region
    int maxFrames = std::max(raceInfo.idleAnim.frameCount, raceInfo.moveAnim.frameCount);
    uint16_t frameWidth = (maxFrames > 0) ? static_cast<uint16_t>(raceInfo.atlasW / maxFrames) : raceInfo.atlasW;
    uint16_t frameHeight = raceInfo.atlasH;

    // Calculate collision dimensions with size multiplier
    float halfWidth = static_cast<float>(frameWidth) * 0.5f * raceInfo.sizeMultiplier;
    float halfHeight = static_cast<float>(frameHeight) * 0.5f * raceInfo.sizeMultiplier;

    // Create NPC entity
    EntityHandle handle = createNPC(position, halfWidth, halfHeight);
    if (!handle.isValid()) {
        ENTITY_ERROR("createNPCWithRaceClass: Failed to create NPC entity");
        return INVALID_ENTITY_HANDLE;
    }

    // Get data indices
    size_t index = getIndex(handle);
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;

    // Set up CharacterData with computed stats (race base × class multiplier)
    auto& charData = m_characterData[typeIndex];
    charData.category = CreatureCategory::NPC;
    charData.sex = sex;
    charData.typeId = m_raceNameToId.count(race) ? m_raceNameToId[race] : 0;
    charData.subtypeId = m_classNameToId.count(charClass) ? m_classNameToId[charClass] : 0;
    charData.maxHealth = raceInfo.baseHealth * classInfo.healthMult;
    charData.health = charData.maxHealth;
    charData.maxStamina = raceInfo.baseStamina * classInfo.staminaMult;
    charData.stamina = charData.maxStamina;
    charData.attackDamage = raceInfo.baseAttackDamage * classInfo.attackDamageMult;
    charData.attackRange = raceInfo.baseAttackRange * classInfo.attackRangeMult;
    charData.moveSpeed = raceInfo.baseMoveSpeed * classInfo.moveSpeedMult;
    charData.priority = classInfo.basePriority;
    charData.faction = (factionOverride != 0xFF) ? factionOverride : classInfo.defaultFaction;
    charData.emotionalResilience = classInfo.emotionalResilience;

    // Apply faction-based collision layers
    applyFactionCollision(index, charData.faction);

    // Set up NPCRenderData from race info
    auto& renderData = m_npcRenderData[typeIndex];
    renderData.cachedTexture = TextureManager::Instance().getTexturePtr("atlas");
    if (!renderData.cachedTexture) {
        ENTITY_ERROR("createNPCWithRaceClass: Atlas texture not loaded");
    }

    // Check for unmapped texture (atlasX and atlasY both 0) - use debug texture as fallback
    if (raceInfo.atlasX == 0 && raceInfo.atlasY == 0) {
        ENTITY_WARN(std::format("Race '{}' has no atlas mapping, using debug fallback texture", race));
        renderData.cachedTexture = TextureManager::Instance().getTexturePtr("debug");
        renderData.atlasX = 0;
        renderData.atlasY = 0;
        renderData.frameWidth = 32;
        renderData.frameHeight = 32;
        renderData.idleSpeedMs = 150;
        renderData.moveSpeedMs = 100;
        renderData.numIdleFrames = 1;
        renderData.numMoveFrames = 1;
        renderData.idleRow = 0;
        renderData.moveRow = 0;
    } else {
        renderData.atlasX = raceInfo.atlasX;
        renderData.atlasY = raceInfo.atlasY;
        renderData.frameWidth = frameWidth;
        renderData.frameHeight = frameHeight;
        renderData.idleSpeedMs = static_cast<uint16_t>(std::max(1, raceInfo.idleAnim.speed));
        renderData.moveSpeedMs = static_cast<uint16_t>(std::max(1, raceInfo.moveAnim.speed));
        renderData.numIdleFrames = static_cast<uint8_t>(std::max(1, raceInfo.idleAnim.frameCount));
        renderData.numMoveFrames = static_cast<uint8_t>(std::max(1, raceInfo.moveAnim.frameCount));
        renderData.idleRow = static_cast<uint8_t>(raceInfo.idleAnim.row);
        renderData.moveRow = static_cast<uint8_t>(raceInfo.moveAnim.row);
    }
    renderData.currentFrame = 0;
    renderData.animationAccumulator = 0.0f;
    renderData.flipMode = 0;

    // Set merchant flag based on class
    if (classInfo.isMerchant) {
        charData.stateFlags |= CharacterData::STATE_MERCHANT;
    }

    // Add starting items from class definition
    if (!classInfo.startingItems.empty()) {
        auto& rtm = ResourceTemplateManager::Instance();
        for (const auto& [itemId, qty] : classInfo.startingItems) {
            auto itemHandle = rtm.getHandleById(itemId);
            if (itemHandle.isValid()) {
                addToInventory(charData.inventoryIndex, itemHandle, qty);
            } else {
                ENTITY_WARN(std::format("Starting item '{}' not found for class '{}'",
                                        itemId, classInfo.name));
            }
        }
    }

    ENTITY_DEBUG(std::format("Created {} {} at ({},{}) HP:{:.0f} DMG:{:.1f} SPD:{:.0f}{}",
                            race, charClass, position.getX(), position.getY(),
                            charData.maxHealth, charData.attackDamage, charData.moveSpeed,
                            classInfo.isMerchant ? " [Merchant]" : ""));

    // Auto-register with AIManager using class's suggested behavior
    AIManager::Instance().registerEntity(handle,
        classInfo.suggestedBehavior.empty() ? "Wander" : classInfo.suggestedBehavior);

    return handle;
}

EntityHandle EntityDataManager::createMonster(const Vector2D& position,
                                               const std::string& monsterType,
                                               const std::string& variant,
                                               Sex sex,
                                               uint8_t factionOverride) {
    // Look up type and variant in registries
    auto typeIt = m_monsterTypeRegistry.find(monsterType);
    if (typeIt == m_monsterTypeRegistry.end()) {
        ENTITY_ERROR(std::format("Unknown monster type '{}' - must be registered in monster_types.json", monsterType));
        return EntityHandle{};
    }

    auto variantIt = m_monsterVariantRegistry.find(variant);
    if (variantIt == m_monsterVariantRegistry.end()) {
        ENTITY_ERROR(std::format("Unknown monster variant '{}' - must be registered in monster_variants.json", variant));
        return EntityHandle{};
    }

    const MonsterTypeInfo& typeInfo = typeIt->second;
    const MonsterVariantInfo& variantInfo = variantIt->second;

    // Calculate frame dimensions
    int maxFrames = std::max(typeInfo.idleAnim.frameCount, typeInfo.moveAnim.frameCount);
    uint16_t frameWidth = (maxFrames > 0) ? static_cast<uint16_t>(typeInfo.atlasW / maxFrames) : typeInfo.atlasW;
    uint16_t frameHeight = typeInfo.atlasH;

    float halfWidth = static_cast<float>(frameWidth) * 0.5f * typeInfo.sizeMultiplier;
    float halfHeight = static_cast<float>(frameHeight) * 0.5f * typeInfo.sizeMultiplier;

    // Create NPC entity (monsters use same underlying entity type)
    EntityHandle handle = createNPC(position, halfWidth, halfHeight);
    if (!handle.isValid()) {
        ENTITY_ERROR("createMonster: Failed to create entity");
        return INVALID_ENTITY_HANDLE;
    }

    size_t index = getIndex(handle);
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;

    // Set up CharacterData
    auto& charData = m_characterData[typeIndex];
    charData.category = CreatureCategory::Monster;
    charData.sex = sex;
    charData.typeId = m_monsterTypeNameToId.count(monsterType) ? m_monsterTypeNameToId[monsterType] : 0;
    charData.subtypeId = m_monsterVariantNameToId.count(variant) ? m_monsterVariantNameToId[variant] : 0;
    charData.maxHealth = typeInfo.baseHealth * variantInfo.healthMult;
    charData.health = charData.maxHealth;
    charData.maxStamina = typeInfo.baseStamina * variantInfo.staminaMult;
    charData.stamina = charData.maxStamina;
    charData.attackDamage = typeInfo.baseAttackDamage * variantInfo.attackDamageMult;
    charData.attackRange = typeInfo.baseAttackRange * variantInfo.attackRangeMult;
    charData.moveSpeed = typeInfo.baseMoveSpeed * variantInfo.moveSpeedMult;
    charData.priority = variantInfo.basePriority;
    charData.faction = (factionOverride != 0xFF) ? factionOverride : typeInfo.defaultFaction;

    applyFactionCollision(index, charData.faction);

    // Set up render data
    auto& renderData = m_npcRenderData[typeIndex];
    renderData.cachedTexture = TextureManager::Instance().getTexturePtr("atlas");
    // Check for unmapped texture - use debug texture as fallback
    if (typeInfo.atlasX == 0 && typeInfo.atlasY == 0) {
        ENTITY_WARN(std::format("Monster type '{}' has no atlas mapping, using debug fallback texture", monsterType));
        renderData.cachedTexture = TextureManager::Instance().getTexturePtr("debug");
        renderData.atlasX = 0;
        renderData.atlasY = 0;
        renderData.frameWidth = 32;
        renderData.frameHeight = 32;
        renderData.idleSpeedMs = 150;
        renderData.moveSpeedMs = 100;
        renderData.numIdleFrames = 1;
        renderData.numMoveFrames = 1;
        renderData.idleRow = 0;
        renderData.moveRow = 0;
    } else {
        renderData.atlasX = typeInfo.atlasX;
        renderData.atlasY = typeInfo.atlasY;
        renderData.frameWidth = frameWidth;
        renderData.frameHeight = frameHeight;
        renderData.idleSpeedMs = static_cast<uint16_t>(std::max(1, typeInfo.idleAnim.speed));
        renderData.moveSpeedMs = static_cast<uint16_t>(std::max(1, typeInfo.moveAnim.speed));
        renderData.numIdleFrames = static_cast<uint8_t>(std::max(1, typeInfo.idleAnim.frameCount));
        renderData.numMoveFrames = static_cast<uint8_t>(std::max(1, typeInfo.moveAnim.frameCount));
        renderData.idleRow = static_cast<uint8_t>(typeInfo.idleAnim.row);
        renderData.moveRow = static_cast<uint8_t>(typeInfo.moveAnim.row);
    }
    renderData.currentFrame = 0;
    renderData.animationAccumulator = 0.0f;
    renderData.flipMode = 0;

    ENTITY_DEBUG(std::format("Created {} {} at ({},{}) HP:{:.0f} DMG:{:.1f}",
                            monsterType, variant, position.getX(), position.getY(),
                            charData.maxHealth, charData.attackDamage));

    return handle;
}

EntityHandle EntityDataManager::createAnimal(const Vector2D& position,
                                              const std::string& species,
                                              const std::string& role,
                                              Sex sex,
                                              uint8_t factionOverride) {
    // Look up species and role in registries
    auto speciesIt = m_speciesRegistry.find(species);
    if (speciesIt == m_speciesRegistry.end()) {
        ENTITY_ERROR(std::format("Unknown species '{}' - must be registered in species.json", species));
        return EntityHandle{};
    }

    auto roleIt = m_animalRoleRegistry.find(role);
    if (roleIt == m_animalRoleRegistry.end()) {
        ENTITY_ERROR(std::format("Unknown animal role '{}' - must be registered in animal_roles.json", role));
        return EntityHandle{};
    }

    const SpeciesInfo& speciesInfo = speciesIt->second;
    const AnimalRoleInfo& roleInfo = roleIt->second;

    // Calculate frame dimensions
    int maxFrames = std::max(speciesInfo.idleAnim.frameCount, speciesInfo.moveAnim.frameCount);
    uint16_t frameWidth = (maxFrames > 0) ? static_cast<uint16_t>(speciesInfo.atlasW / maxFrames) : speciesInfo.atlasW;
    uint16_t frameHeight = speciesInfo.atlasH;

    float halfWidth = static_cast<float>(frameWidth) * 0.5f * speciesInfo.sizeMultiplier;
    float halfHeight = static_cast<float>(frameHeight) * 0.5f * speciesInfo.sizeMultiplier;

    // Create NPC entity (animals use same underlying entity type)
    EntityHandle handle = createNPC(position, halfWidth, halfHeight);
    if (!handle.isValid()) {
        ENTITY_ERROR("createAnimal: Failed to create entity");
        return INVALID_ENTITY_HANDLE;
    }

    size_t index = getIndex(handle);
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;

    // Set up CharacterData
    auto& charData = m_characterData[typeIndex];
    charData.category = CreatureCategory::Animal;
    charData.sex = sex;
    charData.typeId = m_speciesNameToId.count(species) ? m_speciesNameToId[species] : 0;
    charData.subtypeId = m_animalRoleNameToId.count(role) ? m_animalRoleNameToId[role] : 0;
    charData.maxHealth = speciesInfo.baseHealth * roleInfo.healthMult;
    charData.health = charData.maxHealth;
    charData.maxStamina = speciesInfo.baseStamina * roleInfo.staminaMult;
    charData.stamina = charData.maxStamina;
    charData.attackDamage = speciesInfo.baseAttackDamage * roleInfo.attackDamageMult;
    charData.attackRange = speciesInfo.baseAttackRange;  // Animals don't have range multiplier
    charData.moveSpeed = speciesInfo.baseMoveSpeed * roleInfo.moveSpeedMult;
    charData.priority = roleInfo.basePriority;
    charData.faction = (factionOverride != 0xFF) ? factionOverride : roleInfo.defaultFaction;

    applyFactionCollision(index, charData.faction);

    // Set up render data
    auto& renderData = m_npcRenderData[typeIndex];
    renderData.cachedTexture = TextureManager::Instance().getTexturePtr("atlas");
    // Check for unmapped texture - use debug texture as fallback
    if (speciesInfo.atlasX == 0 && speciesInfo.atlasY == 0) {
        ENTITY_WARN(std::format("Species '{}' has no atlas mapping, using debug fallback texture", species));
        renderData.cachedTexture = TextureManager::Instance().getTexturePtr("debug");
        renderData.atlasX = 0;
        renderData.atlasY = 0;
        renderData.frameWidth = 32;
        renderData.frameHeight = 32;
        renderData.idleSpeedMs = 150;
        renderData.moveSpeedMs = 100;
        renderData.numIdleFrames = 1;
        renderData.numMoveFrames = 1;
        renderData.idleRow = 0;
        renderData.moveRow = 0;
    } else {
        renderData.atlasX = speciesInfo.atlasX;
        renderData.atlasY = speciesInfo.atlasY;
        renderData.frameWidth = frameWidth;
        renderData.frameHeight = frameHeight;
        renderData.idleSpeedMs = static_cast<uint16_t>(std::max(1, speciesInfo.idleAnim.speed));
        renderData.moveSpeedMs = static_cast<uint16_t>(std::max(1, speciesInfo.moveAnim.speed));
        renderData.numIdleFrames = static_cast<uint8_t>(std::max(1, speciesInfo.idleAnim.frameCount));
        renderData.numMoveFrames = static_cast<uint8_t>(std::max(1, speciesInfo.moveAnim.frameCount));
        renderData.idleRow = static_cast<uint8_t>(speciesInfo.idleAnim.row);
        renderData.moveRow = static_cast<uint8_t>(speciesInfo.moveAnim.row);
    }
    renderData.currentFrame = 0;
    renderData.animationAccumulator = 0.0f;
    renderData.flipMode = 0;

    ENTITY_DEBUG(std::format("Created {} {} at ({},{}) HP:{:.0f} SPD:{:.0f}",
                            species, role, position.getX(), position.getY(),
                            charData.maxHealth, charData.moveSpeed));

    return handle;
}

void EntityDataManager::applyFactionCollision(size_t index, uint8_t faction) {
    auto& hot = m_hotData[index];
    if (faction == 1) {  // Enemy
        hot.collisionLayers = HammerEngine::CollisionLayer::Layer_Enemy;
        hot.collisionMask = HammerEngine::CollisionLayer::Layer_Player |
                            HammerEngine::CollisionLayer::Layer_Environment |
                            HammerEngine::CollisionLayer::Layer_Projectile |
                            HammerEngine::CollisionLayer::Layer_Enemy;
    } else {  // Friendly (0) or Neutral (2)
        hot.collisionLayers = HammerEngine::CollisionLayer::Layer_Default;
        hot.collisionMask = HammerEngine::CollisionLayer::Layer_Player |
                            HammerEngine::CollisionLayer::Layer_Environment |
                            HammerEngine::CollisionLayer::Layer_Projectile;
    }
}

void EntityDataManager::setFaction(EntityHandle handle, uint8_t newFaction) {
    size_t index = getIndex(handle);
    if (index == SIZE_MAX) return;

    auto& charData = m_characterData[index];
    if (charData.faction == newFaction) return;  // No change

    charData.faction = newFaction;
    applyFactionCollision(index, newFaction);
}

// Registry getters
const RaceInfo* EntityDataManager::getRaceInfo(const std::string& race) const {
    auto it = m_raceRegistry.find(race);
    return (it != m_raceRegistry.end()) ? &it->second : nullptr;
}

const ClassInfo* EntityDataManager::getClassInfo(const std::string& charClass) const {
    auto it = m_classRegistry.find(charClass);
    return (it != m_classRegistry.end()) ? &it->second : nullptr;
}

std::vector<std::string> EntityDataManager::getRaceIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_raceRegistry.size());
    for (const auto& [id, _] : m_raceRegistry) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<std::string> EntityDataManager::getClassIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_classRegistry.size());
    for (const auto& [id, _] : m_classRegistry) {
        ids.push_back(id);
    }
    return ids;
}

const MonsterTypeInfo* EntityDataManager::getMonsterTypeInfo(const std::string& monsterType) const {
    auto it = m_monsterTypeRegistry.find(monsterType);
    return (it != m_monsterTypeRegistry.end()) ? &it->second : nullptr;
}

const MonsterVariantInfo* EntityDataManager::getMonsterVariantInfo(const std::string& variant) const {
    auto it = m_monsterVariantRegistry.find(variant);
    return (it != m_monsterVariantRegistry.end()) ? &it->second : nullptr;
}

const SpeciesInfo* EntityDataManager::getSpeciesInfo(const std::string& species) const {
    auto it = m_speciesRegistry.find(species);
    return (it != m_speciesRegistry.end()) ? &it->second : nullptr;
}

const AnimalRoleInfo* EntityDataManager::getAnimalRoleInfo(const std::string& role) const {
    auto it = m_animalRoleRegistry.find(role);
    return (it != m_animalRoleRegistry.end()) ? &it->second : nullptr;
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
    if (EntityTraits::usesStaticPool(handle.kind)) {
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

bool EntityDataManager::initNPCAsmerchant(EntityHandle handle, uint16_t maxSlots) {
    if (!handle.isValid() || handle.getKind() != EntityKind::NPC) {
        ENTITY_ERROR("initNPCAsmerchant: Invalid handle or not an NPC");
        return false;
    }

    size_t idx = getIndex(handle);
    if (idx == SIZE_MAX) {
        ENTITY_ERROR("initNPCAsmerchant: Entity not found in EDM");
        return false;
    }

    // Get character data
    auto& charData = getCharacterDataByIndex(idx);

    // Check if already has inventory
    if (charData.hasInventory()) {
        ENTITY_WARN("initNPCAsmerchant: NPC already has inventory");
        return true;  // Already set up
    }

    // Create inventory
    uint32_t invIdx = createInventory(maxSlots, false);  // Not world-tracked
    if (invIdx == INVALID_INVENTORY_INDEX) {
        ENTITY_ERROR("initNPCAsmerchant: Failed to create inventory");
        return false;
    }

    // Store inventory index and set merchant flag
    charData.inventoryIndex = invIdx;
    charData.stateFlags |= CharacterData::STATE_MERCHANT;

    ENTITY_INFO(std::format("NPC {} initialized as merchant with {} slots",
                            handle.getId(), maxSlots));
    return true;
}

bool EntityDataManager::isNPCMerchant(EntityHandle handle) const {
    if (!handle.isValid() || handle.getKind() != EntityKind::NPC) {
        return false;
    }

    size_t idx = getIndex(handle);
    if (idx == SIZE_MAX) {
        return false;
    }

    const auto& charData = getCharacterDataByIndex(idx);
    return charData.isMerchant() && charData.hasInventory();
}

uint32_t EntityDataManager::getNPCInventoryIndex(EntityHandle handle) const {
    if (!handle.isValid() || handle.getKind() != EntityKind::NPC) {
        return INVALID_INVENTORY_INDEX;
    }

    size_t idx = getIndex(handle);
    if (idx == SIZE_MAX) {
        return INVALID_INVENTORY_INDEX;
    }

    const auto& charData = getCharacterDataByIndex(idx);
    return charData.inventoryIndex;
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
    if (EntityTraits::usesStaticPool(handle.kind)) {
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
    if (EntityTraits::usesStaticPool(handle.kind)) {
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
    if (EntityTraits::usesStaticPool(handle.kind)) {
        return m_staticHotData[index];
    }
    return m_hotData[index];
}

const EntityHotData& EntityDataManager::getHotData(EntityHandle handle) const {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");

    // Route to correct pool based on entity kind
    if (EntityTraits::usesStaticPool(handle.kind)) {
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
// NPC MEMORY DATA MANAGEMENT
// ============================================================================

void EntityDataManager::initMemoryData(size_t index) {
    if (index >= m_memoryData.size()) {
        return;
    }
    auto& data = m_memoryData[index];
    data.clear();
    data.setValid(true);

    // Generate random personality traits for this NPC
    static thread_local std::mt19937 s_personalityRng{std::random_device{}()};
    data.personality.randomize(s_personalityRng);
}

void EntityDataManager::clearMemoryData(size_t index) {
    if (index >= m_memoryData.size()) {
        return;
    }
    auto& data = m_memoryData[index];

    // Clean up overflow if present
    if (data.hasOverflow() && data.overflowId > 0) {
        m_memoryOverflow.erase(data.overflowId);
    }

    data.clear();
}

void EntityDataManager::addMemory(size_t index, const MemoryEntry& entry, bool useOverflow) {
    if (index >= m_memoryData.size()) {
        return;
    }

    auto& data = m_memoryData[index];
    if (!data.isValid()) {
        initMemoryData(index);
    }

    // Try to add to inline slots (circular buffer)
    if (data.memoryCount < NPCMemoryData::INLINE_MEMORY_COUNT) {
        data.memories[data.nextInlineSlot] = entry;
        data.nextInlineSlot = (data.nextInlineSlot + 1) % NPCMemoryData::INLINE_MEMORY_COUNT;
        data.memoryCount++;
    } else if (useOverflow) {
        // Allocate overflow if needed
        if (!data.hasOverflow()) {
            data.overflowId = m_nextMemoryOverflowId++;
            data.flags |= NPCMemoryData::FLAG_HAS_OVERFLOW;
            m_memoryOverflow[data.overflowId] = MemoryOverflow{};
        }

        auto& overflow = m_memoryOverflow[data.overflowId];
        overflow.extraMemories.push_back(entry);
        overflow.trimToMax();
        data.memoryCount = static_cast<uint16_t>(
            NPCMemoryData::INLINE_MEMORY_COUNT + overflow.extraMemories.size());
    } else {
        // Overwrite oldest inline memory (circular buffer)
        data.memories[data.nextInlineSlot] = entry;
        data.nextInlineSlot = (data.nextInlineSlot + 1) % NPCMemoryData::INLINE_MEMORY_COUNT;
        // memoryCount stays at INLINE_MEMORY_COUNT
    }
}

void EntityDataManager::findMemoriesByType(size_t index, MemoryType type,
                                           std::vector<const MemoryEntry*>& outMemories,
                                           size_t maxResults) const {
    outMemories.clear();
    if (index >= m_memoryData.size()) {
        return;
    }

    const auto& data = m_memoryData[index];
    if (!data.isValid()) {
        return;
    }

    // Search inline memories
    for (size_t i = 0; i < NPCMemoryData::INLINE_MEMORY_COUNT; ++i) {
        const auto& mem = data.memories[i];
        if (mem.isValid() && mem.type == type) {
            outMemories.push_back(&mem);
            if (maxResults > 0 && outMemories.size() >= maxResults) {
                return;
            }
        }
    }

    // Search overflow if present
    if (data.hasOverflow() && data.overflowId > 0) {
        auto it = m_memoryOverflow.find(data.overflowId);
        if (it != m_memoryOverflow.end()) {
            for (const auto& mem : it->second.extraMemories) {
                if (mem.isValid() && mem.type == type) {
                    outMemories.push_back(&mem);
                    if (maxResults > 0 && outMemories.size() >= maxResults) {
                        return;
                    }
                }
            }
        }
    }
}

void EntityDataManager::findMemoriesOfEntity(size_t index, EntityHandle subject,
                                              std::vector<const MemoryEntry*>& outMemories) const {
    outMemories.clear();
    if (index >= m_memoryData.size()) {
        return;
    }

    const auto& data = m_memoryData[index];
    if (!data.isValid()) {
        return;
    }

    // Search inline memories
    for (const auto& mem : data.memories) {
        if (mem.isValid() && mem.subject == subject) {
            outMemories.push_back(&mem);
        }
    }

    // Search overflow
    if (data.hasOverflow() && data.overflowId > 0) {
        auto it = m_memoryOverflow.find(data.overflowId);
        if (it != m_memoryOverflow.end()) {
            for (const auto& mem : it->second.extraMemories) {
                if (mem.isValid() && mem.subject == subject) {
                    outMemories.push_back(&mem);
                }
            }
        }
    }
}

void EntityDataManager::updateEmotionalDecay(size_t index, float deltaTime, float decayRate) {
    if (index >= m_memoryData.size()) {
        return;
    }

    auto& data = m_memoryData[index];
    if (!data.isValid()) {
        return;
    }

    data.emotions.decay(decayRate, deltaTime);
    data.lastDecayTime += deltaTime;
}

void EntityDataManager::modifyEmotions(size_t index, float aggression, float fear,
                                        float curiosity, float suspicion) {
    if (index >= m_memoryData.size()) {
        return;
    }

    auto& data = m_memoryData[index];
    if (!data.isValid()) {
        return;
    }

    data.emotions.aggression = std::clamp(data.emotions.aggression + aggression, 0.0f, 1.0f);
    data.emotions.fear = std::clamp(data.emotions.fear + fear, 0.0f, 1.0f);
    data.emotions.curiosity = std::clamp(data.emotions.curiosity + curiosity, 0.0f, 1.0f);
    data.emotions.suspicion = std::clamp(data.emotions.suspicion + suspicion, 0.0f, 1.0f);
}

void EntityDataManager::recordCombatEvent(size_t index, EntityHandle attacker,
                                           EntityHandle target, float damage, bool wasAttacked,
                                           float gameTime) {
    if (index >= m_memoryData.size()) {
        return;
    }

    auto& memData = m_memoryData[index];
    if (!memData.isValid()) {
        initMemoryData(index);
    }

    // Get class resilience from CharacterData (typeLocalIndex lookup)
    float classResilience = 0.5f;  // Default if no character data
    if (index < m_hotData.size()) {
        uint32_t typeIndex = m_hotData[index].typeLocalIndex;
        if (typeIndex < m_characterData.size()) {
            classResilience = m_characterData[typeIndex].emotionalResilience;
        }
    }

    // Combined resilience from class + personality traits
    float effectiveResilience = memData.personality.getEffectiveResilience(classResilience);
    float emotionScale = 1.0f - effectiveResilience;  // High resilience = low emotion change

    // Update aggregate stats
    if (wasAttacked) {
        memData.lastAttacker = attacker;
        memData.totalDamageReceived += damage;

        // Fear increases based on damage, scaled by resilience and bravery
        // Bravery specifically reduces fear gain
        float fearScale = emotionScale * (1.0f - memData.personality.bravery * 0.5f);
        float fearIncrease = (damage / 100.0f) * fearScale;
        memData.emotions.fear = std::min(1.0f, memData.emotions.fear + fearIncrease);
    } else {
        memData.lastTarget = target;
        memData.totalDamageDealt += damage;

        // Aggression increases when dealing damage, scaled by resilience
        // Personality aggression trait boosts the effect
        float aggressionScale = emotionScale * (1.0f + memData.personality.aggression * 0.5f);
        float aggressionIncrease = (damage / 150.0f) * aggressionScale;
        memData.emotions.aggression = std::min(1.0f, memData.emotions.aggression + aggressionIncrease);
    }

    auto& data = memData;  // Alias for rest of function

    data.lastCombatTime = gameTime;
    data.combatEncounters++;
    data.flags |= NPCMemoryData::FLAG_IN_COMBAT;

    // Create memory entry
    MemoryEntry mem;
    mem.subject = wasAttacked ? attacker : target;
    if (index < m_hotData.size()) {
        mem.location = m_hotData[index].transform.position;
    }
    mem.timestamp = gameTime;
    mem.value = damage;
    mem.type = wasAttacked ? MemoryType::DamageReceived : MemoryType::DamageDealt;
    mem.importance = static_cast<uint8_t>(std::min(255.0f, damage * 2.0f));
    mem.flags = MemoryEntry::FLAG_VALID;

    addMemory(index, mem, true);  // Use overflow for combat (important history)
}

void EntityDataManager::addLocationToHistory(size_t index, const Vector2D& location) {
    if (index >= m_memoryData.size()) {
        return;
    }

    auto& data = m_memoryData[index];
    if (!data.isValid()) {
        return;
    }

    // Circular buffer for location history
    size_t slot = data.locationCount % NPCMemoryData::INLINE_LOCATION_COUNT;
    data.locationHistory[slot] = location;
    if (data.locationCount < NPCMemoryData::INLINE_LOCATION_COUNT) {
        data.locationCount++;
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

std::span<const size_t> EntityDataManager::getRenderableIndices() const {
    if (m_tierIndicesDirty || m_renderableIndicesDirty) {
        // Rebuild renderable indices (alive OR dying entities in Active tier)
        m_renderableIndices.clear();
        m_renderableIndices.reserve(m_hotData.size() / 4);  // Estimate ~25% are active

        for (size_t i = 0; i < m_hotData.size(); ++i) {
            const auto& hot = m_hotData[i];
            // Include entities that are alive OR dying (in death animation)
            if (!hot.isAlive() && !hot.isDying()) {
                continue;
            }
            // Only include Active tier entities
            if (hot.tier == SimulationTier::Active) {
                m_renderableIndices.push_back(i);
            }
        }
        m_renderableIndicesDirty = false;
    }
    return std::span<const size_t>(m_renderableIndices);
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

        std::copy_if(m_activeIndices.begin(), m_activeIndices.end(),
            std::back_inserter(m_activeCollisionIndices),
            [this](size_t idx) { return m_hotData[idx].hasCollision(); });
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

        std::copy_if(m_activeIndices.begin(), m_activeIndices.end(),
            std::back_inserter(m_triggerDetectionIndices),
            [this](size_t idx) { return m_hotData[idx].needsTriggerDetection(); });
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
// CREATURE COMPOSITION REGISTRY INITIALIZATION
// ============================================================================

void EntityDataManager::initializeRaceRegistry() {
    const std::string jsonPath = HammerEngine::ResourcePath::resolve("res/data/races.json");
    JsonReader reader;

    // Load atlas.json for coordinate lookup (following WorldManager pattern)
    JsonReader atlasReader;
    std::unordered_map<std::string, JsonValue> atlasRegions;
    if (atlasReader.loadFromFile(HammerEngine::ResourcePath::resolve("res/data/atlas.json"))) {
        const auto& atlasRoot = atlasReader.getRoot();
        if (atlasRoot.hasKey("regions")) {
            atlasRegions = atlasRoot["regions"].asObject();
        }
    }

    auto getAtlasCoords = [&atlasRegions](const std::string& texId)
        -> std::tuple<uint16_t, uint16_t, uint16_t, uint16_t> {
        auto it = atlasRegions.find(texId);
        if (it == atlasRegions.end()) return {0, 0, 64, 32};
        const auto& r = it->second;
        return {
            static_cast<uint16_t>(r["x"].asInt()),
            static_cast<uint16_t>(r["y"].asInt()),
            static_cast<uint16_t>(r["w"].asInt()),
            static_cast<uint16_t>(r["h"].asInt())
        };
    };

    if (reader.loadFromFile(jsonPath)) {
        const JsonValue& root = reader.getRoot();

        if (root.isObject() && root.hasKey("races") && root["races"].isArray()) {
            const JsonValue& races = root["races"];

            for (size_t i = 0; i < races.size(); ++i) {
                const JsonValue& r = races[i];

                if (!r.hasKey("id") || !r["id"].isString()) {
                    ENTITY_WARN(std::format("Race at index {} missing 'id'", i));
                    continue;
                }

                std::string id = r["id"].asString();
                RaceInfo info;
                info.name = r.hasKey("name") ? r["name"].asString() : id;

                // Base stats
                info.baseHealth = r.hasKey("baseHealth") ? static_cast<float>(r["baseHealth"].asNumber()) : 100.0f;
                info.baseStamina = r.hasKey("baseStamina") ? static_cast<float>(r["baseStamina"].asNumber()) : 100.0f;
                info.baseMoveSpeed = r.hasKey("baseMoveSpeed") ? static_cast<float>(r["baseMoveSpeed"].asNumber()) : 100.0f;
                info.baseAttackDamage = r.hasKey("baseAttackDamage") ? static_cast<float>(r["baseAttackDamage"].asNumber()) : 10.0f;
                info.baseAttackRange = r.hasKey("baseAttackRange") ? static_cast<float>(r["baseAttackRange"].asNumber()) : 50.0f;

                // Visual - look up atlas coords via textureId
                std::string texId = r.hasKey("textureId") ? r["textureId"].asString() : "";
                auto [ax, ay, aw, ah] = getAtlasCoords(texId);
                info.atlasX = ax;
                info.atlasY = ay;
                info.atlasW = aw;
                info.atlasH = ah;

                // Animations
                info.idleAnim = {0, 1, 150};
                if (r.hasKey("idleAnim") && r["idleAnim"].isObject()) {
                    const JsonValue& idle = r["idleAnim"];
                    info.idleAnim.row = idle.hasKey("row") ? idle["row"].asInt() : 0;
                    info.idleAnim.frameCount = idle.hasKey("frameCount") ? idle["frameCount"].asInt() : 1;
                    info.idleAnim.speed = idle.hasKey("speed") ? idle["speed"].asInt() : 150;
                }
                info.moveAnim = {0, 2, 100};
                if (r.hasKey("moveAnim") && r["moveAnim"].isObject()) {
                    const JsonValue& move = r["moveAnim"];
                    info.moveAnim.row = move.hasKey("row") ? move["row"].asInt() : 0;
                    info.moveAnim.frameCount = move.hasKey("frameCount") ? move["frameCount"].asInt() : 2;
                    info.moveAnim.speed = move.hasKey("speed") ? move["speed"].asInt() : 100;
                }

                info.sizeMultiplier = r.hasKey("sizeMultiplier") ? static_cast<float>(r["sizeMultiplier"].asNumber()) : 1.0f;

                // Store and create ID mapping
                uint8_t raceId = static_cast<uint8_t>(m_raceIdToName.size());
                m_raceRegistry[id] = info;
                m_raceNameToId[id] = raceId;
                m_raceIdToName.push_back(id);

                ENTITY_DEBUG(std::format("Loaded race '{}' HP:{} SPD:{}", id, info.baseHealth, info.baseMoveSpeed));
            }

            ENTITY_INFO(std::format("Loaded {} races from {}", m_raceRegistry.size(), jsonPath));
            return;
        }
    }

    // Fallback defaults
    ENTITY_WARN(std::format("Failed to load races from {}, using defaults", jsonPath));
    AnimationConfig idle{0, 1, 150};
    AnimationConfig move{0, 2, 100};

    // Atlas coordinates: human=295,1126 elf=65,1126 orc=0,1159 dwarf=0,1126
    m_raceRegistry["Human"] = {"Human", 100, 100, 100, 10, 50, 295, 1126, 64, 32, idle, move, 1.0f};
    m_raceNameToId["Human"] = 0; m_raceIdToName.push_back("Human");

    m_raceRegistry["Elf"] = {"Elf", 80, 120, 120, 8, 60, 65, 1126, 64, 32, idle, move, 0.9f};
    m_raceNameToId["Elf"] = 1; m_raceIdToName.push_back("Elf");

    m_raceRegistry["Orc"] = {"Orc", 150, 80, 80, 15, 45, 0, 1159, 64, 32, idle, move, 1.2f};
    m_raceNameToId["Orc"] = 2; m_raceIdToName.push_back("Orc");

    m_raceRegistry["Dwarf"] = {"Dwarf", 120, 90, 70, 12, 40, 0, 1126, 64, 32, idle, move, 0.85f};
    m_raceNameToId["Dwarf"] = 3; m_raceIdToName.push_back("Dwarf");

    ENTITY_INFO(std::format("Initialized race registry with {} races (fallback)", m_raceRegistry.size()));
}

void EntityDataManager::initializeClassRegistry() {
    const std::string jsonPath = HammerEngine::ResourcePath::resolve("res/data/classes.json");
    JsonReader reader;

    if (reader.loadFromFile(jsonPath)) {
        const JsonValue& root = reader.getRoot();

        if (root.isObject() && root.hasKey("classes") && root["classes"].isArray()) {
            const JsonValue& classes = root["classes"];

            for (size_t i = 0; i < classes.size(); ++i) {
                const JsonValue& c = classes[i];

                if (!c.hasKey("id") || !c["id"].isString()) {
                    ENTITY_WARN(std::format("Class at index {} missing 'id'", i));
                    continue;
                }

                std::string id = c["id"].asString();
                ClassInfo info;
                info.name = c.hasKey("name") ? c["name"].asString() : id;

                // Multipliers
                info.healthMult = c.hasKey("healthMult") ? static_cast<float>(c["healthMult"].asNumber()) : 1.0f;
                info.staminaMult = c.hasKey("staminaMult") ? static_cast<float>(c["staminaMult"].asNumber()) : 1.0f;
                info.moveSpeedMult = c.hasKey("moveSpeedMult") ? static_cast<float>(c["moveSpeedMult"].asNumber()) : 1.0f;
                info.attackDamageMult = c.hasKey("attackDamageMult") ? static_cast<float>(c["attackDamageMult"].asNumber()) : 1.0f;
                info.attackRangeMult = c.hasKey("attackRangeMult") ? static_cast<float>(c["attackRangeMult"].asNumber()) : 1.0f;

                // AI
                info.suggestedBehavior = c.hasKey("suggestedBehavior") ? c["suggestedBehavior"].asString() : "Idle";
                info.basePriority = c.hasKey("basePriority") ? static_cast<uint8_t>(c["basePriority"].asInt()) : 5;
                info.defaultFaction = c.hasKey("defaultFaction") ? static_cast<uint8_t>(c["defaultFaction"].asInt()) : 0;

                // Commerce
                info.isMerchant = c.hasKey("isMerchant") ? c["isMerchant"].asBool() : false;

                // Emotional resilience (0.0 = very emotional, 1.0 = stoic)
                info.emotionalResilience = c.hasKey("emotionalResilience") ?
                    static_cast<float>(c["emotionalResilience"].asNumber()) : 0.5f;

                // Starting items
                if (c.hasKey("startingItems") && c["startingItems"].isArray()) {
                    for (size_t j = 0; j < c["startingItems"].size(); ++j) {
                        const auto& item = c["startingItems"][j];
                        if (item.hasKey("id") && item["id"].isString()) {
                            std::string itemId = item["id"].asString();
                            int qty = item.hasKey("quantity") ? item["quantity"].asInt() : 1;
                            info.startingItems.emplace_back(itemId, qty);
                        }
                    }
                }

                uint8_t classId = static_cast<uint8_t>(m_classIdToName.size());
                m_classRegistry[id] = info;
                m_classNameToId[id] = classId;
                m_classIdToName.push_back(id);

                ENTITY_DEBUG(std::format("Loaded class '{}' HP:{}x DMG:{}x", id, info.healthMult, info.attackDamageMult));
            }

            ENTITY_INFO(std::format("Loaded {} classes from {}", m_classRegistry.size(), jsonPath));
            return;
        }
    }

    // Fallback defaults
    ENTITY_WARN(std::format("Failed to load classes from {}, using defaults", jsonPath));

    // emotionalResilience: 0.7 for warriors, 0.8 for guards, 0.3 for merchants, etc.
    m_classRegistry["Warrior"] = {"Warrior", 1.3f, 1.0f, 0.9f, 1.5f, 1.0f, "Chase", 7, 1, false, 0.7f, {}};
    m_classNameToId["Warrior"] = 0; m_classIdToName.push_back("Warrior");

    m_classRegistry["Guard"] = {"Guard", 1.2f, 1.1f, 0.8f, 1.2f, 1.0f, "Guard", 6, 0, false, 0.8f, {}};
    m_classNameToId["Guard"] = 1; m_classIdToName.push_back("Guard");

    m_classRegistry["GeneralMerchant"] = {"GeneralMerchant", 0.7f, 0.8f, 0.9f, 0.3f, 0.5f, "Idle", 2, 0, true, 0.3f, {}};
    m_classNameToId["GeneralMerchant"] = 2; m_classIdToName.push_back("GeneralMerchant");

    m_classRegistry["Rogue"] = {"Rogue", 0.8f, 1.3f, 1.3f, 1.2f, 0.8f, "Chase", 8, 1, false, 0.5f, {}};
    m_classNameToId["Rogue"] = 3; m_classIdToName.push_back("Rogue");

    m_classRegistry["Mage"] = {"Mage", 0.6f, 1.5f, 0.85f, 1.8f, 2.5f, "Attack", 7, 2, false, 0.4f, {}};
    m_classNameToId["Mage"] = 4; m_classIdToName.push_back("Mage");

    m_classRegistry["Farmer"] = {"Farmer", 0.9f, 1.1f, 1.0f, 0.5f, 0.5f, "Wander", 3, 0, true, 0.4f, {}};
    m_classNameToId["Farmer"] = 5; m_classIdToName.push_back("Farmer");

    ENTITY_INFO(std::format("Initialized class registry with {} classes (fallback)", m_classRegistry.size()));
}

void EntityDataManager::initializeMonsterTypeRegistry() {
    const std::string jsonPath = HammerEngine::ResourcePath::resolve("res/data/monster_types.json");
    JsonReader reader;

    // Load atlas.json for coordinate lookup (following WorldManager pattern)
    JsonReader atlasReader;
    std::unordered_map<std::string, JsonValue> atlasRegions;
    if (atlasReader.loadFromFile(HammerEngine::ResourcePath::resolve("res/data/atlas.json"))) {
        const auto& atlasRoot = atlasReader.getRoot();
        if (atlasRoot.hasKey("regions")) {
            atlasRegions = atlasRoot["regions"].asObject();
        }
    }

    auto getAtlasCoords = [&atlasRegions](const std::string& texId)
        -> std::tuple<uint16_t, uint16_t, uint16_t, uint16_t> {
        auto it = atlasRegions.find(texId);
        if (it == atlasRegions.end()) return {0, 0, 64, 32};
        const auto& r = it->second;
        return {
            static_cast<uint16_t>(r["x"].asInt()),
            static_cast<uint16_t>(r["y"].asInt()),
            static_cast<uint16_t>(r["w"].asInt()),
            static_cast<uint16_t>(r["h"].asInt())
        };
    };

    if (reader.loadFromFile(jsonPath)) {
        const JsonValue& root = reader.getRoot();

        if (root.isObject() && root.hasKey("monsterTypes") && root["monsterTypes"].isArray()) {
            const JsonValue& types = root["monsterTypes"];

            for (size_t i = 0; i < types.size(); ++i) {
                const JsonValue& t = types[i];

                if (!t.hasKey("id") || !t["id"].isString()) continue;

                std::string id = t["id"].asString();
                MonsterTypeInfo info;
                info.name = t.hasKey("name") ? t["name"].asString() : id;
                info.baseHealth = t.hasKey("baseHealth") ? static_cast<float>(t["baseHealth"].asNumber()) : 100.0f;
                info.baseStamina = t.hasKey("baseStamina") ? static_cast<float>(t["baseStamina"].asNumber()) : 100.0f;
                info.baseMoveSpeed = t.hasKey("baseMoveSpeed") ? static_cast<float>(t["baseMoveSpeed"].asNumber()) : 100.0f;
                info.baseAttackDamage = t.hasKey("baseAttackDamage") ? static_cast<float>(t["baseAttackDamage"].asNumber()) : 10.0f;
                info.baseAttackRange = t.hasKey("baseAttackRange") ? static_cast<float>(t["baseAttackRange"].asNumber()) : 50.0f;

                // Visual - look up atlas coords via textureId
                std::string texId = t.hasKey("textureId") ? t["textureId"].asString() : "";
                auto [ax, ay, aw, ah] = getAtlasCoords(texId);
                info.atlasX = ax;
                info.atlasY = ay;
                info.atlasW = aw;
                info.atlasH = ah;
                info.idleAnim = {0, 1, 150};
                info.moveAnim = {0, 2, 100};
                info.sizeMultiplier = t.hasKey("sizeMultiplier") ? static_cast<float>(t["sizeMultiplier"].asNumber()) : 1.0f;
                info.defaultFaction = t.hasKey("defaultFaction") ? static_cast<uint8_t>(t["defaultFaction"].asInt()) : 1;

                uint8_t typeId = static_cast<uint8_t>(m_monsterTypeIdToName.size());
                m_monsterTypeRegistry[id] = info;
                m_monsterTypeNameToId[id] = typeId;
                m_monsterTypeIdToName.push_back(id);
            }

            ENTITY_INFO(std::format("Loaded {} monster types from {}", m_monsterTypeRegistry.size(), jsonPath));
            return;
        }
    }

    // Fallback defaults
    ENTITY_WARN(std::format("Failed to load monster types from {}, using defaults", jsonPath));
    AnimationConfig idle{0, 1, 150};
    AnimationConfig move{0, 2, 100};

    m_monsterTypeRegistry["Goblin"] = {"Goblin", 40, 80, 90, 8, 40, 0, 0, 64, 32, idle, move, 0.8f, 1};
    m_monsterTypeNameToId["Goblin"] = 0; m_monsterTypeIdToName.push_back("Goblin");

    m_monsterTypeRegistry["Skeleton"] = {"Skeleton", 60, 60, 70, 12, 50, 0, 32, 64, 32, idle, move, 1.0f, 1};
    m_monsterTypeNameToId["Skeleton"] = 1; m_monsterTypeIdToName.push_back("Skeleton");

    m_monsterTypeRegistry["Slime"] = {"Slime", 30, 100, 50, 5, 30, 0, 64, 64, 32, idle, move, 0.6f, 1};
    m_monsterTypeNameToId["Slime"] = 2; m_monsterTypeIdToName.push_back("Slime");

    m_monsterTypeRegistry["Dragon"] = {"Dragon", 500, 200, 60, 50, 100, 0, 96, 128, 64, idle, move, 2.0f, 1};
    m_monsterTypeNameToId["Dragon"] = 3; m_monsterTypeIdToName.push_back("Dragon");

    ENTITY_INFO(std::format("Initialized monster type registry with {} types (fallback)", m_monsterTypeRegistry.size()));
}

void EntityDataManager::initializeMonsterVariantRegistry() {
    const std::string jsonPath = HammerEngine::ResourcePath::resolve("res/data/monster_variants.json");
    JsonReader reader;

    if (reader.loadFromFile(jsonPath)) {
        const JsonValue& root = reader.getRoot();

        if (root.isObject() && root.hasKey("variants") && root["variants"].isArray()) {
            const JsonValue& variants = root["variants"];

            for (size_t i = 0; i < variants.size(); ++i) {
                const JsonValue& v = variants[i];

                if (!v.hasKey("id") || !v["id"].isString()) continue;

                std::string id = v["id"].asString();
                MonsterVariantInfo info;
                info.name = v.hasKey("name") ? v["name"].asString() : id;
                info.healthMult = v.hasKey("healthMult") ? static_cast<float>(v["healthMult"].asNumber()) : 1.0f;
                info.staminaMult = v.hasKey("staminaMult") ? static_cast<float>(v["staminaMult"].asNumber()) : 1.0f;
                info.moveSpeedMult = v.hasKey("moveSpeedMult") ? static_cast<float>(v["moveSpeedMult"].asNumber()) : 1.0f;
                info.attackDamageMult = v.hasKey("attackDamageMult") ? static_cast<float>(v["attackDamageMult"].asNumber()) : 1.0f;
                info.attackRangeMult = v.hasKey("attackRangeMult") ? static_cast<float>(v["attackRangeMult"].asNumber()) : 1.0f;
                info.suggestedBehavior = v.hasKey("suggestedBehavior") ? v["suggestedBehavior"].asString() : "Chase";
                info.basePriority = v.hasKey("basePriority") ? static_cast<uint8_t>(v["basePriority"].asInt()) : 5;

                uint8_t variantId = static_cast<uint8_t>(m_monsterVariantIdToName.size());
                m_monsterVariantRegistry[id] = info;
                m_monsterVariantNameToId[id] = variantId;
                m_monsterVariantIdToName.push_back(id);
            }

            ENTITY_INFO(std::format("Loaded {} monster variants from {}", m_monsterVariantRegistry.size(), jsonPath));
            return;
        }
    }

    // Fallback defaults
    ENTITY_WARN(std::format("Failed to load monster variants from {}, using defaults", jsonPath));

    m_monsterVariantRegistry["Scout"] = {"Scout", 0.7f, 1.0f, 1.3f, 0.8f, 1.0f, "Chase", 5};
    m_monsterVariantNameToId["Scout"] = 0; m_monsterVariantIdToName.push_back("Scout");

    m_monsterVariantRegistry["Brute"] = {"Brute", 1.5f, 0.8f, 0.8f, 1.4f, 1.0f, "Chase", 6};
    m_monsterVariantNameToId["Brute"] = 1; m_monsterVariantIdToName.push_back("Brute");

    m_monsterVariantRegistry["Shaman"] = {"Shaman", 0.8f, 1.5f, 0.9f, 1.6f, 2.0f, "Attack", 7};
    m_monsterVariantNameToId["Shaman"] = 2; m_monsterVariantIdToName.push_back("Shaman");

    m_monsterVariantRegistry["Boss"] = {"Boss", 3.0f, 2.0f, 1.0f, 2.0f, 1.2f, "Attack", 9};
    m_monsterVariantNameToId["Boss"] = 3; m_monsterVariantIdToName.push_back("Boss");

    ENTITY_INFO(std::format("Initialized monster variant registry with {} variants (fallback)", m_monsterVariantRegistry.size()));
}

void EntityDataManager::initializeSpeciesRegistry() {
    const std::string jsonPath = HammerEngine::ResourcePath::resolve("res/data/species.json");
    JsonReader reader;

    // Load atlas.json for coordinate lookup (following WorldManager pattern)
    JsonReader atlasReader;
    std::unordered_map<std::string, JsonValue> atlasRegions;
    if (atlasReader.loadFromFile(HammerEngine::ResourcePath::resolve("res/data/atlas.json"))) {
        const auto& atlasRoot = atlasReader.getRoot();
        if (atlasRoot.hasKey("regions")) {
            atlasRegions = atlasRoot["regions"].asObject();
        }
    }

    auto getAtlasCoords = [&atlasRegions](const std::string& texId)
        -> std::tuple<uint16_t, uint16_t, uint16_t, uint16_t> {
        auto it = atlasRegions.find(texId);
        if (it == atlasRegions.end()) return {0, 0, 64, 32};
        const auto& r = it->second;
        return {
            static_cast<uint16_t>(r["x"].asInt()),
            static_cast<uint16_t>(r["y"].asInt()),
            static_cast<uint16_t>(r["w"].asInt()),
            static_cast<uint16_t>(r["h"].asInt())
        };
    };

    if (reader.loadFromFile(jsonPath)) {
        const JsonValue& root = reader.getRoot();

        if (root.isObject() && root.hasKey("species") && root["species"].isArray()) {
            const JsonValue& speciesList = root["species"];

            for (size_t i = 0; i < speciesList.size(); ++i) {
                const JsonValue& s = speciesList[i];

                if (!s.hasKey("id") || !s["id"].isString()) continue;

                std::string id = s["id"].asString();
                SpeciesInfo info;
                info.name = s.hasKey("name") ? s["name"].asString() : id;
                info.baseHealth = s.hasKey("baseHealth") ? static_cast<float>(s["baseHealth"].asNumber()) : 50.0f;
                info.baseStamina = s.hasKey("baseStamina") ? static_cast<float>(s["baseStamina"].asNumber()) : 100.0f;
                info.baseMoveSpeed = s.hasKey("baseMoveSpeed") ? static_cast<float>(s["baseMoveSpeed"].asNumber()) : 80.0f;
                info.baseAttackDamage = s.hasKey("baseAttackDamage") ? static_cast<float>(s["baseAttackDamage"].asNumber()) : 5.0f;
                info.baseAttackRange = s.hasKey("baseAttackRange") ? static_cast<float>(s["baseAttackRange"].asNumber()) : 30.0f;

                // Visual - look up atlas coords via textureId
                std::string texId = s.hasKey("textureId") ? s["textureId"].asString() : "";
                auto [ax, ay, aw, ah] = getAtlasCoords(texId);
                info.atlasX = ax;
                info.atlasY = ay;
                info.atlasW = aw;
                info.atlasH = ah;
                info.idleAnim = {0, 1, 150};
                info.moveAnim = {0, 2, 100};
                info.sizeMultiplier = s.hasKey("sizeMultiplier") ? static_cast<float>(s["sizeMultiplier"].asNumber()) : 1.0f;
                info.predator = s.hasKey("predator") ? s["predator"].asBool() : false;

                uint8_t speciesId = static_cast<uint8_t>(m_speciesIdToName.size());
                m_speciesRegistry[id] = info;
                m_speciesNameToId[id] = speciesId;
                m_speciesIdToName.push_back(id);
            }

            ENTITY_INFO(std::format("Loaded {} species from {}", m_speciesRegistry.size(), jsonPath));
            return;
        }
    }

    // Fallback defaults
    ENTITY_WARN(std::format("Failed to load species from {}, using defaults", jsonPath));
    AnimationConfig idle{0, 1, 150};
    AnimationConfig move{0, 2, 100};

    m_speciesRegistry["Wolf"] = {"Wolf", 60, 100, 120, 10, 40, 0, 128, 64, 32, idle, move, 1.0f, true};
    m_speciesNameToId["Wolf"] = 0; m_speciesIdToName.push_back("Wolf");

    m_speciesRegistry["Bear"] = {"Bear", 150, 80, 70, 25, 50, 0, 160, 96, 48, idle, move, 1.5f, true};
    m_speciesNameToId["Bear"] = 1; m_speciesIdToName.push_back("Bear");

    m_speciesRegistry["Deer"] = {"Deer", 40, 120, 130, 5, 30, 0, 208, 64, 32, idle, move, 1.1f, false};
    m_speciesNameToId["Deer"] = 2; m_speciesIdToName.push_back("Deer");

    m_speciesRegistry["Rabbit"] = {"Rabbit", 15, 150, 150, 2, 20, 0, 240, 32, 16, idle, move, 0.4f, false};
    m_speciesNameToId["Rabbit"] = 3; m_speciesIdToName.push_back("Rabbit");

    ENTITY_INFO(std::format("Initialized species registry with {} species (fallback)", m_speciesRegistry.size()));
}

void EntityDataManager::initializeAnimalRoleRegistry() {
    const std::string jsonPath = HammerEngine::ResourcePath::resolve("res/data/animal_roles.json");
    JsonReader reader;

    if (reader.loadFromFile(jsonPath)) {
        const JsonValue& root = reader.getRoot();

        if (root.isObject() && root.hasKey("roles") && root["roles"].isArray()) {
            const JsonValue& roles = root["roles"];

            for (size_t i = 0; i < roles.size(); ++i) {
                const JsonValue& r = roles[i];

                if (!r.hasKey("id") || !r["id"].isString()) continue;

                std::string id = r["id"].asString();
                AnimalRoleInfo info;
                info.name = r.hasKey("name") ? r["name"].asString() : id;
                info.healthMult = r.hasKey("healthMult") ? static_cast<float>(r["healthMult"].asNumber()) : 1.0f;
                info.staminaMult = r.hasKey("staminaMult") ? static_cast<float>(r["staminaMult"].asNumber()) : 1.0f;
                info.moveSpeedMult = r.hasKey("moveSpeedMult") ? static_cast<float>(r["moveSpeedMult"].asNumber()) : 1.0f;
                info.attackDamageMult = r.hasKey("attackDamageMult") ? static_cast<float>(r["attackDamageMult"].asNumber()) : 1.0f;
                info.suggestedBehavior = r.hasKey("suggestedBehavior") ? r["suggestedBehavior"].asString() : "Wander";
                info.basePriority = r.hasKey("basePriority") ? static_cast<uint8_t>(r["basePriority"].asInt()) : 5;
                info.defaultFaction = r.hasKey("defaultFaction") ? static_cast<uint8_t>(r["defaultFaction"].asInt()) : 2;

                uint8_t roleId = static_cast<uint8_t>(m_animalRoleIdToName.size());
                m_animalRoleRegistry[id] = info;
                m_animalRoleNameToId[id] = roleId;
                m_animalRoleIdToName.push_back(id);
            }

            ENTITY_INFO(std::format("Loaded {} animal roles from {}", m_animalRoleRegistry.size(), jsonPath));
            return;
        }
    }

    // Fallback defaults
    ENTITY_WARN(std::format("Failed to load animal roles from {}, using defaults", jsonPath));

    m_animalRoleRegistry["Pup"] = {"Pup", 0.5f, 0.8f, 1.1f, 0.4f, "Wander", 3, 2};
    m_animalRoleNameToId["Pup"] = 0; m_animalRoleIdToName.push_back("Pup");

    m_animalRoleRegistry["Adult"] = {"Adult", 1.0f, 1.0f, 1.0f, 1.0f, "Wander", 5, 2};
    m_animalRoleNameToId["Adult"] = 1; m_animalRoleIdToName.push_back("Adult");

    m_animalRoleRegistry["Alpha"] = {"Alpha", 1.5f, 1.2f, 1.1f, 1.5f, "Guard", 7, 2};
    m_animalRoleNameToId["Alpha"] = 2; m_animalRoleIdToName.push_back("Alpha");

    ENTITY_INFO(std::format("Initialized animal role registry with {} roles (fallback)", m_animalRoleRegistry.size()));
}
