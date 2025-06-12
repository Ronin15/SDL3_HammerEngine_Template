/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/AIManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <thread>

bool AIManager::init() {
    if (m_initialized.load(std::memory_order_acquire)) {
        if (!m_isShutdown) {
            AI_INFO("AIManager already initialized");
        }
        return true;
    }

    try {
        // Initialize behavior type mapping
        m_behaviorTypeMap["Wander"] = BehaviorType::Wander;
        m_behaviorTypeMap["Guard"] = BehaviorType::Guard;
        m_behaviorTypeMap["Patrol"] = BehaviorType::Patrol;
        m_behaviorTypeMap["Follow"] = BehaviorType::Follow;
        m_behaviorTypeMap["Chase"] = BehaviorType::Chase;
        m_behaviorTypeMap["Attack"] = BehaviorType::Attack;
        m_behaviorTypeMap["Flee"] = BehaviorType::Flee;
        m_behaviorTypeMap["Idle"] = BehaviorType::Idle;

        // Configure threading based on ThreadSystem availability
        bool threadSystemExists = Forge::ThreadSystem::Exists();
        m_useThreading.store(threadSystemExists, std::memory_order_release);

        // Reserve space for better performance
        m_entities.reserve(1000);
        m_managedEntities.reserve(1000);
        m_pendingAssignments.reserve(100);
        m_messageQueue.reserve(100);

        m_initialized.store(true, std::memory_order_release);

        // Only log if not in shutdown to avoid static destruction order issues
        if (!m_isShutdown) {
            AI_INFO("AIManager initialized");
            if (threadSystemExists) {
                // Cache ThreadSystem reference for better performance
                const Forge::ThreadSystem& threadSystem = Forge::ThreadSystem::Instance();
                (void)threadSystem; // Mark as intentionally used for logging
                AI_INFO("Threading enabled with " + std::to_string(threadSystem.getThreadCount()) + " threads");
            }
        }

        return true;
    }
    catch (const std::exception& e) {
        AI_CRITICAL("AIManager initialization failed: " + std::string(e.what()));
        return false;
    }
}

void AIManager::clean() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    // Only log if not in shutdown to avoid static destruction order issues
    if (!m_isShutdown) {
        AI_INFO("Cleaning up AIManager");
    }

    // Clear all data structures
    {
        std::unique_lock<std::shared_mutex> entitiesLock(m_entitiesMutex);
        std::unique_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);
        std::lock_guard<std::mutex> assignmentsLock(m_assignmentsMutex);
        std::lock_guard<std::mutex> messagesLock(m_messagesMutex);

        m_entities.clear();
        m_entityToIndex.clear();
        m_behaviorTemplates.clear();

        m_pendingAssignments.clear();
        m_messageQueue.clear();

        for (auto& stats : m_behaviorStats) {
            stats.reset();
        }
        m_globalStats.reset();
        
        // Reset assignment counter for complete state reset
        m_totalAssignmentCount.store(0, std::memory_order_relaxed);
    }

    m_initialized.store(false, std::memory_order_release);
    m_isShutdown = true;
    // Skip logging during shutdown to avoid static destruction order issues
}

