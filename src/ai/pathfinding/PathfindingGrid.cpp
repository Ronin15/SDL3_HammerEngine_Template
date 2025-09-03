/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/pathfinding/PathfindingGrid.hpp"
#include "managers/WorldManager.hpp"
#include "world/WorldData.hpp"
#include <queue>
#include <limits>
#include <cmath>
#include <unordered_map>
#include <stdexcept>
#include "core/Logger.hpp"

namespace HammerEngine {

// NodePool will be thread_local within findPath function

PathfindingGrid::PathfindingGrid(int width, int height, float cellSize, const Vector2D& worldOffset)
    : m_w(width), m_h(height), m_cell(cellSize), m_offset(worldOffset) {
    
    // Validate grid dimensions to prevent 0x0 grids
    if (m_w <= 0 || m_h <= 0) {
        throw std::invalid_argument("PathfindingGrid dimensions must be positive: " + 
                                    std::to_string(width) + "x" + std::to_string(height));
    }
    
    if (cellSize <= 0.0f) {
        throw std::invalid_argument("PathfindingGrid cell size must be positive: " + 
                                    std::to_string(cellSize));
    }
    
    m_blocked.assign(static_cast<size_t>(m_w * m_h), 0);
    m_weight.assign(static_cast<size_t>(m_w * m_h), 1.0f);
}

bool PathfindingGrid::inBounds(int gx, int gy) const {
    return gx >= 0 && gy >= 0 && gx < m_w && gy < m_h;
}

bool PathfindingGrid::isBlocked(int gx, int gy) const {
    if (!inBounds(gx, gy)) return true;
    return m_blocked[static_cast<size_t>(gy * m_w + gx)] != 0;
}

std::pair<int,int> PathfindingGrid::worldToGrid(const Vector2D& w) const {
    int gx = static_cast<int>(std::floor((w.getX() - m_offset.getX()) / m_cell));
    int gy = static_cast<int>(std::floor((w.getY() - m_offset.getY()) / m_cell));
    return {gx, gy};
}

Vector2D PathfindingGrid::gridToWorld(int gx, int gy) const {
    float wx = m_offset.getX() + gx * m_cell + m_cell * 0.5f;
    float wy = m_offset.getY() + gy * m_cell + m_cell * 0.5f;
    return Vector2D(wx, wy);
}

bool PathfindingGrid::findNearestOpen(int gx, int gy, int maxRadius, int& outGX, int& outGY) const {
    if (inBounds(gx, gy) && !isBlocked(gx, gy)) { outGX = gx; outGY = gy; return true; }
    for (int r = 1; r <= maxRadius; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            int y = gy + dy;
            int x1 = gx - r;
            int x2 = gx + r;
            if (inBounds(x1, y) && !isBlocked(x1, y)) { outGX = x1; outGY = y; return true; }
            if (inBounds(x2, y) && !isBlocked(x2, y)) { outGX = x2; outGY = y; return true; }
        }
        for (int dx = -r + 1; dx <= r - 1; ++dx) {
            int x = gx + dx;
            int y1 = gy - r;
            int y2 = gy + r;
            if (inBounds(x, y1) && !isBlocked(x, y1)) { outGX = x; outGY = y1; return true; }
            if (inBounds(x, y2) && !isBlocked(x, y2)) { outGX = x; outGY = y2; return true; }
        }
    }
    return false;
}

