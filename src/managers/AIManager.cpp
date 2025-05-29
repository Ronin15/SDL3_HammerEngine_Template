/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/AIManager.hpp"
#include "core/ThreadSystem.hpp"
#include <SDL3/SDL.h>

// AIManager implementation

AIManager::AIManager()
    : m_initialized(false)
    , m_useThreading(true)
    , m_maxThreads(0) {
}

AIManager::~AIManager() {
    if (m_initialized.load(std::memory_order_acquire)) {
        clean();
    }
}

bool AIManager::init() {
    if (m_initialized.load(std::memory_order_acquire)) {
        std::cout << "Forge Game Engine - AIManager already initialized" << std::endl;
        return true;
    }

    try {
        // Initialize behavior batches
        m_batchesValid.store(false, std::memory_order_release);
        m_cacheValid.store(false, std::memory_order_release);

        // Set threading mode based on ThreadSystem availability
        bool threadSystemExists = Forge::ThreadSystem::Exists();
        m_useThreading.store(threadSystemExists, std::memory_order_release);

        // Set initialized flag
        m_initialized.store(true, std::memory_order_release);

        // Log threading status
        if (threadSystemExists) {
            std::cout << "Forge Game Engine - AIManager: ThreadSystem detected with "
                      << Forge::ThreadSystem::Instance().getThreadCount() << " threads" << std::endl;
        } else {
            std::cout << "Forge Game Engine - AIManager: ThreadSystem not available, using single-threaded mode" << std::endl;
        }

        std::cout << "Forge Game Engine - AIManager initialized" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - AIManager initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void AIManager::clean() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    std::cout << "Forge Game Engine - Cleaning up AIManager resources" << std::endl;

    // Clear all behaviors and entities - need to acquire all locks to be safe
    {
        std::unique_lock<std::shared_mutex> entityLock(m_entityMutex);
        std::unique_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        std::lock_guard<std::mutex> batchesLock(m_batchesMutex);
        std::lock_guard<std::mutex> perfLock(m_perfStatsMutex);
        std::lock_guard<std::mutex> pendingLock(m_pendingAssignmentsMutex);

        m_behaviors.clear();
        m_entityBehaviors.clear();
        m_entityBehaviorInstances.clear();
        m_entityBehaviorCache.clear();
        m_behaviorBatches.clear();
        m_behaviorPerformanceStats.clear();
        m_pendingBehaviorAssignments.clear();
        m_pendingAssignmentCount.store(0, std::memory_order_release);
    }

    m_initialized.store(false, std::memory_order_release);
    std::cout << "Forge Game Engine - AIManager resources cleaned" << std::endl;
}

void AIManager::configureThreading(bool useThreading, unsigned int maxThreads) {
    m_useThreading.store(useThreading, std::memory_order_release);

    // If maxThreads is 0 (auto), set it to optimal thread count based on hardware
    if (maxThreads == 0) {
        // Get available hardware threads
        unsigned int hwThreads = std::thread::hardware_concurrency();

        // If we couldn't determine the number, default to 4
        if (hwThreads == 0) hwThreads = 4;

        // Reserve 1 thread for main game thread on systems with ≤ 4 cores
        // Reserve 2 threads for main game thread and other systems on systems with > 4 cores
        maxThreads = (hwThreads <= 4) ? hwThreads - 1 : hwThreads - 2;

        // Ensure we have at least 1 thread
        maxThreads = std::max(1u, maxThreads);
    }

    m_maxThreads = maxThreads;

    if (useThreading) {
        std::cout << "Forge Game Engine - AIManager: Threading enabled" << std::endl;
        std::cout << "Forge Game Engine - AIManager: Using " << m_maxThreads
                  << " threads (hardware concurrency: " << std::thread::hardware_concurrency() << ")" << std::endl;
    } else {
        std::cout << "Forge Game Engine - AIManager: Threading disabled" << std::endl;
    }
}

void AIManager::registerBehavior(const std::string& behaviorName, std::shared_ptr<AIBehavior> behavior) {
    if (!behavior) {
        std::cerr << "Forge Game Engine - Error: Attempt to register null behavior: " << behaviorName << std::endl;
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_behaviorsMutex);
    m_behaviors[behaviorName] = behavior;
    invalidateOptimizationCaches();
}

bool AIManager::hasBehavior(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
    return m_behaviors.find(name) != m_behaviors.end();
}

std::shared_ptr<AIBehavior> AIManager::getBehavior(const std::string& behaviorName) const {
    std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);

    auto it = m_behaviors.find(behaviorName);
    if (it != m_behaviors.end()) {
        return it->second;
    }
    return nullptr;
}