void AIManager::update([[maybe_unused]] float deltaTime) {
    if (!m_initialized.load(std::memory_order_acquire) ||
        m_globallyPaused.load(std::memory_order_acquire)) {
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    try {
        // Process pending assignments
        processPendingBehaviorAssignments();

        // Update all AI entities - minimize lock scope to prevent main thread blocking
        size_t entityCount;
        bool useThreading;
        {
            std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
            entityCount = m_entities.size();
            useThreading = (entityCount >= THREADING_THRESHOLD && m_useThreading.load(std::memory_order_acquire));
        }

        if (useThreading) {
            // Check ThreadSystem availability
            if (Forge::ThreadSystem::Exists()) {
                auto& threadSystem = Forge::ThreadSystem::Instance();

                // Proper WorkerBudget calculation with architectural respect
                size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
                Forge::WorkerBudget budget = Forge::calculateWorkerBudget(availableWorkers);
                size_t aiWorkerBudget = budget.aiAllocated;
                

                // Cache frame counter for efficiency
                uint64_t currentFrame = m_frameCounter.load(std::memory_order_relaxed);
                
                // Fast frame throttling check - avoid expensive operations if already processed this frame
                uint64_t lastTaskFrame = m_lastFrameWithTasks.load(std::memory_order_relaxed);
                if (lastTaskFrame == currentFrame) {
                    // Already processed this frame, skip to avoid duplicate work
                    processBatch(0, entityCount, deltaTime);
                } else {
                    // Early queue pressure check - cached queue size
                    size_t currentQueueSize = threadSystem.getQueueSize();
                    size_t maxQueuePressure = aiWorkerBudget * 3; // Use AI worker budget, not total workers
                    
                    if (currentQueueSize >= maxQueuePressure) {
                        // Queue overloaded - single-threaded fallback
                        processBatch(0, entityCount, deltaTime);
                    } else if (m_lastFrameWithTasks.compare_exchange_strong(lastTaskFrame, currentFrame, std::memory_order_relaxed)) {
                        // Successfully claimed this frame for task submission
                        
                        // Behavior-aware batch sizing based on computational complexity
                        // Chase behavior is extremely expensive during convergence scenarios
                        constexpr size_t WANDER_ENTITIES_PER_MS = 150; // Wander/Patrol are lighter
                        constexpr size_t CHASE_ENTITIES_PER_MS = 12;   // Chase convergence is extremely heavy
                        constexpr size_t TARGET_TASK_DURATION_MS = 8;  // More aggressive target duration
                        
                        // Estimate predominant behavior complexity (simple heuristic)
                        size_t chaseCount = 0;
                        {
                            std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
                            for (const auto& entityData : m_entities) {
                                if (entityData.behaviorType == BehaviorType::Chase) {
                                    chaseCount++;
                                }
                            }
                        }
                        
                        // Use chase rate if >50% entities are chasing, otherwise use wander rate
                        size_t entitiesPerMs = (chaseCount > entityCount / 2) ? CHASE_ENTITIES_PER_MS : WANDER_ENTITIES_PER_MS;
                        size_t maxEntitiesPerTask = entitiesPerMs * TARGET_TASK_DURATION_MS;
                        
                        size_t targetTasks = (entityCount + maxEntitiesPerTask - 1) / maxEntitiesPerTask; // Ceiling division
                        targetTasks = std::min(targetTasks, aiWorkerBudget * 2); // Don't exceed 2x AI workers
                        targetTasks = std::max(size_t(1), targetTasks); // At least 1 task
                        
                        size_t entitiesPerTask = entityCount / targetTasks;
                        size_t remainder = entityCount % targetTasks;

                        // Submit larger batches - typically 2-4 tasks instead of 20-50
                        size_t processedEntities = 0;
                        for (size_t taskIdx = 0; taskIdx < targetTasks; ++taskIdx) {
                            size_t taskSize = entitiesPerTask + (taskIdx < remainder ? 1 : 0);
                            size_t taskEnd = processedEntities + taskSize;
                            
                            threadSystem.enqueueTask([this, processedEntities, taskEnd, deltaTime]() {
                                processBatch(processedEntities, taskEnd, deltaTime);
                            }, Forge::TaskPriority::Normal, "AI_Batch");
                            
                            processedEntities = taskEnd;
                        }
                    } else {
                        // Frame throttled - single-threaded fallback
                        processBatch(0, entityCount, deltaTime);
                    }
                }
            } else {
                // Fallback to single-threaded processing if ThreadSystem not available
                processBatch(0, entityCount, deltaTime);
            }
        } else {
            // Single-threaded processing for smaller counts
            processBatch(0, entityCount, deltaTime);
        }

        // Process message queue
        processMessageQueue();

        // Simplified performance tracking - less lock contention
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(endTime - startTime).count();

        // Only update stats periodically to reduce lock contention
        uint64_t currentFrame = m_frameCounter.fetch_add(1, std::memory_order_relaxed);
        
        // Clean up inactive entities less frequently to prevent race conditions
        uint64_t lastCleanup = m_lastCleanupFrame.load(std::memory_order_relaxed);
        if (currentFrame - lastCleanup > 300) { // Cleanup every 5 seconds at 60fps
            cleanupInactiveEntities();
            m_lastCleanupFrame.store(currentFrame, std::memory_order_relaxed);
        }
        
        if (currentFrame % 60 == 0) { // Update stats every 60 frames instead of every frame
            std::lock_guard<std::mutex> statsLock(m_statsMutex);
            m_globalStats.addSample(duration, m_entities.size());
        }
        
        // Log periodic summary every 300 frames (approximately every 5 seconds at 60fps)
        if (currentFrame % 300 == 0 && m_entities.size() > 0) {
            std::lock_guard<std::mutex> statsLock(m_statsMutex);
            double avgDuration = m_globalStats.updateCount > 0 ? 
                (m_globalStats.totalUpdateTime / m_globalStats.updateCount) : 0.0;
            AI_DEBUG("AI Summary - Entities: " + std::to_string(m_entities.size()) + 
                   ", Avg Update: " + std::to_string(avgDuration) + "ms" +
                   ", Current: " + std::to_string(duration) + "ms" +
                   ", Total Executions: " + std::to_string(m_totalBehaviorExecutions.load()) +
                   ", Entities/sec: " + std::to_string(m_globalStats.entitiesPerSecond));
        }
        
        // Only log individual frames when exceptionally slow
        if (duration > 50.0) {
            AI_DEBUG("Very slow AI update: " + std::to_string(duration) + "ms for " + std::to_string(m_entities.size()) + " entities");
        }

    } catch (const std::exception& e) {
        AI_ERROR("Exception in AIManager::update: " + std::string(e.what()));
    }
}

void AIManager::registerBehavior(const std::string& name, std::shared_ptr<AIBehavior> behavior) {
    if (!behavior) {
        AI_ERROR("Attempt to register null behavior: " + name);
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_behaviorsMutex);
    m_behaviorTemplates[name] = behavior;

    AI_INFO("Registered behavior: " + name);
}

bool AIManager::hasBehavior(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
    return m_behaviorTemplates.find(name) != m_behaviorTemplates.end();
}

std::shared_ptr<AIBehavior> AIManager::getBehavior(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
    auto it = m_behaviorTemplates.find(name);
    return (it != m_behaviorTemplates.end()) ? it->second : nullptr;
}

void AIManager::assignBehaviorToEntity(EntityPtr entity, const std::string& behaviorName) {
    if (!entity) {
        AI_ERROR("Attempted to assign behavior to null entity");
        return;
    }

    // Get behavior template
    std::shared_ptr<AIBehavior> behaviorTemplate;
    {
        std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
        auto it = m_behaviorTemplates.find(behaviorName);
        if (it == m_behaviorTemplates.end()) {
            AI_ERROR("Behavior '" + behaviorName + "' not registered");
            return;
        }
        behaviorTemplate = it->second;
    }

    // Create behavior instance
    std::shared_ptr<AIBehavior> behaviorInstance;
    try {
        behaviorInstance = behaviorTemplate->clone();
    } catch (const std::exception& e) {
        AI_ERROR("Error cloning behavior " + behaviorName + " for entity: " + std::string(e.what()));
        return;
    }

    if (!behaviorInstance) {
        AI_ERROR("Failed to clone behavior " + behaviorName);
        return;
    }

    // CRITICAL FIX: Use exclusive lock for entire assignment to prevent race conditions
    // This ensures atomic entity lookup + modification, preventing the assignment counter bug
    bool isNewEntity = false;
    {
        std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

        // Double-check entity existence under exclusive lock to prevent race conditions
        auto it = m_entityToIndex.find(entity);
        if (it != m_entityToIndex.end()) {
            // Update existing entry - preserve existing priority
            size_t index = it->second;
            if (index < m_entities.size()) {
                m_entities[index].behavior = behaviorInstance;
                m_entities[index].behaviorType = inferBehaviorType(behaviorName);
                m_entities[index].active = true;
                // Keep existing priority, frameCounter, etc.
            }
        } else {
            // Create new entry with default priority
            AIEntityData entityData;
            entityData.entity = entity;
            entityData.behavior = behaviorInstance;
            entityData.behaviorType = inferBehaviorType(behaviorName);
            entityData.lastPosition = entity->getPosition();
            entityData.frameCounter = 0;
            entityData.priority = 5; // Default priority matching AIDemoState
            entityData.active = true;

            size_t index = m_entities.size();
            m_entities.push_back(std::move(entityData));
            m_entityToIndex[entity] = index;
            isNewEntity = true;
        }
    }

    // Initialize behavior
    try {
        behaviorInstance->init(entity);
        
        // Thread-safe assignment tracking - only count new entity assignments, not behavior updates
        if (isNewEntity) {
            size_t currentCount = m_totalAssignmentCount.fetch_add(1, std::memory_order_relaxed);
            
            if (currentCount < 5) {
                // Log first 5 assignments for debugging
                AI_INFO("Assigned behavior '" + behaviorName + "' to new entity");
            } else if (currentCount == 5) {
                // Switch to batch mode notification
                AI_INFO("Switching to batch assignment mode for performance");
            } else if (currentCount % 1000 == 0) {
                // Log milestone every 1000 new entity assignments
                AI_INFO("Batch assigned " + std::to_string(currentCount) + " behaviors");
            }
        }
    } catch (const std::exception& e) {
        AI_ERROR("Error initializing " + behaviorName + " for entity: " + std::string(e.what()));
    }
}



void AIManager::unassignBehaviorFromEntity(EntityPtr entity) {
    if (!entity) return;

    std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);
    auto it = m_entityToIndex.find(entity);
    if (it != m_entityToIndex.end()) {
        size_t index = it->second;
        if (index < m_entities.size()) {
            m_entities[index].active = false;
            m_entities[index].behavior.reset();
        }
        m_entityToIndex.erase(it);
        // Behavior unassigned successfully - no logging to reduce console spam
    }
}

