/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef BEHAVIOR_EXECUTORS_HPP
#define BEHAVIOR_EXECUTORS_HPP

/**
 * @file BehaviorExecutors.hpp
 * @brief Data-oriented behavior execution functions (no virtual dispatch)
 *
 * This file provides free functions for executing AI behaviors. Each behavior
 * type has an execute function that takes a BehaviorContext and its config.
 *
 * Benefits over virtual dispatch:
 * - No vtable lookup overhead
 * - Better cache locality (configs stored contiguously in EDM)
 * - Switch-based dispatch is branch-predicted after first iteration
 * - Easier to add SIMD optimization in the future
 *
 * All state is stored in EDM's BehaviorData union.
 * All config is stored in EDM's BehaviorConfigData union.
 */

#include "ai/BehaviorConfig.hpp"
#include "managers/EntityDataManager.hpp"  // For BehaviorType, BehaviorData, PathData, etc.
#include "managers/EventManager.hpp"       // For EventManager::DeferredEvent
#include <vector>

/**
 * @brief Context passed to behavior execution functions
 *
 * Provides lock-free access to entity state during behavior execution.
 * Pre-populated by AIManager before each behavior update.
 */
struct BehaviorContext {
    TransformData& transform;      // Direct read/write access (lock-free)
    EntityHotData& hotData;        // Entity metadata (halfWidth, halfHeight, etc.)
    EntityHandle::IDType entityId; // For staggering calculations
    size_t edmIndex;               // EDM index for vector-based state storage (contention-free)
    float deltaTime;

    // Player info cached once per update batch - avoids lock contention in behaviors
    EntityHandle playerHandle;     // Cached player handle (no lock needed)
    Vector2D playerPosition;       // Cached player position (no lock needed)
    Vector2D playerVelocity;       // Cached player velocity (for movement detection)
    bool playerValid{false};       // Whether player is valid this frame

    // Pre-fetched EDM data - avoids repeated Instance() calls in behaviors
    BehaviorData& behaviorData;      // Guaranteed valid for behavior execution
    PathData* pathData{nullptr};     // Optional: some behaviors support direct movement fallback
    NPCMemoryData& memoryData;       // Guaranteed valid for NPC behavior execution
    const CharacterData& characterData;  // Guaranteed valid for behavior execution

    // World bounds cached once per frame - avoids WorldManager::Instance() calls in behaviors
    float worldMinX{0.0f};
    float worldMinY{0.0f};
    float worldMaxX{0.0f};
    float worldMaxY{0.0f};
    bool worldBoundsValid{false};         // Whether world bounds are available

    // Game time cached once per frame - absolute time for memory timestamps, encounter logging,
    // and future combat timing comparisons. Not currently consumed by behaviors but available
    // for systems that need absolute time (e.g., MemoryEntry timestamps).
    float gameTime{0.0f};

    BehaviorContext(TransformData& t, EntityHotData& h, EntityHandle::IDType id, size_t idx, float dt,
                    EntityHandle pHandle, const Vector2D& pPos, const Vector2D& pVel, bool pValid,
                    BehaviorData& bData, PathData* pData, NPCMemoryData& mData,
                    const CharacterData& cData,
                    float wMinX, float wMinY, float wMaxX, float wMaxY, bool wBoundsValid,
                    float gTime)
        : transform(t), hotData(h), entityId(id), edmIndex(idx), deltaTime(dt),
          playerHandle(pHandle), playerPosition(pPos), playerVelocity(pVel), playerValid(pValid),
          behaviorData(bData), pathData(pData), memoryData(mData), characterData(cData),
          worldMinX(wMinX), worldMinY(wMinY), worldMaxX(wMaxX), worldMaxY(wMaxY),
          worldBoundsValid(wBoundsValid), gameTime(gTime) {}
};

// ============================================================================
// BEHAVIOR MESSAGE IDs
// ============================================================================

/**
 * @brief Message IDs for behavior-specific commands
 *
 * These can be queued via queueBehaviorMessage() and processed at the start
 * of each behavior's execute function via processPendingMessages().
 */
namespace BehaviorMessage {
    // Attack messages
    constexpr uint8_t ATTACK_TARGET = 1;     // Force attack on explicit target
    constexpr uint8_t RETREAT = 2;           // Allies retreat when nearby attacker retreats

    // Flee messages
    constexpr uint8_t PANIC = 10;            // Witness lethal combat — force flee
    constexpr uint8_t CALM_DOWN = 11;        // Guard all-clear — reduce fear

    // Distress messages
    constexpr uint8_t DISTRESS = 20;         // Victim/fleeing entity calls nearby guards — force SUSPICIOUS

    // Guard messages
    constexpr uint8_t RAISE_ALERT = 22;      // Guard/civilian under attack — force HOSTILE
}

