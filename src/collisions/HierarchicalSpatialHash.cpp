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
}

void HierarchicalSpatialHash::insert(size_t bodyIndex, const AABB& aabb) {

    // Get the coarse regions this body overlaps
    std::vector<CoarseCoord> regions;
    getCoarseCoordsForAABB(aabb, regions);

    // Insert into all overlapping regions
    for (const auto& regionCoord : regions) {
        // THREAD SAFETY: Unique lock for exclusive write access to regions
        std::unique_lock<std::shared_mutex> lock(m_regionsMutex);
        Region& region = m_regions[regionCoord];
        region.coord = regionCoord;
        lock.unlock(); // Release lock before calling insertIntoRegion
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
}

void HierarchicalSpatialHash::remove(size_t bodyIndex) {

    auto locationIt = m_bodyLocations.find(bodyIndex);
    if (locationIt == m_bodyLocations.end()) {
        return; // Body not found
    }

    const BodyLocation& location = locationIt->second;
    const AABB& aabb = location.lastAABB;

    // Get all regions this body was in
    std::vector<CoarseCoord> regions;
    getCoarseCoordsForAABB(aabb, regions);

    // Remove from all regions
    for (const auto& regionCoord : regions) {
        // THREAD SAFETY: Unique lock for exclusive write access to regions
        std::unique_lock<std::shared_mutex> lock(m_regionsMutex);
        auto regionIt = m_regions.find(regionCoord);
        if (regionIt != m_regions.end()) {
            lock.unlock(); // Release lock before calling removeFromRegion
            removeFromRegion(regionIt->second, bodyIndex, aabb);

            // Re-acquire lock for potential erase
            lock.lock();
            // Clean up empty regions - need to re-find in case it changed
            regionIt = m_regions.find(regionCoord);
            if (regionIt != m_regions.end() && regionIt->second.bodyCount == 0) {
                m_regions.erase(regionIt);
            }
        }
    }

    m_bodyLocations.erase(locationIt);
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
    std::vector<CoarseCoord> oldRegions;
    std::vector<CoarseCoord> newRegions;
    getCoarseCoordsForAABB(oldAABB, oldRegions);
    getCoarseCoordsForAABB(newAABB, newRegions);

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
    // THREAD SAFETY: Unique lock for exclusive write access to regions
    std::unique_lock<std::shared_mutex> lock(m_regionsMutex);
    m_regions.clear();
    m_bodyLocations.clear();
    lock.unlock();
}

void HierarchicalSpatialHash::queryRegion(const AABB& area, std::vector<size_t>& outBodyIndices) const {
    outBodyIndices.clear();
    outBodyIndices.reserve(64); // Reserve for typical query result size

    // PERFORMANCE: Use persistent buffers to eliminate per-query allocations (single-threaded safe)
    m_tempSeenBodies.clear();
    m_tempSeenBodies.reserve(64);

    // Get all coarse regions this query overlaps (reuse persistent buffer - NO allocation!)
    getCoarseCoordsForAABB(area, m_tempQueryRegions);

    // PERFORMANCE: No mutex needed - single-threaded collision system
    for (const auto& regionCoord : m_tempQueryRegions) {
        auto regionIt = m_regions.find(regionCoord);
        if (regionIt == m_regions.end()) {
            continue;
        }

        const Region& region = regionIt->second;

        if (region.hasFineSplit) {
            // Query only fine cells that overlap the query area (reuse persistent buffer - NO allocation!)
            getFineCoordList(area, regionCoord, m_tempQueryFineCells);

            for (const auto& fineCoord : m_tempQueryFineCells) {
                GridKey key = computeGridKey(fineCoord);
                auto fineCellIt = region.fineCells.find(key);
                if (fineCellIt != region.fineCells.end()) {
                    for (size_t bodyIndex : fineCellIt->second) {
                        // PERFORMANCE: O(1) hash set insertion for deduplication
                        if (m_tempSeenBodies.insert(bodyIndex).second) {
                            outBodyIndices.push_back(bodyIndex);
                        }
                    }
                }
            }
        } else {
            // Query coarse cell directly
            for (size_t bodyIndex : region.bodyIndices) {
                // PERFORMANCE: O(1) hash set insertion for deduplication
                if (m_tempSeenBodies.insert(bodyIndex).second) {
                    outBodyIndices.push_back(bodyIndex);
                }
            }
        }
    }
}

void HierarchicalSpatialHash::queryRegionBounds(float minX, float minY, float maxX, float maxY, std::vector<size_t>& outBodyIndices) const {
    outBodyIndices.clear();
    outBodyIndices.reserve(64); // Reserve for typical query result size

    // PERFORMANCE: Use persistent buffers to eliminate per-query allocations (single-threaded safe)
    m_tempSeenBodies.clear();
    m_tempSeenBodies.reserve(64);

    // Get all coarse regions this query overlaps using bounds directly (NO AABB construction!)
    getCoarseCoordsForBounds(minX, minY, maxX, maxY, m_tempQueryRegions);

    // Construct AABB only if needed for fine cell queries (rare case)
    AABB queryArea(
        (minX + maxX) * 0.5f, // centerX
        (minY + maxY) * 0.5f, // centerY
        (maxX - minX) * 0.5f, // halfWidth
        (maxY - minY) * 0.5f  // halfHeight
    );

    // PERFORMANCE: No mutex needed - single-threaded collision system
    for (const auto& regionCoord : m_tempQueryRegions) {
        auto regionIt = m_regions.find(regionCoord);
        if (regionIt == m_regions.end()) {
            continue;
        }

        const Region& region = regionIt->second;

        if (region.hasFineSplit) {
            // Query only fine cells that overlap the query area (reuse persistent buffer - NO allocation!)
            getFineCoordList(queryArea, regionCoord, m_tempQueryFineCells);

            for (const auto& fineCoord : m_tempQueryFineCells) {
                GridKey key = computeGridKey(fineCoord);
                auto fineCellIt = region.fineCells.find(key);
                if (fineCellIt != region.fineCells.end()) {
                    for (size_t bodyIndex : fineCellIt->second) {
                        // PERFORMANCE: O(1) hash set insertion for deduplication
                        if (m_tempSeenBodies.insert(bodyIndex).second) {
                            outBodyIndices.push_back(bodyIndex);
                        }
                    }
                }
            }
        } else {
            // Query coarse cell directly
            for (size_t bodyIndex : region.bodyIndices) {
                // PERFORMANCE: O(1) hash set insertion for deduplication
                if (m_tempSeenBodies.insert(bodyIndex).second) {
                    outBodyIndices.push_back(bodyIndex);
                }
            }
        }
    }
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
}

// ========== Private Helper Methods ==========

HierarchicalSpatialHash::CoarseCoord HierarchicalSpatialHash::getCoarseCoord(const AABB& aabb) const {
    return {
        static_cast<int32_t>(std::floor(aabb.center.getX() / COARSE_CELL_SIZE)),
        static_cast<int32_t>(std::floor(aabb.center.getY() / COARSE_CELL_SIZE))
    };
}

void HierarchicalSpatialHash::getCoarseCoordsForAABB(const AABB& aabb, std::vector<CoarseCoord>& out) const {
    getCoarseCoordsForBounds(aabb.left(), aabb.top(), aabb.right(), aabb.bottom(), out);
}

void HierarchicalSpatialHash::getCoarseCoordsForBounds(float minX, float minY, float maxX, float maxY, std::vector<CoarseCoord>& out) const {
    int32_t gridMinX = static_cast<int32_t>(std::floor(minX / COARSE_CELL_SIZE));
    int32_t gridMaxX = static_cast<int32_t>(std::floor(maxX / COARSE_CELL_SIZE));
    int32_t gridMinY = static_cast<int32_t>(std::floor(minY / COARSE_CELL_SIZE));
    int32_t gridMaxY = static_cast<int32_t>(std::floor(maxY / COARSE_CELL_SIZE));

    out.clear();
    for (int32_t y = gridMinY; y <= gridMaxY; ++y) {
        for (int32_t x = gridMinX; x <= gridMaxX; ++x) {
            out.push_back({x, y});
        }
    }
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

void HierarchicalSpatialHash::getFineCoordList(const AABB& aabb, const CoarseCoord& region, std::vector<FineCoord>& out) const {
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

    out.clear();
    for (int32_t y = minY; y <= maxY; ++y) {
        for (int32_t x = minX; x <= maxX; ++x) {
            out.push_back({x, y});
        }
    }
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

} // namespace HammerEngine