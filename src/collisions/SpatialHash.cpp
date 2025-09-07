/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "collisions/SpatialHash.hpp"
#include <unordered_set>
#include <algorithm> // std::remove
#include <cmath>     // std::floor

namespace HammerEngine {

SpatialHash::SpatialHash(float cellSize, float movementThreshold) 
    : m_cellSize(cellSize), m_movementThreshold(movementThreshold) {}

void SpatialHash::insert(EntityID id, const AABB& aabb) {
    m_aabbs[id] = aabb;
    forEachOverlappingCell(aabb, [&](CellCoord c){ 
        auto& cell = m_cells[c];
        // Reserve space in cell vector to reduce reallocations
        if (cell.capacity() == 0) {
            cell.reserve(8); // Typical cell has 4-8 entities
        }
        cell.push_back(id); 
    });
}

void SpatialHash::remove(EntityID id) {
    auto it = m_aabbs.find(id);
    if (it == m_aabbs.end()) return;
    const AABB aabb = it->second;
    forEachOverlappingCell(aabb, [&](CellCoord c){
        auto cit = m_cells.find(c);
        if (cit == m_cells.end()) return;
        auto& v = cit->second;
        v.erase(std::remove(v.begin(), v.end(), id), v.end());
        if (v.empty()) m_cells.erase(cit);
    });
    m_aabbs.erase(it);
}

void SpatialHash::update(EntityID id, const AABB& newAABB) {
    auto it = m_aabbs.find(id);
    if (it == m_aabbs.end()) {
        insert(id, newAABB);
        return;
    }
    
    const AABB& oldAABB = it->second;
    
    // Check if the entity moved to different cells
    const int oldMinX = static_cast<int>(std::floor(oldAABB.left() / m_cellSize));
    const int oldMaxX = static_cast<int>(std::floor(oldAABB.right() / m_cellSize));
    const int oldMinY = static_cast<int>(std::floor(oldAABB.top() / m_cellSize));
    const int oldMaxY = static_cast<int>(std::floor(oldAABB.bottom() / m_cellSize));
    
    const int newMinX = static_cast<int>(std::floor(newAABB.left() / m_cellSize));
    const int newMaxX = static_cast<int>(std::floor(newAABB.right() / m_cellSize));
    const int newMinY = static_cast<int>(std::floor(newAABB.top() / m_cellSize));
    const int newMaxY = static_cast<int>(std::floor(newAABB.bottom() / m_cellSize));
    
    // OPTIMIZATION #3: Movement threshold - check if entity moved significantly before expensive cell updates
    if (!hasMovedSignificantly(oldAABB, newAABB)) {
        it->second = newAABB;  // Update stored AABB but don't rehash spatial cells
        return;
    }
    
    // Early exit if entity didn't change cells
    if (oldMinX == newMinX && oldMaxX == newMaxX && oldMinY == newMinY && oldMaxY == newMaxY) {
        it->second = newAABB;  // Just update AABB
        return;
    }
    
    // Remove from old cells that are no longer overlapped
    for (int y = oldMinY; y <= oldMaxY; ++y) {
        for (int x = oldMinX; x <= oldMaxX; ++x) {
            if (x >= newMinX && x <= newMaxX && y >= newMinY && y <= newMaxY) {
                continue; // Still overlapping, don't remove
            }
            CellCoord c{x, y};
            auto cit = m_cells.find(c);
            if (cit != m_cells.end()) {
                auto& v = cit->second;
                v.erase(std::remove(v.begin(), v.end(), id), v.end());
                if (v.empty()) {
                    m_cells.erase(cit);
                }
            }
        }
    }
    
    // Add to new cells that weren't previously overlapped
    for (int y = newMinY; y <= newMaxY; ++y) {
        for (int x = newMinX; x <= newMaxX; ++x) {
            if (x >= oldMinX && x <= oldMaxX && y >= oldMinY && y <= oldMaxY) {
                continue; // Already in this cell
            }
            CellCoord c{x, y};
            m_cells[c].push_back(id);
        }
    }
    
    it->second = newAABB;
}

void SpatialHash::query(const AABB& area, std::vector<EntityID>& out) const {
    out.clear();
    
    // OPTIMIZATION #4: Pre-reserve space based on estimated cell count
    // Each cell typically has 2-8 entities, area covers multiple cells
    float areaWidth = area.right() - area.left();
    float areaHeight = area.bottom() - area.top();
    int cellsX = std::max(1, static_cast<int>(std::ceil(areaWidth / m_cellSize)));
    int cellsY = std::max(1, static_cast<int>(std::ceil(areaHeight / m_cellSize)));
    size_t estimatedEntities = cellsX * cellsY * 4; // Assume avg 4 entities per cell
    out.reserve(estimatedEntities);
    
    // Use a smaller temporary set for deduplication when we expect many overlaps
    thread_local std::unordered_set<EntityID> seenIds;
    seenIds.clear();
    
    forEachOverlappingCell(area, [&](CellCoord c){
        auto it = m_cells.find(c);
        if (it == m_cells.end()) return;
        
        const auto& cellEntities = it->second;
        for (EntityID id : cellEntities) {
            if (seenIds.insert(id).second) {
                out.push_back(id);
            }
        }
    });
}

void SpatialHash::clear() {
    m_cells.clear();
    m_aabbs.clear();
}

void SpatialHash::forEachOverlappingCell(const AABB& aabb, const std::function<void(CellCoord)>& fn) const {
    const int minX = static_cast<int>(std::floor(aabb.left() / m_cellSize));
    const int maxX = static_cast<int>(std::floor(aabb.right() / m_cellSize));
    const int minY = static_cast<int>(std::floor(aabb.top() / m_cellSize));
    const int maxY = static_cast<int>(std::floor(aabb.bottom() / m_cellSize));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            fn(CellCoord{x, y});
        }
    }
}

bool SpatialHash::hasMovedSignificantly(const AABB& oldAABB, const AABB& newAABB) const {
    // Check if center moved more than threshold
    Vector2D oldCenter = oldAABB.center;
    Vector2D newCenter = newAABB.center;
    float distanceSquared = (newCenter - oldCenter).lengthSquared();
    return distanceSquared > (m_movementThreshold * m_movementThreshold);
}

} // namespace HammerEngine
