/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef PATHFINDING_GRID_HPP
#define PATHFINDING_GRID_HPP

#include <vector>
#include <utility>
#include <cstdint>
#include "utils/Vector2D.hpp"

namespace HammerEngine {

enum class PathfindingResult { SUCCESS, NO_PATH_FOUND, INVALID_START, INVALID_GOAL, TIMEOUT };

class PathfindingGrid {
public:
    PathfindingGrid(int width, int height, float cellSize, const Vector2D& worldOffset);

    void rebuildFromWorld();                 // pull from WorldManager::grid
    PathfindingResult findPath(const Vector2D& start, const Vector2D& goal,
                               std::vector<Vector2D>& outPath);

    void setAllowDiagonal(bool allow) { m_allowDiagonal = allow; }
    void setMaxIterations(int maxIters) { m_maxIterations = maxIters; }
    void setCosts(float straight, float diagonal) { m_costStraight = straight; m_costDiagonal = diagonal; }

    // Dynamic weighting for avoidance fields
    void resetWeights(float defaultWeight = 1.0f);
    void addWeightCircle(const Vector2D& worldCenter, float worldRadius, float weightMultiplier);

private:
    int m_w, m_h; float m_cell; Vector2D m_offset;
    std::vector<uint8_t> m_blocked; // 0 walkable, 1 blocked
    std::vector<float> m_weight;    // movement multipliers per cell

    bool m_allowDiagonal{true};
    int m_maxIterations{8000};  // Reduced from 20000 to prevent long stalls
    float m_costStraight{1.0f};
    float m_costDiagonal{1.41421356f};

    bool isBlocked(int gx, int gy) const;
    bool inBounds(int gx, int gy) const;
    std::pair<int,int> worldToGrid(const Vector2D& w) const;
    Vector2D gridToWorld(int gx, int gy) const;

    // Helper: find nearest unblocked cell within maxRadius (grid units)
    bool findNearestOpen(int gx, int gy, int maxRadius, int& outGX, int& outGY) const;
};

} // namespace HammerEngine

#endif // PATHFINDING_GRID_HPP