void PathfindingGrid::rebuildFromWorld() {
    const WorldManager& wm = WorldManager::Instance();
    const auto* world = wm.getWorldData();
    if (!world) { PATHFIND_WARN("rebuildFromWorld(): no active world"); return; }
    m_h = static_cast<int>(world->grid.size());
    m_w = m_h > 0 ? static_cast<int>(world->grid[0].size()) : 0;
    m_blocked.assign(static_cast<size_t>(m_w * m_h), 0);
    m_weight.assign(static_cast<size_t>(m_w * m_h), 1.0f);
    int blockedCount = 0;
    for (int y = 0; y < m_h; ++y) {
        for (int x = 0; x < m_w; ++x) {
            const auto& tile = world->grid[y][x];
            bool blocked = tile.obstacleType != ObstacleType::NONE; // Allow movement through water
            m_blocked[static_cast<size_t>(y * m_w + x)] = blocked ? 1 : 0;
            if (blocked) ++blockedCount;
            
            // Set movement weights - water is slower but not impassable
            float weight = 1.0f;
            if (tile.isWater) {
                weight = 2.0f; // Water takes 2x longer to traverse
            }
            m_weight[static_cast<size_t>(y * m_w + x)] = weight;
        }
    }
    PATHFIND_INFO("Grid rebuilt: " + std::to_string(m_w) + "x" + std::to_string(m_h) +
                  ", blocked=" + std::to_string(blockedCount) + "/" + std::to_string(m_w * m_h) +
                  " (" + std::to_string((100.0f * blockedCount) / (m_w * m_h)) + "% blocked)");
}

void PathfindingGrid::smoothPath(std::vector<Vector2D>& path) {
    if (path.size() <= 2) return; // Can't smooth paths with 2 or fewer nodes
    
    std::vector<Vector2D> smoothed;
    smoothed.reserve(path.size());
    smoothed.push_back(path[0]); // Always keep start
    
    size_t i = 0;
    while (i < path.size() - 1) {
        // Look ahead for line-of-sight optimization
        size_t farthest = i + 1;
        for (size_t j = i + 2; j < path.size(); j++) {
            if (hasLineOfSight(path[i], path[j])) {
                farthest = j;
            } else {
                break; // Blocked, stop looking ahead
            }
        }
        
        // Add the farthest reachable point
        if (farthest != i + 1) {
            smoothed.push_back(path[farthest]);
            i = farthest;
        } else {
            smoothed.push_back(path[i + 1]);
            i++;
        }
    }
    
    // Always keep goal - check if coordinates are different
    if (smoothed.empty() || 
        smoothed.back().getX() != path.back().getX() || 
        smoothed.back().getY() != path.back().getY()) {
        smoothed.push_back(path.back());
    }
    
    path = std::move(smoothed);
}

bool PathfindingGrid::hasLineOfSight(const Vector2D& start, const Vector2D& end) {
    auto [sx, sy] = worldToGrid(start);
    auto [ex, ey] = worldToGrid(end);
    
    // Simple Bresenham-like line check
    int dx = abs(ex - sx), dy = abs(ey - sy);
    int x = sx, y = sy;
    int xStep = (ex > sx) ? 1 : -1;
    int yStep = (ey > sy) ? 1 : -1;
    
    if (dx > dy) {
        int err = dx / 2;
        while (x != ex) {
            if (!inBounds(x, y) || isBlocked(x, y)) return false;
            err -= dy;
            if (err < 0) { y += yStep; err += dx; }
            x += xStep;
        }
    } else {
        int err = dy / 2;
        while (y != ey) {
            if (!inBounds(x, y) || isBlocked(x, y)) return false;
            err -= dx;
            if (err < 0) { x += xStep; err += dy; }
            y += yStep;
        }
    }
    
    return !isBlocked(ex, ey); // Check final position
}