void AIManager::assignBehaviorToEntity(EntityPtr entity, const std::string& behaviorName) {
    if (!entity) {
        std::cerr << "Forge Game Engine - Error: Attempted to assign behavior to null entity" << std::endl;
        return;
    }

    // First check if behavior template exists
    std::shared_ptr<AIBehavior> behaviorTemplate = nullptr;
    {
        std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
        auto it = m_behaviors.find(behaviorName);
        if (it == m_behaviors.end()) {
            std::cerr << "Forge Game Engine - Behavior '" << behaviorName << "' not registered" << std::endl;
            return;
        }
        behaviorTemplate = it->second;
    }

    // Create a unique instance of the behavior for this entity
    std::shared_ptr<AIBehavior> behaviorInstance = nullptr;
    try {
        behaviorInstance = behaviorTemplate->clone();
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Error cloning behavior " << behaviorName
                  << " for entity: " << e.what() << std::endl;
        return;
    }

    if (!behaviorInstance) {
        std::cerr << "Forge Game Engine - Failed to clone behavior " << behaviorName << std::endl;
        return;
    }

    // Create weak pointer from shared pointer
    EntityWeakPtr entityWeak = entity;

    // Store the unique behavior instance for this entity
    {
        std::unique_lock<std::shared_mutex> lock(m_entityMutex);
        m_entityBehaviors[entityWeak] = behaviorName;
        m_entityBehaviorInstances[entityWeak] = behaviorInstance;
    }

    // Mark caches for deferred invalidation instead of immediate invalidation
    m_cacheInvalidationPending.store(true, std::memory_order_release);

    // Initialize the entity with its unique behavior instance
    try {
        behaviorInstance->init(entity);
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Error initializing " << behaviorName
                  << " for entity: " << e.what() << std::endl;
    }
}

// Batched behavior assignment system implementations
void AIManager::queueBehaviorAssignment(EntityPtr entity, const std::string& behaviorName) {
    if (!entity) {
        std::cerr << "Forge Game Engine - Error: Attempted to queue behavior assignment for null entity" << std::endl;
        return;
    }

    if (!m_initialized.load(std::memory_order_acquire)) {
        std::cerr << "Forge Game Engine - Error: AIManager not initialized" << std::endl;
        return;
    }

    try {
        std::lock_guard<std::mutex> lock(m_pendingAssignmentsMutex);
        m_pendingBehaviorAssignments.emplace_back(entity, behaviorName);
        m_pendingAssignmentCount.store(m_pendingBehaviorAssignments.size(), std::memory_order_release);

        AI_LOG("Queued behavior assignment: " << behaviorName << " for entity (queue size: " << m_pendingBehaviorAssignments.size() << ")");
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Exception in queueBehaviorAssignment: " << e.what() << std::endl;
    }
}

size_t AIManager::processPendingBehaviorAssignments() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return 0;
    }

    std::vector<PendingBehaviorAssignment> assignmentsToProcess;

    // Move pending assignments to local vector to minimize lock time
    {
        std::lock_guard<std::mutex> lock(m_pendingAssignmentsMutex);
        if (m_pendingBehaviorAssignments.empty()) {
            return 0;
        }

        assignmentsToProcess = std::move(m_pendingBehaviorAssignments);
        m_pendingBehaviorAssignments.clear();
        m_pendingAssignmentCount.store(0, std::memory_order_release);
    }

    size_t processedCount = 0;
    size_t failedCount = 0;

    AI_LOG("Processing " << assignmentsToProcess.size() << " batched behavior assignments");

    for (const auto& assignment : assignmentsToProcess) {
        try {
            // Check if entity is still valid
            if (assignment.entity) {
                assignBehaviorToEntity(assignment.entity, assignment.behaviorName);
                processedCount++;
            } else {
                failedCount++;
                AI_LOG("Skipped assignment for expired entity: " << assignment.behaviorName);
            }
        } catch (const std::exception& e) {
            failedCount++;
            std::cerr << "Forge Game Engine - Exception processing batched assignment for "
                      << assignment.behaviorName << ": " << e.what() << std::endl;
        }
    }

    if (failedCount > 0) {
        std::cerr << "Forge Game Engine - Warning: " << failedCount
                  << " out of " << assignmentsToProcess.size()
                  << " batched behavior assignments failed" << std::endl;
    }

    AI_LOG("Processed " << processedCount << " behavior assignments (" << failedCount << " failed)");
    return processedCount;
}

