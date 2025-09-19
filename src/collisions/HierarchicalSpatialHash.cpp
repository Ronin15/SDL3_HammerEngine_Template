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
    m_queryCache.reserve(256);
}

void HierarchicalSpatialHash::insert(size_t bodyIndex, const AABB& aabb) {
    assert(!m_inThreadedMode.load(std::memory_order_acquire) &&
           "Cannot insert during threaded queries");

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
            location.fineCell = computeMortonCode(fineCoord);
        }

        m_bodyLocations[bodyIndex] = location;
    }

    invalidateQueryCache();
}

void HierarchicalSpatialHash::remove(size_t bodyIndex) {
    assert(!m_inThreadedMode.load(std::memory_order_acquire) &&
           "Cannot remove during threaded queries");

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
    assert(!m_inThreadedMode.load(std::memory_order_acquire) &&
           "Cannot update during threaded queries");

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
        if (regionIt != m_regions.end() && regionIt->second.hasFineSplit) {
            FineCoord oldFine = getFineCoord(oldAABB, regionCoord);
            FineCoord newFine = getFineCoord(newAABB, regionCoord);

            if (oldFine.x != newFine.x || oldFine.y != newFine.y) {
                // Move between fine cells within same region
                MortonCode oldMorton = computeMortonCode(oldFine);
                MortonCode newMorton = computeMortonCode(newFine);

                Region& region = regionIt->second;

                // Remove from old fine cell
                auto oldCellIt = region.fineCells.find(oldMorton);
                if (oldCellIt != region.fineCells.end()) {
                    auto& bodyVec = oldCellIt->second;
                    bodyVec.erase(std::remove(bodyVec.begin(), bodyVec.end(), bodyIndex), bodyVec.end());
                    if (bodyVec.empty()) {
                        region.fineCells.erase(oldCellIt);
                    }
                }

                // Add to new fine cell
                region.fineCells[newMorton].push_back(bodyIndex);

                // Update location tracking
                auto locationIt = m_bodyLocations.find(bodyIndex);
                if (locationIt != m_bodyLocations.end()) {
                    locationIt->second.fineCell = newMorton;
                    locationIt->second.lastAABB = newAABB;
                }
            } else {
                // Same fine cell, just update AABB
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
    assert(!m_inThreadedMode.load(std::memory_order_acquire) &&
           "Cannot clear during threaded queries");

    m_regions.clear();
    m_bodyLocations.clear();
    invalidateQueryCache();
}

void HierarchicalSpatialHash::queryRegion(const AABB& area, std::vector<size_t>& outBodyIndices) const {
    outBodyIndices.clear();
    std::unordered_set<size_t> seenBodies; // Avoid duplicates

    // Get all coarse regions this query overlaps
    std::vector<CoarseCoord> queryRegions = getCoarseCoordsForAABB(area);

    for (const auto& regionCoord : queryRegions) {
        auto regionIt = m_regions.find(regionCoord);
        if (regionIt == m_regions.end()) continue;

        const Region& region = regionIt->second;

        if (region.hasFineSplit) {
            // Query fine cells within this region
            // TODO: This could be optimized to only check fine cells that overlap the query area
            for (const auto& fineCellPair : region.fineCells) {
                for (size_t bodyIndex : fineCellPair.second) {
                    if (seenBodies.emplace(bodyIndex).second) {
                        outBodyIndices.push_back(bodyIndex);
                    }
                }
            }
        } else {
            // Query coarse cell directly
            for (size_t bodyIndex : region.bodyIndices) {
                if (seenBodies.emplace(bodyIndex).second) {
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
    assert(!m_inThreadedMode.load(std::memory_order_acquire) &&
           "Cannot insert batch during threaded queries");

    // Pre-reserve capacity
    m_bodyLocations.reserve(m_bodyLocations.size() + bodies.size());

    for (const auto& body : bodies) {
        insert(body.first, body.second);
    }
}

void HierarchicalSpatialHash::updateBatch(const std::vector<std::tuple<size_t, AABB, AABB>>& updates) {
    assert(!m_inThreadedMode.load(std::memory_order_acquire) &&
           "Cannot update batch during threaded queries");

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

HierarchicalSpatialHash::MortonCode HierarchicalSpatialHash::computeMortonCode(const FineCoord& coord) const {
    // Ensure coordinates are non-negative for Morton encoding
    uint32_t x = static_cast<uint32_t>(coord.x + 32768); // Offset to handle negative coords
    uint32_t y = static_cast<uint32_t>(coord.y + 32768);
    return MortonUtils::encode(x, y);
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
        MortonCode morton = computeMortonCode(fineCoord);
        region.fineCells[morton].push_back(bodyIndex);
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
        MortonCode morton = computeMortonCode(fineCoord);

        auto fineCellIt = region.fineCells.find(morton);
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
            MortonCode morton = computeMortonCode(fineCoord);
            region.fineCells[morton].push_back(bodyIndex);
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
    m_queryCache.clear();
    m_cacheVersion.fetch_add(1, std::memory_order_relaxed);
}

bool HierarchicalSpatialHash::getCachedQuery(size_t bodyIndex, std::vector<size_t>& outCandidates) const {
    auto cacheIt = m_queryCache.find(bodyIndex);
    if (cacheIt != m_queryCache.end()) {
        outCandidates = cacheIt->second;
        return true;
    }
    return false;
}

void HierarchicalSpatialHash::cacheQuery(size_t bodyIndex, const std::vector<size_t>& candidates) const {
    // Only cache if not in threaded mode (to avoid race conditions)
    if (!m_inThreadedMode.load(std::memory_order_acquire)) {
        m_queryCache[bodyIndex] = candidates;
    }
}

// ========== Morton Code Utilities ==========

namespace MortonUtils {

uint64_t encode(uint32_t x, uint32_t y) {
    // Interleave bits of x and y to create Morton code
    auto expandBits = [](uint32_t v) -> uint64_t {
        uint64_t x = v;
        x = (x | (x << 16)) & 0x0000FFFF0000FFFF;
        x = (x | (x << 8))  & 0x00FF00FF00FF00FF;
        x = (x | (x << 4))  & 0x0F0F0F0F0F0F0F0F;
        x = (x | (x << 2))  & 0x3333333333333333;
        x = (x | (x << 1))  & 0x5555555555555555;
        return x;
    };

    return expandBits(x) | (expandBits(y) << 1);
}

void decode(uint64_t morton, uint32_t& x, uint32_t& y) {
    auto compactBits = [](uint64_t x) -> uint32_t {
        x = x & 0x5555555555555555;
        x = (x | (x >> 1))  & 0x3333333333333333;
        x = (x | (x >> 2))  & 0x0F0F0F0F0F0F0F0F;
        x = (x | (x >> 4))  & 0x00FF00FF00FF00FF;
        x = (x | (x >> 8))  & 0x0000FFFF0000FFFF;
        x = (x | (x >> 16)) & 0x00000000FFFFFFFF;
        return static_cast<uint32_t>(x);
    };

    x = compactBits(morton);
    y = compactBits(morton >> 1);
}

uint64_t distance(uint64_t a, uint64_t b) {
    // XOR gives bit difference, popcount gives distance measure
    return __builtin_popcountll(a ^ b);
}

void sortByMortonCode(std::vector<size_t>& bodyIndices,
                     const std::function<AABB(size_t)>& getAABB) {
    std::sort(bodyIndices.begin(), bodyIndices.end(),
              [&getAABB](size_t a, size_t b) {
                  AABB aabbA = getAABB(a);
                  AABB aabbB = getAABB(b);

                  uint32_t xA = static_cast<uint32_t>(aabbA.center.getX() + 32768);
                  uint32_t yA = static_cast<uint32_t>(aabbA.center.getY() + 32768);
                  uint32_t xB = static_cast<uint32_t>(aabbB.center.getX() + 32768);
                  uint32_t yB = static_cast<uint32_t>(aabbB.center.getY() + 32768);

                  return encode(xA, yA) < encode(xB, yB);
              });
}

} // namespace MortonUtils

} // namespace HammerEngine