/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef ENTITY_DATA_MANAGER_HPP
#define ENTITY_DATA_MANAGER_HPP

/**
 * @file EntityDataManager.hpp
 * @brief Central data authority for all entity data (Data-Oriented Design)
 *
 * EntityDataManager is a pure DATA STORE, not a processor. It owns:
 * - All entity transform data (position, velocity, acceleration)
 * - Type-specific data blocks (CharacterData, ItemData, ProjectileData, etc.)
 * - Simulation tier tracking (Active/Background/Hibernated)
 *
 * Processing systems read from and write to EntityDataManager:
 * - AIManager processes Active tier behaviors
 * - CollisionManager processes collision detection
 * - BackgroundSimulator processes Background tier entities
 * - Type-specific systems (ProjectileSystem, etc.)
 *
 * Benefits:
 * - Single source of truth (eliminates 4x position duplication)
 * - Cache-optimal SoA storage (~5MB contiguous vs ~30MB scattered)
 * - Supports 100K+ entities with tiered simulation
 *
 * THREADING CONTRACT:
 * - All structural operations (create/destroy/register/getIndex) MUST be called
 *   from the main thread only. These operations are NOT thread-safe.
 * - Index-based accessors (getHotDataByIndex, getTransformByIndex) are lock-free
 *   and safe for parallel batch processing with non-overlapping index ranges.
 * - Parallel batch processing uses pre-cached indices to avoid map lookups.
 * - GameEngine::update() sequential order guarantees no concurrent structural changes:
 *   EventManager → GameStateManager → AIManager → CollisionManager → BackgroundSimManager
 */

#include "collisions/CollisionBody.hpp"
#include "collisions/TriggerTag.hpp"
#include "entities/EntityHandle.hpp"
#include "utils/ResourceHandle.hpp"
#include "utils/Vector2D.hpp"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <limits>
#include <mutex>
#include <span>
#include <vector>

// Forward declarations
class Entity;

/**
 * @brief Transform data for entity movement (32 bytes)
 */
struct TransformData {
    Vector2D position{0.0f, 0.0f};         // Current position (8 bytes)
    Vector2D previousPosition{0.0f, 0.0f}; // For interpolation (8 bytes)
    Vector2D velocity{0.0f, 0.0f};         // Current velocity (8 bytes)
    Vector2D acceleration{0.0f, 0.0f};     // Current acceleration (8 bytes)
};

static_assert(sizeof(TransformData) == 32, "TransformData should be 32 bytes");

/**
 * @brief Hot data accessed every frame (64 bytes, one cache line)
 *
 * Packed for sequential access during batch processing.
 * All frequently-accessed data in one contiguous array.
 *
 * NOTE: This is for DYNAMIC entities (Player, NPC, Projectile, etc.) that:
 * - Move around and have AI/physics
 * - Are managed by the tier system (Active/Background/Hibernated)
 * - Only Active tier entities participate in collision detection
 *
 * STATIC obstacles (walls, buildings, terrain) are NOT stored here.
 * They live in CollisionManager's m_staticBodies storage and are always
 * checked for collision regardless of tier. This separation allows:
 * - Statics to never be iterated unnecessarily
 * - Statics to be in a compact spatial hash for O(1) queries
 * - Dynamic entities to be tier-filtered efficiently
 */
struct EntityHotData {
    TransformData transform;        // 32 bytes
    float halfWidth{16.0f};         // 4 bytes: Half-width for collision
    float halfHeight{16.0f};        // 4 bytes: Half-height for collision
    EntityKind kind{EntityKind::NPC};           // 1 byte
    SimulationTier tier{SimulationTier::Active}; // 1 byte
    uint8_t flags{0};               // 1 byte: alive, dirty, etc.
    uint8_t generation{0};          // 1 byte: Handle generation
    uint32_t typeLocalIndex{0};     // 4 bytes: Index into type-specific array

    // Collision data (only for entities that participate in collision)
    uint16_t collisionLayers{HammerEngine::CollisionLayer::Layer_Default};  // 2 bytes: Which layer(s) this entity is on
    uint16_t collisionMask{0xFFFF};  // 2 bytes: Which layers this entity collides with
    uint8_t collisionFlags{0};       // 1 byte: COLLISION_ENABLED, IS_TRIGGER
    uint8_t triggerTag{0};           // 1 byte: TriggerTag for trigger entities
    uint8_t triggerType{0};          // 1 byte: TriggerType (EventOnly, Physical)
    uint8_t _padding[9]{};           // 9 bytes: Pad to 64-byte cache line

    // Entity flag constants
    static constexpr uint8_t FLAG_ALIVE = 0x01;
    static constexpr uint8_t FLAG_DIRTY = 0x02;
    static constexpr uint8_t FLAG_PENDING_DESTROY = 0x04;

    // Collision flag constants
    static constexpr uint8_t COLLISION_ENABLED = 0x01;
    static constexpr uint8_t IS_TRIGGER = 0x02;
    static constexpr uint8_t NEEDS_TRIGGER_DETECTION = 0x04;

    [[nodiscard]] bool isAlive() const noexcept { return flags & FLAG_ALIVE; }
    [[nodiscard]] bool isDirty() const noexcept { return flags & FLAG_DIRTY; }
    [[nodiscard]] bool isPendingDestroy() const noexcept {
        return flags & FLAG_PENDING_DESTROY;
    }
    [[nodiscard]] bool hasCollision() const noexcept {
        return collisionFlags & COLLISION_ENABLED;
    }
    [[nodiscard]] bool isTrigger() const noexcept {
        return collisionFlags & IS_TRIGGER;
    }
    [[nodiscard]] bool needsTriggerDetection() const noexcept {
        return collisionFlags & NEEDS_TRIGGER_DETECTION;
    }

    void setAlive(bool alive) noexcept {
        if (alive) flags |= FLAG_ALIVE;
        else flags &= ~FLAG_ALIVE;
    }
    void setDirty(bool dirty) noexcept {
        if (dirty) flags |= FLAG_DIRTY;
        else flags &= ~FLAG_DIRTY;
    }
    void markForDestruction() noexcept { flags |= FLAG_PENDING_DESTROY; }