size_t AIManager::getPendingBehaviorAssignmentCount() const {
    return m_pendingAssignmentCount.load(std::memory_order_acquire);
}

void AIManager::unassignBehaviorFromEntity(EntityPtr entity) {
    if (!entity) {
        return;
    }

    try {
        // Create weak pointer from shared pointer
        EntityWeakPtr entityWeak = entity;

        // Get the behavior for this entity before removing it
        std::shared_ptr<AIBehavior> behavior = nullptr;
        std::string behaviorName;
        {
            std::shared_lock<std::shared_mutex> entityLock(m_entityMutex);
            auto it = m_entityBehaviors.find(entityWeak);
            if (it != m_entityBehaviors.end()) {
                behaviorName = it->second;
            }
        }

        // Clean up the entity's behavior instance
        if (!behaviorName.empty()) {
            std::shared_ptr<AIBehavior> behaviorInstance = nullptr;
            {
                std::shared_lock<std::shared_mutex> entityLock2(m_entityMutex);
                auto instanceIt = m_entityBehaviorInstances.find(entityWeak);
                if (instanceIt != m_entityBehaviorInstances.end()) {
                    behaviorInstance = instanceIt->second;
                }
            }

            if (behaviorInstance) {
                try {
                    behaviorInstance->cleanupEntity(entity);
                } catch (...) {
                    // Silently catch cleanup errors but continue with unassignment
                }
            }
        }

        // Remove entity from behavior maps - use write lock
        {
            std::unique_lock<std::shared_mutex> lock(m_entityMutex);
            m_entityBehaviors.erase(entityWeak);
            m_entityBehaviorInstances.erase(entityWeak);
        }

        // Mark caches for deferred invalidation instead of immediate invalidation
        m_cacheInvalidationPending.store(true, std::memory_order_release);

        AI_LOG("Unassigned behavior from entity at " << entity.get());
    } catch (...) {
        // Catch any exceptions to prevent crashes
        std::cerr << "Forge Game Engine - AIManager: Error unassigning behavior from entity" << std::endl;
    }
}

bool AIManager::entityHasBehavior(EntityPtr entity) const {
    if (!entity) {
        return false;
    }

    EntityWeakPtr entityWeak = entity;

    // Check for entity in map - read lock
    std::shared_lock<std::shared_mutex> lock(m_entityMutex);
    return m_entityBehaviors.find(entityWeak) != m_entityBehaviors.end();
}

bool AIManager::hasEntityWithBehavior(const std::string& behaviorName) const {
    // First check if cache is valid - might avoid locking
    if (m_batchesValid.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(m_batchesMutex);
        auto it = m_behaviorBatches.find(behaviorName);
        if (it != m_behaviorBatches.end()) {
            return !it->second.empty();
        }
    }

    // Fallback to entity check
    std::shared_lock<std::shared_mutex> lock(m_entityMutex);
    for (const auto& [entity, behavior] : m_entityBehaviors) {
        if (behavior == behaviorName) {
            return true;
        }
    }

    return false;
}

void AIManager::updateEntityBehavior(EntityPtr entity) {
    if (!entity) return;

    std::shared_lock<std::shared_mutex> lock(m_entityMutex);
    EntityWeakPtr entityWeak = entity;
    auto behaviorIt = m_entityBehaviorInstances.find(entityWeak);
    if (behaviorIt != m_entityBehaviorInstances.end() && behaviorIt->second) {
        behaviorIt->second->executeLogic(entity);
    }
}

void AIManager::update() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    // Check for pending cache invalidation and handle it here in the update thread
    if (m_cacheInvalidationPending.load(std::memory_order_acquire)) {
        // Safely invalidate caches during update cycle when no other operations are in progress
        invalidateOptimizationCaches();
        m_cacheInvalidationPending.store(false, std::memory_order_release);
    }

    // First ensure caches are valid
    ensureOptimizationCachesValid();

    // Process all behaviors in optimized batches
    batchUpdateAllBehaviors();

    // Process any pending messages
    processMessageQueue();
}