namespace Behaviors {

// ============================================================================
// EXECUTION FUNCTIONS (one per behavior type)
// ============================================================================

/**
 * @brief Execute Idle behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Idle behavior configuration
 */
void executeIdle(BehaviorContext& ctx, const VoidLight::IdleBehaviorConfig& config);

/**
 * @brief Execute Wander behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Wander behavior configuration
 */
void executeWander(BehaviorContext& ctx, const VoidLight::WanderBehaviorConfig& config);

/**
 * @brief Execute Chase behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Chase behavior configuration
 */
void executeChase(BehaviorContext& ctx, const VoidLight::ChaseBehaviorConfig& config);

/**
 * @brief Execute Patrol behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Patrol behavior configuration
 */
void executePatrol(BehaviorContext& ctx, const VoidLight::PatrolBehaviorConfig& config);

/**
 * @brief Execute Guard behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Guard behavior configuration
 */
void executeGuard(BehaviorContext& ctx, const VoidLight::GuardBehaviorConfig& config);

/**
 * @brief Execute Attack behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Attack behavior configuration
 */
void executeAttack(BehaviorContext& ctx, const VoidLight::AttackBehaviorConfig& config);

/**
 * @brief Execute Flee behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Flee behavior configuration
 */
void executeFlee(BehaviorContext& ctx, const VoidLight::FleeBehaviorConfig& config);

/**
 * @brief Execute Follow behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Follow behavior configuration
 */
void executeFollow(BehaviorContext& ctx, const VoidLight::FollowBehaviorConfig& config);

// ============================================================================
// INITIALIZATION FUNCTIONS (called when behavior assigned)
// ============================================================================

/**
 * @brief Initialize Idle behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Idle behavior configuration
 */
void initIdle(size_t edmIndex, const VoidLight::IdleBehaviorConfig& config);

/**
 * @brief Initialize Wander behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Wander behavior configuration
 */
void initWander(size_t edmIndex, const VoidLight::WanderBehaviorConfig& config);

/**
 * @brief Initialize Chase behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Chase behavior configuration
 */
void initChase(size_t edmIndex, const VoidLight::ChaseBehaviorConfig& config);

/**
 * @brief Initialize Patrol behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Patrol behavior configuration
 */
void initPatrol(size_t edmIndex, const VoidLight::PatrolBehaviorConfig& config);

/**
 * @brief Initialize Guard behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Guard behavior configuration
 */
void initGuard(size_t edmIndex, const VoidLight::GuardBehaviorConfig& config);

/**
 * @brief Initialize Attack behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Attack behavior configuration
 */
void initAttack(size_t edmIndex, const VoidLight::AttackBehaviorConfig& config);

/**
 * @brief Initialize Flee behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Flee behavior configuration
 */
void initFlee(size_t edmIndex, const VoidLight::FleeBehaviorConfig& config);

/**
 * @brief Initialize Follow behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Follow behavior configuration
 */
void initFollow(size_t edmIndex, const VoidLight::FollowBehaviorConfig& config);

// ============================================================================
// MAIN DISPATCHER
// ============================================================================

/**
 * @brief Execute behavior based on config type (switch dispatch)
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param configData Behavior configuration with type tag
 *
 * This is the main entry point for behavior execution. It dispatches to
 * the appropriate execute function based on the behavior type.
 */
void execute(BehaviorContext& ctx, const VoidLight::BehaviorConfigData& configData);

/**
 * @brief Initialize behavior state based on config type (switch dispatch)
 * @param edmIndex Entity's index in EDM
 * @param configData Behavior configuration with type tag
 *
 * This is the main entry point for behavior initialization. It dispatches to
 * the appropriate init function based on the behavior type.
 */
void init(size_t edmIndex, const VoidLight::BehaviorConfigData& configData);

// ============================================================================
// BEHAVIOR SWITCHING (called from within behaviors)
// ============================================================================

/**
 * @brief Switch entity to a new behavior type with default config
 * @param edmIndex Entity's index in EDM
 * @param newType The new behavior type to switch to
 *
 * Queues a behavior transition command for main-thread commit by AIManager.
 * Uses default config for the behavior type.
 */
void switchBehavior(size_t edmIndex, BehaviorType newType);

/**
 * @brief Switch entity to a new behavior with custom config
 * @param edmIndex Entity's index in EDM
 * @param config Custom behavior configuration
 *
 * Queues a behavior transition command for main-thread commit by AIManager.
 */
void switchBehavior(size_t edmIndex, const VoidLight::BehaviorConfigData& config);

// ============================================================================
// UTILITY FUNCTIONS (shared across behaviors)
// ============================================================================

/**
 * @brief Check if entity was recently attacked
 * @param ctx BehaviorContext with memoryData
 * @param thresholdSeconds Time window to consider "recent"
 * @return true if lastCombatTime < threshold and has valid attacker
 */
bool isUnderRecentAttack(const BehaviorContext& ctx, float thresholdSeconds = 1.0f);

/**
 * @brief Check if entity should flee based on accumulated fear
 * @param ctx BehaviorContext with memoryData
 * @return true if fear exceeds threshold and bravery is low
 */
bool shouldFleeFromFear(const BehaviorContext& ctx);

/**
 * @brief Check if entity is on alert from suspicion
 * @param ctx BehaviorContext with memoryData
 * @param suspicionThreshold Suspicion level to consider "on alert" (default 0.5)
 * @return true if suspicion exceeds threshold
 */
bool isOnAlert(const BehaviorContext& ctx, float suspicionThreshold = 0.5f);

/**
 * @brief Check if entity should fight back against its attacker
 * @param ctx BehaviorContext with memoryData
 * @return true if brave + aggressive enough and attacker is alive
 *
 * Any faction. Checks bravery (with crowd courage), combined aggression,
 * and verifies lastAttacker is still alive. Works for NPC-on-NPC and Player-on-NPC.
 */
bool shouldRetaliate(const BehaviorContext& ctx);

/**
 * @brief Get the handle of the last entity that attacked this one
 * @param ctx BehaviorContext with memoryData
 * @return EntityHandle of attacker, or invalid handle if none
 */
EntityHandle getLastAttacker(const BehaviorContext& ctx);

/**
 * @brief Calculate relationship level between NPC and a subject entity
 * @param npcHandle NPC whose memories/emotions to evaluate
 * @param subjectHandle Entity to evaluate relationship with (typically player)
 * @return Relationship score from -1.0 (hostile) to +1.0 (trusted), 0.0 if neutral/unknown
 *
 * Computed from NPC's emotional state and inline interaction memories with the subject.
 * Standalone query — does not require BehaviorContext, callable from controllers.
 */
[[nodiscard]] float getRelationshipLevel(EntityHandle npcHandle, EntityHandle subjectHandle);

/**
 * @brief Normalize a direction vector
 * @param vector Vector to normalize
 * @return Normalized vector (unit length) or zero vector if input is zero
 */
Vector2D normalizeDirection(const Vector2D& vector);

/**
 * @brief Calculate angle from one position to another
 * @param from Starting position
 * @param to Target position
 * @return Angle in radians
 */
float calculateAngleToTarget(const Vector2D& from, const Vector2D& to);

/**
 * @brief Get default config for a behavior type
 * @param type Behavior type
 * @return Default BehaviorConfigData for that type
 */
VoidLight::BehaviorConfigData getDefaultConfig(BehaviorType type);

// ============================================================================
// COMBAT EVENT PROCESSING (AI-layer wrappers for EDM data + emotion logic)
// ============================================================================

// ============================================================================
// WORLD BOUNDS CACHE (main-thread update, worker-thread read)
// ============================================================================

/**
 * @brief Cache world bounds for use by behaviors during batch processing
 *
 * Called from AIManager::update() on the main thread before batch processing.
 * Behaviors read the cached values during worker thread execution, avoiding
 * WorldManager::Instance() calls from worker threads.
 */
void cacheWorldBounds();

/**
 * @brief Get cached world bounds
 * @param[out] minX, minY, maxX, maxY World bounds in tile coordinates
 * @return true if cached bounds are valid
 */
bool getCachedWorldBounds(float& minX, float& minY, float& maxX, float& maxY);

// ============================================================================
// MESSAGE QUEUE FUNCTIONS
// ============================================================================

/**
 * @brief Queue a message for an entity's behavior
 * @param edmIndex Entity's index in EDM
 * @param messageId BehaviorMessage::* constant
 * @param param Optional parameter (behavior-specific)
 *
 * Enqueues a command for AIManager's main-thread pre-pass commit.
 *
 * @note THREAD SAFETY: Safe from any thread.
 */
void queueBehaviorMessage(size_t edmIndex, uint8_t messageId, uint8_t param = 0);

/**
 * @brief Clear all pending messages for an entity
 * @param edmIndex Entity's index in EDM
 * @note Must be called from AIManager commit path on main thread.
 */
void clearPendingMessages(size_t edmIndex);

// ============================================================================
// DAMAGE EVENT COLLECTION (thread-local buffer for lock-free combat)
// ============================================================================

/**
 * @brief Collect deferred damage events from thread-local buffer
 * @return Vector of deferred events to dispatch (moved, buffer cleared)
 *
 * Attack behaviors defer damage event creation to avoid locking EventManager
 * during batch execution. Call this after processBatch to collect and dispatch.
 */
void collectDeferredDamageEvents(std::vector<EventManager::DeferredEvent>& out);

// ============================================================================
// DEFERRED BEHAVIOR MESSAGE COLLECTION (thread-local buffer for inter-entity messages)
// ============================================================================

/**
 * @brief Defer a behavior message for thread-safe delivery via AICommandBus
 * @param targetEdmIndex Target entity's EDM index
 * @param messageId BehaviorMessage::* constant
 * @param param Optional parameter
 *
 * Safe to call from worker threads during batch processing.
 * Enqueues directly to AICommandBus.
 */
void deferBehaviorMessage(size_t targetEdmIndex, uint8_t messageId, uint8_t param = 0);

} // namespace Behaviors

#endif // BEHAVIOR_EXECUTORS_HPP