    void setCollisionEnabled(bool enabled) noexcept {
        if (enabled) collisionFlags |= COLLISION_ENABLED;
        else collisionFlags &= ~COLLISION_ENABLED;
    }
    void setTrigger(bool trigger) noexcept {
        if (trigger) collisionFlags |= IS_TRIGGER;
        else collisionFlags &= ~IS_TRIGGER;
    }
    void setTriggerDetection(bool enabled) noexcept {
        if (enabled) collisionFlags |= NEEDS_TRIGGER_DETECTION;
        else collisionFlags &= ~NEEDS_TRIGGER_DETECTION;
    }

    [[nodiscard]] bool isEventOnlyTrigger() const noexcept {
        return isTrigger() && triggerType == static_cast<uint8_t>(HammerEngine::TriggerType::EventOnly);
    }
};

static_assert(sizeof(EntityHotData) == 64, "EntityHotData should be 64 bytes (one cache line)");

// ============================================================================
// TYPE-SPECIFIC DATA BLOCKS
// ============================================================================

/**
 * @brief Character data for Player and NPC entities
 */
struct CharacterData {
    float health{100.0f};
    float maxHealth{100.0f};
    float stamina{100.0f};
    float maxStamina{100.0f};
    float attackDamage{10.0f};
    float attackRange{50.0f};
    uint8_t faction{0};        // 0=Friendly, 1=Enemy, 2=Neutral
    uint8_t behaviorType{0};   // BehaviorType enum
    uint8_t priority{5};       // AI priority (0-9)
    uint8_t stateFlags{0};     // alive, stunned, invulnerable, etc.

    static constexpr uint8_t STATE_ALIVE = 0x01;
    static constexpr uint8_t STATE_STUNNED = 0x02;
    static constexpr uint8_t STATE_INVULNERABLE = 0x04;

    [[nodiscard]] bool isCharacterAlive() const noexcept {
        return stateFlags & STATE_ALIVE;
    }
};

/**
 * @brief Item data for DroppedItem entities
 */
struct ItemData {
    HammerEngine::ResourceHandle resourceHandle;  // Item template reference
    int quantity{1};
    float pickupTimer{0.5f};    // Delay before pickup allowed
    float bobTimer{0.0f};       // Visual bobbing effect
    uint8_t flags{0};

    static constexpr uint8_t FLAG_CAN_PICKUP = 0x01;
    static constexpr uint8_t FLAG_IS_STACKED = 0x02;

    [[nodiscard]] bool canPickup() const noexcept {
        return (flags & FLAG_CAN_PICKUP) && quantity > 0;
    }
};

/**
 * @brief Projectile data for Projectile entities
 */
struct ProjectileData {
    EntityHandle owner;         // Who fired this projectile
    float damage{10.0f};
    float lifetime{5.0f};       // Time until despawn
    float speed{200.0f};
    uint8_t damageType{0};      // Physical, Fire, Ice, etc.
    uint8_t flags{0};

    static constexpr uint8_t FLAG_PIERCING = 0x01;
    static constexpr uint8_t FLAG_HOMING = 0x02;
    static constexpr uint8_t FLAG_EXPLOSIVE = 0x04;
};

/**
 * @brief Container data for Container entities (chests, barrels)
 */
struct ContainerData {
    uint32_t inventoryId{0};    // Reference to inventory storage
    uint16_t maxSlots{20};
    uint8_t containerType{0};   // Chest, Barrel, Corpse
    uint8_t lockLevel{0};       // 0 = unlocked
    bool isOpen{false};
};

/**
 * @brief Harvestable data for resource nodes (trees, ore)
 */
struct HarvestableData {
    HammerEngine::ResourceHandle yieldResource;
    int yieldMin{1};
    int yieldMax{3};
    float respawnTime{60.0f};   // Seconds until respawn
    float currentRespawn{0.0f}; // Time remaining
    uint8_t harvestType{0};     // Mining, Chopping, Gathering
    bool isDepleted{false};
};

/**
 * @brief Area effect data for AoE zones (spell effects, traps)
 */
struct AreaEffectData {
    EntityHandle owner;         // Who created this effect
    float radius{50.0f};
    float damage{5.0f};         // Damage per tick
    float tickInterval{0.5f};   // Seconds between ticks
    float duration{5.0f};       // Total duration
    float elapsed{0.0f};        // Time since creation
    float lastTick{0.0f};       // Time since last damage tick
    uint8_t effectType{0};      // Poison, Fire, Heal, Slow
};

/**
 * @brief Per-entity fixed-size waypoint storage slot (256 bytes, cache-aligned)
 *
 * Each entity owns one slot with space for MAX_WAYPOINTS_PER_ENTITY waypoints.
 * This eliminates contention from the old shared WaypointPool bump allocator.
 *
 * Benefits:
 * - Lock-free writes: Each entity writes to its own slot (no shared state)
 * - No fragmentation: Fixed memory per entity, overwrite in place
 * - Cache-friendly: 64-byte alignment, 4 cache lines per slot
 * - Simple: No allocation tracking, just overwrite the slot
 *
 * Threading: Safe for parallel writes when each thread writes to different entities.
 * pathRequestPending flag ensures single writer per entity at a time.
 */
struct alignas(64) FixedWaypointSlot {
    static constexpr size_t MAX_WAYPOINTS_PER_ENTITY = 32;
    Vector2D waypoints[MAX_WAYPOINTS_PER_ENTITY];

    [[nodiscard]] const Vector2D& operator[](size_t idx) const noexcept {
        assert(idx < MAX_WAYPOINTS_PER_ENTITY);
        return waypoints[idx];
    }

    Vector2D& operator[](size_t idx) noexcept {
        assert(idx < MAX_WAYPOINTS_PER_ENTITY);
        return waypoints[idx];
    }

