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
 * - Asynchronous (non-blocking) AI processing for optimal game engine performance
 * - ThreadSystem and WorkerBudget integration for optimal scaling
 * - Type-indexed behavior storage for fast lookups
 * - Cache-friendly data structures with reduced lock contention
 * - Cross-platform threading optimizations (Windows/Linux/Mac)
 * - Smart pointer usage throughout for memory safety
 * - Scales to 10k+ entities while maintaining 60+ FPS
 */

#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include "entities/Entity.hpp"
#include "ai/AIBehavior.hpp"

// Conditional debug logging
#ifdef AI_DEBUG_LOGGING
    #define AI_LOG(x) std::cout << "Forge Game Engine - [AI Manager] " << x << std::endl
#else
    #define AI_LOG(x)
#endif

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
 * @brief Optimized AI entity data structure
 */
struct AIEntityData {
    EntityPtr entity;
    std::shared_ptr<AIBehavior> behavior;
    BehaviorType behaviorType;
    Vector2D lastPosition;
    float lastUpdateTime;
    int frameCounter;
    int priority;
    bool active;

    AIEntityData() : behaviorType(BehaviorType::Custom), lastPosition{0.0f, 0.0f},
                     lastUpdateTime(0.0f), frameCounter(0), priority(0), active(true) {}
};

/**
 * @brief AI Performance statistics
 */
struct AIPerformanceStats {
    double totalUpdateTime{0.0};
    uint64_t updateCount{0};
    uint64_t entitiesProcessed{0};
    double entitiesPerSecond{0.0};

    void addSample(double timeMs, uint64_t entities) {
        totalUpdateTime += timeMs;
        updateCount++;
        entitiesProcessed += entities;
        if (totalUpdateTime > 0) {
            entitiesPerSecond = (entitiesProcessed * 1000.0) / totalUpdateTime;
        }
    }

    void reset() {
        totalUpdateTime = 0.0;
        updateCount = 0;
        entitiesProcessed = 0;
        entitiesPerSecond = 0.0;
    }
};

/**
 * @brief High-performance AI Manager
 */
class AIManager {
public:
    static AIManager& Instance() {
        static AIManager instance;
        return instance;
    }

    /**
     * @brief Initializes the AI Manager and its internal systems
     * @return true if initialization successful, false otherwise
     */
    bool init();
    
    /**
     * @brief Cleans up all AI resources and marks manager as shut down
     */
    void clean();
    
    /**
     * @brief Updates all active AI entities using asynchronous processing
     * 
     * PERFORMANCE FIX: Uses fire-and-forget threading to avoid locking the main thread.
     * This resolves Windows performance issues with 10k+ entities while maintaining
     * 60+ FPS. AI processing happens asynchronously in background worker threads.
     * 
     * Key Features:
     * - Non-blocking: Main thread continues immediately for optimal FPS
     * - ThreadSystem integration: Uses WorkerBudget for optimal allocation
     * - Cross-platform: Optimized for Windows/Linux/Mac performance
     * - Scales to 10k+ entities with maintained 60+ FPS
     * 
     * @param deltaTime Time elapsed since last update in seconds
     */
    void update(float deltaTime);
    
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
    void registerBehavior(const std::string& name, std::shared_ptr<AIBehavior> behavior);
    
    /**
     * @brief Checks if a behavior template is registered
     * @param name Name of the behavior to check
     * @return true if behavior is registered, false otherwise
     */
    bool hasBehavior(const std::string& name) const;
    
    /**
     * @brief Retrieves a registered behavior template
     * @param name Name of the behavior to retrieve
     * @return Shared pointer to behavior template, or nullptr if not found
     */
    std::shared_ptr<AIBehavior> getBehavior(const std::string& name) const;

    /**
     * @brief Assigns a behavior to an entity immediately
     * @param entity Pointer to the entity to assign behavior to
     * @param behaviorName Name of the behavior to assign
     */
    void assignBehaviorToEntity(EntityPtr entity, const std::string& behaviorName);
    
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
     */
    void queueBehaviorAssignment(EntityPtr entity, const std::string& behaviorName);
    
    /**
     * @brief Processes all pending behavior assignments
     * @return Number of assignments processed
     */
    size_t processPendingBehaviorAssignments();

