/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef SPATIAL_HASH_HPP
#define SPATIAL_HASH_HPP

#include <unordered_map>
#include <vector>
#include <cstdint>
#include <functional>
#include "collisions/AABB.hpp"
#include "entities/Entity.hpp"

namespace HammerEngine {

class SpatialHash {
public:
    explicit SpatialHash(float cellSize = 32.0f);

    void insert(EntityID id, const AABB& aabb);
    void remove(EntityID id);
    void update(EntityID id, const AABB& aabb);
    void query(const AABB& area, std::vector<EntityID>& out) const;
    void clear();

private:
    struct CellCoord { int x; int y; };
    struct CellCoordHash {
        size_t operator()(const CellCoord& c) const noexcept {
            return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32) ^
                   static_cast<uint32_t>(c.y);
        }
    };
    struct CellCoordEq {
        bool operator()(const CellCoord& a, const CellCoord& b) const noexcept {
            return a.x == b.x && a.y == b.y;
        }
    };

    float m_cellSize{32.0f};
    std::unordered_map<EntityID, AABB> m_aabbs; // latest bounds per id
    std::unordered_map<CellCoord, std::vector<EntityID>, CellCoordHash, CellCoordEq> m_cells;

    void forEachOverlappingCell(const AABB& aabb, const std::function<void(CellCoord)>& fn) const;
};

} // namespace HammerEngine

#endif // SPATIAL_HASH_HPP