    /** @brief Get read-only span of path waypoints */
    [[nodiscard]] std::span<const Vector2D> getPath(size_t length) const noexcept {
        return std::span<const Vector2D>(waypoints, std::min(length, MAX_WAYPOINTS_PER_ENTITY));
    }
};

static_assert(sizeof(FixedWaypointSlot) == 256, "FixedWaypointSlot must be 256 bytes (4 cache lines)");

/**
 * @brief Path state for AI entities (indexed by edmIndex)
 *
 * Stores pathfinding state for AI entities. Waypoints are stored in per-entity
 * FixedWaypointSlot for lock-free parallel writes with no contention.
 *
 * Threading: Safe for parallel reads during AI batch processing.
 * Each entity has its own waypoint slot - no shared state to contend on.
 */
struct PathData {
    uint16_t pathLength{0};             // Number of waypoints (max 32)
    uint16_t navIndex{0};               // Current waypoint index
    float pathUpdateTimer{0.0f};        // Time since last path update
    float progressTimer{0.0f};          // Time since last progress
    float lastNodeDistance{std::numeric_limits<float>::max()};
    float stallTimer{0.0f};             // Stall detection
    float pathRequestCooldown{0.0f};    // Prevent request spam
    Vector2D currentWaypoint{0, 0};     // Cached current waypoint for fast access
    bool hasPath{false};                // Quick check if path is valid
    std::atomic<uint8_t> pathRequestPending{0}; // Path request in flight (release/acquire)

    PathData() = default;
    PathData(const PathData&) = delete;
    PathData& operator=(const PathData&) = delete;
    PathData(PathData&& other) noexcept { *this = std::move(other); }
    PathData& operator=(PathData&& other) noexcept {
        if (this != &other) {
            pathLength = other.pathLength;
            navIndex = other.navIndex;
            pathUpdateTimer = other.pathUpdateTimer;
            progressTimer = other.progressTimer;
            lastNodeDistance = other.lastNodeDistance;
            stallTimer = other.stallTimer;
            pathRequestCooldown = other.pathRequestCooldown;
            currentWaypoint = other.currentWaypoint;
            hasPath = other.hasPath;
            pathRequestPending.store(
                other.pathRequestPending.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            other.pathRequestPending.store(0, std::memory_order_relaxed);
        }
        return *this;
    }

    void clear() noexcept {
        pathLength = 0;
        navIndex = 0;
        pathUpdateTimer = 0.0f;
        progressTimer = 0.0f;
        lastNodeDistance = std::numeric_limits<float>::max();
        stallTimer = 0.0f;
        pathRequestCooldown = 0.0f;
        currentWaypoint = Vector2D{0, 0};
        hasPath = false;
        pathRequestPending.store(0, std::memory_order_relaxed);
    }

    [[nodiscard]] bool isFollowingPath() const noexcept {
        return hasPath && navIndex < pathLength;
    }

    void advanceWaypoint() noexcept {
        if (navIndex < pathLength) {
            ++navIndex;
            progressTimer = 0.0f;
            stallTimer = 0.0f;
        }
    }

    [[nodiscard]] size_t size() const noexcept { return pathLength; }
};

/**
 * @brief Behavior type identifiers for AI behaviors
 */
enum class BehaviorType : uint8_t {
    Wander = 0,
    Guard = 1,
    Patrol = 2,
    Follow = 3,
    Chase = 4,
    Attack = 5,
    Flee = 6,
    Idle = 7,
    Custom = 8,
    COUNT = 9,
    None = 0xFF  // Invalid/uninitialized
};

/**
 * @brief Compact behavior-specific state (indexed by edmIndex like PathData)
 *
 * Uses tagged union - only ONE behavior can be active per entity at a time.
 * All pathfinding state is in PathData - this stores behavior-specific state only.
 *
 * Threading: Safe for parallel reads during AI batch processing.
 * Each thread accesses distinct edmIndex ranges.
 */
struct BehaviorData {
    // Common header (all behaviors)
    BehaviorType behaviorType{BehaviorType::None};
    uint8_t flags{0};
    uint8_t _pad[2]{};

    // Common separation state (used by most behaviors)
    float separationTimer{0.0f};
    Vector2D lastSepVelocity;

    // Common crowd analysis cache
    float lastCrowdAnalysis{0.0f};
    int cachedNearbyCount{0};
    Vector2D cachedClusterCenter;

    static constexpr uint8_t FLAG_VALID = 0x01;
    static constexpr uint8_t FLAG_INITIALIZED = 0x02;

    // Behavior-specific state union (largest is AttackState ~140 bytes)
    // Note: Union requires explicit constructor due to non-trivial Vector2D
    union StateUnion {
        // Default constructor initializes raw bytes to zero
        StateUnion() : raw{} {}
        struct { // WanderState (~64 bytes)
            Vector2D currentDirection;
            Vector2D previousVelocity;
            Vector2D lastStallPosition;
            float directionChangeTimer;
            float lastDirectionFlip;
            float startDelay;
            float stallTimer;
            float stallPositionVariance;
            float unstickTimer;
            bool movementStarted;
            uint8_t _pad[3];
        } wander;

        struct { // IdleState (~48 bytes)
            Vector2D originalPosition;
            Vector2D currentOffset;
            float movementTimer;
            float turnTimer;
            float movementInterval;
            float turnInterval;
            float currentAngle;
            bool initialized;
            uint8_t _pad[3];
        } idle;

        struct { // GuardState (~112 bytes)
            Vector2D assignedPosition;
            Vector2D lastKnownThreatPosition;
            Vector2D investigationTarget;
            Vector2D currentPatrolTarget;
            Vector2D roamTarget;
            float threatSightingTimer;
            float alertTimer;
            float investigationTimer;
            float positionCheckTimer;
            float patrolMoveTimer;
            float alertDecayTimer;
            float currentHeading;
            float roamTimer;
            uint32_t currentPatrolIndex;
            uint8_t currentAlertLevel;  // 0=Calm, 1=Suspicious, 2=Alert, 3=Combat
            uint8_t currentMode;
            bool hasActiveThreat;
            bool isInvestigating;
            bool returningToPost;
            bool onDuty;
            bool alertRaised;
            bool helpCalled;
        } guard;