PathfindingResult PathfindingGrid::findPath(const Vector2D& start, const Vector2D& goal,
                                            std::vector<Vector2D>& outPath) {
    outPath.clear();
    auto [sx, sy] = worldToGrid(start);
    auto [gx, gy] = worldToGrid(goal);
    
    // Clamp grid coordinates to ensure they're away from exact boundaries
    // This prevents problematic boundary pathfinding even if world-space clamping fails
    const int GRID_MARGIN = 8; // Much larger margin to avoid near-boundary pathfinding issues
    gx = std::clamp(gx, GRID_MARGIN, m_w - 1 - GRID_MARGIN);
    gy = std::clamp(gy, GRID_MARGIN, m_h - 1 - GRID_MARGIN);
    sx = std::clamp(sx, GRID_MARGIN, m_w - 1 - GRID_MARGIN);
    sy = std::clamp(sy, GRID_MARGIN, m_h - 1 - GRID_MARGIN);
    
    if (!inBounds(sx, sy)) { 
        m_stats.totalRequests++;
        m_stats.invalidStarts++; 
        // Invalid start warnings removed - covered in PathfinderManager status reporting
        return PathfindingResult::INVALID_START; 
    }
    if (!inBounds(gx, gy)) { 
        m_stats.totalRequests++;
        m_stats.invalidGoals++; 
        // Invalid goal warnings removed - covered in PathfinderManager status reporting
        return PathfindingResult::INVALID_GOAL; 
    }
    
    // Enhanced goal validation - reject problematic goals early
    if (isBlocked(gx, gy)) {
        // Try to find a nearby unblocked cell instead of rejecting outright
        int nearGx = gx, nearGy = gy;
        if (findNearestOpen(gx, gy, 3, nearGx, nearGy)) {
            gx = nearGx; gy = nearGy; // Use the nearby open cell
        } else {
            m_stats.totalRequests++;
            m_stats.invalidGoals++;
            return PathfindingResult::INVALID_GOAL;
        }
    }
    
    // Quick reachability test - if goal is in a completely isolated area, reject early
    int dxGoal = std::abs(sx - gx), dyGoal = std::abs(sy - gy);
    int directDistance = std::max(dxGoal, dyGoal);
    
    // For very long distances, do a quick connectivity check (reduced threshold to be less aggressive)
    if (directDistance > 120) {
        // Sample a few points along the direct line to check for major barriers
        int samples = std::min(8, directDistance / 10);
        int blockedSamples = 0;
        
        for (int i = 1; i < samples; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(samples);
            int mx = sx + static_cast<int>((gx - sx) * t);
            int my = sy + static_cast<int>((gy - sy) * t);
            
            // Check if this sample point has any open neighbors (basic connectivity)
            bool hasOpenNeighbor = false;
            for (int dx = -1; dx <= 1 && !hasOpenNeighbor; dx++) {
                for (int dy = -1; dy <= 1 && !hasOpenNeighbor; dy++) {
                    int nx = mx + dx, ny = my + dy;
                    if (inBounds(nx, ny) && !isBlocked(nx, ny)) {
                        hasOpenNeighbor = true;
                    }
                }
            }
            
            if (!hasOpenNeighbor) blockedSamples++;
        }
        
        // If more than 75% of samples are in completely blocked areas, likely unreachable (more lenient threshold)
        if (blockedSamples > (samples * 3) / 4) {
            m_stats.totalRequests++;
            m_stats.invalidGoals++;
            PATHFIND_DEBUG("Goal rejected: appears unreachable based on connectivity test");
            return PathfindingResult::NO_PATH_FOUND;
        }
    }
    // Nudge start/goal if blocked (common after collision resolution)
    int nsx = sx, nsy = sy, ngx = gx, ngy = gy;
    bool startOk = !isBlocked(sx, sy) || findNearestOpen(sx, sy, 4, nsx, nsy);
    bool goalOk  = !isBlocked(gx, gy) || findNearestOpen(gx, gy, 6, ngx, ngy);
    if (!startOk || !goalOk) { PATHFIND_DEBUG("findPath(): start or goal blocked"); return PathfindingResult::NO_PATH_FOUND; }
    sx = nsx; sy = nsy; gx = ngx; gy = ngy;

    // Early success: if start equals goal after nudging
    if (sx == gx && sy == gy) {
        outPath.push_back(gridToWorld(gx, gy));
        m_stats.totalRequests++;
        m_stats.successfulPaths++;
        m_stats.totalIterations += 1; // Minimal iteration count
        return PathfindingResult::SUCCESS;
    }

    // Line-of-sight shortcut: Check direct path for simple terrain
    Vector2D worldStart = gridToWorld(sx, sy);
    Vector2D worldGoal = gridToWorld(gx, gy);
    if (hasLineOfSight(worldStart, worldGoal)) {
        // Direct path available - create simple 2-point path
        outPath.push_back(worldStart);
        outPath.push_back(worldGoal);
        m_stats.totalRequests++;
        m_stats.successfulPaths++;
        m_stats.totalIterations += 2; // Minimal iteration count for line-of-sight
        return PathfindingResult::SUCCESS;
    }

    const int W = m_w, H = m_h;
    const size_t N = static_cast<size_t>(W * H);

    std::vector<uint8_t> closed(N, 0);
    auto idx = [&](int x, int y){ return y * W + x; };
    auto h = [&](int x, int y){
        // Octile distance - keep heuristic admissible for optimal A*
        int dx = std::abs(x - gx); int dy = std::abs(y - gy);
        int dmin = std::min(dx, dy); int dmax = std::max(dx, dy);
        float baseDistance = m_costDiagonal * dmin + m_costStraight * (dmax - dmin);
        // Keep heuristic admissible (never overestimate) for efficient pathfinding
        return baseDistance;
    };

    // Keep baseDistance for iteration calculations  
    int baseDistance = directDistance;
    
    // Allow full grid access for reliable pathfinding

    // A* pathfinding using thread-local object pooling for memory optimization
    thread_local NodePool nodePool;
    int gridSize = m_w * m_h;
    nodePool.ensureCapacity(gridSize);
    nodePool.reset();
    
    // Use pooled containers
    auto& open = nodePool.openQueue;
    auto& gScore = nodePool.gScoreBuffer;
    auto& fScore = nodePool.fScoreBuffer;
    auto& parent = nodePool.parentBuffer;

    size_t sIdx = static_cast<size_t>(idx(sx, sy));
    if (sIdx >= gScore.size()) {
        PATHFIND_ERROR("Buffer size mismatch: index " + std::to_string(sIdx) + 
                       " >= buffer size " + std::to_string(gScore.size()));
        return PathfindingResult::INVALID_START;
    }
    gScore[sIdx] = 0.0f;
    fScore[sIdx] = static_cast<float>(h(sx, sy));
    open.push(NodePool::Node{sx, sy, fScore[sIdx]});

    int iterations = 0;
    const int dirs = m_allowDiagonal ? 8 : 4;
    const int dx8[8] = {1,-1,0,0, 1,1,-1,-1};
    const int dy8[8] = {0,0,1,-1, 1,-1,1,-1};

    // Performance-focused: Dynamic iteration limits based on distance (optimized for better success rate)
    int baseIters = std::max(500, baseDistance * 20); // Reduced base to start faster
    int dynamicMaxIters = std::min(m_maxIterations, baseIters + 1500); // Tighter buffer for quick resolution
    
    // Performance-focused: Reasonable queue limits to balance memory and success rate
    
    // Memory protection for very complex pathfinding scenarios
    const size_t maxAbsoluteQueueSize = 5000;

    while (!open.empty() && iterations++ < dynamicMaxIters) {
        // Only terminate on truly excessive memory usage, not normal pathfinding complexity
        if (open.size() > maxAbsoluteQueueSize) {
            PATHFIND_DEBUG("Emergency termination: queue size " + std::to_string(open.size()) + 
                          " exceeded emergency limit (max: " + std::to_string(maxAbsoluteQueueSize) + ")");
            break;
        }
        
        NodePool::Node cur = open.top(); open.pop();
        
        // Verbose iteration debugging removed - spammed console output
        int cIndex = idx(cur.x, cur.y);
        if (closed[cIndex]) continue;
        closed[cIndex] = 1;
        if (cur.x == gx && cur.y == gy) {
            // reconstruct
            std::vector<Vector2D> rev;
            int cx = cur.x, cy = cur.y;
            while (!(cx == sx && cy == sy)) {
                rev.push_back(gridToWorld(cx, cy));
                int p = parent[static_cast<size_t>(idx(cx, cy))];
                if (p < 0) break; // shouldn't happen
                cy = p / W; cx = p % W;
            }
            rev.push_back(gridToWorld(sx, sy));
            outPath.assign(rev.rbegin(), rev.rend());
            
            
            // Apply path smoothing to reduce unnecessary waypoints
            smoothPath(outPath);
            
            // Update statistics instead of individual logging
            m_stats.totalRequests++;
            m_stats.successfulPaths++;
            m_stats.totalIterations += iterations;
            uint64_t totalPathLength = m_stats.avgPathLength * (m_stats.successfulPaths - 1) + outPath.size();
            m_stats.avgPathLength = static_cast<uint32_t>(totalPathLength / m_stats.successfulPaths);
            
            return PathfindingResult::SUCCESS;
        }
        for (int i = 0; i < dirs; ++i) {
            int nx = cur.x + dx8[i];
            int ny = cur.y + dy8[i];
            
            // EMERGENCY FIX: Only check bounds, no ROI restrictions
            bool outsideBounds = (nx < 0 || nx >= W || ny < 0 || ny >= H);
            
            if (outsideBounds) continue;  // Only reject actual out-of-bounds cells
                
            size_t nIndex = static_cast<size_t>(idx(nx, ny));
            if (closed[nIndex] || isBlocked(nx, ny)) continue;
            
            // No-corner-cutting: if moving diagonally, both orthogonal neighbors must be open
            if (m_allowDiagonal && i >= 4) {
                int ox = cur.x + dx8[i]; int oy = cur.y;
                int px = cur.x; int py = cur.y + dy8[i];
                if (isBlocked(ox, oy) || isBlocked(px, py)) continue;
            }
            
            float step = (i < 4) ? m_costStraight : m_costDiagonal;
            float weight = (nIndex < m_weight.size() && m_weight[nIndex] > 0.0f) ? m_weight[nIndex] : 1.0f;
            float tentative = gScore[static_cast<size_t>(idx(cur.x, cur.y))] + step * weight;
            
            // Only add to queue if we found a better path
            if (tentative < gScore[nIndex]) {
                parent[nIndex] = idx(cur.x, cur.y);
                gScore[nIndex] = tentative;
                fScore[nIndex] = tentative + h(nx, ny);
                open.push(NodePool::Node{nx, ny, fScore[nIndex]});
            }
        }
    }
    
    // Update statistics for timeout case
    m_stats.totalRequests++;
    m_stats.timeouts++;
    m_stats.totalIterations += iterations;
    
    // Individual timeout warnings removed - comprehensive status reporting now handled by PathfinderManager
    // Statistics are still tracked in m_stats for consolidated reporting
    
    return PathfindingResult::TIMEOUT;
}

void PathfindingGrid::resetWeights(float defaultWeight) {
    m_weight.assign(static_cast<size_t>(m_w * m_h), defaultWeight);
}

void PathfindingGrid::addWeightCircle(const Vector2D& worldCenter, float worldRadius, float weightMultiplier) {
    if (weightMultiplier <= 1.0f) return; // only increase cost
    auto [cx, cy] = worldToGrid(worldCenter);
    int rad = static_cast<int>(std::ceil(worldRadius / m_cell));
    int x0 = std::max(0, cx - rad);
    int y0 = std::max(0, cy - rad);
    int x1 = std::min(m_w - 1, cx + rad);
    int y1 = std::min(m_h - 1, cy + rad);
    float radCells = worldRadius / m_cell;
    float r2 = radCells * radCells;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float dx = static_cast<float>(x - cx);
            float dy = static_cast<float>(y - cy);
            if (dx*dx + dy*dy <= r2) {
                size_t i = static_cast<size_t>(y * m_w + x);
                if (i < m_weight.size()) m_weight[i] = std::max(m_weight[i], weightMultiplier);
            }
        }
    }
}

} // namespace HammerEngine
