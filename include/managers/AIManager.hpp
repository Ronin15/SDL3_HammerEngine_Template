/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef AI_MANAGER_HPP
#define AI_MANAGER_HPP

/**
 * @file AIManager.hpp
 * @brief Central manager for AI behaviors and entity AI assignments
 *
 * The AIManager provides a centralized system for:
 * - Registering reusable AI behaviors
 * - Assigning behaviors to game entities
 * - Updating all AI-controlled entities efficiently
 * - Communicating with AI behaviors via messages
 *
 * Usage example:
 *
 * 1. Register behaviors:
 *    auto wanderBehavior = std::make_shared<WanderBehavior>();
 *    AIManager::Instance().registerBehavior("Wander", wanderBehavior);
 *
 * 2. Assign behavior to entity:
 *    AIManager::Instance().assignBehaviorToEntity(npc, "Wander");
 *
 * 3. Send message to entity's behavior:
 *    AIManager::Instance().sendMessageToEntity(npc, "pause");
 */

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <boost/container/flat_map.hpp>
#include "entities/Entity.hpp"
#include "ai/AIBehavior.hpp"



// Conditional debug logging macros
#ifdef AI_DEBUG_LOGGING
    #define AI_LOG(x) std::cout << "Forge Game Engine - [AI Manager] " << x << std::endl
    #define AI_LOG_DETAIL(x) std::cout << x << std::endl
#else
    #define AI_LOG(x)
    #define AI_LOG_DETAIL(x)
#endif

// Thread access model for documentation
enum class ThreadAccess {
    MainThreadOnly,      // Only the main thread can access
    WorkerThreadsOnly,   // Only worker threads can access
    Concurrent,          // Concurrent read/write requires synchronization
    ReadOnly             // Multiple threads can read, no writes after init
};

// AIBehavior is now fully included, not just forward declared

class AIManager {
public:
    /**
     * @brief Get the singleton instance of AIManager
     * @return Reference to the AIManager instance
     * @thread_safety Thread-safe, can be called from any thread
     */
    static AIManager& Instance() {
        static AIManager instance;
        return instance;
    }

    /**
     * @brief Initialize the AI Manager
     * @return True if initialization succeeded, false otherwise
     * @thread_safety Should be called from main thread during initialization
     */
    bool init();

    /**
     * @brief Update all AI-controlled entities
     *
     * This method is automatically called by the game engine
     * and updates all entities with assigned AI behaviors.
     * Updates can run in parallel using the ThreadSystem if available.
     *
     * @thread_safety Thread-safe, can be called from any thread
     */
    void update();

    /**
     * @brief Reset all AI behaviors without shutting down the manager
     *
     * This method clears all registered behaviors and entity assignments
     * but keeps the manager initialized. Use this when changing game states
     * or scenes while the game is still running.
     *
     * @thread_safety Must be called when no AI updates are in progress
     */
    void resetBehaviors();

    /**
     * @brief Clean up resources used by the AI Manager
     *
     * @thread_safety Must be called from main thread during shutdown
     */
    void clean();

    /**
     * @brief Configure threading options for AI processing
     * @param useThreading Whether to use background threads for AI updates
     * @param maxThreads Maximum number of threads to use for AI (0 = auto)
     * @thread_safety Must be called before any AI updates begin
     */
    void configureThreading(bool useThreading, unsigned int maxThreads = 0);

    // AI behavior management
    /**
     * @brief Register a new behavior for use with entities
     * @param behaviorName Unique identifier for the behavior
     * @param behavior Shared pointer to the behavior implementation
     * @thread_safety Thread-safe, but behaviors should ideally be registered at startup
     */
    void registerBehavior(const std::string& behaviorName, std::shared_ptr<AIBehavior> behavior);

    /**
     * @brief Check if a behavior exists
     * @param behaviorName Name of the behavior to check
     * @return True if the behavior exists, false otherwise
     * @thread_safety Thread-safe, can be called from any thread
     */
    bool hasBehavior(const std::string& behaviorName) const;

    /**
     * @brief Get a shared pointer to a behavior
     * @param behaviorName Name of the behavior to retrieve
     * @return Shared pointer to the behavior, or nullptr if not found
     * @thread_safety Thread-safe, can be called from any thread
     */
    std::shared_ptr<AIBehavior> getBehavior(const std::string& behaviorName) const;

    // Entity-behavior assignment
    /**
     * @brief Assign an AI behavior to an entity
     * @param entity Shared pointer to the entity
     * @param behaviorName Name of the behavior to assign
     * @thread_safety Thread-safe, can be called from any thread
     */
    void assignBehaviorToEntity(EntityPtr entity, const std::string& behaviorName);