        struct { // FollowState (~72 bytes)
            Vector2D lastTargetPosition;
            Vector2D currentVelocity;
            Vector2D desiredPosition;
            Vector2D formationOffset;
            Vector2D lastSepForce;
            float currentSpeed;
            float currentHeading;
            float backoffTimer;
            int formationSlot;
            bool isFollowing;
            bool targetMoving;
            bool inFormation;
            bool isStopped;
        } follow;

        struct { // FleeState (~80 bytes)
            Vector2D lastThreatPosition;
            Vector2D fleeDirection;
            Vector2D lastKnownSafeDirection;
            float fleeTimer;
            float directionChangeTimer;
            float panicTimer;
            float currentStamina;
            float zigzagTimer;
            float navRadius;
            float backoffTimer;
            int zigzagDirection;
            bool isFleeing;
            bool isInPanic;
            bool hasValidThreat;
            uint8_t _pad;
        } flee;

        struct { // ChaseState (~64 bytes)
            Vector2D lastKnownTargetPos;      // Last known target position
            Vector2D currentDirection;         // Current movement direction
            Vector2D lastStallPosition;        // Position when stall was detected
            float timeWithoutSight;            // Time since last line of sight
            float stallPositionVariance;       // Variance for stall detection
            float unstickTimer;                // Timer for unstick behavior
            float crowdCheckTimer;             // Throttle crowd detection
            float pathRequestCooldown;         // Cooldown between path requests
            float stallRecoveryCooldown;       // Cooldown after stall recovery
            float behaviorChangeCooldown;      // Cooldown for behavior state changes
            int recalcCounter;                 // Path recalculation counter
            int cachedChaserCount;             // Cached number of chasers nearby
            bool isChasing;                    // Currently in chase mode
            bool hasLineOfSight;               // Has line of sight to target
            uint8_t _pad[2];                   // Padding for alignment
        } chase;

        struct { // AttackState (~140 bytes)
            Vector2D lastTargetPosition;
            Vector2D attackPosition;
            Vector2D retreatPosition;
            Vector2D strafeVector;
            float attackTimer;
            float stateChangeTimer;
            float damageTimer;
            float comboTimer;
            float strafeTimer;
            float currentHealth;
            float maxHealth;
            float currentStamina;
            float targetDistance;
            float attackChargeTime;
            float recoveryTimer;
            float preferredAttackAngle;
            int currentCombo;
            int attacksInCombo;
            int strafeDirectionInt;
            uint8_t currentState;  // 0=Seeking, 1=Approaching, 2=Attacking, 3=Recovering, 4=Retreating, 5=Circling
            bool inCombat;
            bool hasTarget;
            bool isCharging;
            bool isRetreating;
            bool canAttack;
            bool lastAttackHit;
            bool specialAttackReady;
            bool circleStrafing;
            bool flanking;
            uint8_t _pad[2];
        } attack;

        uint8_t raw[144]; // Ensure union is large enough
    };

    StateUnion state;

    // Default constructor
    BehaviorData() = default;

    void clear() noexcept {
        behaviorType = BehaviorType::None;
        flags = 0;
        separationTimer = 0.0f;
        lastSepVelocity = Vector2D{};
        lastCrowdAnalysis = 0.0f;
        cachedNearbyCount = 0;
        cachedClusterCenter = Vector2D{};
        state = StateUnion{};
    }

    [[nodiscard]] bool isValid() const noexcept { return flags & FLAG_VALID; }

    void setValid(bool v) noexcept {
        if (v) flags |= FLAG_VALID;
        else flags &= ~FLAG_VALID;
    }

    [[nodiscard]] bool isInitialized() const noexcept { return flags & FLAG_INITIALIZED; }

    void setInitialized(bool v) noexcept {
        if (v) flags |= FLAG_INITIALIZED;
        else flags &= ~FLAG_INITIALIZED;
    }
};

// Ensure BehaviorData fits in ~200 bytes (3 cache lines)
static_assert(sizeof(BehaviorData) <= 200, "BehaviorData exceeds 200 bytes");

// ============================================================================
// ENTITY DATA MANAGER
// ============================================================================

/**
 * @brief Central data authority for all entity data
 *
 * This is a DATA STORE, not a processor. Systems read from and write to this
 * manager. It does not have an update() method - processing happens in
 * AIManager, CollisionManager, and type-specific systems.
 */
class EntityDataManager {
public:
    static EntityDataManager& Instance() {
        static EntityDataManager instance;
        return instance;
    }

    // ========================================================================
    // LIFECYCLE
    // ========================================================================

    /**
     * @brief Initialize the entity data manager
     * @return true if initialization successful
     */
    bool init();

    /**
     * @brief Check if manager is initialized
     */
    [[nodiscard]] bool isInitialized() const noexcept {
        return m_initialized.load(std::memory_order_acquire);
    }

    /**
     * @brief Clean up all entity data
     */
    void clean();

    /**
     * @brief Prepare for game state transition (clears all entities)
     */
    void prepareForStateTransition();

    // ========================================================================
    // ENTITY CREATION
    // ========================================================================

    /**
     * @brief Create a new NPC entity
     * @param position Initial world position
     * @param halfWidth Collision half-width
     * @param halfHeight Collision half-height
     * @return Handle to the created entity
     */
    EntityHandle createNPC(const Vector2D& position,
                          float halfWidth = 16.0f,
                          float halfHeight = 16.0f);

    /**
     * @brief Create the player entity
     * @param position Initial world position
     * @return Handle to the player entity
     */
    EntityHandle createPlayer(const Vector2D& position);

