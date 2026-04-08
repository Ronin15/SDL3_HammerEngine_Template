/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/ProjectileManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "events/CollisionEvent.hpp"
#include "events/EntityEvents.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "utils/SIMDMath.hpp"
#include <algorithm>
#include <chrono>
#include <format>

using namespace VoidLight::SIMD;

constexpr float MIN_PROJECTILE_SPEED_SQ = 1.0f;

// ============================================================================
// LIFECYCLE
// ============================================================================

bool ProjectileManager::init()
{
    if (m_initialized.load(std::memory_order_acquire))
    {
        PROJ_WARN("ProjectileManager already initialized");
        return true;
    }

    if (!EntityDataManager::Instance().isInitialized())
    {
        PROJ_ERROR("EntityDataManager must be initialized before ProjectileManager");
        return false;
    }

    // Reset state
    m_isShutdown.store(false, std::memory_order_release);
    m_globallyPaused.store(false, std::memory_order_release);

    // Reserve buffers
    m_activeProjectileIndices.reserve(500);
    m_destroyQueue.reserve(64);
    m_batchFutures.reserve(16);
    m_batchDestroyQueues.reserve(16);

    // Subscribe to collision events for projectile-hit-to-damage conversion
    subscribeCollisionHandler();

    m_initialized.store(true, std::memory_order_release);
    PROJ_INFO("ProjectileManager initialized successfully");
    return true;
}

void ProjectileManager::clean()
{
    if (!m_initialized.load(std::memory_order_acquire))
    {
        return;
    }

    PROJ_INFO("Cleaning up ProjectileManager...");

    m_isShutdown.store(true, std::memory_order_release);

    // Wait for any pending async work
    for (auto& future : m_batchFutures)
    {
        if (future.valid())
        {
            future.wait();
        }
    }

    unsubscribeCollisionHandler();

    // Clear buffers
    m_activeProjectileIndices.clear();
    m_activeProjectileIndices.shrink_to_fit();
    m_destroyQueue.clear();
    m_destroyQueue.shrink_to_fit();
    m_batchFutures.clear();
    m_batchFutures.shrink_to_fit();
    m_batchDestroyQueues.clear();
    m_batchDestroyQueues.shrink_to_fit();

    m_perf = PerfStats{};
    m_initialized.store(false, std::memory_order_release);
    PROJ_INFO("ProjectileManager cleaned up");
}

void ProjectileManager::prepareForStateTransition()
{
    PROJ_INFO("Preparing for state transition...");

    // Wait for any pending async work
    for (auto& future : m_batchFutures)
    {
        if (future.valid())
        {
            future.wait();
        }
    }
    m_batchFutures.clear();

    // Destroy all active projectiles
    auto& edm = EntityDataManager::Instance();
    auto projectileSpan = edm.getIndicesByKind(EntityKind::Projectile);
    for (size_t idx : projectileSpan)
    {
        EntityHandle handle = edm.getHandle(idx);
        if (handle.isValid())
        {
            edm.destroyEntity(handle);
        }
    }

    // Clear buffers (keep capacity)
    m_activeProjectileIndices.clear();
    m_destroyQueue.clear();
    for (auto& queue : m_batchDestroyQueues)
    {
        queue.clear();
    }

    PROJ_INFO("State transition preparation complete");
}


// ============================================================================
// COLLISION EVENT HANDLER
// ============================================================================

void ProjectileManager::subscribeCollisionHandler()
{
    if (m_collisionHandlerRegistered)
    {
        return;
    }

    auto& eventMgr = EventManager::Instance();
    m_collisionHandlerToken = eventMgr.registerPersistentHandlerWithToken(
        EventTypeId::Collision,
        [this](const EventData& data) { handleCollisionEvent(data); });
    m_collisionHandlerRegistered = true;
}

void ProjectileManager::unsubscribeCollisionHandler()
{
    if (!m_collisionHandlerRegistered)
    {
        return;
    }

    EventManager::Instance().removeHandler(m_collisionHandlerToken);
    m_collisionHandlerRegistered = false;
}