bool AIManager::entityHasBehavior(EntityPtr entity) const {
    if (!entity) return false;

    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    auto it = m_entityToIndex.find(entity);
    if (it != m_entityToIndex.end()) {
        size_t index = it->second;
        return index < m_entities.size() && m_entities[index].active && m_entities[index].behavior;
    }
    return false;
}

void AIManager::queueBehaviorAssignment(EntityPtr entity, const std::string& behaviorName) {
    if (!entity) return;

    std::lock_guard<std::mutex> lock(m_assignmentsMutex);
    
    // Check for duplicate assignments - remove existing entry for this entity
    auto it = std::find_if(m_pendingAssignments.begin(), m_pendingAssignments.end(),
        [entity](const PendingAssignment& assignment) {
            return assignment.entity == entity;
        });
    
    if (it != m_pendingAssignments.end()) {
        // Replace existing assignment with new behavior
        it->behaviorName = behaviorName;
    } else {
        // Add new assignment
        m_pendingAssignments.emplace_back(entity, behaviorName);
    }
}

size_t AIManager::processPendingBehaviorAssignments() {
    // CRITICAL: Wait for any active AI batch processing to complete before modifying entity assignments
    // This prevents race conditions that caused the assignment counter bug
    if (Forge::ThreadSystem::Exists()) {
        auto& threadSystem = Forge::ThreadSystem::Instance();
        // Simple busy-wait with yield to avoid blocking main thread performance
        int maxWaitCycles = 10; // Limit wait time to ~1ms max
        while (threadSystem.isBusy() && maxWaitCycles > 0) {
            std::this_thread::yield();
            maxWaitCycles--;
        }
    }

    std::vector<PendingAssignment> assignments;

    {
        std::lock_guard<std::mutex> lock(m_assignmentsMutex);
        assignments.swap(m_pendingAssignments);
    }

    size_t processed = 0;
    for (const auto& assignment : assignments) {
        if (assignment.entity) {
            // Check if this entity already has the requested behavior to avoid redundant work
            {
                std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
                auto it = m_entityToIndex.find(assignment.entity);
                if (it != m_entityToIndex.end()) {
                    size_t index = it->second;
                    if (index < m_entities.size() && m_entities[index].behavior) {
                        std::string currentBehavior = m_entities[index].behavior->getName();
                        if (currentBehavior == assignment.behaviorName) {
                            // Entity already has this behavior, skip assignment
                            continue;
                        }
                    }
                }
            }
            
            assignBehaviorToEntity(assignment.entity, assignment.behaviorName);
            processed++;
        }
    }

    return processed;
}