    /**
     * @brief Create a dropped item entity
     * @param position World position
     * @param resourceHandle Item template reference
     * @param quantity Stack size
     * @return Handle to the created entity
     */
    EntityHandle createDroppedItem(const Vector2D& position,
                                   HammerEngine::ResourceHandle resourceHandle,
                                   int quantity = 1);

    // ========================================================================
    // PHASE 1: REGISTRATION OF EXISTING ENTITIES (Parallel Storage)
    // These methods register entities that were created via the old pattern
    // (Entity subclass constructors). They mirror data into EntityDataManager
    // until Phase 4 when Entity becomes a lightweight view.
    // ========================================================================

    /**
     * @brief Register an existing NPC entity with EntityDataManager
     * @param entityId Existing entity ID from Entity::getID()
     * @param position Current position
     * @param halfWidth Collision half-width
     * @param halfHeight Collision half-height
     * @param health Current health (for CharacterData)
     * @param maxHealth Max health
     * @return Handle to the registered entity
     */
    EntityHandle registerNPC(EntityHandle::IDType entityId,
                             const Vector2D& position,
                             float halfWidth = 16.0f,
                             float halfHeight = 16.0f,
                             float health = 100.0f,
                             float maxHealth = 100.0f);

    /**
     * @brief Register an existing Player entity with EntityDataManager
     * @param entityId Existing entity ID from Entity::getID()
     * @param position Current position
     * @param halfWidth Collision half-width
     * @param halfHeight Collision half-height
     * @return Handle to the registered entity
     */
    EntityHandle registerPlayer(EntityHandle::IDType entityId,
                                const Vector2D& position,
                                float halfWidth = 32.0f,
                                float halfHeight = 32.0f);

    /**
     * @brief Register an existing DroppedItem entity with EntityDataManager
     * @param entityId Existing entity ID from Entity::getID()
     * @param position Current position
     * @param resourceHandle Item template reference
     * @param quantity Stack size
     * @return Handle to the registered entity
     */
    EntityHandle registerDroppedItem(EntityHandle::IDType entityId,
                                     const Vector2D& position,
                                     HammerEngine::ResourceHandle resourceHandle,
                                     int quantity = 1);

    /**
     * @brief Unregister an entity (called when Entity is destroyed)
     * @param entityId Entity ID to unregister
     */
    void unregisterEntity(EntityHandle::IDType entityId);

    /**
     * @brief Create a projectile entity
     * @param position Initial position
     * @param velocity Initial velocity
     * @param owner Entity that fired this projectile
     * @param damage Damage on hit
     * @param lifetime Time until despawn
     * @return Handle to the created entity
     */
    EntityHandle createProjectile(const Vector2D& position,
                                  const Vector2D& velocity,
                                  EntityHandle owner,
                                  float damage,
                                  float lifetime = 5.0f);

    /**
     * @brief Create an area effect entity
     * @param position Center position
     * @param radius Effect radius
     * @param owner Entity that created this effect
     * @param damage Damage per tick
     * @param duration Total duration
     * @return Handle to the created entity
     */
    EntityHandle createAreaEffect(const Vector2D& position,
                                  float radius,
                                  EntityHandle owner,
                                  float damage,
                                  float duration);

    /**
     * @brief Create a static obstacle entity (world geometry)
     * @param position Center position
     * @param halfWidth Collision half-width
     * @param halfHeight Collision half-height
     * @return Handle to the created entity
     *
     * Static obstacles are used for world tiles, walls, and terrain collision.
     * They don't move, have no AI, and use Hibernated tier for minimal overhead.
     */
    EntityHandle createStaticBody(const Vector2D& position,
                                  float halfWidth,
                                  float halfHeight);

    /**
     * @brief Create a trigger entity for detecting entity overlap
     * @param position Center position
     * @param halfWidth Collision half-width
     * @param halfHeight Collision half-height
     * @param tag Semantic tag (Water, BossArea, etc.)
     * @param type EventOnly (skip broadphase) or Physical (full collision)
     * @return Handle to the created trigger entity
     *
     * Triggers are stored in static storage (don't move).
     * EventOnly triggers skip physics broadphase, only detect player overlap.
     * Physical triggers participate in full broadphase + resolution.
     */
    EntityHandle createTrigger(const Vector2D& position,
                               float halfWidth,
                               float halfHeight,
                               HammerEngine::TriggerTag tag,
                               HammerEngine::TriggerType type);

    /**
     * @brief Mark an entity for destruction (processed at end of frame)
     * @param handle Entity to destroy
     */
    void destroyEntity(EntityHandle handle);

    /**
     * @brief Process pending destructions (call at end of frame)
     */
    void processDestructionQueue();

    // ========================================================================
    // HANDLE VALIDATION
    // ========================================================================

    /**
     * @brief Check if an entity handle is valid and refers to a live entity
     * @param handle Handle to validate
     * @return true if handle is valid and entity exists
     */
    [[nodiscard]] bool isValidHandle(EntityHandle handle) const;

    /**
     * @brief Get the storage index for a handle (internal use)
     * @param handle Entity handle
     * @return Storage index, or SIZE_MAX if invalid
     */
    [[nodiscard]] size_t getIndex(EntityHandle handle) const;

    /**
     * @brief Look up storage index by EntityID
     * @param entityId Entity's unique ID
     * @return Storage index, or SIZE_MAX if not registered
     */
    [[nodiscard]] size_t findIndexByEntityId(EntityHandle::IDType entityId) const {
        auto it = m_idToIndex.find(entityId);
        return (it != m_idToIndex.end()) ? it->second : SIZE_MAX;
    }

    // ========================================================================
    // TRANSFORM ACCESS (Single Source of Truth)
    // ========================================================================

    /**
     * @brief Get mutable transform data for an entity
     * @param handle Entity handle
     * @return Reference to transform data
     */
    [[nodiscard]] TransformData& getTransform(EntityHandle handle);

    /**
     * @brief Get const transform data for an entity
     * @param handle Entity handle
     * @return Const reference to transform data
     */
    [[nodiscard]] const TransformData& getTransform(EntityHandle handle) const;

