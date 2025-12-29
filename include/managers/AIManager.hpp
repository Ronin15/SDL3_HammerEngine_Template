/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef AI_MANAGER_HPP
#define AI_MANAGER_HPP

/**
 * @file AIManager.hpp
 * @brief High-performance AI manager with cross-platform optimization
 *
 * Enhanced AIManager with Windows performance fixes and optimizations:
 * - Asynchronous (non-blocking) AI processing for optimal game engine
 * performance
 * - ThreadSystem and WorkerBudget integration for optimal scaling
 * - Type-indexed behavior storage for fast lookups
 * - Cache-friendly data structures with reduced lock contention
 * - Cross-platform threading optimizations (Windows/Linux/Mac)
 * - Smart pointer usage throughout for memory safety
 * - Scales to 10k+ entities while maintaining 60+ FPS
 */

#include "ai/AIBehavior.hpp"
#include "core/WorkerBudget.hpp"
#include "entities/Entity.hpp"
#include "managers/CollisionManager.hpp"
#include <array>
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

// PathfinderManager available for centralized pathfinding services
class PathfinderManager;

// Performance configuration constants
namespace AIConfig {
// Assignment per-frame limit removed. Assignment batching is now dynamic and
// thread-aware.
constexpr size_t ASSIGNMENT_QUEUE_RESERVE =
    1000; // Reserve capacity for assignment queue
} // namespace AIConfig

/**
 * @brief Behavior type enumeration for fast dispatch
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
  COUNT = 9
};

/**
 * @brief Cache-efficient AI entity data using Structure of Arrays (SoA)
 * Hot data (frequently accessed) is separated from cold data for better cache
 * performance
 *
 * NOTE: Position data is now owned by EntityDataManager (Phase 2 of Entity System Overhaul).
 * AIManager reads positions from Entity::getPosition() which will eventually
 * redirect to EntityDataManager in Phase 4.
 */
struct AIEntityData {
  // Hot data - accessed every frame
  // Position removed: EntityDataManager is the single source of truth for transforms
  struct HotData {
    float distanceSquared; // Cached distance to player (4 bytes)
    uint16_t frameCounter; // Frame counter (2 bytes)
    uint8_t priority;      // Priority level (1 byte)
    uint8_t behaviorType;  // Behavior type enum (1 byte)
    bool active;           // Active flag (1 byte)
    bool shouldUpdate;     // Update flag (1 byte)
    // Total: 10 bytes, padding to 16 for cache alignment
    uint8_t padding[6];
  };

  // Cold data - accessed occasionally
  EntityPtr entity;
  std::shared_ptr<AIBehavior> behavior;
  float lastUpdateTime;

  AIEntityData() : entity(nullptr), behavior(nullptr), lastUpdateTime(0.0f) {}
};

/**
 * @brief Pre-fetched batch data for lock-free parallel processing
 *
 * This struct holds copies of all entity data needed for AI processing.
 * By copying all data once (single lock), batches can process in parallel
 * without any lock contention, eliminating serialized lock acquisition bottleneck.
 */
struct PreFetchedBatchData {
    std::vector<EntityPtr> entities;
    std::vector<std::shared_ptr<AIBehavior>> behaviors;
    std::vector<AIEntityData::HotData> hotDataCopy;
    std::vector<size_t> edmIndices;  // EntityDataManager indices for lock-free batch access
    // NOTE: halfWidths/halfHeights REMOVED - accessed via EntityDataManager::getHotDataByIndex()

    void reserve(size_t capacity) {
        entities.reserve(capacity);
        behaviors.reserve(capacity);
        hotDataCopy.reserve(capacity);
        edmIndices.reserve(capacity);
    }
};

/**
 * @brief High-performance AI Manager
 */
class AIManager {
public:
  static AIManager &Instance() {
    static AIManager instance;
    return instance;
  }

  /**
   * @brief Initializes the AI Manager and its internal systems
   * @return true if initialization successful, false otherwise
   */
  bool init();

  /**
   * @brief Checks if the AI Manager has been initialized
   * @return true if initialized, false otherwise
   */
  bool isInitialized() const {
    return m_initialized.load(std::memory_order_acquire);
  }