    /**
     * @brief Process multiple entities with the same behavior type in batches
     * @param behaviorName The behavior to process
     * @param entities Vector of entities to process
     * @thread_safety Thread-safe, can be called from any thread
     */
    void batchProcessEntities(const std::string& behaviorName, const std::vector<EntityPtr>& entities);

    /**
     * @brief Process all behaviors in batches for maximum performance
     * This is the most optimized way to update all AI entities.
     * It will automatically use threading if available.
     * @thread_safety Thread-safe, can be called from any thread
     */
    void batchUpdateAllBehaviors();

    /**
     * @brief Remove AI behavior from an entity
     * @param entity Shared pointer to the entity
     * @thread_safety Thread-safe, can be called from any thread
     */
    void unassignBehaviorFromEntity(EntityPtr entity);

    /**
     * @brief Rebuild optimization caches if they're invalid
     * This will be called automatically when needed, but can be called
     * manually if you know the caches should be refreshed
     * @thread_safety Thread-safe, can be called from any thread
     */
    void ensureOptimizationCachesValid();

    /**
     * @brief Check if an entity has an assigned behavior
     * @param entity Shared pointer to the entity
     * @return True if the entity has a behavior, false otherwise
     * @thread_safety Thread-safe, can be called from any thread
     */
    bool entityHasBehavior(EntityPtr entity) const;

    /**
     * @brief Check if there's any entity with a specific behavior
     * @param behaviorName Name of the behavior to check for
     * @return True if any entity has this behavior, false otherwise
     * @thread_safety Thread-safe, can be called from any thread
     */
    bool hasEntityWithBehavior(const std::string& behaviorName) const;

    // Advanced features
    /**
     * @brief Send a message to a specific entity's behavior
     * @param entity Target entity shared pointer
     * @param message Message string (e.g., "pause", "resume", "attack")
     * @param immediate If true, delivers immediately; if false, queues for next update
     * @thread_safety Thread-safe, can be called from any thread
     */
    void sendMessageToEntity(EntityPtr entity, const std::string& message, bool immediate = false);

    /**
     * @brief Send a message to all entity behaviors
     * @param message Message string to broadcast
     * @param immediate If true, delivers immediately; if false, queues for next update
     * @thread_safety Thread-safe, can be called from any thread
     */
    void broadcastMessage(const std::string& message, bool immediate = false);

    /**
     * @brief Process all queued messages
     * This happens automatically during update() but can be called manually
     * @thread_safety Thread-safe, can be called from any thread
     */
    void processMessageQueue();

    // Utility methods
    /**
     * @brief Get the number of registered behaviors
     * @return Count of behaviors
     * @thread_safety Thread-safe, can be called from any thread
     */
    size_t getBehaviorCount() const;

    /**
     * @brief Get the number of entities with AI behaviors
     * @return Count of managed entities
     * @thread_safety Thread-safe, can be called from any thread
     */
    size_t getManagedEntityCount() const;

private:
    // Singleton constructor
    AIManager();
    ~AIManager();

    // Delete copy constructor and assignment operator
    AIManager(const AIManager&) = delete;
    AIManager& operator=(const AIManager&) = delete;

    // Storage for behaviors and entity assignments
    // ThreadAccess: ReadOnly after initialization for m_behaviors
    // ThreadAccess: Concurrent for m_entityBehaviors
    boost::container::flat_map<std::string, std::shared_ptr<AIBehavior>> m_behaviors{};
    boost::container::flat_map<EntityWeakPtr, std::string, std::owner_less<EntityWeakPtr>> m_entityBehaviors{};

    // Thread synchronization for entity-behavior map
    mutable std::shared_mutex m_entityMutex{};
    mutable std::shared_mutex m_behaviorsMutex{};

    // Performance tracing for AI operations
    struct PerformanceStats {
        double totalUpdateTimeMs{0.0};
        double averageUpdateTimeMs{0.0};
        uint64_t updateCount{0};
        double maxUpdateTimeMs{0.0};
        double minUpdateTimeMs{std::numeric_limits<double>::max()};

        // Constructor to ensure proper initialization
        PerformanceStats() 
            : totalUpdateTimeMs(0.0)
            , averageUpdateTimeMs(0.0)
            , updateCount(0)
            , maxUpdateTimeMs(0.0)
            , minUpdateTimeMs(std::numeric_limits<double>::max())
        {}

        void addSample(double timeMs) {
            totalUpdateTimeMs += timeMs;
            updateCount++;
            averageUpdateTimeMs = totalUpdateTimeMs / updateCount;
            maxUpdateTimeMs = std::max(maxUpdateTimeMs, timeMs);
            minUpdateTimeMs = std::min(minUpdateTimeMs, timeMs);
        }