    /**
     * @brief Get transform by storage index (for batch processing)
     * @param index Storage index
     * @return Reference to transform data
     */
    [[nodiscard]] TransformData& getTransformByIndex(size_t index);
    [[nodiscard]] const TransformData& getTransformByIndex(size_t index) const;

    /**
     * @brief Get static transform by index (for collision system)
     */
    [[nodiscard]] const TransformData& getStaticTransformByIndex(size_t index) const;

    // ========================================================================
    // HOT DATA ACCESS
    // ========================================================================

    /**
     * @brief Get hot data by handle
     */
    [[nodiscard]] EntityHotData& getHotData(EntityHandle handle);
    [[nodiscard]] const EntityHotData& getHotData(EntityHandle handle) const;

    /**
     * @brief Get hot data by index (for batch processing)
     */
    [[nodiscard]] EntityHotData& getHotDataByIndex(size_t index);
    [[nodiscard]] const EntityHotData& getHotDataByIndex(size_t index) const;

    /**
     * @brief Get read-only span of all hot data (for batch iteration)
     */
    [[nodiscard]] std::span<const EntityHotData> getHotDataArray() const;

    /**
     * @brief Get read-only span of static hot data (for collision system)
     */
    [[nodiscard]] std::span<const EntityHotData> getStaticHotDataArray() const;

    /**
     * @brief Get static hot data by index
     */
    [[nodiscard]] const EntityHotData& getStaticHotDataByIndex(size_t index) const;

    /**
     * @brief Get static entity index from ID
     */
    [[nodiscard]] size_t getStaticIndex(EntityHandle handle) const;

    // ========================================================================
    // TYPE-SPECIFIC DATA ACCESS
    // ========================================================================

    [[nodiscard]] CharacterData& getCharacterData(EntityHandle handle);
    [[nodiscard]] const CharacterData& getCharacterData(EntityHandle handle) const;

    [[nodiscard]] ItemData& getItemData(EntityHandle handle);
    [[nodiscard]] const ItemData& getItemData(EntityHandle handle) const;

    [[nodiscard]] ProjectileData& getProjectileData(EntityHandle handle);
    [[nodiscard]] const ProjectileData& getProjectileData(EntityHandle handle) const;

    [[nodiscard]] ContainerData& getContainerData(EntityHandle handle);
    [[nodiscard]] const ContainerData& getContainerData(EntityHandle handle) const;

    [[nodiscard]] HarvestableData& getHarvestableData(EntityHandle handle);
    [[nodiscard]] const HarvestableData& getHarvestableData(EntityHandle handle) const;

    [[nodiscard]] AreaEffectData& getAreaEffectData(EntityHandle handle);
    [[nodiscard]] const AreaEffectData& getAreaEffectData(EntityHandle handle) const;

    // ========================================================================
    // BY-INDEX TYPE-SPECIFIC ACCESS (for batch processing)
    // ========================================================================

    /**
     * @brief Get character data by EDM index (for batch processing)
     * @param index EDM index from getActiveIndices()
     * @return CharacterData for the entity
     * @note Only valid for NPC/Player entities
     */
    [[nodiscard]] CharacterData& getCharacterDataByIndex(size_t index);
    [[nodiscard]] const CharacterData& getCharacterDataByIndex(size_t index) const;

    // ========================================================================
    // PATH DATA ACCESS (for AI pathfinding - indexed by edmIndex)
    // ========================================================================

    /**
     * @brief Get path data by EDM index
     * @param index EDM index from getActiveIndices()
     * @return PathData for the entity
     * @note Path data grows lazily - accessing an index will ensure storage exists
     */
    [[nodiscard]] PathData& getPathData(size_t index);
    [[nodiscard]] const PathData& getPathData(size_t index) const;

    /**
     * @brief Check if path data exists for an entity
     * @param index EDM index
     * @return true if path data storage exists and index is valid
     */
    [[nodiscard]] bool hasPathData(size_t index) const noexcept;

    /**
     * @brief Ensure path data storage exists for an entity
     * @param index EDM index
     * Called automatically when AI behavior is assigned
     */
    void ensurePathData(size_t index);

    /**
     * @brief Clear path data for an entity (called on destruction)
     * @param index EDM index
     */
    void clearPathData(size_t index);

    /**
     * @brief Get raw waypoint slot for direct write (zero-copy)
     */
    [[nodiscard]] Vector2D* getWaypointSlot(size_t index) noexcept;

    /**
     * @brief Finalize path after direct write
     */
    void finalizePath(size_t index, uint16_t length) noexcept;

    /**
     * @brief Advance waypoint and update cached currentWaypoint
     * @param index EDM index
     * Call this instead of PathData::advanceWaypoint() to keep cache in sync.
     */
    void advanceWaypointWithCache(size_t index);

    /**
     * @brief Get waypoint from entity's path
     * @param entityIdx EDM index
     * @param waypointIdx Index within the path
     * @return Waypoint position (inline for hot-path access)
     */
    [[nodiscard]] Vector2D getWaypoint(size_t entityIdx, size_t waypointIdx) const;

    /**
     * @brief Get current waypoint for entity's path
     * @param entityIdx EDM index
     * @return Current waypoint position (inline for hot-path access)
     */
    [[nodiscard]] Vector2D getCurrentWaypoint(size_t entityIdx) const;

    /**
     * @brief Get goal (last waypoint) of entity's path
     * @param entityIdx EDM index
     * @return Goal position (inline for hot-path access)
     */
    [[nodiscard]] Vector2D getPathGoal(size_t entityIdx) const;

    /**
     * @brief Clear all waypoint slots (call on state transitions)
     * With per-entity slots, this just clears the vector.
     */
    void clearWaypointSlots() noexcept { m_waypointSlots.clear(); }

    // ========================================================================
    // BEHAVIOR DATA ACCESS (for AI behaviors - indexed by edmIndex)
    // ========================================================================