void AIManager::setPlayerForDistanceOptimization(EntityPtr player) {
    std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);
    m_playerEntity = player;
}

EntityPtr AIManager::getPlayerReference() const {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    return m_playerEntity.lock();
}

Vector2D AIManager::getPlayerPosition() const {
    auto player = getPlayerReference();
    return player ? player->getPosition() : Vector2D(0, 0);
}

bool AIManager::isPlayerValid() const {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    return !m_playerEntity.expired();
}

void AIManager::registerEntityForUpdates(EntityPtr entity, int priority) {
    if (!entity) return;

    // Clamp priority to valid range (0-9)
    priority = std::max(0, std::min(9, priority));

    std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

    // Check if already registered
    auto it = m_entityToIndex.find(entity);
    if (it != m_entityToIndex.end()) {
        // Update priority if already exists
        size_t index = it->second;
        if (index < m_entities.size()) {
            m_entities[index].priority = priority;
        }
        return;
    }

    // Create new entry - behavior will be assigned separately
    AIEntityData entityData;
    entityData.entity = entity;
    entityData.priority = priority;
    entityData.frameCounter = 0;
    entityData.lastPosition = entity->getPosition();
    entityData.lastUpdateTime = std::chrono::duration<float, std::milli>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    entityData.active = true;

    size_t index = m_entities.size();
    m_entities.push_back(std::move(entityData));
    m_entityToIndex[entity] = index;
}