bool AIManager::isUpdateSafe() const {
    // Don't update if not initialized
    if (!m_initialized.load(std::memory_order_acquire)) {
        return false;
    }

    // Don't update if cache invalidation is pending (rapid behavior assignments in progress)
    if (m_cacheInvalidationPending.load(std::memory_order_acquire)) {
        return false;
    }

    // Don't update if already processing messages to avoid recursion
    if (m_processingMessages.load(std::memory_order_acquire)) {
        return false;
    }

    return true;
}

void AIManager::invalidateOptimizationCaches() {
    m_cacheValid.store(false, std::memory_order_release);
    m_batchesValid.store(false, std::memory_order_release);
}

void AIManager::ensureOptimizationCachesValid() {
    // First check if cache needs rebuilding
    if (!m_cacheValid.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        // Double-check after acquiring lock
        if (!m_cacheValid.load(std::memory_order_acquire)) {
            rebuildEntityBehaviorCache();
            m_cacheValid.store(true, std::memory_order_release);
        }
    }

    // Then check if batches need rebuilding
    if (!m_batchesValid.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(m_batchesMutex);
        // Double-check after acquiring lock
        if (!m_batchesValid.load(std::memory_order_acquire)) {
            rebuildBehaviorBatches();
            m_batchesValid.store(true, std::memory_order_release);
        }
    }
}

void AIManager::rebuildEntityBehaviorCache() {
    // NOTE: This method assumes m_cacheMutex is already held by caller
    // (specifically ensureOptimizationCachesValid)

    // Clear existing cache
    m_entityBehaviorCache.clear();

    // Read entity-behavior mappings - read lock
    std::shared_lock<std::shared_mutex> entityLock(m_entityMutex);
    std::shared_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);

    // Rebuild cache with latest mappings
    for (const auto& [entityWeak, behaviorName] : m_entityBehaviors) {
        // Skip expired entity references
        if (entityWeak.expired()) continue;

        auto behaviorIt = m_behaviors.find(behaviorName);
        if (behaviorIt != m_behaviors.end()) {
            EntityBehaviorCache cacheEntry;
            cacheEntry.entityWeak = entityWeak;
            cacheEntry.behaviorWeak = behaviorIt->second;
            cacheEntry.behaviorName = behaviorName;
            cacheEntry.lastUpdateTime = getCurrentTimeNanos();

            m_entityBehaviorCache.push_back(std::move(cacheEntry));
        }
    }

    AI_LOG("Rebuilt entity behavior cache with " << m_entityBehaviorCache.size() << " entries");
}

void AIManager::rebuildBehaviorBatches() {
    // Clear existing batches
    m_behaviorBatches.clear();

    // Read entity-behavior mappings - read lock
    std::shared_lock<std::shared_mutex> lock(m_entityMutex);

    // Group entities by behavior
    for (const auto& [entityWeak, behaviorName] : m_entityBehaviors) {
        // Skip expired entity references
        if (entityWeak.expired()) continue;

        // Get the shared pointer from the weak pointer
        EntityPtr entity = entityWeak.lock();
        if (!entity) continue;
        m_behaviorBatches[behaviorName].push_back(entity);
    }

    AI_LOG("Rebuilt behavior batches with " << m_behaviorBatches.size() << " behavior types");
}

void AIManager::batchUpdateAllBehaviors() {
    // Ensure caches are valid
    ensureOptimizationCachesValid();

    // Only use threading if enabled and ThreadSystem is available
    bool useThreading = m_useThreading.load(std::memory_order_acquire) && 
                         Forge::ThreadSystem::Exists();

    if (useThreading) {
        // Create a copy of behavior batch map to avoid lock contention during processing
        boost::container::flat_map<std::string, BehaviorBatch> batchesCopy;
        {
            std::lock_guard<std::mutex> lock(m_batchesMutex);
            batchesCopy = m_behaviorBatches;
        }

        // Create a vector to store futures for all batches
        std::vector<std::future<void>> batchFutures;
        batchFutures.reserve(batchesCopy.size());

        // Process each behavior type in parallel
        for (const auto& [behaviorName, batch] : batchesCopy) {
            if (batch.empty()) continue;

            auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
                [this, behaviorName, batch]() {
                    updateBehaviorBatch(behaviorName, batch);
                },
                Forge::TaskPriority::Normal,
                "AI Batch Update: " + behaviorName + " (" + std::to_string(batch.size()) + " entities)");

            batchFutures.push_back(std::move(future));
        }

        // Wait for all batch updates to complete
        for (auto& future : batchFutures) {
            try {
                future.get();
            } catch (const std::exception& e) {
                std::cerr << "Exception in AI batch update: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Unknown exception in AI batch update" << std::endl;
            }
        }
    } else {
        // Fallback to sequential processing if threading is disabled
        std::lock_guard<std::mutex> lock(m_batchesMutex);
        for (const auto& [behaviorName, batch] : m_behaviorBatches) {
            if (!batch.empty()) {
                updateBehaviorBatch(behaviorName, batch);
            }
        }
    }
}