    /**
     * @brief Get behavior data by EDM index
     * @param index EDM index from getActiveIndices()
     * @return BehaviorData for the entity
     */
    [[nodiscard]] BehaviorData& getBehaviorData(size_t index);
    [[nodiscard]] const BehaviorData& getBehaviorData(size_t index) const;

    /**
     * @brief Check if behavior data exists and is valid for an entity
     * @param index EDM index
     * @return true if behavior data exists and is valid
     */
    [[nodiscard]] bool hasBehaviorData(size_t index) const noexcept;

    /**
     * @brief Initialize behavior data for a specific behavior type
     * @param index EDM index
     * @param type The BehaviorType to initialize
     */
    void initBehaviorData(size_t index, BehaviorType type);

    /**
     * @brief Clear behavior data for an entity (called on behavior change/destruction)
     * @param index EDM index
     */
    void clearBehaviorData(size_t index);

    // ========================================================================
    // SIMULATION TIER MANAGEMENT
    // ========================================================================

    /**
     * @brief Set the simulation tier for an entity
     * @param handle Entity handle
     * @param tier New simulation tier
     */
    void setSimulationTier(EntityHandle handle, SimulationTier tier);

    /**
     * @brief Update simulation tiers based on distance from reference point
     * @param referencePoint Typically player/camera position
     * @param activeRadius Entities within this are Active
     * @param backgroundRadius Entities within this (but outside activeRadius) are Background
     */
    void updateSimulationTiers(const Vector2D& referencePoint,
                               float activeRadius = 1500.0f,
                               float backgroundRadius = 10000.0f);

    /**
     * @brief Get indices of all Active tier entities
     */
    [[nodiscard]] std::span<const size_t> getActiveIndices() const;

    /**
     * @brief Get indices of Active tier entities with collision enabled
     * Cached and rebuilt when tiers change or collision is enabled/disabled.
     * Used by CollisionManager to avoid filtering in hot loop.
     */
    [[nodiscard]] std::span<const size_t> getActiveIndicesWithCollision() const;

    /**
     * @brief Get indices of Active tier entities that need trigger detection
     * Cached and rebuilt when trigger detection flag changes.
     * Used by CollisionManager for EventOnly trigger detection.
     */
    [[nodiscard]] std::span<const size_t> getTriggerDetectionIndices() const;

    /**
     * @brief Mark trigger detection indices as needing rebuild
     */
    void markTriggerDetectionDirty() noexcept { m_triggerDetectionDirty = true; }

    /**
     * @brief Get indices of all Background tier entities
     */
    [[nodiscard]] std::span<const size_t> getBackgroundIndices() const;

    /**
     * @brief Get indices of entities by kind
     */
    [[nodiscard]] std::span<const size_t> getIndicesByKind(EntityKind kind) const;

    // ========================================================================
    // QUERIES
    // ========================================================================

    /**
     * @brief Find entities within a radius
     * @param center Query center point
     * @param radius Search radius
     * @param outHandles Output vector for found handles
     * @param kindFilter Optional: only return entities of this kind (COUNT = all)
     */
    void queryEntitiesInRadius(const Vector2D& center,
                               float radius,
                               std::vector<EntityHandle>& outHandles,
                               EntityKind kindFilter = EntityKind::COUNT) const;

    /**
     * @brief Get total entity count
     */
    [[nodiscard]] size_t getEntityCount() const noexcept;

    /**
     * @brief Get count of entities by kind
     */
    [[nodiscard]] size_t getEntityCount(EntityKind kind) const noexcept;

    /**
     * @brief Get count of entities by tier
     */
    [[nodiscard]] size_t getEntityCount(SimulationTier tier) const noexcept;

    // ========================================================================
    // ENTITY ID LOOKUP
    // ========================================================================

    /**
     * @brief Get entity ID by index
     */
    [[nodiscard]] EntityHandle::IDType getEntityId(size_t index) const;

    /**
     * @brief Get handle by index
     */
    [[nodiscard]] EntityHandle getHandle(size_t index) const;

private:
    EntityDataManager() = default;
    ~EntityDataManager();

    EntityDataManager(const EntityDataManager&) = delete;
    EntityDataManager& operator=(const EntityDataManager&) = delete;

    // Internal allocation helpers
    size_t allocateSlot();
    void freeSlot(size_t index);
    uint8_t nextGeneration(size_t index);

    // ========================================================================
    // STORAGE (Structure of Arrays)
    // ========================================================================

    // Shared data (indexed by global entity index)
    std::vector<EntityHotData> m_hotData;           // Dynamic entities only
    std::vector<EntityHotData> m_staticHotData;     // Static entities (separate, not tiered)
    std::vector<EntityHandle::IDType> m_entityIds;
    std::vector<EntityHandle::IDType> m_staticEntityIds;

    // ID to index mapping
    std::unordered_map<EntityHandle::IDType, size_t> m_idToIndex;
    std::unordered_map<EntityHandle::IDType, size_t> m_staticIdToIndex;

    // Type-specific data (indexed by typeLocalIndex in EntityHotData)
    std::vector<CharacterData> m_characterData;      // Player + NPC
    std::vector<ItemData> m_itemData;                // DroppedItem
    std::vector<ProjectileData> m_projectileData;    // Projectile
    std::vector<ContainerData> m_containerData;      // Container
    std::vector<HarvestableData> m_harvestableData;  // Harvestable
    std::vector<AreaEffectData> m_areaEffectData;    // AreaEffect

    // Path data (indexed by edmIndex, sparse - grows lazily for AI entities)
    std::vector<PathData> m_pathData;

    // Per-entity waypoint slots (indexed parallel to m_pathData)
    // Each entity owns one 256-byte slot for lock-free writes
    std::vector<FixedWaypointSlot> m_waypointSlots;
    // Behavior data (indexed by edmIndex, pre-allocated alongside hotData)
    std::vector<BehaviorData> m_behaviorData;