  /**
   * @brief Cleans up all AI resources and marks manager as shut down
   */
  void clean();

  /**
   * @brief Prepares for state transition by safely cleaning up entities
   * @details Call this before exit() in game states to avoid deadlocks
   */
  void prepareForStateTransition();

  /**
   * @brief Updates all active AI entities using lock-free asynchronous
   * processing
   *
   * PERFORMANCE IMPROVEMENTS:
   * - Lock-free double buffering eliminates contention
   * - Cache-efficient SoA layout for 3-4x better performance
   * - Optimized scalar distance calculations for scattered memory access
   * - Simplified batch processing with WorkerBudget integration
   *
   * Key Features:
   * - Zero locks during update phase
   * - Cache-friendly memory access patterns
   * - Cross-platform threading optimizations
   * - Scales to 20k+ entities at 60+ FPS
   *
   * @param deltaTime Time elapsed since last update in seconds
   */
  void update(float deltaTime);

  /**
   * @brief Waits for all async batch operations to complete
   *
   * This should be called before systems that depend on AI collision updates
   * (e.g., CollisionManager) to ensure all async collision data is ready.
   *
   * Fast path: ~1ns atomic check if no pending batches
   * Slow path: blocks until all batches complete on low-core systems
   */
  void waitForAsyncBatchCompletion();

  /**
   * @brief Wait for all pending behavior assignment batches to complete
   *
   * Provides deterministic synchronization for state transitions.
   * Blocks until all assignment futures complete, ensuring no dangling references
   * to entity data that may be cleared during state changes.
   *
   * Fast path: ~1ns check if no assignments running
   * Slow path: blocks until all assignment batches complete
   */
  void waitForAssignmentCompletion();

  /**
   * @brief Checks if AIManager has been shut down
   * @return true if manager is shut down, false otherwise
   */
  bool isShutdown() const { return m_isShutdown; }

  /**
   * @brief Registers a behavior template for use by AI entities
   * @param name Unique name identifier for the behavior
   * @param behavior Shared pointer to the behavior template to register
   */
  void registerBehavior(const std::string &name,
                        std::shared_ptr<AIBehavior> behavior);

  /**
   * @brief Checks if a behavior template is registered
   * @param name Name of the behavior to check
   * @return true if behavior is registered, false otherwise
   */
  bool hasBehavior(const std::string &name) const;

  /**
   * @brief Retrieves a registered behavior template
   * @param name Name of the behavior to retrieve
   * @return Shared pointer to behavior template, or nullptr if not found
   */
  std::shared_ptr<AIBehavior> getBehavior(const std::string &name) const;

  /**
   * @brief Assigns a behavior to an entity immediately
   * @param entity Pointer to the entity to assign behavior to
   * @param behaviorName Name of the behavior to assign
   */
  void assignBehaviorToEntity(EntityPtr entity,
                              const std::string &behaviorName);

  /**
   * @brief Removes behavior assignment from an entity
   * @param entity Pointer to the entity to unassign behavior from
   */
  void unassignBehaviorFromEntity(EntityPtr entity);

  /**
   * @brief Checks if an entity has an assigned behavior
   * @param entity Pointer to the entity to check
   * @return true if entity has assigned behavior, false otherwise
   */
  bool entityHasBehavior(EntityPtr entity) const;

  /**
   * @brief Queues a behavior assignment for batch processing
   * @param entity Pointer to the entity to assign behavior to
   * @param behaviorName Name of the behavior to assign
   *
   * Assignment batching is now dynamic and thread-aware. All pending
   * assignments are processed in optimal batches using the ThreadSystem.
   */
  void queueBehaviorAssignment(EntityPtr entity,
                               const std::string &behaviorName);
  /**
   * @brief Processes all pending behavior assignments
   * @return Number of assignments processed
   *
   * This method now uses the ThreadSystem and WorkerBudget to process all
   * assignments in optimal parallel batches. No artificial per-frame limit is
   * imposed; all assignments are processed as fast as system resources allow.
   */
  size_t processPendingBehaviorAssignments();
  // Player reference for AI targeting
  void setPlayerForDistanceOptimization(EntityPtr player);
  EntityPtr getPlayerReference() const;
  Vector2D getPlayerPosition() const;
  bool isPlayerValid() const;

