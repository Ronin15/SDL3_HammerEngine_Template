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
#include "managers/EntityDataManager.hpp"  // For BehaviorType, BehaviorContext
#include "managers/EventManager.hpp"       // For EventManager::DeferredEvent
#include <vector>

// Forward declarations
struct BehaviorContext;
struct BehaviorData;

namespace Behaviors {

// ============================================================================
// EXECUTION FUNCTIONS (one per behavior type)
// ============================================================================

/**
 * @brief Execute Idle behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Idle behavior configuration
 */
void executeIdle(BehaviorContext& ctx, const HammerEngine::IdleBehaviorConfig& config);

/**
 * @brief Execute Wander behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Wander behavior configuration
 */
void executeWander(BehaviorContext& ctx, const HammerEngine::WanderBehaviorConfig& config);

/**
 * @brief Execute Chase behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Chase behavior configuration
 */
void executeChase(BehaviorContext& ctx, const HammerEngine::ChaseBehaviorConfig& config);

/**
 * @brief Execute Patrol behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Patrol behavior configuration
 */
void executePatrol(BehaviorContext& ctx, const HammerEngine::PatrolBehaviorConfig& config);

/**
 * @brief Execute Guard behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Guard behavior configuration
 */
void executeGuard(BehaviorContext& ctx, const HammerEngine::GuardBehaviorConfig& config);

/**
 * @brief Execute Attack behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Attack behavior configuration
 */
void executeAttack(BehaviorContext& ctx, const HammerEngine::AttackBehaviorConfig& config);

/**
 * @brief Execute Flee behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Flee behavior configuration
 */
void executeFlee(BehaviorContext& ctx, const HammerEngine::FleeBehaviorConfig& config);

/**
 * @brief Execute Follow behavior logic
 * @param ctx Pre-populated BehaviorContext with EDM references
 * @param config Follow behavior configuration
 */
void executeFollow(BehaviorContext& ctx, const HammerEngine::FollowBehaviorConfig& config);

// ============================================================================
// INITIALIZATION FUNCTIONS (called when behavior assigned)
// ============================================================================

/**
 * @brief Initialize Idle behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Idle behavior configuration
 */
void initIdle(size_t edmIndex, const HammerEngine::IdleBehaviorConfig& config);

/**
 * @brief Initialize Wander behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Wander behavior configuration
 */
void initWander(size_t edmIndex, const HammerEngine::WanderBehaviorConfig& config);

/**
 * @brief Initialize Chase behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Chase behavior configuration
 */
void initChase(size_t edmIndex, const HammerEngine::ChaseBehaviorConfig& config);

/**
 * @brief Initialize Patrol behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Patrol behavior configuration
 */
void initPatrol(size_t edmIndex, const HammerEngine::PatrolBehaviorConfig& config);

/**
 * @brief Initialize Guard behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Guard behavior configuration
 */
void initGuard(size_t edmIndex, const HammerEngine::GuardBehaviorConfig& config);

/**
 * @brief Initialize Attack behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Attack behavior configuration
 */
void initAttack(size_t edmIndex, const HammerEngine::AttackBehaviorConfig& config);

/**
 * @brief Initialize Flee behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Flee behavior configuration
 */
void initFlee(size_t edmIndex, const HammerEngine::FleeBehaviorConfig& config);

/**
 * @brief Initialize Follow behavior state in EDM
 * @param edmIndex Entity's index in EDM
 * @param config Follow behavior configuration
 */
void initFollow(size_t edmIndex, const HammerEngine::FollowBehaviorConfig& config);

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
void execute(BehaviorContext& ctx, const HammerEngine::BehaviorConfigData& configData);

/**
 * @brief Initialize behavior state based on config type (switch dispatch)
 * @param edmIndex Entity's index in EDM
 * @param configData Behavior configuration with type tag
 *
 * This is the main entry point for behavior initialization. It dispatches to
 * the appropriate init function based on the behavior type.
 */
void init(size_t edmIndex, const HammerEngine::BehaviorConfigData& configData);

// ============================================================================
// BEHAVIOR SWITCHING (called from within behaviors)
// ============================================================================

/**
 * @brief Switch entity to a new behavior type with default config
 * @param edmIndex Entity's index in EDM
 * @param newType The new behavior type to switch to
 *
 * Called by behaviors when they need to transition (e.g., Idle -> Flee when attacked).
 * Uses default config for the behavior type.
 */
void switchBehavior(size_t edmIndex, BehaviorType newType);

/**
 * @brief Switch entity to a new behavior with custom config
 * @param edmIndex Entity's index in EDM
 * @param config Custom behavior configuration
 *
 * Called by behaviors when they need to transition with specific configuration.
 */
void switchBehavior(size_t edmIndex, const HammerEngine::BehaviorConfigData& config);

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
 * @brief Get the handle of the last entity that attacked this one
 * @param ctx BehaviorContext with memoryData
 * @return EntityHandle of attacker, or invalid handle if none
 */
EntityHandle getLastAttacker(const BehaviorContext& ctx);

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
HammerEngine::BehaviorConfigData getDefaultConfig(BehaviorType type);

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
std::vector<EventManager::DeferredEvent> collectDeferredDamageEvents();

} // namespace Behaviors

#endif // BEHAVIOR_EXECUTORS_HPP