void AIManager::unregisterEntityFromUpdates(EntityPtr entity) {
    if (!entity) return;

    std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);
    auto it = m_entityToIndex.find(entity);
    if (it != m_entityToIndex.end()) {
        size_t index = it->second;
        if (index < m_entities.size()) {
            m_entities[index].active = false;
        }
        m_entityToIndex.erase(it);
    }
}



void AIManager::setGlobalPause(bool paused) {
    m_globallyPaused.store(paused, std::memory_order_release);
    AI_INFO("Global AI pause: " + std::string(paused ? "enabled" : "disabled"));
}

bool AIManager::isGloballyPaused() const {
    return m_globallyPaused.load(std::memory_order_acquire);
}

void AIManager::resetBehaviors() {
    AI_INFO("Resetting all AI behaviors");

    std::unique_lock<std::shared_mutex> entitiesLock(m_entitiesMutex);
    std::unique_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);

    m_entities.clear();
    m_entityToIndex.clear();
    m_behaviorTemplates.clear();

    // Reset behavior execution counter
    m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);

    for (auto& stats : m_behaviorStats) {
        stats.reset();
    }
}

void AIManager::configureThreading(bool useThreading, unsigned int maxThreads) {
    m_useThreading.store(useThreading, std::memory_order_release);

    if (maxThreads == 0) {
        unsigned int hwThreads = std::thread::hardware_concurrency();
        maxThreads = (hwThreads == 0) ? 4 : std::max(1u, hwThreads - 1);
    }

    m_maxThreads = maxThreads;

    AI_INFO("Threading " + std::string(useThreading ? "enabled" : "disabled") +
           " with " + std::to_string(maxThreads) + " max threads");
}

void AIManager::configurePriorityMultiplier(float multiplier) {
    m_priorityMultiplier.store(multiplier, std::memory_order_release);
    AI_INFO("Priority multiplier set to: " + std::to_string(multiplier));
}

AIPerformanceStats AIManager::getPerformanceStats() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_globalStats;
}

size_t AIManager::getBehaviorCount() const {
    std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
    return m_behaviorTemplates.size();
}

size_t AIManager::getManagedEntityCount() const {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    return m_entities.size();
}

size_t AIManager::getBehaviorUpdateCount() const {
    return m_totalBehaviorExecutions.load(std::memory_order_relaxed);
}

size_t AIManager::getTotalAssignmentCount() const {
    // Thread-safe access to assignment counter atomic member
    return m_totalAssignmentCount.load(std::memory_order_relaxed);
}

