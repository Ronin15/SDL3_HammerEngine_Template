/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/EntityDataManager.hpp"
#include "core/Logger.hpp"
#include "utils/UniqueID.hpp"
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
        m_itemData.reserve(ITEM_CAPACITY);
        m_projectileData.reserve(PROJECTILE_CAPACITY);
        m_containerData.reserve(100);
        m_harvestableData.reserve(500);
        m_areaEffectData.reserve(EFFECT_CAPACITY);

        // Path data (indexed by edmIndex, sparse for non-AI entities)
        m_pathData.reserve(CHARACTER_CAPACITY);

        m_activeIndices.reserve(INITIAL_CAPACITY);
        m_backgroundIndices.reserve(INITIAL_CAPACITY);
        m_hibernatedIndices.reserve(INITIAL_CAPACITY);

        for (auto& kindVec : m_kindIndices) {
            kindVec.reserve(1000);
        }

        m_destructionQueue.reserve(100);
        m_destroyBuffer.reserve(100);  // Match destruction queue capacity
        m_freeSlots.reserve(1000);

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
    m_itemData.clear();
    m_projectileData.clear();
    m_containerData.clear();
    m_harvestableData.clear();
    m_areaEffectData.clear();
    m_pathData.clear();

    // Clear type-specific free-lists
    m_freeCharacterSlots.clear();
    m_freeItemSlots.clear();
    m_freeProjectileSlots.clear();
    m_freeContainerSlots.clear();
    m_freeHarvestableSlots.clear();
    m_freeAreaEffectSlots.clear();

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
    m_itemData.clear();
    m_projectileData.clear();
    m_containerData.clear();
    m_harvestableData.clear();
    m_areaEffectData.clear();
    m_pathData.clear();

    // Clear type-specific free-lists
    m_freeCharacterSlots.clear();
    m_freeItemSlots.clear();
    m_freeProjectileSlots.clear();
    m_freeContainerSlots.clear();
    m_freeHarvestableSlots.clear();
    m_freeAreaEffectSlots.clear();

    m_activeIndices.clear();
    m_backgroundIndices.clear();
    m_hibernatedIndices.clear();

    for (auto& kindVec : m_kindIndices) {
        kindVec.clear();
    }

    m_freeSlots.clear();
    m_tierIndicesDirty = true;
    m_kindIndicesDirty = true;

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
        // Pre-allocate PathData to match - avoids concurrent resize during AI processing
        m_pathData.emplace_back();
    }

    m_tierIndicesDirty = true;
    m_kindIndicesDirty = true;

    return index;
}

void EntityDataManager::freeSlot(size_t index) {
    if (index >= m_hotData.size()) {
        return;
    }

    // Capture type info BEFORE clearing (for type-specific free-list)
    EntityKind kind = m_hotData[index].kind;
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;

    // Clear path data for AI entities
    clearPathData(index);

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
        case EntityKind::NPC:
            m_freeCharacterSlots.push_back(typeIndex);
            break;
        case EntityKind::DroppedItem:
            m_freeItemSlots.push_back(typeIndex);
            break;
        case EntityKind::Projectile:
            m_freeProjectileSlots.push_back(typeIndex);
            break;
        case EntityKind::Container:
            m_freeContainerSlots.push_back(typeIndex);
            break;
        case EntityKind::Harvestable:
            m_freeHarvestableSlots.push_back(typeIndex);
            break;
        case EntityKind::AreaEffect:
            m_freeAreaEffectSlots.push_back(typeIndex);
            break;
        default:
            // StaticObstacle, Prop, Trigger have no type-specific data
            break;
    }

    m_tierIndicesDirty = true;
    m_kindIndicesDirty = true;
}

