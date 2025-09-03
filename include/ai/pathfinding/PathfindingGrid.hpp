/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef PATHFINDING_GRID_HPP
#define PATHFINDING_GRID_HPP

#include <vector>
#include <utility>
#include <cstdint>
#include <ostream>
#include <queue>
#include <limits>
#include <algorithm>
#include "utils/Vector2D.hpp"

namespace HammerEngine {

enum class PathfindingResult { SUCCESS, NO_PATH_FOUND, INVALID_START, INVALID_GOAL, TIMEOUT };

// Stream operator for PathfindingResult to support test output
inline std::ostream& operator<<(std::ostream& os, const PathfindingResult& result) {
    switch (result) {
        case PathfindingResult::SUCCESS: return os << "SUCCESS";
        case PathfindingResult::NO_PATH_FOUND: return os << "NO_PATH_FOUND";
        case PathfindingResult::INVALID_START: return os << "INVALID_START";
        case PathfindingResult::INVALID_GOAL: return os << "INVALID_GOAL";
        case PathfindingResult::TIMEOUT: return os << "TIMEOUT";
        default: return os << "UNKNOWN";
    }
}

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

    // Statistics
    struct PathfindingStats {
        uint64_t totalRequests{0};
        uint64_t successfulPaths{0}; 
        uint64_t timeouts{0};
        uint64_t invalidStarts{0};
        uint64_t invalidGoals{0};
        uint64_t totalIterations{0};
        uint32_t avgPathLength{0};
        uint32_t framesSinceReset{0};
    };
    
    void resetStats() { m_stats = PathfindingStats{}; }
    PathfindingStats getStats() const { return m_stats; }

private:
    int m_w, m_h; float m_cell; Vector2D m_offset;
    std::vector<uint8_t> m_blocked; // 0 walkable, 1 blocked
    std::vector<float> m_weight;    // movement multipliers per cell

    bool m_allowDiagonal{true};
    int m_maxIterations{12000}; // Performance-tuned: increased from 8K to 12K for better success rate
    float m_costStraight{1.0f};
    float m_costDiagonal{1.41421356f};

    PathfindingStats m_stats{};

    bool isBlocked(int gx, int gy) const;
    bool inBounds(int gx, int gy) const;
    std::pair<int,int> worldToGrid(const Vector2D& w) const;
    Vector2D gridToWorld(int gx, int gy) const;

    // Helper: find nearest unblocked cell within maxRadius (grid units)
    bool findNearestOpen(int gx, int gy, int maxRadius, int& outGX, int& outGY) const;
    
    // Path smoothing functions
    void smoothPath(std::vector<Vector2D>& path);
    bool hasLineOfSight(const Vector2D& start, const Vector2D& end);

private:
    // Object pools for memory optimization
    struct NodePool {
        struct Node { int x; int y; float f; };
        struct Cmp { bool operator()(const Node& a, const Node& b) const { return a.f > b.f; } };
        
        // Pre-allocated containers to avoid repeated allocation/deallocation
        std::priority_queue<Node, std::vector<Node>, Cmp> openQueue;
        std::vector<float> gScoreBuffer;
        std::vector<float> fScoreBuffer;
        std::vector<int> parentBuffer;
        std::vector<Vector2D> pathBuffer;
        
        void ensureCapacity(int gridSize) {
            if (gScoreBuffer.size() < static_cast<size_t>(gridSize)) {
                gScoreBuffer.resize(gridSize);
                fScoreBuffer.resize(gridSize);
                parentBuffer.resize(gridSize);
                pathBuffer.reserve(std::max(128, gridSize / 10)); // Reasonable path length estimate
            }
        }
        
        void reset() {
            // Clear but don't deallocate
            while (!openQueue.empty()) openQueue.pop();
            // Only reset if buffers are properly sized
            if (!gScoreBuffer.empty()) {
                std::fill(gScoreBuffer.begin(), gScoreBuffer.end(), std::numeric_limits<float>::infinity());
                std::fill(fScoreBuffer.begin(), fScoreBuffer.end(), std::numeric_limits<float>::infinity());
                std::fill(parentBuffer.begin(), parentBuffer.end(), -1);
            }
            pathBuffer.clear();
        }
    };
    
    // NodePool will be thread_local within findPath function
};

} // namespace HammerEngine

#endif // PATHFINDING_GRID_HPP