void AIManager::sendMessageToEntity(EntityPtr entity, const std::string& message, bool immediate) {
    if (!entity) return;

    if (immediate) {
        std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
        auto it = m_entityToIndex.find(entity);
        if (it != m_entityToIndex.end()) {
            size_t index = it->second;
            if (index < m_entities.size() && m_entities[index].behavior) {
                try {
                    m_entities[index].behavior->onMessage(entity, message);
                } catch (const std::exception& e) {
                    AI_ERROR("Error sending immediate message: " + std::string(e.what()));
                }
            }
        }
    } else {
        std::lock_guard<std::mutex> lock(m_messagesMutex);
        m_messageQueue.emplace_back(entity, message);
    }
}

void AIManager::broadcastMessage(const std::string& message, bool immediate) {
    if (immediate) {
        std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
        for (const auto& entityData : m_entities) {
            if (entityData.active && entityData.behavior && entityData.entity) {
                try {
                    entityData.behavior->onMessage(entityData.entity, message);
                } catch (const std::exception& e) {
                    AI_ERROR("Error broadcasting immediate message: " + std::string(e.what()));
                }
            }
        }
    } else {
        std::lock_guard<std::mutex> lock(m_messagesMutex);
        m_messageQueue.emplace_back(EntityPtr(), message);
    }
}