uint8_t EntityDataManager::nextGeneration(size_t index) {
    if (index >= m_generations.size()) {
        return 1;
    }
    return static_cast<uint8_t>((m_generations[index] + 1) % 256);
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

    // Allocate character data (reuse freed slot if available)
    uint32_t charIndex;
    if (!m_freeCharacterSlots.empty()) {
        charIndex = m_freeCharacterSlots.back();
        m_freeCharacterSlots.pop_back();
        m_characterData[charIndex] = CharacterData{};
    } else {
        charIndex = static_cast<uint32_t>(m_characterData.size());
        m_characterData.emplace_back();
    }
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

EntityHandle EntityDataManager::createPlayer(const Vector2D& position) {

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
    hot.halfHeight = 24.0f;
    hot.kind = EntityKind::Player;
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

    // Allocate character data (reuse freed slot if available)
    uint32_t charIndex;
    if (!m_freeCharacterSlots.empty()) {
        charIndex = m_freeCharacterSlots.back();
        m_freeCharacterSlots.pop_back();
    } else {
        charIndex = static_cast<uint32_t>(m_characterData.size());
        m_characterData.emplace_back();
    }
    auto& charData = m_characterData[charIndex];
    charData = CharacterData{};  // Reset to default
    charData.health = 100.0f;
    charData.maxHealth = 100.0f;
    charData.stamina = 100.0f;
    charData.maxStamina = 100.0f;
    charData.attackDamage = 25.0f;
    charData.attackRange = 50.0f;
    charData.stateFlags = CharacterData::STATE_ALIVE;
    hot.typeLocalIndex = charIndex;

    // Store ID and mapping
    m_entityIds[index] = id;
    m_generations[index] = generation;
    m_idToIndex[id] = index;

    // Update counters
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::Player)].fetch_add(1, std::memory_order_relaxed);
    m_countByTier[static_cast<size_t>(SimulationTier::Active)].fetch_add(1, std::memory_order_relaxed);
    m_tierIndicesDirty = true;

    ENTITY_INFO(std::format("Created Player entity {} at ({}, {})",
                           id, position.getX(), position.getY()));

    return EntityHandle{id, EntityKind::Player, generation};
}

EntityHandle EntityDataManager::createDroppedItem(const Vector2D& position,
                                                  HammerEngine::ResourceHandle resourceHandle,
                                                  int quantity) {

    size_t index = allocateSlot();
    EntityHandle::IDType id = HammerEngine::UniqueID::generate();
    uint8_t generation = nextGeneration(index);

    // Initialize hot data
    auto& hot = m_hotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = Vector2D{0.0f, 0.0f};
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = 8.0f;
    hot.halfHeight = 8.0f;
    hot.kind = EntityKind::DroppedItem;
    hot.tier = SimulationTier::Active;
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;

    // Initialize collision data (DroppedItems only collide with player for pickup)
    hot.collisionLayers = HammerEngine::CollisionLayer::Layer_Default;
    hot.collisionMask = HammerEngine::CollisionLayer::Layer_Player;
    hot.collisionFlags = EntityHotData::COLLISION_ENABLED;
    hot.triggerTag = 0;

    // Allocate item data (reuse freed slot if available)
    uint32_t itemIndex;
    if (!m_freeItemSlots.empty()) {
        itemIndex = m_freeItemSlots.back();
        m_freeItemSlots.pop_back();
    } else {
        itemIndex = static_cast<uint32_t>(m_itemData.size());
        m_itemData.emplace_back();
    }
    auto& item = m_itemData[itemIndex];
    item = ItemData{};  // Reset to default
    item.resourceHandle = resourceHandle;
    item.quantity = quantity;
    item.pickupTimer = 0.5f;
    item.bobTimer = 0.0f;
    item.flags = 0;
    hot.typeLocalIndex = itemIndex;

    // Store ID and mapping
    m_entityIds[index] = id;
    m_generations[index] = generation;
    m_idToIndex[id] = index;

    // Update counters
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::DroppedItem)].fetch_add(1, std::memory_order_relaxed);
    m_countByTier[static_cast<size_t>(SimulationTier::Active)].fetch_add(1, std::memory_order_relaxed);
    m_tierIndicesDirty = true;

    return EntityHandle{id, EntityKind::DroppedItem, generation};
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

