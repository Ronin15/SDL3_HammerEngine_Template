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

        // Update all AI entities
        {
            std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);

            if (m_entities.size() >= THREADING_THRESHOLD && m_useThreading.load(std::memory_order_acquire)) {
                // Check ThreadSystem availability
                if (Forge::ThreadSystem::Exists()) {
                    auto& threadSystem = Forge::ThreadSystem::Instance();

                    // Proper WorkerBudget calculation with architectural respect
                    size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
                    Forge::WorkerBudget budget = Forge::calculateWorkerBudget(availableWorkers);
                    size_t aiWorkerBudget = budget.aiAllocated;
                    

                    // Optimized queue pressure check - respect ThreadSystem capacity
                    size_t currentQueueSize = threadSystem.getQueueSize();
                    size_t maxQueuePressure = availableWorkers * 3; // Reasonable threshold
                    
                    // Use buffer capacity for high workloads (architectural alignment)
                    size_t optimalWorkerCount = budget.getOptimalWorkerCount(aiWorkerBudget, m_entities.size(), 1000);
                    

                    // Only fallback if queue pressure is too high
                    if (currentQueueSize < maxQueuePressure) {
                        // Calculate batch size based on allocated workers (not fixed)
                        size_t targetBatches = std::max(size_t(1), optimalWorkerCount); // Prevent division by zero
                        size_t batchSize = std::max(size_t(50), m_entities.size() / targetBatches);
                        
                        // Apply reasonable limits for cache efficiency
                        if (m_entities.size() < 1000) {
                            batchSize = std::min(batchSize, size_t(200));
                        } else if (m_entities.size() < 10000) {
                            batchSize = std::min(batchSize, size_t(400));
                        } else {
                            batchSize = std::min(batchSize, size_t(600));
                        }

                        // Use Normal priority for AI batches to maintain proper priority system
                        Forge::TaskPriority batchPriority = Forge::TaskPriority::Normal;

                        // Submit batches respecting WorkerBudget allocation
                        for (size_t i = 0; i < m_entities.size(); i += batchSize) {
                            size_t batchEnd = std::min(i + batchSize, m_entities.size());

                            threadSystem.enqueueTask([this, i, batchEnd, deltaTime]() {
                                processBatch(i, batchEnd, deltaTime);
                            }, batchPriority, "AI_Batch");
                        }

                        // No blocking wait - let AI processing be asynchronous for optimal performance
                        // This allows the main thread to continue immediately while AI catches up
                    } else {
                        // Fallback to single-threaded
                        processBatch(0, m_entities.size(), deltaTime);
                    }
                } else {
                    // Fallback to single-threaded processing if ThreadSystem not available
                    processBatch(0, m_entities.size(), deltaTime);
                }
            } else {
                // Single-threaded processing for smaller counts
                processBatch(0, m_entities.size(), deltaTime);
            }
        }

        // Process message queue
        processMessageQueue();

        // Clean up inactive entities periodically
        cleanupInactiveEntities();

        // Simplified performance tracking - less lock contention
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(endTime - startTime).count();

        // Only update stats periodically to reduce lock contention
        uint64_t currentFrame = m_frameCounter.fetch_add(1, std::memory_order_relaxed);
        
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

    // Add to unified spatial system
    {
        std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

        // Check if entity already exists
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
            // Create new entry with default priority (should be set by registerEntityForUpdates first)
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
        }
    }

    // Initialize behavior
    try {
        behaviorInstance->init(entity);
        
        // Thread-safe assignment tracking using atomic member variable
        size_t currentCount = m_totalAssignmentCount.fetch_add(1, std::memory_order_relaxed);
        
        if (currentCount < 5) {
            // Log first 5 assignments for debugging
            AI_INFO("Assigned behavior '" + behaviorName + "' to entity");
        } else if (currentCount == 5) {
            // Switch to batch mode notification
            AI_INFO("Switching to batch assignment mode for performance");
        } else if (currentCount % 1000 == 0) {
            // Log milestone every 1000 assignments
            AI_INFO("Batch assigned " + std::to_string(currentCount) + " behaviors");
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
    m_pendingAssignments.emplace_back(entity, behaviorName);
}

size_t AIManager::processPendingBehaviorAssignments() {
    std::vector<PendingAssignment> assignments;

    {
        std::lock_guard<std::mutex> lock(m_assignmentsMutex);
        assignments.swap(m_pendingAssignments);
    }

    size_t processed = 0;
    for (const auto& assignment : assignments) {
        if (assignment.entity) {
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
            // Optimized distance check using cached player position
            bool shouldUpdate = true;
            if (hasPlayer) {
                entityData.frameCounter++;
                Vector2D entityPos = entityData.entity->getPosition();
                float distance = (entityPos - playerPos).length();

                // Simplified distance-based updates
                float adjustedDist = m_maxUpdateDistance.load(std::memory_order_relaxed) * 
                                   (1.0f + entityData.priority * 0.1f);

                if (distance > adjustedDist) {
                    // Simple frame-based culling for distant entities
                    shouldUpdate = (entityData.frameCounter % 30 == 0);
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