void AIManager::processMessageQueue() {
    if (m_processingMessages.exchange(true)) {
        return; // Already processing
    }

    std::vector<QueuedMessage> messages;
    {
        std::lock_guard<std::mutex> lock(m_messagesMutex);
        messages.swap(m_messageQueue);
    }

    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);

    for (const auto& queuedMessage : messages) {
        try {
            if (queuedMessage.targetEntity.expired()) {
                // Broadcast message
                for (const auto& entityData : m_entities) {
                    if (entityData.active && entityData.behavior && entityData.entity) {
                        entityData.behavior->onMessage(entityData.entity, queuedMessage.message);
                    }
                }
            } else {
                // Targeted message
                EntityPtr target = queuedMessage.targetEntity.lock();
                if (target) {
                    auto it = m_entityToIndex.find(target);
                    if (it != m_entityToIndex.end()) {
                        size_t index = it->second;
                        if (index < m_entities.size() && m_entities[index].behavior) {
                            m_entities[index].behavior->onMessage(target, queuedMessage.message);
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            AI_ERROR("Error processing queued message: " + std::string(e.what()));
        }
    }

    m_processingMessages.store(false);
}

BehaviorType AIManager::inferBehaviorType(const std::string& behaviorName) const {
    auto it = m_behaviorTypeMap.find(behaviorName);
    return (it != m_behaviorTypeMap.end()) ? it->second : BehaviorType::Custom;
}

void AIManager::processBatch(size_t start, size_t end, float deltaTime) {
    // Cache player reference once per batch
    EntityPtr player = m_playerEntity.lock();
    Vector2D playerPos{0.0f, 0.0f};
    bool hasPlayer = false;
    
    if (player) {
        playerPos = player->getPosition();
        hasPlayer = true;
    }

    // Batch execution counter to reduce atomic operations
    size_t batchExecutions = 0;

    for (size_t i = start; i < end; ++i) {
        if (i >= m_entities.size()) break;

        auto& entityData = m_entities[i];
        if (!entityData.active || !entityData.entity || !entityData.behavior) {
            continue;
        }

        try {
            // Optimized distance check with minimal calculations
            bool shouldUpdate = true;
            if (hasPlayer) {
                entityData.frameCounter++;
                
                // Fast distance culling using squared distance
                Vector2D entityPos = entityData.entity->getPosition();
                Vector2D diff = entityPos - playerPos;
                float distanceSquared = diff.lengthSquared();
                
                // Cache the squared distance threshold
                float maxDist = m_maxUpdateDistance.load(std::memory_order_relaxed);
                float priorityMultiplier = 1.0f + entityData.priority * 0.1f;
                float maxDistSquared = maxDist * maxDist * priorityMultiplier * priorityMultiplier;

                if (distanceSquared > maxDistSquared) {
                    // Behavior-specific culling intervals optimized for performance
                    int cullInterval = (entityData.behaviorType == BehaviorType::Chase) ? 60 : 45;
                    shouldUpdate = (entityData.frameCounter % cullInterval == 0);
                }
            }

            if (shouldUpdate) {
                // Update spatial tracking only when needed
                entityData.lastPosition = entityData.entity->getPosition();

                // Execute behavior logic
                entityData.behavior->executeLogic(entityData.entity);
                batchExecutions++;

                // Update entity
                entityData.entity->update(deltaTime);
            }
        } catch (const std::exception& e) {
            AI_ERROR("Error in batch processing entity: " + std::string(e.what()));
            entityData.active = false;
        }
    }

    // Single atomic update per batch instead of per entity
    if (batchExecutions > 0) {
        m_totalBehaviorExecutions.fetch_add(batchExecutions, std::memory_order_relaxed);
    }
}

void AIManager::cleanupInactiveEntities() {
    std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

    // Remove inactive entities and rebuild index map
    auto newEnd = std::remove_if(m_entities.begin(), m_entities.end(),
        [](const AIEntityData& data) {
            return !data.active || !data.entity;
        });

    if (newEnd != m_entities.end()) {
        m_entities.erase(newEnd, m_entities.end());

        // Rebuild index map
        m_entityToIndex.clear();
        for (size_t i = 0; i < m_entities.size(); ++i) {
            if (m_entities[i].entity) {
                m_entityToIndex[m_entities[i].entity] = i;
            }
        }
    }
}

bool AIManager::shouldUpdateEntity(EntityPtr entity, EntityPtr player, int& frameCounter, int entityPriority) {
    if (!entity) return false;

    frameCounter++;

    if (!player) {
        return frameCounter % 10 == 0; // Update every 10 frames if no player
    }

    Vector2D entityPos = entity->getPosition();
    Vector2D playerPos = player->getPosition();
    float distance = (entityPos - playerPos).length();

    // Simplified priority calculation - fewer atomic loads
    float priorityFactor = 1.0f + (entityPriority * 0.1f);
    float adjustedDist = m_maxUpdateDistance.load(std::memory_order_relaxed) * priorityFactor;

    if (distance <= adjustedDist) {
        return true; // Close: every frame
    } else if (distance <= adjustedDist * 2.0f) {
        return frameCounter % 15 == 0; // Medium: every 15 frames
    } else {
        return frameCounter % 30 == 0; // Far: every 30 frames
    }
}

void AIManager::updateEntityBehavior(EntityPtr entity) {
    if (!entity) return;

    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    auto it = m_entityToIndex.find(entity);
    if (it != m_entityToIndex.end()) {
        size_t index = it->second;
        if (index < m_entities.size() && m_entities[index].behavior) {
            try {
                m_entities[index].behavior->executeLogic(entity);
                m_totalBehaviorExecutions.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                AI_ERROR("Error updating entity behavior: " + std::string(e.what()));
            }
        }
    }
}

void AIManager::recordPerformance(BehaviorType type, double timeMs, uint64_t entities) {
    std::lock_guard<std::mutex> lock(m_statsMutex);

    size_t typeIndex = static_cast<size_t>(type);
    if (typeIndex < m_behaviorStats.size()) {
        m_behaviorStats[typeIndex].addSample(timeMs, entities);
    }
}

uint64_t AIManager::getCurrentTimeNanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
}



int AIManager::getEntityPriority(EntityPtr entity) const {
    if (!entity) return DEFAULT_PRIORITY;

    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    auto it = m_entityToIndex.find(entity);
    if (it != m_entityToIndex.end() && it->second < m_entities.size()) {
        return m_entities[it->second].priority;
    }
    return DEFAULT_PRIORITY;
}

float AIManager::getUpdateRangeMultiplier(int priority) const {
    // Higher priority = larger update range multiplier
    return 1.0f + (std::max(0, std::min(9, priority)) * 0.1f);
}

void AIManager::registerEntityForUpdates(EntityPtr entity, int priority, const std::string& behaviorName) {
    // Register for updates
    registerEntityForUpdates(entity, priority);

    // Queue behavior assignment internally
    queueBehaviorAssignment(entity, behaviorName);
}