    // Player reference for AI targeting
    void setPlayerForDistanceOptimization(EntityPtr player);
    EntityPtr getPlayerReference() const;
    Vector2D getPlayerPosition() const;
    bool isPlayerValid() const;

    // Entity management (now unified with spatial system)
    /**
     * @brief Register entity for AI updates with priority-based distance optimization
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
    void registerEntityForUpdates(EntityPtr entity, int priority, const std::string& behaviorName);
    void unregisterEntityFromUpdates(EntityPtr entity);

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
    void configureThreading(bool useThreading, unsigned int maxThreads = 0);
    void configurePriorityMultiplier(float multiplier = 1.0f);

    // Performance monitoring
    AIPerformanceStats getPerformanceStats() const;
    size_t getBehaviorCount() const;
    size_t getManagedEntityCount() const;
    size_t getBehaviorUpdateCount() const;
    
    // Thread-safe assignment tracking (atomic counter only)
    size_t getTotalAssignmentCount() const;
    


    // Message system
    void sendMessageToEntity(EntityPtr entity, const std::string& message, bool immediate = false);
    void broadcastMessage(const std::string& message, bool immediate = false);
    void processMessageQueue();

private:
    AIManager() = default;
    ~AIManager() {
        if (!m_isShutdown) {
            clean();
        }
    }
    AIManager(const AIManager&) = delete;
    AIManager& operator=(const AIManager&) = delete;

    // Core storage - single optimized approach
    std::vector<AIEntityData> m_entities;
    std::unordered_map<EntityPtr, size_t> m_entityToIndex;
    std::unordered_map<std::string, std::shared_ptr<AIBehavior>> m_behaviorTemplates;
    std::unordered_map<std::string, BehaviorType> m_behaviorTypeMap;

    // Performance stats per behavior type
    std::array<AIPerformanceStats, static_cast<size_t>(BehaviorType::COUNT)> m_behaviorStats;
    AIPerformanceStats m_globalStats;

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

        PendingAssignment(EntityPtr e, const std::string& b) : entity(e), behaviorName(b) {}
    };
    std::vector<PendingAssignment> m_pendingAssignments;
    std::unordered_map<EntityPtr, std::string> m_pendingAssignmentIndex; // For deduplication

    // Message queue
    struct QueuedMessage {
        EntityWeakPtr targetEntity;  // empty for broadcast
        std::string message;
        uint64_t timestamp;

        QueuedMessage(EntityPtr target, const std::string& msg)
            : targetEntity(target), message(msg), timestamp(getCurrentTimeNanos()) {}
    };
    std::vector<QueuedMessage> m_messageQueue;

    // Threading and state
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_useThreading{true};
    std::atomic<bool> m_globallyPaused{false};
    std::atomic<bool> m_processingMessages{false};
    unsigned int m_maxThreads{0};

    // Behavior execution tracking
    std::atomic<size_t> m_totalBehaviorExecutions{0};
    
    // Thread-safe assignment tracking
    std::atomic<size_t> m_totalAssignmentCount{0};
    
    // Frame counter for periodic logging (thread-safe)
    std::atomic<uint64_t> m_frameCounter{0};
    
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
    mutable std::mutex m_statsMutex;

    // Batch processing constants optimized for cross-platform performance optimized for cross-platform performance
    static constexpr size_t BATCH_SIZE = 64;                // Optimal cache-friendly batch size                   // Cache-friendly batch size
    static constexpr size_t THREADING_THRESHOLD = 200;      // Entities threshold for threading activation         // Entities threshold for threading activation

    // Helper methods - optimized for non-blocking performance - optimized for asynchronous processing
    BehaviorType inferBehaviorType(const std::string& behaviorName) const;
    void processBatch(size_t start, size_t end, float deltaTime);  // Non-blocking batch processing          // Lock-free batch processing
    void cleanupInactiveEntities();
    bool shouldUpdateEntity(EntityPtr entity, EntityPtr player, int& frameCounter, int entityPriority);
    void updateEntityBehavior(EntityPtr entity);
    void recordPerformance(BehaviorType type, double timeMs, uint64_t entities);
    static uint64_t getCurrentTimeNanos();
    
    // Shutdown state
    bool m_isShutdown{false};
};

#endif // AI_MANAGER_HPP
