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
#include "entities/EntityHandle.hpp"
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
  // Position/distance removed: EntityDataManager is the single source of truth
  struct HotData {
    uint8_t priority;      // Priority level (1 byte)
    uint8_t behaviorType;  // Behavior type enum (1 byte)
    bool active;           // Active flag (1 byte)
    uint8_t padding[5];    // Pad to 8 bytes for alignment
  };

  // Cold data - accessed occasionally
  EntityPtr entity;
  std::shared_ptr<AIBehavior> behavior;
  float lastUpdateTime;

  AIEntityData() : entity(nullptr), behavior(nullptr), lastUpdateTime(0.0f) {}
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
   */
  void assignBehavior(EntityHandle handle, const std::string &behaviorName);

  /**
   * @brief Removes behavior assignment from an entity
   */
  void unassignBehavior(EntityHandle handle);

  /**
   * @brief Checks if an entity has an assigned behavior
   */
  bool hasBehavior(EntityHandle handle) const;


  // Player handle for AI targeting
  void setPlayerHandle(EntityHandle player);
  EntityHandle getPlayerHandle() const;
  Vector2D getPlayerPosition() const;
  bool isPlayerValid() const;

  // Entity registration
  void registerEntity(EntityHandle handle);
  void registerEntity(EntityHandle handle, const std::string &behaviorName);
  void unregisterEntity(EntityHandle handle);

  /**
   * @brief Query handles within a radius
   */
  void queryHandlesInRadius(const Vector2D& center, float radius,
                            std::vector<EntityHandle>& outHandles,
                            bool excludePlayer = true) const;

  // Global controls
  void setGlobalPause(bool paused);
  bool isGloballyPaused() const;

  // Priority from EDM CharacterData
  int getEntityPriority(EntityHandle handle) const;
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

  // Performance monitoring
  size_t getBehaviorCount() const;
  size_t getBehaviorUpdateCount() const;

  // Thread-safe assignment tracking (atomic counter only)
  size_t getTotalAssignmentCount() const;

  // Message system
  void sendMessageToEntity(EntityHandle handle, const std::string &message,
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
  // Position/size data lives in EntityDataManager (single source of truth)
  // AIManager stores AI-specific data (behaviors, priorities) + cached EDM indices
  struct EntityStorage {
    std::vector<AIEntityData::HotData> hotData;
    std::vector<EntityHandle> handles;  // 8 bytes each (vs 16 byte shared_ptr)
    std::vector<std::shared_ptr<AIBehavior>> behaviors;
    std::vector<float> lastUpdateTimes;
    std::vector<size_t> edmIndices;  // Cached for O(1) batch access

    size_t size() const { return handles.size(); }
    void reserve(size_t capacity) {
      hotData.reserve(capacity);
      handles.reserve(capacity);
      behaviors.reserve(capacity);
      lastUpdateTimes.reserve(capacity);
      edmIndices.reserve(capacity);
    }
  };

  EntityStorage m_storage;
  std::unordered_map<EntityHandle, size_t> m_handleToIndex;
  std::unordered_map<std::string, std::shared_ptr<AIBehavior>>
      m_behaviorTemplates;
  std::unordered_map<std::string, BehaviorType> m_behaviorTypeMap;

  // Sparse behavior storage indexed by EntityDataManager index for O(1) lookup
  // Used with getActiveIndices() to iterate only Active tier entities
  std::vector<std::shared_ptr<AIBehavior>> m_behaviorsByEdmIndex;

  // Shared behaviors indexed by BehaviorType for O(1) lookup in processBatch
  // Each behavior instance handles multiple entities via internal m_entityStates map
  std::array<std::shared_ptr<AIBehavior>, static_cast<size_t>(BehaviorType::COUNT)>
      m_behaviorsByType{};

  // NOTE: Behavior caches removed - they were duplicates of the primary maps:
  // - m_behaviorCache duplicated m_behaviorTemplates (same O(1) lookup)
  // - m_behaviorTypeCache duplicated m_behaviorTypeMap (immutable after init)
  // - m_behaviorCacheMutex was only needed for cache writes on miss (removed)
  // The maps are now accessed directly with appropriate locking.

  // Player handle
  EntityHandle m_playerHandle{};

  // Message queue
  struct QueuedMessage {
    EntityHandle targetHandle{}; // invalid for broadcast
    std::string message;
    uint64_t timestamp;

    QueuedMessage(EntityHandle target, const std::string &msg)
        : targetHandle(target), message(msg), timestamp(getCurrentTimeNanos()) {}
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

  // Thread-safe assignment tracking
  std::atomic<size_t> m_totalAssignmentCount{0};

  // Frame counter for cache invalidation and distance staggering (operational)
  std::atomic<uint64_t> m_frameCounter{0};

  // Cleanup timing (thread-safe)
  std::atomic<uint64_t> m_lastCleanupFrame{0};

  // Distance culling removed - EDM tier system handles this via updateSimulationTiers()

  // Thread synchronization
  mutable std::shared_mutex m_entitiesMutex;
  mutable std::shared_mutex m_behaviorsMutex;
  mutable std::mutex m_messagesMutex;

  // Cached manager references (avoid singleton lookups in hot paths)
  PathfinderManager* mp_pathfinderManager{nullptr};

  // Batch futures for parallel processing - reused via clear() each frame
  std::vector<std::future<void>> m_batchFutures;

  // Reusable buffer for Active tier EDM indices (avoids per-frame allocation)
  std::vector<size_t> m_activeIndicesBuffer;

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
  std::atomic<size_t> m_threadingThreshold{500};  // Updated: single-threaded faster below 500

  // Optimized helper methods
  BehaviorType inferBehaviorType(const std::string &behaviorName) const;
  void assignBehaviorInternal(EntityHandle handle, const std::string &behaviorName);

  // Process batch of Active tier entities using EDM indices directly
  // No tier check needed - getActiveIndices() already filters to Active tier
  void processBatch(const std::vector<size_t>& activeIndices,
                    size_t start, size_t end,
                    float deltaTime,
                    float worldWidth, float worldHeight);
  void cleanupInactiveEntities();
  void cleanupAllEntities();
  void updateDistancesScalar(const Vector2D &playerPos);
  static uint64_t getCurrentTimeNanos();

  // Legacy pathfinding methods removed - use PathfinderManager instead

  // Lock-free message queue
  struct alignas(CACHE_LINE_SIZE) LockFreeMessage {
    EntityHandle target{};  // Invalid handle for broadcast
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
