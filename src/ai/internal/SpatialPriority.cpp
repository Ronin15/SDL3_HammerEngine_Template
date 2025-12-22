/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include <algorithm>
#include <sstream>
#include <cmath>
#include <vector>
#include <iomanip>
#include <format>
#include "core/Logger.hpp"
#include "ai/internal/SpatialPriority.hpp"

namespace AIInternal {

SpatialPriority::SpatialPriority()
{
    // Reserve space for typical entity counts to minimize allocations
    m_entityFrameStates.reserve(1000);
    GAMEENGINE_INFO("SpatialPriority system initialized with zone-based pathfinding optimization");
}

PathPriority SpatialPriority::getEntityPriority(EntityID entityId, const Vector2D& entityPos, const Vector2D& playerPos)
{
    (void)entityId; // Parameter used for future entity-specific priority adjustments
    
    // Calculate squared distance to avoid expensive sqrt operation
    const Vector2D diff = entityPos - playerPos;
    const float distanceSquared = diff.getX() * diff.getX() + diff.getY() * diff.getY();
    
    // Classify based on distance zones
    PathPriority priority = classifyByDistance(distanceSquared);
    
    // Update statistics (atomic operations for thread safety)
    m_totalEntitiesProcessed.fetch_add(1, std::memory_order_relaxed);
    
    switch (priority) {
        case PathPriority::Critical:
            m_highPriorityCount.fetch_add(1, std::memory_order_relaxed);
            break;
        case PathPriority::High:
            m_highPriorityCount.fetch_add(1, std::memory_order_relaxed);
            break;
        case PathPriority::Normal:
            m_normalPriorityCount.fetch_add(1, std::memory_order_relaxed);
            break;
        case PathPriority::Low:
            m_lowPriorityCount.fetch_add(1, std::memory_order_relaxed);
            break;
    }
    
    return priority;
}

bool SpatialPriority::shouldProcessThisFrame(EntityID entityId, PathPriority priority, uint64_t currentFrame)
{
    // Critical/High priority entities are always processed
    if (priority == PathPriority::Critical || priority == PathPriority::High) {
        updateEntityFrameState(entityId, priority, currentFrame, true);
        return true;
    }
    
    // Check frame skipping logic for Normal and Low priority entities
    auto it = m_entityFrameStates.find(entityId);
    if (it == m_entityFrameStates.end()) {
        // First time seeing this entity, process it
        updateEntityFrameState(entityId, priority, currentFrame, true);
        return true;
    }
    
    const EntityFrameState& state = it->second;
    
    // Check if we should skip based on frame intervals
    bool shouldProcess = !shouldSkipBasedOnFrames(state, priority, currentFrame);
    
    if (!shouldProcess) {
        m_entitiesSkipped.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Update frame state regardless of whether we process or skip
    updateEntityFrameState(entityId, priority, currentFrame, shouldProcess);
    
    return shouldProcess;
}

void SpatialPriority::updateFrameSkipping(uint64_t currentFrame)
{
    m_currentFrame.store(currentFrame, std::memory_order_relaxed);
    
    const size_t currentEntityCount = m_entityFrameStates.size();
    
    // Determine cleanup strategy based on entity count
    if (currentEntityCount >= MAX_TRACKED_ENTITIES) {
        // Emergency cleanup - we're at capacity
        performEntityCleanup(currentFrame, true);
        m_emergencyCleanupCount.fetch_add(1, std::memory_order_relaxed);
        GAMEENGINE_WARN(std::format("SpatialPriority: Emergency cleanup triggered - at max capacity ({} entities)",
                       MAX_TRACKED_ENTITIES));
    }
    else if (currentEntityCount >= AGGRESSIVE_CLEANUP_THRESHOLD) {
        // Aggressive cleanup - approaching capacity
        if (currentFrame % AGGRESSIVE_CLEANUP_INTERVAL == 0) {
            performEntityCleanup(currentFrame, true);
            m_aggressiveCleanupCount.fetch_add(1, std::memory_order_relaxed);
            GAMEENGINE_INFO(std::format("SpatialPriority: Aggressive cleanup at {} entities", currentEntityCount));
        }
    }
    else if (currentFrame % ENTITY_CLEANUP_FRAME_INTERVAL == 0) {
        // Normal cleanup (every 600 frames ≈ 10 seconds at 60 FPS)
        performEntityCleanup(currentFrame, false);
        m_normalCleanupCount.fetch_add(1, std::memory_order_relaxed);
    }
}

float SpatialPriority::getDistanceThreshold(PathPriority priority)
{
    switch (priority) {
        case PathPriority::Critical:
        case PathPriority::High:
            return NEAR_DISTANCE;
        case PathPriority::Normal:
            return MEDIUM_DISTANCE;
        case PathPriority::Low:
        default:
            return FAR_DISTANCE;
    }
}

uint32_t SpatialPriority::getFrameSkipInterval(PathPriority priority)
{
    switch (priority) {
        case PathPriority::Critical:
        case PathPriority::High:
            return HIGH_PRIORITY_SKIP;
        case PathPriority::Normal:
            return NORMAL_PRIORITY_SKIP;
        case PathPriority::Low:
        default:
            return LOW_PRIORITY_SKIP;
    }
}

std::string SpatialPriority::getPerformanceStats() const
{
    std::ostringstream oss;
    
    const uint64_t total = m_totalEntitiesProcessed.load(std::memory_order_relaxed);
    const uint64_t skipped = m_entitiesSkipped.load(std::memory_order_relaxed);
    const uint64_t high = m_highPriorityCount.load(std::memory_order_relaxed);
    const uint64_t normal = m_normalPriorityCount.load(std::memory_order_relaxed);
    const uint64_t low = m_lowPriorityCount.load(std::memory_order_relaxed);
    
    // Entity tracking stats
    const size_t activeEntities = m_entityFrameStates.size();
    const uint64_t rejected = m_entitiesRejected.load(std::memory_order_relaxed);
    const uint64_t normalCleanups = m_normalCleanupCount.load(std::memory_order_relaxed);
    const uint64_t aggressiveCleanups = m_aggressiveCleanupCount.load(std::memory_order_relaxed);
    const uint64_t emergencyCleanups = m_emergencyCleanupCount.load(std::memory_order_relaxed);
    
    oss << "SpatialPriority Stats: "
        << "Total=" << total
        << " Skipped=" << skipped;
    
    if (total > 0) {
        oss << " SkipRate=" << (100.0 * skipped / total) << "%";
    }
    
    oss << " Zones[High=" << high 
        << " Normal=" << normal 
        << " Low=" << low << "]"
        << " ActiveEntities=" << activeEntities 
        << "/" << MAX_TRACKED_ENTITIES;
    
    // Add capacity and cleanup information
    const double capacityPercent = (100.0 * activeEntities) / MAX_TRACKED_ENTITIES;
    oss << " Capacity=" << std::fixed << std::setprecision(1) << capacityPercent << "%";
    
    if (rejected > 0) {
        oss << " Rejected=" << rejected;
    }
    
    if (normalCleanups > 0 || aggressiveCleanups > 0 || emergencyCleanups > 0) {
        oss << " Cleanups[Normal=" << normalCleanups 
            << " Aggressive=" << aggressiveCleanups 
            << " Emergency=" << emergencyCleanups << "]";
    }
    
    return oss.str();
}

void SpatialPriority::resetFrameTracking()
{
    m_entityFrameStates.clear();
    m_currentFrame.store(0, std::memory_order_relaxed);
    
    // Reset statistics
    m_totalEntitiesProcessed.store(0, std::memory_order_relaxed);
    m_entitiesSkipped.store(0, std::memory_order_relaxed);
    m_highPriorityCount.store(0, std::memory_order_relaxed);
    m_normalPriorityCount.store(0, std::memory_order_relaxed);
    m_lowPriorityCount.store(0, std::memory_order_relaxed);
    
    // Reset cleanup statistics
    m_normalCleanupCount.store(0, std::memory_order_relaxed);
    m_aggressiveCleanupCount.store(0, std::memory_order_relaxed);
    m_emergencyCleanupCount.store(0, std::memory_order_relaxed);
    m_entitiesRejected.store(0, std::memory_order_relaxed);
    
    GAMEENGINE_INFO("SpatialPriority frame tracking reset");
}

// Private helper methods

PathPriority SpatialPriority::classifyByDistance(float distanceSquared) const
{
    // Compare against squared thresholds to avoid sqrt calculations
    if (distanceSquared <= (NEAR_DISTANCE * NEAR_DISTANCE)) {
        return PathPriority::High;
    }
    else if (distanceSquared <= (MEDIUM_DISTANCE * MEDIUM_DISTANCE)) {
        return PathPriority::Normal;
    }
    else {
        // Far and culled zones both use Low priority, but with different frame skipping
        return PathPriority::Low;
    }
}

bool SpatialPriority::shouldSkipBasedOnFrames(const EntityFrameState& state, PathPriority priority, uint64_t currentFrame) const
{
    const uint64_t framesSinceLastProcess = currentFrame - state.lastProcessedFrame;
    const uint32_t skipInterval = getFrameSkipInterval(priority);
    
    // Always process if enough frames have passed based on skip interval
    if (framesSinceLastProcess >= skipInterval) {
        return false; // Don't skip
    }
    
    return true; // Skip this frame
}

void SpatialPriority::updateEntityFrameState(EntityID entityId, PathPriority priority, uint64_t currentFrame, bool wasProcessed)
{
    // Check if this is a new entity
    auto it = m_entityFrameStates.find(entityId);
    if (it == m_entityFrameStates.end()) {
        // New entity - check if we should track it
        if (!shouldTrackNewEntity()) {
            m_entitiesRejected.fetch_add(1, std::memory_order_relaxed);
            return; // Don't track this entity if at capacity
        }
    }
    
    EntityFrameState& state = m_entityFrameStates[entityId];
    
    if (wasProcessed) {
        state.lastProcessedFrame = currentFrame;
        state.consecutiveSkips = 0;
    } else {
        state.consecutiveSkips++;
    }
    
    state.lastPriority = static_cast<int>(priority);
    
    // Detect entities that are being skipped too frequently (debugging aid)
    GAMEENGINE_WARN_IF(state.consecutiveSkips > 60 && priority != PathPriority::Low,
        std::format("Entity {} has been skipped {} frames consecutively",
                    entityId, state.consecutiveSkips));
}

void SpatialPriority::performEntityCleanup(uint64_t currentFrame, bool forceAggressive)
{
    const size_t sizeBeforeCleanup = m_entityFrameStates.size();
    size_t entitiesRemoved = 0;
    
    if (forceAggressive) {
        // Aggressive cleanup: Remove entities more aggressively
        // 1. Remove entities not seen for 300 frames (5 seconds at 60 FPS)
        // 2. If still over threshold, remove Low priority entities not seen for 150 frames
        // 3. If still over threshold, use LRU eviction
        
        const uint64_t aggressiveThreshold = 300;
        const uint64_t veryAggressiveThreshold = 150;
        
        auto it = m_entityFrameStates.begin();
        while (it != m_entityFrameStates.end()) {
            const uint64_t framesSinceLastSeen = currentFrame - it->second.lastProcessedFrame;
            
            bool shouldRemove = false;
            
            // First pass: Remove old entities
            if (framesSinceLastSeen > aggressiveThreshold) {
                shouldRemove = true;
            }
            // Second pass: If still over threshold, remove Low priority entities more aggressively
            else if (m_entityFrameStates.size() > AGGRESSIVE_CLEANUP_THRESHOLD && 
                     it->second.lastPriority == static_cast<int>(PathPriority::Low) &&
                     framesSinceLastSeen > veryAggressiveThreshold) {
                shouldRemove = true;
            }
            
            if (shouldRemove) {
                it = m_entityFrameStates.erase(it);
                ++entitiesRemoved;
            } else {
                ++it;
            }
        }
        
        // Emergency LRU eviction if still over capacity
        if (m_entityFrameStates.size() >= MAX_TRACKED_ENTITIES) {
            // Create vector of entities sorted by last processed frame
            std::vector<std::pair<uint64_t, EntityID>> entityFrames;
            entityFrames.reserve(m_entityFrameStates.size());
            
            std::transform(m_entityFrameStates.begin(), m_entityFrameStates.end(),
                          std::back_inserter(entityFrames),
                          [](const auto& pair) {
                              return std::make_pair(pair.second.lastProcessedFrame, pair.first);
                          });
            
            // Sort by frame (oldest first)
            std::sort(entityFrames.begin(), entityFrames.end());
            
            // Remove oldest entities until we're under capacity
            const size_t targetSize = AGGRESSIVE_CLEANUP_THRESHOLD;
            size_t toRemove = m_entityFrameStates.size() - targetSize;
            
            for (size_t i = 0; i < std::min(toRemove, entityFrames.size()); ++i) {
                m_entityFrameStates.erase(entityFrames[i].second);
                ++entitiesRemoved;
            }
        }
        
    } else {
        // Normal cleanup: Remove entities that haven't been seen for 10 seconds
        const uint64_t normalThreshold = ENTITY_CLEANUP_FRAME_INTERVAL;
        
        auto it = m_entityFrameStates.begin();
        while (it != m_entityFrameStates.end()) {
            if (currentFrame - it->second.lastProcessedFrame > normalThreshold) {
                it = m_entityFrameStates.erase(it);
                ++entitiesRemoved;
            } else {
                ++it;
            }
        }
    }
    
    const size_t sizeAfterCleanup = m_entityFrameStates.size();

    GAMEENGINE_INFO_IF(entitiesRemoved > 0,
        std::format("SpatialPriority cleanup: {} entities removed ({} -> {})",
                    entitiesRemoved, sizeBeforeCleanup, sizeAfterCleanup));
}

bool SpatialPriority::shouldTrackNewEntity() const
{
    return m_entityFrameStates.size() < MAX_TRACKED_ENTITIES;
}

} // namespace AIInternal
