/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "collisions/HierarchicalSpatialHash.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <cassert>

namespace HammerEngine {

// ========== HierarchicalSpatialHash Implementation ==========

HierarchicalSpatialHash::HierarchicalSpatialHash() {
    // Reserve reasonable initial capacity
    m_regions.reserve(64);
    m_bodyLocations.reserve(1024);

    // Initialize cache with fixed size
    m_queryCache.resize(CACHE_SIZE);
}

void HierarchicalSpatialHash::insert(size_t bodyIndex, const AABB& aabb) {

    // Get the coarse regions this body overlaps
    std::vector<CoarseCoord> regions = getCoarseCoordsForAABB(aabb);

    // Insert into all overlapping regions
    for (const auto& regionCoord : regions) {
        Region& region = m_regions[regionCoord];
        region.coord = regionCoord;
        insertIntoRegion(region, bodyIndex, aabb);
    }

    // Track the body's location for updates/removals
    if (!regions.empty()) {
        BodyLocation location;
        location.region = regions[0]; // Use first region as primary
        location.fineCell = 0; // Will be set if region has fine subdivision
        location.lastAABB = aabb;

        // If primary region has fine subdivision, compute fine cell
        auto regionIt = m_regions.find(regions[0]);
        if (regionIt != m_regions.end() && regionIt->second.hasFineSplit) {
            FineCoord fineCoord = getFineCoord(aabb, regions[0]);
            location.fineCell = computeGridKey(fineCoord);
        }

        m_bodyLocations[bodyIndex] = location;
    }

    invalidateQueryCache();
}

void HierarchicalSpatialHash::remove(size_t bodyIndex) {

    auto locationIt = m_bodyLocations.find(bodyIndex);
    if (locationIt == m_bodyLocations.end()) {
        return; // Body not found
    }

    const BodyLocation& location = locationIt->second;
    const AABB& aabb = location.lastAABB;

    // Get all regions this body was in
    std::vector<CoarseCoord> regions = getCoarseCoordsForAABB(aabb);

    // Remove from all regions
    for (const auto& regionCoord : regions) {
        auto regionIt = m_regions.find(regionCoord);
        if (regionIt != m_regions.end()) {
            removeFromRegion(regionIt->second, bodyIndex, aabb);

            // Clean up empty regions
            if (regionIt->second.bodyCount == 0) {
                m_regions.erase(regionIt);
            }
        }
    }

    m_bodyLocations.erase(locationIt);
    invalidateQueryCache();
}

void HierarchicalSpatialHash::update(size_t bodyIndex, const AABB& oldAABB, const AABB& newAABB) {

    // Check movement threshold to avoid unnecessary work
    if (!hasMovedSignificantly(oldAABB, newAABB)) {
        // Update stored AABB but don't rehash
        auto locationIt = m_bodyLocations.find(bodyIndex);
        if (locationIt != m_bodyLocations.end()) {
            locationIt->second.lastAABB = newAABB;
        }
        return;
    }

    // Check if regions changed
    std::vector<CoarseCoord> oldRegions = getCoarseCoordsForAABB(oldAABB);
    std::vector<CoarseCoord> newRegions = getCoarseCoordsForAABB(newAABB);

    // If regions are the same, just update fine cells if needed
    if (oldRegions == newRegions && oldRegions.size() == 1) {
        CoarseCoord regionCoord = oldRegions[0];
        auto regionIt = m_regions.find(regionCoord);
        if (regionIt != m_regions.end()) {
            if (regionIt->second.hasFineSplit) {
                // Handle fine-subdivided region
                FineCoord oldFine = getFineCoord(oldAABB, regionCoord);
                FineCoord newFine = getFineCoord(newAABB, regionCoord);

                if (oldFine.x != newFine.x || oldFine.y != newFine.y) {
                    // Move between fine cells within same region
                    GridKey oldKey = computeGridKey(oldFine);
                    GridKey newKey = computeGridKey(newFine);

                    Region& region = regionIt->second;

                    // Remove from old fine cell
                    auto oldCellIt = region.fineCells.find(oldKey);
                    if (oldCellIt != region.fineCells.end()) {
                        auto& bodyVec = oldCellIt->second;
                        bodyVec.erase(std::remove(bodyVec.begin(), bodyVec.end(), bodyIndex), bodyVec.end());
                        if (bodyVec.empty()) {
                            region.fineCells.erase(oldCellIt);
                        }
                    }

                    // Add to new fine cell
                    region.fineCells[newKey].push_back(bodyIndex);

                    // Update location tracking
                    auto locationIt = m_bodyLocations.find(bodyIndex);
                    if (locationIt != m_bodyLocations.end()) {
                        locationIt->second.fineCell = newKey;
                        locationIt->second.lastAABB = newAABB;
                    }
                } else {
                    // Same fine cell, just update AABB
                    auto locationIt = m_bodyLocations.find(bodyIndex);
                    if (locationIt != m_bodyLocations.end()) {
                        locationIt->second.lastAABB = newAABB;
                    }
                }
            } else {
                // Handle non-subdivided region - just update AABB in location tracking
                // The body remains in the same coarse cell (region.bodyIndices)
                auto locationIt = m_bodyLocations.find(bodyIndex);
                if (locationIt != m_bodyLocations.end()) {
                    locationIt->second.lastAABB = newAABB;
                }
            }
        }
        return;
    }

    // Full remove/insert if regions changed
    remove(bodyIndex);
    insert(bodyIndex, newAABB);
}

void HierarchicalSpatialHash::clear() {

    m_regions.clear();
    m_bodyLocations.clear();
    invalidateQueryCache();
}

void HierarchicalSpatialHash::queryRegion(const AABB& area, std::vector<size_t>& outBodyIndices) const {
    outBodyIndices.clear();
    outBodyIndices.reserve(64); // Reserve for typical query result size

    // PERFORMANCE: Use vector for deduplication - much faster for small sets
    std::vector<size_t> seenBodies; // Changed from unordered_set
    seenBodies.reserve(64);

    // Get all coarse regions this query overlaps
    std::vector<CoarseCoord> queryRegions = getCoarseCoordsForAABB(area);
    queryRegions.reserve(9); // Most queries overlap at most 3x3 cells

    for (const auto& regionCoord : queryRegions) {
        auto regionIt = m_regions.find(regionCoord);
        if (regionIt == m_regions.end()) continue;

        const Region& region = regionIt->second;

        if (region.hasFineSplit) {
            // Query only fine cells that overlap the query area
            std::vector<FineCoord> queryFineCells = getFineCoordList(area, regionCoord);

            for (const auto& fineCoord : queryFineCells) {
                GridKey key = computeGridKey(fineCoord);
                auto fineCellIt = region.fineCells.find(key);
                if (fineCellIt != region.fineCells.end()) {
                    for (size_t bodyIndex : fineCellIt->second) {
                        // PERFORMANCE: Linear search in small vector is faster than hash lookup
                        if (std::find(seenBodies.begin(), seenBodies.end(), bodyIndex) == seenBodies.end()) {
                            seenBodies.push_back(bodyIndex);
                            outBodyIndices.push_back(bodyIndex);
                        }
                    }
                }
            }
        } else {
            // Query coarse cell directly
            for (size_t bodyIndex : region.bodyIndices) {
                // PERFORMANCE: Linear search in small vector is faster than hash lookup
                if (std::find(seenBodies.begin(), seenBodies.end(), bodyIndex) == seenBodies.end()) {
                    seenBodies.push_back(bodyIndex);
                    outBodyIndices.push_back(bodyIndex);
                }
            }
        }
    }
}

void HierarchicalSpatialHash::queryBroadphase(size_t queryBodyIndex, const AABB& queryAABB,
                                            std::vector<size_t>& outCandidates) const {
    // Check cache first
    if (getCachedQuery(queryBodyIndex, outCandidates)) {
        return;
    }

    // Perform spatial query
    queryRegion(queryAABB, outCandidates);

    // Remove self from candidates
    outCandidates.erase(std::remove(outCandidates.begin(), outCandidates.end(), queryBodyIndex),
                       outCandidates.end());

    // Cache the result
    cacheQuery(queryBodyIndex, outCandidates);
}

// ========== Batch Operations ==========

void HierarchicalSpatialHash::insertBatch(const std::vector<std::pair<size_t, AABB>>& bodies) {

    // Pre-reserve capacity
    m_bodyLocations.reserve(m_bodyLocations.size() + bodies.size());

    for (const auto& body : bodies) {
        insert(body.first, body.second);
    }
}

void HierarchicalSpatialHash::updateBatch(const std::vector<std::tuple<size_t, AABB, AABB>>& updates) {

    for (const auto& updateInfo : updates) {
        update(std::get<0>(updateInfo), std::get<1>(updateInfo), std::get<2>(updateInfo));
    }
}

// ========== Thread Safety ==========

void HierarchicalSpatialHash::prepareForThreadedQueries() {
    m_inThreadedMode.store(true, std::memory_order_release);
}

void HierarchicalSpatialHash::finishThreadedQueries() {
    m_inThreadedMode.store(false, std::memory_order_release);
}

// ========== Statistics and Debugging ==========

size_t HierarchicalSpatialHash::getActiveRegionCount() const {
    size_t activeCount = 0;
    for (const auto& regionPair : m_regions) {
        if (regionPair.second.bodyCount > 0) {
            activeCount++;
        }
    }
    return activeCount;
}

size_t HierarchicalSpatialHash::getTotalFineCells() const {
    size_t totalCells = 0;
    for (const auto& regionPair : m_regions) {
        totalCells += regionPair.second.fineCells.size();
    }
    return totalCells;
}

void HierarchicalSpatialHash::logStatistics() const {
    size_t totalBodies = m_bodyLocations.size();
    size_t activeRegions = getActiveRegionCount();
    size_t totalFineCells = getTotalFineCells();

    COLLISION_INFO("HierarchicalSpatialHash Statistics:");
    COLLISION_INFO("  Total Bodies: " + std::to_string(totalBodies));
    COLLISION_INFO("  Total Regions: " + std::to_string(m_regions.size()));
    COLLISION_INFO("  Active Regions: " + std::to_string(activeRegions));
    COLLISION_INFO("  Total Fine Cells: " + std::to_string(totalFineCells));
    COLLISION_INFO("  Cached Queries: " + std::to_string(m_queryCache.size()));
}

// ========== Private Helper Methods ==========

HierarchicalSpatialHash::CoarseCoord HierarchicalSpatialHash::getCoarseCoord(const AABB& aabb) const {
    return {
        static_cast<int32_t>(std::floor(aabb.center.getX() / COARSE_CELL_SIZE)),
        static_cast<int32_t>(std::floor(aabb.center.getY() / COARSE_CELL_SIZE))
    };
}

std::vector<HierarchicalSpatialHash::CoarseCoord>
HierarchicalSpatialHash::getCoarseCoordsForAABB(const AABB& aabb) const {
    int32_t minX = static_cast<int32_t>(std::floor(aabb.left() / COARSE_CELL_SIZE));
    int32_t maxX = static_cast<int32_t>(std::floor(aabb.right() / COARSE_CELL_SIZE));
    int32_t minY = static_cast<int32_t>(std::floor(aabb.top() / COARSE_CELL_SIZE));
    int32_t maxY = static_cast<int32_t>(std::floor(aabb.bottom() / COARSE_CELL_SIZE));

    std::vector<CoarseCoord> coords;
    for (int32_t y = minY; y <= maxY; ++y) {
        for (int32_t x = minX; x <= maxX; ++x) {
            coords.push_back({x, y});
        }
    }
    return coords;
}

HierarchicalSpatialHash::FineCoord HierarchicalSpatialHash::getFineCoord(const AABB& aabb,
                                                                         const CoarseCoord& region) const {
    // Compute fine coordinates relative to the region's origin
    float regionOriginX = region.x * COARSE_CELL_SIZE;
    float regionOriginY = region.y * COARSE_CELL_SIZE;
    float relativeX = aabb.center.getX() - regionOriginX;
    float relativeY = aabb.center.getY() - regionOriginY;

    return {
        static_cast<int32_t>(std::floor(relativeX / FINE_CELL_SIZE)),
        static_cast<int32_t>(std::floor(relativeY / FINE_CELL_SIZE))
    };
}

std::vector<HierarchicalSpatialHash::FineCoord>
HierarchicalSpatialHash::getFineCoordList(const AABB& aabb, const CoarseCoord& region) const {
    // Compute region's origin in world coordinates
    float regionOriginX = region.x * COARSE_CELL_SIZE;
    float regionOriginY = region.y * COARSE_CELL_SIZE;

    // Convert AABB bounds to region-relative coordinates
    float relativeLeft = aabb.left() - regionOriginX;
    float relativeRight = aabb.right() - regionOriginX;
    float relativeTop = aabb.top() - regionOriginY;
    float relativeBottom = aabb.bottom() - regionOriginY;

    // Find fine cell bounds
    int32_t minX = static_cast<int32_t>(std::floor(relativeLeft / FINE_CELL_SIZE));
    int32_t maxX = static_cast<int32_t>(std::floor(relativeRight / FINE_CELL_SIZE));
    int32_t minY = static_cast<int32_t>(std::floor(relativeTop / FINE_CELL_SIZE));
    int32_t maxY = static_cast<int32_t>(std::floor(relativeBottom / FINE_CELL_SIZE));

    // Clamp to region bounds (fine cells per coarse cell = COARSE_CELL_SIZE / FINE_CELL_SIZE = 4)
    int32_t maxCellIndex = static_cast<int32_t>(COARSE_CELL_SIZE / FINE_CELL_SIZE) - 1;
    minX = std::max(0, std::min(minX, maxCellIndex));
    maxX = std::max(0, std::min(maxX, maxCellIndex));
    minY = std::max(0, std::min(minY, maxCellIndex));
    maxY = std::max(0, std::min(maxY, maxCellIndex));

    std::vector<FineCoord> coords;
    for (int32_t y = minY; y <= maxY; ++y) {
        for (int32_t x = minX; x <= maxX; ++x) {
            coords.push_back({x, y});
        }
    }
    return coords;
}

HierarchicalSpatialHash::GridKey HierarchicalSpatialHash::computeGridKey(const FineCoord& coord) const {
    // Simple 2D grid hash - much faster than Morton encoding
    // Pack coordinates into 64-bit key: (x << 32) | y
    uint32_t x = static_cast<uint32_t>(coord.x + 32768); // Offset for negative coords
    uint32_t y = static_cast<uint32_t>(coord.y + 32768);
    return (static_cast<uint64_t>(x) << 32) | y;
}

bool HierarchicalSpatialHash::hasMovedSignificantly(const AABB& oldAABB, const AABB& newAABB) const {
    Vector2D centerDelta = newAABB.center - oldAABB.center;
    float distanceSquared = centerDelta.lengthSquared();
    return distanceSquared > (MOVEMENT_THRESHOLD * MOVEMENT_THRESHOLD);
}

void HierarchicalSpatialHash::insertIntoRegion(Region& region, size_t bodyIndex, const AABB& aabb) {
    region.bodyCount++;

    if (region.hasFineSplit) {
        // Insert into fine cell
        FineCoord fineCoord = getFineCoord(aabb, region.coord);
        GridKey key = computeGridKey(fineCoord);
        region.fineCells[key].push_back(bodyIndex);
    } else {
        // Insert into coarse cell
        region.bodyIndices.push_back(bodyIndex);

        // Check if we should subdivide
        if (region.bodyCount > REGION_ACTIVE_THRESHOLD) {
            subdivideRegion(region);
        }
    }
}

void HierarchicalSpatialHash::removeFromRegion(Region& region, size_t bodyIndex, const AABB& aabb) {
    region.bodyCount--;

    if (region.hasFineSplit) {
        // Remove from fine cell
        FineCoord fineCoord = getFineCoord(aabb, region.coord);
        GridKey key = computeGridKey(fineCoord);

        auto fineCellIt = region.fineCells.find(key);
        if (fineCellIt != region.fineCells.end()) {
            auto& bodyVec = fineCellIt->second;
            bodyVec.erase(std::remove(bodyVec.begin(), bodyVec.end(), bodyIndex), bodyVec.end());
            if (bodyVec.empty()) {
                region.fineCells.erase(fineCellIt);
            }
        }

        // Check if we should unsubdivide
        if (region.bodyCount <= REGION_ACTIVE_THRESHOLD) {
            unsubdivideRegion(region);
        }
    } else {
        // Remove from coarse cell
        auto& bodyVec = region.bodyIndices;
        bodyVec.erase(std::remove(bodyVec.begin(), bodyVec.end(), bodyIndex), bodyVec.end());
    }
}

void HierarchicalSpatialHash::subdivideRegion(Region& region) {
    if (region.hasFineSplit) return; // Already subdivided

    // Move all bodies from coarse list to fine cells
    for (size_t bodyIndex : region.bodyIndices) {
        auto locationIt = m_bodyLocations.find(bodyIndex);
        if (locationIt != m_bodyLocations.end()) {
            const AABB& aabb = locationIt->second.lastAABB;
            FineCoord fineCoord = getFineCoord(aabb, region.coord);
            GridKey key = computeGridKey(fineCoord);
            region.fineCells[key].push_back(bodyIndex);
        }
    }

    region.bodyIndices.clear();
    region.hasFineSplit = true;
}

void HierarchicalSpatialHash::unsubdivideRegion(Region& region) {
    if (!region.hasFineSplit) return; // Not subdivided

    // Move all bodies from fine cells back to coarse list
    for (const auto& fineCellPair : region.fineCells) {
        for (size_t bodyIndex : fineCellPair.second) {
            region.bodyIndices.push_back(bodyIndex);
        }
    }

    region.fineCells.clear();
    region.hasFineSplit = false;
}

void HierarchicalSpatialHash::invalidateQueryCache() const {
    // Increment global version to invalidate all cached entries
    m_globalVersion.fetch_add(1, std::memory_order_release);
}

bool HierarchicalSpatialHash::getCachedQuery(size_t bodyIndex, std::vector<size_t>& outCandidates) const {
    // Hash the body index to a cache slot
    size_t slot = bodyIndex & (CACHE_SIZE - 1);

    // Use shared lock for reading cache (multiple threads can read simultaneously)
    std::shared_lock<std::shared_mutex> lock(m_cacheMutex);

    // Load the entry atomically
    size_t cachedIndex = m_queryCache[slot].bodyIndex.load(std::memory_order_acquire);
    if (cachedIndex == bodyIndex) {
        // Check version to ensure data is still valid
        uint64_t entryVersion = m_queryCache[slot].version.load(std::memory_order_acquire);
        if (entryVersion == m_globalVersion.load(std::memory_order_acquire)) {
            size_t count = m_queryCache[slot].candidateCount.load(std::memory_order_acquire);
            outCandidates.clear();
            outCandidates.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                outCandidates.push_back(m_queryCache[slot].candidates[i]);
            }
            return true;
        }
    }
    return false;
}

void HierarchicalSpatialHash::cacheQuery(size_t bodyIndex, const std::vector<size_t>& candidates) const {
    // Hash to slot
    size_t slot = bodyIndex & (CACHE_SIZE - 1);

    // Use exclusive lock for writing cache (only one thread can write at a time)
    std::unique_lock<std::shared_mutex> lock(m_cacheMutex);

    // Copy to fixed array (up to MAX_CANDIDATES)
    size_t count = std::min(candidates.size(), CacheEntry::MAX_CANDIDATES);
    for (size_t i = 0; i < count; ++i) {
        m_queryCache[slot].candidates[i] = candidates[i];
    }

    // Update metadata atomically
    m_queryCache[slot].candidateCount.store(count, std::memory_order_release);
    m_queryCache[slot].version.store(m_globalVersion.load(std::memory_order_relaxed), std::memory_order_release);
    m_queryCache[slot].bodyIndex.store(bodyIndex, std::memory_order_release);
}

// Note: Removed MortonUtils - replaced with simple 2D grid hash for better performance

} // namespace HammerEngine