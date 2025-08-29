/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "collisions/SpatialHash.hpp"

namespace HammerEngine {

SpatialHash::SpatialHash(float cellSize) : m_cellSize(cellSize) {}

void SpatialHash::insert(EntityID id, const AABB& aabb) {
    m_aabbs[id] = aabb;
    forEachOverlappingCell(aabb, [&](CellCoord c){ m_cells[c].push_back(id); });
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

void SpatialHash::update(EntityID id, const AABB& aabb) {
    remove(id);
    insert(id, aabb);
}

void SpatialHash::query(const AABB& area, std::vector<EntityID>& out) const {
    out.clear();
    forEachOverlappingCell(area, [&](CellCoord c){
        auto it = m_cells.find(c);
        if (it == m_cells.end()) return;
        out.insert(out.end(), it->second.begin(), it->second.end());
    });
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
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

} // namespace HammerEngine