  // Entity management (now unified with spatial system)
  /**
   * @brief Register entity for AI updates with priority-based distance
   * optimization
   * @param entity The entity to register
   * @param priority Priority level (0-9):
   *   - 0-2: Background entities (1.0x-1.2x update range)
   *   - 3-5: Standard entities (1.3x-1.5x update range)
   *   - 6-8: Important entities (1.6x-1.8x update range)
   *   - 9: Critical entities (1.9x update range)
   * Higher priority = larger update distances = more responsive AI
   */
  void registerEntityForUpdates(EntityPtr entity, int priority = 5);

  /**
   * @brief Register entity for AI updates and assign behavior in one call
   * @param entity The entity to register
   * @param priority Priority level (0-9) - see above for ranges
   * @param behaviorName Name of the behavior to assign
   */
  void registerEntityForUpdates(EntityPtr entity, int priority,
                                const std::string &behaviorName);
  void unregisterEntityFromUpdates(EntityPtr entity);

  /**
   * @brief Query entities within a radius of a position
   * @param center Center point for the query
   * @param radius Radius to search within
   * @param outEntities Output vector of entities found (cleared before populating)
   * @param excludePlayer If true, excludes the player entity from results
   * @note Thread-safe. Useful for combat hit detection, area effects, etc.
   */
  void queryEntitiesInRadius(const Vector2D& center, float radius,
                             std::vector<EntityPtr>& outEntities,
                             bool excludePlayer = true) const;

  // Update extents for an entity if its sprite/physics size changes
  void updateEntityExtents(EntityPtr entity, float halfW, float halfH);

  // Global controls
  void setGlobalPause(bool paused);
  bool isGloballyPaused() const;

  // Priority system utilities
  int getEntityPriority(EntityPtr entity) const;
  float getUpdateRangeMultiplier(int priority) const;
  static constexpr int AI_MIN_PRIORITY = 0;
  static constexpr int AI_MAX_PRIORITY = 9;
  static constexpr int DEFAULT_PRIORITY = 5;
  void resetBehaviors();

  // Threading configuration
  void enableThreading(bool enable);
  void setThreadingThreshold(size_t threshold);
  size_t getThreadingThreshold() const;
  void setWaitForBatchCompletion(bool wait);
  bool getWaitForBatchCompletion() const;
  void configurePriorityMultiplier(float multiplier = 1.0f);

  // Performance monitoring
  size_t getBehaviorCount() const;
  size_t getManagedEntityCount() const;
  size_t getBehaviorUpdateCount() const;

  // Thread-safe assignment tracking (atomic counter only)
  size_t getTotalAssignmentCount() const;

  // Message system
  void sendMessageToEntity(EntityPtr entity, const std::string &message,
                           bool immediate = false);
  void broadcastMessage(const std::string &message, bool immediate = false);
  void processMessageQueue();

  /**
   * @brief Get direct access to PathfinderManager for optimal pathfinding
   * performance
   * @return Reference to PathfinderManager instance
   * @details Provides access to centralized pathfinding service for all AI
   * entities
   *
   * All pathfinding functionality has been moved to PathfinderManager.
   * Use PathfinderManager::Instance() to access pathfinding services.
   */
  PathfinderManager &getPathfinderManager() const;

private:
  AIManager() = default;
  ~AIManager();
  AIManager(const AIManager &) = delete;
  AIManager &operator=(const AIManager &) = delete;

  // Cache-efficient storage using Structure of Arrays (SoA)
  // NOTE: Position/size data now lives in EntityDataManager (single source of truth)
  // AIManager only stores AI-specific data (behaviors, priorities, distances)
  struct EntityStorage {
    // Hot data arrays - tightly packed for cache efficiency
    std::vector<AIEntityData::HotData> hotData;  // AI-specific: distanceSquared, priority, etc.