void AIManager::updateBehaviorBatch(const std::string_view& behaviorName, const BehaviorBatch& batch) {
    // This method is now deprecated since AIManager handles all update timing
    // through updateManagedEntities(). Keeping for compatibility but doing nothing.
    // TODO: Remove this method entirely after confirming no external dependencies
    (void)behaviorName; // Unused parameter
    (void)batch; // Unused parameter
}

void AIManager::batchProcessEntities(const std::string& behaviorName, const std::vector<EntityPtr>& entities) {
    // This method is now deprecated since AIManager handles all update timing
    // through updateManagedEntities(). Keeping for compatibility but doing nothing.
    (void)behaviorName; // Unused parameter
    (void)entities; // Unused parameter
}

void AIManager::processEntitiesWithBehavior(std::shared_ptr<AIBehavior> behavior, const std::vector<EntityPtr>& entities, bool useThreading) {
    // This method is now deprecated since AIManager handles all update timing
    // through updateManagedEntities(). Keeping for compatibility but doing nothing.
    (void)behavior; // Unused parameter
    (void)entities; // Unused parameter
    (void)useThreading; // Unused parameter
}

void AIManager::sendMessageToEntity(EntityPtr entity, const std::string& message, bool immediate) {
    if (!entity) return;

    // Create weak pointer from shared pointer
    EntityWeakPtr entityWeak = entity;

    if (immediate) {
        deliverMessageToEntity(entity, message);
    } else {
        m_messageQueue.enqueueMessage(entityWeak, message);
    }
}

void AIManager::broadcastMessage(const std::string& message, bool immediate) {
    if (immediate) {
        deliverBroadcastMessage(message);
    } else {
        EntityWeakPtr nullEntity; // Empty weak_ptr for broadcast
        m_messageQueue.enqueueMessage(nullEntity, message);
    }
}

void AIManager::processMessageQueue() {
    // Check if we're already processing messages
    if (m_processingMessages.exchange(true)) return;

    // Get performance start time
    auto startTime = getCurrentTimeNanos();

    // Swap buffers to minimize lock time
    m_messageQueue.swapBuffers();

    // Process messages - now lock-free since we're using our own buffer
    const auto& messages = m_messageQueue.getProcessingQueue();
    for (const auto& msg : messages) {
        if (!msg.targetEntity.expired()) {
            // If the target entity still exists, deliver the message
            EntityPtr entity = msg.targetEntity.lock();
            if (entity) {
                deliverMessageToEntity(entity, msg.message);
            }
        } else {
            // Empty weak_ptr indicates broadcast
            deliverBroadcastMessage(msg.message);
        }
    }

    // Record performance metrics
    auto endTime = getCurrentTimeNanos();

    {
        double timeMs = (endTime - startTime) / 1000000.0;
        std::lock_guard<std::mutex> lock(m_perfStatsMutex);
        m_messageQueueStats.addSample(timeMs);
    }

    // Reset processing flag
    m_processingMessages.store(false, std::memory_order_release);
}