EntityHandle EntityDataManager::registerNPC(EntityHandle::IDType entityId,
                                            const Vector2D& position,
                                            float halfWidth,
                                            float halfHeight,
                                            float health,
                                            float maxHealth) {
    if (entityId == 0) {
        ENTITY_ERROR("registerNPC: Invalid entity ID (0)");
        return INVALID_ENTITY_HANDLE;
    }


    // Check if already registered
    if (m_idToIndex.find(entityId) != m_idToIndex.end()) {
        ENTITY_WARN(std::format("registerNPC: Entity {} already registered", entityId));
        // Return existing handle
        size_t existingIndex = m_idToIndex[entityId];
        return EntityHandle{entityId, EntityKind::NPC, m_generations[existingIndex]};
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
    hot.kind = EntityKind::NPC;
    hot.tier = SimulationTier::Active;
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;

    // Initialize collision data (NPCs collide with player, environment, other NPCs)
    hot.collisionLayers = HammerEngine::CollisionLayer::Layer_Enemy;
    hot.collisionMask = HammerEngine::CollisionLayer::Layer_Player |
                        HammerEngine::CollisionLayer::Layer_Environment |
                        HammerEngine::CollisionLayer::Layer_Projectile |
                        HammerEngine::CollisionLayer::Layer_Enemy;
    hot.collisionFlags = EntityHotData::COLLISION_ENABLED;
    hot.triggerTag = 0;

    // Allocate character data with provided health values (reuse freed slot if available)
    uint32_t charIndex;
    if (!m_freeCharacterSlots.empty()) {
        charIndex = m_freeCharacterSlots.back();
        m_freeCharacterSlots.pop_back();
    } else {
        charIndex = static_cast<uint32_t>(m_characterData.size());
        m_characterData.emplace_back();
    }
    auto& charData = m_characterData[charIndex];
    charData = CharacterData{};  // Reset to default
    charData.health = health;
    charData.maxHealth = maxHealth;
    charData.stamina = 100.0f;
    charData.maxStamina = 100.0f;
    charData.stateFlags = CharacterData::STATE_ALIVE;
    hot.typeLocalIndex = charIndex;

    // Store ID and mapping (using provided ID, not generating new)
    m_entityIds[index] = entityId;
    m_generations[index] = generation;
    m_idToIndex[entityId] = index;

    // Update counters
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::NPC)].fetch_add(1, std::memory_order_relaxed);
    m_countByTier[static_cast<size_t>(SimulationTier::Active)].fetch_add(1, std::memory_order_relaxed);
    m_tierIndicesDirty = true;

    ENTITY_DEBUG(std::format("Registered NPC entity {} at ({}, {})",
                            entityId, position.getX(), position.getY()));

    return EntityHandle{entityId, EntityKind::NPC, generation};
}

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

    // Allocate character data with player defaults (reuse freed slot if available)
    uint32_t charIndex;
    if (!m_freeCharacterSlots.empty()) {
        charIndex = m_freeCharacterSlots.back();
        m_freeCharacterSlots.pop_back();
    } else {
        charIndex = static_cast<uint32_t>(m_characterData.size());
        m_characterData.emplace_back();
    }
    auto& charData = m_characterData[charIndex];
    charData = CharacterData{};  // Reset to default
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


    // Check if already registered
    if (m_idToIndex.find(entityId) != m_idToIndex.end()) {
        ENTITY_WARN(std::format("registerDroppedItem: Entity {} already registered", entityId));
        size_t existingIndex = m_idToIndex[entityId];
        return EntityHandle{entityId, EntityKind::DroppedItem, m_generations[existingIndex]};
    }

    size_t index = allocateSlot();
    uint8_t generation = nextGeneration(index);

    // Initialize hot data
    auto& hot = m_hotData[index];
    hot.transform.position = position;
    hot.transform.previousPosition = position;
    hot.transform.velocity = Vector2D{0.0f, 0.0f};
    hot.transform.acceleration = Vector2D{0.0f, 0.0f};
    hot.halfWidth = 8.0f;
    hot.halfHeight = 8.0f;
    hot.kind = EntityKind::DroppedItem;
    hot.tier = SimulationTier::Active;
    hot.flags = EntityHotData::FLAG_ALIVE;
    hot.generation = generation;

    // Allocate item data (reuse freed slot if available)
    uint32_t itemIndex;
    if (!m_freeItemSlots.empty()) {
        itemIndex = m_freeItemSlots.back();
        m_freeItemSlots.pop_back();
    } else {
        itemIndex = static_cast<uint32_t>(m_itemData.size());
        m_itemData.emplace_back();
    }
    auto& item = m_itemData[itemIndex];
    item = ItemData{};  // Reset to default
    item.resourceHandle = resourceHandle;
    item.quantity = quantity;
    item.pickupTimer = 0.5f;
    item.bobTimer = 0.0f;
    item.flags = 0;
    hot.typeLocalIndex = itemIndex;

    // Store ID and mapping
    m_entityIds[index] = entityId;
    m_generations[index] = generation;
    m_idToIndex[entityId] = index;

    // Update counters
    m_totalEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_countByKind[static_cast<size_t>(EntityKind::DroppedItem)].fetch_add(1, std::memory_order_relaxed);
    m_countByTier[static_cast<size_t>(SimulationTier::Active)].fetch_add(1, std::memory_order_relaxed);
    m_tierIndicesDirty = true;

    ENTITY_DEBUG(std::format("Registered DroppedItem entity {} at ({}, {})",
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

    std::lock_guard<std::mutex> lock(m_destructionMutex);
    m_destructionQueue.push_back(handle);
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
// HANDLE VALIDATION
// ============================================================================

bool EntityDataManager::isValidHandle(EntityHandle handle) const {
    if (!handle.isValid()) {
        return false;
    }


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

TransformData& EntityDataManager::getTransform(EntityHandle handle) {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    return m_hotData[index].transform;
}

const TransformData& EntityDataManager::getTransform(EntityHandle handle) const {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    return m_hotData[index].transform;
}

TransformData& EntityDataManager::getTransformByIndex(size_t index) {
    assert(index < m_hotData.size() && "Index out of bounds");
    return m_hotData[index].transform;
}

const TransformData& EntityDataManager::getTransformByIndex(size_t index) const {
    assert(index < m_hotData.size() && "Index out of bounds");
    return m_hotData[index].transform;
}

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
    return m_hotData[index];
}

const EntityHotData& EntityDataManager::getHotData(EntityHandle handle) const {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    return m_hotData[index];
}

EntityHotData& EntityDataManager::getHotDataByIndex(size_t index) {
    assert(index < m_hotData.size() && "Index out of bounds");
    return m_hotData[index];
}

const EntityHotData& EntityDataManager::getHotDataByIndex(size_t index) const {
    assert(index < m_hotData.size() && "Index out of bounds");
    return m_hotData[index];
}

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

CharacterData& EntityDataManager::getCharacterDataByIndex(size_t index) {
    assert(index < m_hotData.size() && "Index out of bounds");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_characterData.size() && "Type index out of bounds");
    return m_characterData[typeIndex];
}

const CharacterData& EntityDataManager::getCharacterDataByIndex(size_t index) const {
    assert(index < m_hotData.size() && "Index out of bounds");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_characterData.size() && "Type index out of bounds");
    return m_characterData[typeIndex];
}