        void reset() {
            totalUpdateTimeMs = 0.0;
            averageUpdateTimeMs = 0.0;
            updateCount = 0;
            maxUpdateTimeMs = 0.0;
            minUpdateTimeMs = std::numeric_limits<double>::max();
        }
    };

    // Cache for quick lookup of entity-behavior pairs (optimization)
    struct EntityBehaviorCache {
        EntityWeakPtr entityWeak{};
        std::weak_ptr<AIBehavior> behaviorWeak{};  // Using weak_ptr to avoid ownership issues
        std::string behaviorName;  // Using string to ensure lifetime beyond the behavior

        // Performance statistics
        uint64_t lastUpdateTime{0};
        PerformanceStats perfStats;

        // Constructor to ensure proper initialization
        EntityBehaviorCache() 
            : entityWeak()
            , behaviorWeak()
            , behaviorName()
            , lastUpdateTime(0)
            , perfStats()
        {}
    };
    std::vector<EntityBehaviorCache> m_entityBehaviorCache{};
    std::atomic<bool> m_cacheValid{false};
    mutable std::mutex m_cacheMutex{};

    // Multithreading support
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_useThreading{true}; // Controls whether updates run in parallel
    unsigned int m_maxThreads{0}; // 0 = auto (use ThreadSystem default)

    // For batch processing optimization
    using BehaviorBatch = std::vector<EntityPtr>;
    boost::container::flat_map<std::string, BehaviorBatch> m_behaviorBatches{};
    std::atomic<bool> m_batchesValid{false};
    mutable std::mutex m_batchesMutex{};

    // Private helper methods for optimizations
    void rebuildEntityBehaviorCache();
    void rebuildBehaviorBatches();
    void invalidateOptimizationCaches();
    void updateBehaviorBatch(const std::string_view& behaviorName, const BehaviorBatch& batch);

    // Common helper method to process entities with a behavior (reduces code duplication)
    void processEntitiesWithBehavior(std::shared_ptr<AIBehavior> behavior, const std::vector<EntityPtr>& entities, bool useThreading);

    // Performance monitoring
    boost::container::flat_map<std::string, PerformanceStats> m_behaviorPerformanceStats;
    mutable std::mutex m_perfStatsMutex{};
    void recordBehaviorPerformance(const std::string_view& behaviorName, double timeMs);

    // Utility method to get current high-precision time
    static inline uint64_t getCurrentTimeNanos() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }

    // Message structure for AI communication
    struct QueuedMessage {
        EntityWeakPtr targetEntity{};  // empty weak_ptr for broadcast
        std::string message;
        uint64_t timestamp{0};

        // Explicitly initialize all members
        QueuedMessage() : targetEntity(), message(), timestamp(0) {}

        QueuedMessage(EntityWeakPtr entity, const std::string& msg, uint64_t time)
            : targetEntity(entity), message(msg), timestamp(time) {}
    };

    // Message queue system with thread-safe double-buffering
    class ThreadSafeMessageQueue {
    public:
        void enqueueMessage(EntityWeakPtr target, const std::string& message) {
            std::lock_guard<std::mutex> lock(m_incomingMutex);
            m_incomingQueue.emplace_back(target, message, AIManager::getCurrentTimeNanos());
        }

        void swapBuffers() {
            if (m_swapInProgress.exchange(true)) return;

            std::lock_guard<std::mutex> lock(m_incomingMutex);
            m_processingQueue.clear();
            m_processingQueue.swap(m_incomingQueue);

            m_swapInProgress.store(false);
        }

        const std::vector<QueuedMessage>& getProcessingQueue() const {
            return m_processingQueue;
        }

        bool isEmpty() const {
            std::lock_guard<std::mutex> lock(m_incomingMutex);
            return m_incomingQueue.empty() && m_processingQueue.empty();
        }

        void clear() {
            std::lock_guard<std::mutex> lock(m_incomingMutex);
            m_incomingQueue.clear();
            m_processingQueue.clear();
        }

    private:
        std::vector<QueuedMessage> m_incomingQueue;
        std::vector<QueuedMessage> m_processingQueue;
        mutable std::mutex m_incomingMutex;
        std::atomic<bool> m_swapInProgress{false};
    };

    // Thread-safe message queue implementation
    ThreadSafeMessageQueue m_messageQueue;
    PerformanceStats m_messageQueueStats;
    std::atomic<bool> m_processingMessages{false};

    // Message delivery helpers
    void deliverMessageToEntity(EntityPtr entity, const std::string& message);
    size_t deliverBroadcastMessage(const std::string& message);
};

#endif // AI_MANAGER_HPP