void AIManager::deliverMessageToEntity(EntityPtr entity, const std::string& message) {
    if (!entity) return;

    // Create weak pointer from shared pointer
    EntityWeakPtr entityWeak = entity;

    // Get entity's behavior
    std::shared_ptr<AIBehavior> behavior = nullptr;

    // First try to find in cache for efficiency
    if (m_cacheValid.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        auto it = std::find_if(m_entityBehaviorCache.begin(), m_entityBehaviorCache.end(),
            [&entityWeak](const EntityBehaviorCache& cache) {
                return !cache.entityWeak.expired() && !entityWeak.expired() &&
                       cache.entityWeak.lock() == entityWeak.lock();
            });

        if (it != m_entityBehaviorCache.end()) {
            behavior = it->behaviorWeak.lock();
        }
    }

    // If not found in cache, check the map
    if (!behavior) {
        std::string behaviorName;

        {
            std::shared_lock<std::shared_mutex> entityLock(m_entityMutex);
            auto it = m_entityBehaviors.find(entityWeak);
            if (it != m_entityBehaviors.end()) {
                behaviorName = it->second;
            }
        }

        if (!behaviorName.empty()) {
            std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
            auto it = m_behaviors.find(behaviorName);
            if (it != m_behaviors.end()) {
                behavior = it->second;
            }
        }
    }

    // Deliver message if behavior found
    if (behavior) {
        behavior->onMessage(entity, message);
        AI_LOG("Delivered message '" << message << "' to entity at " << entity.get());
    }
}

size_t AIManager::deliverBroadcastMessage(const std::string& message) {
    size_t deliveredCount = 0;

    // Use cache if valid for efficiency
    if (m_cacheValid.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

        for (const auto& cache : m_entityBehaviorCache) {
            if (!cache.behaviorWeak.expired() && !cache.entityWeak.expired()) {
                EntityPtr entity = cache.entityWeak.lock();
                std::shared_ptr<AIBehavior> behavior = cache.behaviorWeak.lock();
                if (entity && behavior) {
                    behavior->onMessage(entity, message);
                    deliveredCount++;
                }
            }
        }
    } else {
        // Otherwise use entity-behavior map
        std::shared_lock<std::shared_mutex> entityLock(m_entityMutex);
        std::shared_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);

        for (const auto& [entityWeak, behaviorName] : m_entityBehaviors) {
            if (!entityWeak.expired()) {
                EntityPtr entity = entityWeak.lock();
                auto it = m_behaviors.find(behaviorName);
                if (it != m_behaviors.end() && it->second && entity) {
                    it->second->onMessage(entity, message);
                    deliveredCount++;
                }
            }
        }
    }

    AI_LOG("Broadcast message '" << message << "' to " << deliveredCount << " entities");
    return deliveredCount;
}

void AIManager::recordBehaviorPerformance(const std::string_view& behaviorName, double timeMs) {
    std::lock_guard<std::mutex> lock(m_perfStatsMutex);
    m_behaviorPerformanceStats[std::string(behaviorName)].addSample(timeMs);
}

void AIManager::resetBehaviors() {
    // First, send release_entities message to all behaviors
    try {
        broadcastMessage("release_entities", true);
        processMessageQueue();
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Exception during behavior release: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Forge Game Engine - Unknown exception during behavior release" << std::endl;
    }

    // Stop message processing
    m_processingMessages.store(false, std::memory_order_release);
    m_messageQueue.clear();

    // Clear entity assignments but keep registered behaviors
    {
        std::unique_lock<std::shared_mutex> lock(m_entityMutex);
        m_entityBehaviors.clear();
    }

    // Clear caches
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_entityBehaviorCache.clear();
    }

    {
        std::lock_guard<std::mutex> batchesLock(m_batchesMutex);
        m_behaviorBatches.clear();
    }

    // Reset stats
    {
        std::lock_guard<std::mutex> perfLock(m_perfStatsMutex);
        m_behaviorPerformanceStats.clear();
        m_messageQueueStats.reset();
    }

    // Invalidate caches
    m_cacheValid.store(false, std::memory_order_release);
    m_batchesValid.store(false, std::memory_order_release);

    std::cout << "Forge Game Engine - AIManager: Behaviors reset" << std::endl;
}

size_t AIManager::getBehaviorCount() const {
    std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
    return m_behaviors.size();
}

size_t AIManager::getManagedEntityCount() const {
    std::shared_lock<std::shared_mutex> lock(m_entityMutex);
    return m_entityBehaviors.size();
}