    // Cold data arrays - accessed less frequently
    std::vector<EntityPtr> entities;              // For behavior execution compatibility
    std::vector<std::shared_ptr<AIBehavior>> behaviors;
    std::vector<float> lastUpdateTimes;
    std::vector<size_t> edmIndices;               // EntityDataManager indices for lock-free batch access

    // NOTE: halfWidths/halfHeights REMOVED - now accessed via EntityDataManager::getHotDataByIndex()

    size_t size() const { return entities.size(); }
    void reserve(size_t capacity) {
      hotData.reserve(capacity);
      entities.reserve(capacity);
      behaviors.reserve(capacity);
      lastUpdateTimes.reserve(capacity);
      edmIndices.reserve(capacity);
    }
  };

  EntityStorage m_storage;
  std::unordered_map<EntityPtr, size_t> m_entityToIndex;
  std::unordered_map<std::string, std::shared_ptr<AIBehavior>>
      m_behaviorTemplates;
  std::unordered_map<std::string, BehaviorType> m_behaviorTypeMap;

  // Performance optimization: cache frequently accessed behaviors and types
  mutable std::unordered_map<std::string, std::shared_ptr<AIBehavior>>
      m_behaviorCache;
  mutable std::unordered_map<std::string, BehaviorType> m_behaviorTypeCache;
  mutable std::shared_mutex m_behaviorCacheMutex;  // Protects m_behaviorTypeCache

  // Player reference
  EntityWeakPtr m_playerEntity;

  // Entity management for distance optimization
  struct EntityUpdateInfo {
    EntityWeakPtr entityWeak;
    int priority;
    int frameCounter;
    uint64_t lastUpdateTime;

    EntityUpdateInfo() : priority(0), frameCounter(0), lastUpdateTime(0) {}
  };
  std::vector<EntityUpdateInfo> m_managedEntities;

  // Batch assignment queue with deduplication
  struct PendingAssignment {
    EntityPtr entity;
    std::string behaviorName;

    PendingAssignment(EntityPtr e, const std::string &b)
        : entity(e), behaviorName(b) {}
  };
  std::vector<PendingAssignment> m_pendingAssignments;
  std::unordered_map<EntityPtr, std::string>
      m_pendingAssignmentIndex; // For deduplication

  // Message queue
  struct QueuedMessage {
    EntityWeakPtr targetEntity; // empty for broadcast
    std::string message;
    uint64_t timestamp;

    QueuedMessage(EntityPtr target, const std::string &msg)
        : targetEntity(target), message(msg), timestamp(getCurrentTimeNanos()) {
    }
  };
  std::vector<QueuedMessage> m_messageQueue;

  // Threading and state
  std::atomic<bool> m_initialized{false};
  std::atomic<bool> m_useThreading{true};
  std::atomic<bool> m_waitForBatchCompletion{false}; // Default: non-blocking for smooth frames
  std::atomic<bool> m_globallyPaused{false};
  std::atomic<bool> m_processingMessages{false};

  // Legacy pathfinding state removed - all pathfinding handled by
  // PathfinderManager

  // Behavior execution tracking
  std::atomic<size_t> m_totalBehaviorExecutions{0};

  // Active entity counter - avoids iteration every frame
  std::atomic<size_t> m_activeEntityCount{0};

  // Thread-safe assignment tracking
  std::atomic<size_t> m_totalAssignmentCount{0};

  // Frame counter for cache invalidation and distance staggering (operational)
  std::atomic<uint64_t> m_frameCounter{0};

  // Asynchronous assignment processing (replaced with futures for deterministic tracking)
  // std::atomic<bool> m_assignmentInProgress{false};  // DEPRECATED: Replaced by m_assignmentFutures

  // Frame throttling for task submission (thread-safe)
  std::atomic<uint64_t> m_lastFrameWithTasks{0};

  // Cleanup timing (thread-safe)
  std::atomic<uint64_t> m_lastCleanupFrame{0};

  // Distance optimization settings
  std::atomic<float> m_maxUpdateDistance{4000.0f};
  std::atomic<float> m_mediumUpdateDistance{6000.0f};
  std::atomic<float> m_minUpdateDistance{10000.0f};
  std::atomic<float> m_priorityMultiplier{1.0f};