void ProjectileManager::handleCollisionEvent(const EventData& eventData)
{
    if (!m_initialized.load(std::memory_order_acquire) ||
        m_isShutdown.load(std::memory_order_acquire))
    {
        return;
    }

    // Extract CollisionInfo from the event
    if (!eventData.event)
    {
        return;
    }

    auto collisionEvent = std::static_pointer_cast<CollisionEvent>(eventData.event);
    if (!collisionEvent)
    {
        return;
    }

    const auto& info = collisionEvent->getInfo();

    // Only handle movable-movable collisions (projectile vs entity)
    if (!info.isMovableMovable)
    {
        return;
    }

    auto& edm = EntityDataManager::Instance();

    const auto& hotA = edm.getHotDataByIndex(info.indexA);
    const auto& hotB = edm.getHotDataByIndex(info.indexB);

    // Skip if either entity is already dead (destroyed between collision detect and event dispatch)
    if (!hotA.isAlive() || !hotB.isAlive())
    {
        return;
    }

    // Identify which entity is the projectile (if any)
    size_t projIdx = SIZE_MAX;
    size_t targetIdx = SIZE_MAX;

    if (hotA.kind == EntityKind::Projectile && hotB.kind != EntityKind::Projectile)
    {
        projIdx = info.indexA;
        targetIdx = info.indexB;
    }
    else if (hotB.kind == EntityKind::Projectile && hotA.kind != EntityKind::Projectile)
    {
        projIdx = info.indexB;
        targetIdx = info.indexA;
    }
    else
    {
        // Neither is a projectile, or both are — skip
        return;
    }

    const auto& targetHot = edm.getHotDataByIndex(targetIdx);

    // Only damage entities that have health (Player, NPC)
    if (!EntityTraits::hasHealth(targetHot.kind))
    {
        // Hit environment or non-damageable — just destroy projectile
        EntityHandle projHandle = edm.getHandle(projIdx);
        if (projHandle.isValid())
        {
            edm.destroyEntity(projHandle);
        }
        return;
    }

    // Get projectile data for damage info
    const auto& proj = edm.getProjectileData(edm.getHandle(projIdx));
    EntityHandle targetHandle = edm.getHandle(targetIdx);

    // Skip damage if projectile is not moving — stationary projectiles stick but don't hurt
    const auto& projHot = edm.getHotDataByIndex(projIdx);
    const float actualSpeedSq = projHot.transform.velocity.lengthSquared();
    if (actualSpeedSq < MIN_PROJECTILE_SPEED_SQ)
    {
        return;
    }

    // Create and enqueue DamageEvent (deferred — same pipeline as NPC combat)
    auto& eventMgr = EventManager::Instance();
    auto damageEvent = eventMgr.acquireDamageEvent();

    // Knockback scaled by actual projectile speed — faster projectiles hit harder
    constexpr float KNOCKBACK_BASE = 30.0f;
    constexpr float KNOCKBACK_SPEED_FACTOR = 0.1f;
    const float actualSpeed = std::sqrt(actualSpeedSq);
    const float knockbackForce = KNOCKBACK_BASE + actualSpeed * KNOCKBACK_SPEED_FACTOR;
    Vector2D knockback = info.normal * knockbackForce;

    damageEvent->configure(proj.owner, targetHandle, proj.damage, knockback);

    EventData damageData;
    damageData.typeId = EventTypeId::Combat;
    damageData.setActive(true);
    damageData.event = damageEvent;

    std::vector<EventManager::DeferredEvent> batch;
    batch.reserve(1);
    batch.push_back({EventTypeId::Combat, std::move(damageData)});
    eventMgr.enqueueBatch(std::move(batch));

    // Destroy projectile (unless piercing)
    if (!(proj.flags & ProjectileData::FLAG_PIERCING))
    {
        EntityHandle projHandle = edm.getHandle(projIdx);
        if (projHandle.isValid())
        {
            edm.destroyEntity(projHandle);
        }
    }
}


// ============================================================================
// MAIN UPDATE — Position integration + lifetime management
// ============================================================================