// Entity update management implementation
void AIManager::registerEntityForUpdates(EntityPtr entity, EntityPtr player) {
    if (!entity) return;

    {
        std::lock_guard<std::shared_mutex> lock(m_managedEntitiesMutex);

        // Check if entity is already registered
        EntityWeakPtr entityWeak = entity;
        for (const auto& info : m_managedEntities) {
            if (!info.entityWeak.expired() && info.entityWeak.lock() == entity) {
                return; // Already registered
            }
        }

        // Add new entity
        m_managedEntities.emplace_back(entity);
    }

    // Set player if provided
    if (player) {
        setPlayerForDistanceOptimization(player);
    }
}

void AIManager::unregisterEntityFromUpdates(EntityPtr entity) {
    if (!entity) return;

    std::lock_guard<std::shared_mutex> lock(m_managedEntitiesMutex);

    EntityWeakPtr entityWeak = entity;
    m_managedEntities.erase(
        std::remove_if(m_managedEntities.begin(), m_managedEntities.end(),
            [&entityWeak](const EntityUpdateInfo& info) {
                return info.entityWeak.expired() || info.entityWeak.lock() == entityWeak.lock();
            }),
        m_managedEntities.end()
    );
}

void AIManager::setPlayerForDistanceOptimization(EntityPtr player) {
    std::lock_guard<std::shared_mutex> lock(m_managedEntitiesMutex);
    m_playerEntity = player;
}

void AIManager::updateManagedEntities() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    std::shared_lock<std::shared_mutex> lock(m_managedEntitiesMutex);
    
    // Early return if no entities to manage
    if (m_managedEntities.empty()) {
        return;
    }

    // Get player entity for distance calculations
    EntityPtr player = m_playerEntity.lock();

    // Update entities with distance-based optimization
    for (auto& info : m_managedEntities) {
        EntityPtr entity = info.entityWeak.lock();
        if (!entity) continue; // Entity was destroyed

        try {
            if (shouldUpdateEntity(entity, player, info.frameCounter)) {
                // Update entity movement/animation
                entity->update();

                // Update AI behavior if entity has one
                updateEntityBehavior(entity);

                info.lastUpdateTime = getCurrentTimeNanos();
            }
        } catch (const std::exception& e) {
            std::cerr << "Forge Game Engine - ERROR: Exception updating managed entity: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Forge Game Engine - ERROR: Unknown exception updating managed entity" << std::endl;
        }
    }

    // Clean up expired entities (upgrade to exclusive lock for cleanup)
    lock.unlock();
    {
        std::lock_guard<std::shared_mutex> exclusiveLock(m_managedEntitiesMutex);
        m_managedEntities.erase(
            std::remove_if(m_managedEntities.begin(), m_managedEntities.end(),
                [](const EntityUpdateInfo& info) {
                    return info.entityWeak.expired();
                }),
            m_managedEntities.end()
        );
    }
}

void AIManager::configureDistanceThresholds(float maxUpdateDist, float mediumUpdateDist, float minUpdateDist) {
    m_maxUpdateDistance.store(maxUpdateDist, std::memory_order_release);
    m_mediumUpdateDistance.store(mediumUpdateDist, std::memory_order_release);
    m_minUpdateDistance.store(minUpdateDist, std::memory_order_release);
}

size_t AIManager::getRegisteredEntityCount() const {
    std::shared_lock<std::shared_mutex> lock(m_managedEntitiesMutex);
    return m_managedEntities.size();
}

bool AIManager::shouldUpdateEntity(EntityPtr entity, EntityPtr player, int& frameCounter) {
    if (!entity) return false;

    frameCounter++;

    if (!player) {
        return true; // Always update if no player reference
    }

    Vector2D toPlayer = player->getPosition() - entity->getPosition();
    float distSq = toPlayer.lengthSquared();

    // Load distance thresholds
    float maxDist = m_maxUpdateDistance.load(std::memory_order_acquire);
    float mediumDist = m_mediumUpdateDistance.load(std::memory_order_acquire);
    float minDist = m_minUpdateDistance.load(std::memory_order_acquire);

    int requiredFrames;
    if (distSq < maxDist * maxDist) {
        requiredFrames = 1;  // Every frame for close entities
    } else if (distSq < mediumDist * mediumDist) {
        requiredFrames = 15; // Every 15 frames for medium distance
    } else if (distSq < minDist * minDist) {
        requiredFrames = 30; // Every 30 frames for far distance
    } else {
        requiredFrames = 60; // Every 60 frames for very distant entities
    }

    if (frameCounter >= requiredFrames) {
        frameCounter = 0;
        return true;
    }

    return false;
}