  // Thread synchronization
  mutable std::shared_mutex m_entitiesMutex;
  mutable std::shared_mutex m_behaviorsMutex;
  mutable std::mutex m_assignmentsMutex;
  mutable std::mutex m_messagesMutex;

  // Cached manager references (avoid singleton lookups in hot paths)
  PathfinderManager* mp_pathfinderManager{nullptr};

  // Batch futures for parallel processing - reused via clear() each frame
  std::vector<std::future<void>> m_batchFutures;

  // Async assignment tracking for deterministic synchronization (replaces m_assignmentInProgress)
  std::vector<std::future<void>> m_assignmentFutures;
  std::mutex m_assignmentFuturesMutex;  // Protect assignment futures vector

  // Reusable futures buffer for assignment synchronization
  mutable std::vector<std::future<void>> m_reusableAssignmentFutures;

  // Per-batch collision update buffers (zero contention approach)
  std::shared_ptr<std::vector<std::vector<CollisionManager::KinematicUpdate>>> m_batchCollisionUpdates;

  // Reusable pre-fetch buffer to avoid per-frame allocations (128KB for 2000 entities)
  // Cleared each frame but capacity is retained to eliminate heap churn
  PreFetchedBatchData m_reusablePreFetchBuffer;

  // Reusable buffer for multi-threaded path (shared_ptr for lambda capture safety)
  // Multi-threaded batches run async, so we need shared_ptr to extend lifetime
  std::shared_ptr<PreFetchedBatchData> m_reusableMultiThreadedBuffer;

  // Reusable collision update buffer for single-threaded paths
  // Avoids ~128-192KB per-frame allocation (cleared but capacity retained)
  std::vector<CollisionManager::KinematicUpdate> m_reusableCollisionBuffer;

  // Reusable buffers for behavior assignment processing
  // Eliminates per-frame allocations during entity spawning
  mutable std::vector<PendingAssignment> m_reusableToProcessBuffer;
  mutable std::shared_ptr<std::vector<PendingAssignment>> m_reusableAssignmentBatch;

  // Camera bounds cache for entity update culling
  // Only update animations/sprites for entities within camera view + buffer
  float m_cameraMinX{0.0f};
  float m_cameraMaxX{0.0f};
  float m_cameraMinY{0.0f};
  float m_cameraMaxY{0.0f};
  bool m_hasCameraCache{false};

  // Optimized batch processing constants
  static constexpr size_t CACHE_LINE_SIZE = 64; // Standard cache line size
  static constexpr size_t BATCH_SIZE =
      256; // Larger batches for better throughput
  std::atomic<size_t> m_threadingThreshold{200};  // Optimal threshold from benchmark

  // Optimized helper methods
  BehaviorType inferBehaviorType(const std::string &behaviorName) const;

  void processBatch(size_t start, size_t end, float deltaTime,
                    const Vector2D &playerPos,
                    bool hasPlayer,
                    const EntityStorage& storage,
                    std::vector<CollisionManager::KinematicUpdate>& collisionUpdates);
  void cleanupInactiveEntities();
  void cleanupAllEntities();
  void updateDistancesScalar(const Vector2D &playerPos);
  static uint64_t getCurrentTimeNanos();

  // Legacy pathfinding methods removed - use PathfinderManager instead

  // Lock-free message queue
  struct alignas(CACHE_LINE_SIZE) LockFreeMessage {
    EntityWeakPtr target;
    char message[48]; // Fixed size for lock-free queue
    std::atomic<bool> ready{false};
  };
  static constexpr size_t MESSAGE_QUEUE_SIZE = 1024;
  std::array<LockFreeMessage, MESSAGE_QUEUE_SIZE> m_lockFreeMessages{};
  std::atomic<size_t> m_messageWriteIndex{0};
  std::atomic<size_t> m_messageReadIndex{0};

  // Shutdown state
  bool m_isShutdown{false};

  // All pathfinding functionality moved to PathfinderManager
};

#endif // AI_MANAGER_HPP