void ProjectileManager::update(float deltaTime)
{
    if (!m_initialized.load(std::memory_order_acquire) ||
        m_isShutdown.load(std::memory_order_acquire) ||
        m_globallyPaused.load(std::memory_order_acquire))
    {
        return;
    }

    auto& edm = EntityDataManager::Instance();
    auto projectileSpan = edm.getIndicesByKind(EntityKind::Projectile);

    if (projectileSpan.empty())
    {
        m_perf.lastEntitiesProcessed = 0;
        m_perf.lastUpdateMs = 0.0;
        return;
    }

    auto t0 = std::chrono::steady_clock::now();

    // Copy to local buffer (span may be invalidated during processing)
    m_activeProjectileIndices.clear();
    m_activeProjectileIndices.insert(m_activeProjectileIndices.end(),
                                     projectileSpan.begin(), projectileSpan.end());

    const size_t entityCount = m_activeProjectileIndices.size();

    // Query world bounds ONCE per frame
    float worldWidth = 32000.0f;
    float worldHeight = 32000.0f;
    auto& pathMgr = PathfinderManager::Instance();
    if (pathMgr.isInitialized())
    {
        float w, h;
        if (pathMgr.getCachedWorldBounds(w, h) && w > 0 && h > 0)
        {
            worldWidth = w;
            worldHeight = h;
        }
    }

    // WorkerBudget threading decision (follows AIManager pattern)
    auto& budgetMgr = VoidLight::WorkerBudgetManager::Instance();
    auto decision = budgetMgr.shouldUseThreading(
        VoidLight::SystemType::ProjectileSim, entityCount);
    bool useThreading = decision.shouldThread;

    size_t actualBatchCount = 1;
    bool actualWasThreaded = false;

    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;

    if (useThreading)
    {
        size_t optimalWorkerCount = budgetMgr.getOptimalWorkers(
            VoidLight::SystemType::ProjectileSim, entityCount);
        auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
            VoidLight::SystemType::ProjectileSim, entityCount, optimalWorkerCount);

        actualWasThreaded = true;
        actualBatchCount = batchCount;

        size_t entitiesPerBatch = entityCount / batchCount;
        size_t remainingEntities = entityCount % batchCount;

        // Ensure per-batch destroy queues are sized and cleared
        if (m_batchDestroyQueues.size() < batchCount)
        {
            m_batchDestroyQueues.resize(batchCount);
            for (size_t i = 0; i < batchCount; ++i)
            {
                m_batchDestroyQueues[i].reserve(32);
            }
        }
        for (size_t i = 0; i < batchCount; ++i)
        {
            m_batchDestroyQueues[i].clear();
        }

        m_batchFutures.clear();
        m_batchFutures.reserve(batchCount);

        auto& threadSystem = VoidLight::ThreadSystem::Instance();

        startTime = std::chrono::steady_clock::now();

        for (size_t i = 0; i < batchCount; ++i)
        {
            size_t start = i * entitiesPerBatch;
            size_t end = start + entitiesPerBatch;

            if (i == batchCount - 1)
            {
                end += remainingEntities;
            }

            m_batchFutures.push_back(
                threadSystem.enqueueTaskWithResult(
                    [this, start, end, deltaTime, worldWidth, worldHeight, i]()
                    {
                        processBatch(m_activeProjectileIndices, start, end,
                                     deltaTime, worldWidth, worldHeight,
                                     m_batchDestroyQueues[i]);
                    },
                    VoidLight::TaskPriority::Normal, "Proj_Batch"));
        }

        // Wait for all batches
        for (auto& future : m_batchFutures)
        {
            if (future.valid())
            {
                future.get();
            }
        }

        endTime = std::chrono::steady_clock::now();

        // Collect and process destroy queues from all batches
        for (size_t i = 0; i < batchCount; ++i)
        {
            for (const auto& handle : m_batchDestroyQueues[i])
            {
                if (handle.isValid())
                {
                    edm.destroyEntity(handle);
                }
            }
        }
    }
    else
    {
        // Single-threaded processing
        actualWasThreaded = false;
        actualBatchCount = 1;
        m_destroyQueue.clear();

        startTime = std::chrono::steady_clock::now();

        processBatch(m_activeProjectileIndices, 0, entityCount,
                     deltaTime, worldWidth, worldHeight, m_destroyQueue);

        endTime = std::chrono::steady_clock::now();

        // Destroy expired projectiles
        for (const auto& handle : m_destroyQueue)
        {
            if (handle.isValid())
            {
                edm.destroyEntity(handle);
            }
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double batchMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    m_perf.lastEntitiesProcessed = entityCount;
    m_perf.lastUpdateMs = elapsedMs;
    m_perf.lastBatchCount = actualBatchCount;
    m_perf.lastWasThreaded = actualWasThreaded;

    // EMA update for rolling average
    constexpr double PERF_ALPHA = 0.05;
    if (m_perf.totalUpdates == 0)
    {
        m_perf.avgUpdateMs = elapsedMs;
    }
    else
    {
        m_perf.avgUpdateMs = PERF_ALPHA * elapsedMs + (1.0 - PERF_ALPHA) * m_perf.avgUpdateMs;
    }
    m_perf.totalUpdates++;

    // Report ONLY batch time for adaptive tuning
    budgetMgr.reportExecution(VoidLight::SystemType::ProjectileSim,
                              entityCount, actualWasThreaded,
                              actualBatchCount, batchMs);

    VOIDLIGHT_DEBUG_ONLY(
        // Rolling log every 60 seconds (3600 updates at 60Hz)
        if (m_perf.totalUpdates % 3600 == 0 && entityCount > 0)
        {
            PROJ_DEBUG(std::format(
                "Entities: {}, Avg: {:.2f}ms [{}]",
                entityCount, m_perf.avgUpdateMs,
                actualWasThreaded ? std::format("{} batches", actualBatchCount) : "single"));
        }
    )
}


// ============================================================================
// BATCH PROCESSING — SIMD 4-wide position integration + lifetime
// ============================================================================

void ProjectileManager::processBatch(const std::vector<size_t>& indices,
                                     size_t start, size_t end,
                                     float deltaTime,
                                     float worldWidth, float worldHeight,
                                     std::vector<EntityHandle>& outDestroyQueue)
{
    auto& edm = EntityDataManager::Instance();

    // SIMD 4-wide movement batch (follows AIManager pattern)
    std::array<TransformData*, 4> batchTransforms{};
    std::array<const EntityHotData*, 4> batchHotData{};
    std::array<size_t, 4> batchEdmIndices{};
    size_t batchCount = 0;

    // Returns true if projectile hit world boundary (should be destroyed)
    auto updateMovementScalar = [&](TransformData& transform,
                                    const EntityHotData& hotData) -> bool
    {
        Vector2D pos = transform.position + (transform.velocity * deltaTime);

        float halfW = hotData.halfWidth;
        float halfH = hotData.halfHeight;
        float minX = halfW;
        float maxX = worldWidth - halfW;
        float minY = halfH;
        float maxY = worldHeight - halfH;
        if (maxX < minX)
        {
            minX = worldWidth * 0.5f;
            maxX = minX;
        }
        if (maxY < minY)
        {
            minY = worldHeight * 0.5f;
            maxY = minY;
        }
        Vector2D clamped(std::clamp(pos.getX(), minX, maxX),
                         std::clamp(pos.getY(), minY, maxY));
        transform.position = clamped;

        bool hitBoundary = false;
        if (pos.getX() < minX || pos.getX() > maxX)
        {
            transform.velocity.setX(0.0f);
            hitBoundary = true;
        }
        if (pos.getY() < minY || pos.getY() > maxY)
        {
            transform.velocity.setY(0.0f);
            hitBoundary = true;
        }
        return hitBoundary;
    };

    auto flushMovementBatch = [&]()
    {
        if (batchCount == 0)
        {
            return;
        }
        if (batchCount < 4)
        {
            for (size_t lane = 0; lane < batchCount; ++lane)
            {
                if (updateMovementScalar(*batchTransforms[lane], *batchHotData[lane]))
                {
                    EntityHandle handle = edm.getHandle(batchEdmIndices[lane]);
                    outDestroyQueue.push_back(handle);
                }
            }
            batchCount = 0;
            return;
        }

        alignas(16) float posX[4];
        alignas(16) float posY[4];
        alignas(16) float velX[4];
        alignas(16) float velY[4];
        alignas(16) float minX[4];
        alignas(16) float maxX[4];
        alignas(16) float minY[4];
        alignas(16) float maxY[4];

        for (size_t lane = 0; lane < 4; ++lane)
        {
            TransformData* transform = batchTransforms[lane];
            const EntityHotData* hotData = batchHotData[lane];
            posX[lane] = transform->position.getX();
            posY[lane] = transform->position.getY();
            velX[lane] = transform->velocity.getX();
            velY[lane] = transform->velocity.getY();
            float laneMinX = hotData->halfWidth;
            float laneMaxX = worldWidth - hotData->halfWidth;
            float laneMinY = hotData->halfHeight;
            float laneMaxY = worldHeight - hotData->halfHeight;
            if (laneMaxX < laneMinX)
            {
                laneMinX = worldWidth * 0.5f;
                laneMaxX = laneMinX;
            }
            if (laneMaxY < laneMinY)
            {
                laneMinY = worldHeight * 0.5f;
                laneMaxY = laneMinY;
            }
            minX[lane] = laneMinX;
            maxX[lane] = laneMaxX;
            minY[lane] = laneMinY;
            maxY[lane] = laneMaxY;
        }

        const Float4 deltaTimeVec = broadcast(deltaTime);
        Float4 posXv = load4_aligned(posX);
        Float4 posYv = load4_aligned(posY);
        const Float4 velXv = load4_aligned(velX);
        const Float4 velYv = load4_aligned(velY);

        posXv = madd(velXv, deltaTimeVec, posXv);
        posYv = madd(velYv, deltaTimeVec, posYv);

        const Float4 minXv = load4_aligned(minX);
        const Float4 maxXv = load4_aligned(maxX);
        const Float4 minYv = load4_aligned(minY);
        const Float4 maxYv = load4_aligned(maxY);
        const Float4 clampedXv = clamp(posXv, minXv, maxXv);
        const Float4 clampedYv = clamp(posYv, minYv, maxYv);

        const Float4 xDiff =
            bitwise_or(cmplt(clampedXv, posXv), cmplt(posXv, clampedXv));
        const Float4 yDiff =
            bitwise_or(cmplt(clampedYv, posYv), cmplt(posYv, clampedYv));
        const int clampXMask = movemask(xDiff);
        const int clampYMask = movemask(yDiff);

        store4_aligned(posX, clampedXv);
        store4_aligned(posY, clampedYv);

        const int boundaryMask = clampXMask | clampYMask;
        for (size_t lane = 0; lane < 4; ++lane)
        {
            TransformData* transform = batchTransforms[lane];
            transform->position.setX(posX[lane]);
            transform->position.setY(posY[lane]);

            if ((clampXMask >> lane) & 0x1)
            {
                transform->velocity.setX(0.0f);
            }
            if ((clampYMask >> lane) & 0x1)
            {
                transform->velocity.setY(0.0f);
            }

            // Destroy projectiles that hit world boundary
            if ((boundaryMask >> lane) & 0x1)
            {
                EntityHandle handle = edm.getHandle(batchEdmIndices[lane]);
                outDestroyQueue.push_back(handle);
            }
        }

        batchCount = 0;
    };

    // Main processing loop
    for (size_t i = start; i < end && i < indices.size(); ++i)
    {
        size_t edmIdx = indices[i];
        auto& hot = edm.getHotDataByIndex(edmIdx);

        if (!hot.isAlive()) continue;

        // Decrement lifetime first — if expired, destroy without movement
        auto& proj = edm.getProjectileData(hot.typeLocalIndex);
        proj.lifetime -= deltaTime;

        if (proj.lifetime <= 0.0f)
        {
            EntityHandle handle = edm.getHandle(edmIdx);
            outDestroyQueue.push_back(handle);
            continue;
        }

        auto& transform = hot.transform;

        // Store previous position for render interpolation
        transform.previousPosition = transform.position;

        // Accumulate for SIMD batch movement
        batchTransforms[batchCount] = &transform;
        batchHotData[batchCount] = &hot;
        batchEdmIndices[batchCount] = edmIdx;
        ++batchCount;

        if (batchCount == 4)
        {
            flushMovementBatch();
        }
    }

    // Flush remaining SIMD batch
    flushMovementBatch();
}