ItemData& EntityDataManager::getItemData(EntityHandle handle) {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.isItem() && "Entity is not an item");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_itemData.size() && "Type index out of bounds");
    return m_itemData[typeIndex];
}

const ItemData& EntityDataManager::getItemData(EntityHandle handle) const {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.isItem() && "Entity is not an item");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
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
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.getKind() == EntityKind::Container && "Entity is not a container");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_containerData.size() && "Type index out of bounds");
    return m_containerData[typeIndex];
}

const ContainerData& EntityDataManager::getContainerData(EntityHandle handle) const {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.getKind() == EntityKind::Container && "Entity is not a container");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_containerData.size() && "Type index out of bounds");
    return m_containerData[typeIndex];
}

HarvestableData& EntityDataManager::getHarvestableData(EntityHandle handle) {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.getKind() == EntityKind::Harvestable && "Entity is not harvestable");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_harvestableData.size() && "Type index out of bounds");
    return m_harvestableData[typeIndex];
}

const HarvestableData& EntityDataManager::getHarvestableData(EntityHandle handle) const {
    size_t index = getIndex(handle);
    assert(index != SIZE_MAX && "Invalid entity handle");
    assert(handle.getKind() == EntityKind::Harvestable && "Entity is not harvestable");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
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
// PATH DATA ACCESS
// ============================================================================

PathData& EntityDataManager::getPathData(size_t index) {
    ensurePathData(index);
    return m_pathData[index];
}

const PathData& EntityDataManager::getPathData(size_t index) const {
    assert(index < m_pathData.size() && "Path data index out of bounds");
    return m_pathData[index];
}

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

    // Rebuild tier indices if dirty
    if (m_tierIndicesDirty) {
        m_activeIndices.clear();
        m_backgroundIndices.clear();
        m_hibernatedIndices.clear();

        for (size_t i = 0; i < m_hotData.size(); ++i) {
            if (!m_hotData[i].isAlive()) {
                continue;
            }

            switch (m_hotData[i].tier) {
                case SimulationTier::Active:
                    m_activeIndices.push_back(i);
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
        m_activeCollisionDirty = true;     // Collision indices need rebuild when tiers change
        m_triggerDetectionDirty = true;    // Trigger detection indices need rebuild when tiers change

#ifndef NDEBUG
        // Rolling log every 60 seconds using time-based check
        static auto lastLogTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime).count() >= 60) {
            lastLogTime = now;
            size_t tierTotal = m_activeIndices.size() + m_backgroundIndices.size() + m_hibernatedIndices.size();
            size_t dynamicCount = m_hotData.size();  // Only dynamic entities (statics in separate vector)
            ENTITY_DEBUG(std::format(
                "Tiers: Active={}, Background={}, Hibernated={} (Total={}, Dynamic={}, Statics={})",
                m_activeIndices.size(), m_backgroundIndices.size(), m_hibernatedIndices.size(),
                tierTotal, dynamicCount, m_staticHotData.size()));
        }
#endif
    }
}

std::span<const size_t> EntityDataManager::getActiveIndices() const {
    return std::span<const size_t>(m_activeIndices);
}

std::span<const size_t> EntityDataManager::getActiveIndicesWithCollision() const {
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
    return std::span<const size_t>(m_backgroundIndices);
}

std::span<const size_t> EntityDataManager::getIndicesByKind(EntityKind kind) const {
    if (m_kindIndicesDirty) {
        // Rebuild kind indices (const_cast for lazy rebuild)
        auto& self = const_cast<EntityDataManager&>(*this);
        for (auto& kindVec : self.m_kindIndices) {
            kindVec.clear();
        }

        for (size_t i = 0; i < m_hotData.size(); ++i) {
            if (m_hotData[i].isAlive()) {
                self.m_kindIndices[static_cast<size_t>(m_hotData[i].kind)].push_back(i);
            }
        }

        self.m_kindIndicesDirty = false;
    }

    return std::span<const size_t>(m_kindIndices[static_cast<size_t>(kind)]);
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