    // Type-specific free-lists (reuse indices when entities are destroyed)
    std::vector<uint32_t> m_freeCharacterSlots;
    std::vector<uint32_t> m_freeItemSlots;
    std::vector<uint32_t> m_freeProjectileSlots;
    std::vector<uint32_t> m_freeContainerSlots;
    std::vector<uint32_t> m_freeHarvestableSlots;
    std::vector<uint32_t> m_freeAreaEffectSlots;

    // Tier indices (rebuilt when tiers change)
    std::vector<size_t> m_activeIndices;
    std::vector<size_t> m_backgroundIndices;
    std::vector<size_t> m_hibernatedIndices;
    bool m_tierIndicesDirty{true};

    // Collision-enabled active indices (cached for CollisionManager optimization)
    mutable std::vector<size_t> m_activeCollisionIndices;
    mutable bool m_activeCollisionDirty{true};

    // Trigger detection indices (cached for CollisionManager optimization)
    mutable std::vector<size_t> m_triggerDetectionIndices;
    mutable bool m_triggerDetectionDirty{true};

    // Kind indices
    std::array<std::vector<size_t>, static_cast<size_t>(EntityKind::COUNT)> m_kindIndices;
    bool m_kindIndicesDirty{true};

    // Destruction queue and processing buffer (avoid per-frame allocation)
    std::vector<EntityHandle> m_destructionQueue;
    std::vector<EntityHandle> m_destroyBuffer;  // Reused in processDestructionQueue

    // Free list for slot reuse
    std::vector<size_t> m_freeSlots;
    std::vector<size_t> m_freeStaticSlots;

    // Generation counters per slot (for stale handle detection)
    std::vector<uint8_t> m_generations;
    std::vector<uint8_t> m_staticGenerations;

    // Thread safety (destruction queue only - structural ops are main-thread-only)
    std::mutex m_destructionMutex;

    // State
    std::atomic<bool> m_initialized{false};

    // Counters
    std::atomic<size_t> m_totalEntityCount{0};
    std::array<std::atomic<size_t>, static_cast<size_t>(EntityKind::COUNT)> m_countByKind{};
    std::array<std::atomic<size_t>, 3> m_countByTier{};  // Active, Background, Hibernated
};

// ============================================================================
// INLINE HOT-PATH ACCESSORS
// ============================================================================
// These accessors are inlined for zero-overhead access in hot loops.
// They are called thousands of times per frame in AIManager::processBatch(),
// CollisionManager, and PathfinderManager. Inlining eliminates ~5-20 cycles
// of function call overhead per access.

inline EntityHotData& EntityDataManager::getHotDataByIndex(size_t index) {
    assert(index < m_hotData.size() && "Index out of bounds");
    return m_hotData[index];
}

inline const EntityHotData& EntityDataManager::getHotDataByIndex(size_t index) const {
    assert(index < m_hotData.size() && "Index out of bounds");
    return m_hotData[index];
}

inline TransformData& EntityDataManager::getTransformByIndex(size_t index) {
    assert(index < m_hotData.size() && "Index out of bounds");
    return m_hotData[index].transform;
}

inline const TransformData& EntityDataManager::getTransformByIndex(size_t index) const {
    assert(index < m_hotData.size() && "Index out of bounds");
    return m_hotData[index].transform;
}

inline BehaviorData& EntityDataManager::getBehaviorData(size_t index) {
    assert(index < m_behaviorData.size() && "BehaviorData index out of bounds");
    return m_behaviorData[index];
}

inline const BehaviorData& EntityDataManager::getBehaviorData(size_t index) const {
    assert(index < m_behaviorData.size() && "BehaviorData index out of bounds");
    return m_behaviorData[index];
}

inline PathData& EntityDataManager::getPathData(size_t index) {
    // PathData is pre-allocated in allocateSlot(), no lazy resize needed
    assert(index < m_pathData.size() && "PathData not pre-allocated for index");
    return m_pathData[index];
}

inline const PathData& EntityDataManager::getPathData(size_t index) const {
    assert(index < m_pathData.size() && "Path data index out of bounds");
    return m_pathData[index];
}

// Per-entity waypoint slot accessors - O(1) access with no shared state
inline Vector2D* EntityDataManager::getWaypointSlot(size_t index) noexcept {
    return m_waypointSlots[index].waypoints;
}

inline Vector2D EntityDataManager::getWaypoint(size_t entityIdx, size_t waypointIdx) const {
    assert(entityIdx < m_waypointSlots.size() && "Entity waypoint slot out of bounds");
    const auto& pd = m_pathData[entityIdx];
    assert(waypointIdx < pd.pathLength && "Waypoint index out of bounds");
    return m_waypointSlots[entityIdx][waypointIdx];
}

inline Vector2D EntityDataManager::getCurrentWaypoint(size_t entityIdx) const {
    assert(entityIdx < m_waypointSlots.size() && "Entity waypoint slot out of bounds");
    const auto& pd = m_pathData[entityIdx];
    assert(pd.navIndex < pd.pathLength && "Current waypoint out of bounds");
    return m_waypointSlots[entityIdx][pd.navIndex];
}

inline Vector2D EntityDataManager::getPathGoal(size_t entityIdx) const {
    assert(entityIdx < m_waypointSlots.size() && "Entity waypoint slot out of bounds");
    const auto& pd = m_pathData[entityIdx];
    assert(pd.pathLength > 0 && "Cannot get goal of empty path");
    return m_waypointSlots[entityIdx][pd.pathLength - 1];
}

inline CharacterData& EntityDataManager::getCharacterDataByIndex(size_t index) {
    assert(index < m_hotData.size() && "Index out of bounds");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_characterData.size() && "Type index out of bounds");
    return m_characterData[typeIndex];
}

inline const CharacterData& EntityDataManager::getCharacterDataByIndex(size_t index) const {
    assert(index < m_hotData.size() && "Index out of bounds");
    uint32_t typeIndex = m_hotData[index].typeLocalIndex;
    assert(typeIndex < m_characterData.size() && "Type index out of bounds");
    return m_characterData[typeIndex];
}

#endif // ENTITY_DATA_MANAGER_HPP
