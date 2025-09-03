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
    if (!inBounds(sx, sy)) { 
        m_stats.totalRequests++;
        m_stats.invalidStarts++; 
        // Periodic warning with coordinate diagnostics
        static uint32_t invalidStartCount = 0;
        if (++invalidStartCount % 20 == 0) {
            PATHFIND_WARN("Invalid start positions: " + std::to_string(m_stats.invalidStarts) + 
                          " of " + std::to_string(m_stats.totalRequests) + " requests. " +
                          "Latest: world(" + std::to_string(start.getX()) + "," + std::to_string(start.getY()) + 
                          ") -> grid(" + std::to_string(sx) + "," + std::to_string(sy) + 
                          "), bounds=[0,0 to " + std::to_string(m_w-1) + "," + std::to_string(m_h-1) + "]");
        }
        return PathfindingResult::INVALID_START; 
    }
    if (!inBounds(gx, gy)) { 
        m_stats.totalRequests++;
        m_stats.invalidGoals++; 
        // Periodic warning with coordinate diagnostics
        static uint32_t invalidGoalCount = 0;
        if (++invalidGoalCount % 20 == 0) {
            PATHFIND_WARN("Invalid goal positions: " + std::to_string(m_stats.invalidGoals) + 
                          " of " + std::to_string(m_stats.totalRequests) + " requests. " +
                          "Latest: world(" + std::to_string(goal.getX()) + "," + std::to_string(goal.getY()) + 
                          ") -> grid(" + std::to_string(gx) + "," + std::to_string(gy) + 
                          "), bounds=[0,0 to " + std::to_string(m_w-1) + "," + std::to_string(m_h-1) + "]");
        }
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
    
    // For very long distances, do a quick connectivity check
    if (directDistance > 75) {
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
        
        // If more than 50% of samples are in completely blocked areas, likely unreachable
        if (blockedSamples > samples / 2) {
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

    // Restrict search to a tighter ROI to prevent excessive exploration
    // (reuse variables from earlier)
    int baseDistance = directDistance;
    
    // Much tighter ROI: only allow detours up to 1.5x the direct distance + small margin
    int r = std::max(15, static_cast<int>(baseDistance * 1.5f + 10));
    
    // Further limit ROI based on distance - prevent huge search areas
    if (baseDistance > 50) {
        r = std::min(r, baseDistance + 25);  // Cap ROI growth for long distances
    }
    
    // ROI that encompasses both start and goal with minimal buffer
    int roiMinX = std::max(0, std::min(sx, gx) - r);
    int roiMaxX = std::min(W - 1, std::max(sx, gx) + r);
    int roiMinY = std::max(0, std::min(sy, gy) - r);
    int roiMaxY = std::min(H - 1, std::max(sy, gy) + r);

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

    // Conservative iteration limits to prevent timeouts - fail fast on difficult paths
    int baseIters = std::max(500, baseDistance * 25);  // Much more conservative
    int dynamicMaxIters = std::min(m_maxIterations, baseIters + 1000); // Smaller buffer
    
    // Hard cap on iterations based on distance to prevent runaway pathfinding
    if (baseDistance < 10) {
        dynamicMaxIters = std::min(dynamicMaxIters, 1000);
    } else if (baseDistance < 30) {
        dynamicMaxIters = std::min(dynamicMaxIters, 2500);
    } else {
        dynamicMaxIters = std::min(dynamicMaxIters, 5000);
    }
    
    // Early termination if open queue gets too large (indicates poor pathfinding conditions)
    const size_t maxOpenQueueSize = std::max(static_cast<size_t>(500), static_cast<size_t>(baseDistance * 20));
    
    // Additional hard cap on queue size to prevent memory explosion
    const size_t maxAbsoluteQueueSize = 2000;

    while (!open.empty() && iterations++ < dynamicMaxIters) {
        // Early termination if queue becomes too large
        if (open.size() > maxOpenQueueSize || open.size() > maxAbsoluteQueueSize) {
            PATHFIND_DEBUG("Early termination: queue size " + std::to_string(open.size()) + 
                          " exceeded limit (max: " + std::to_string(maxOpenQueueSize) + ")");
            break;
        }
        NodePool::Node cur = open.top(); open.pop();
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
            
            // Combined bounds and ROI check for efficiency
            if (nx < roiMinX || nx > roiMaxX || ny < roiMinY || ny > roiMaxY || 
                nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
                
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
    
    // Enhanced diagnostic logging for timeout patterns
    static uint32_t timeoutCount = 0;
    static std::unordered_map<std::string, int> problematicGoals;
    
    if (++timeoutCount % 5 == 0) {  // Report more frequently to catch patterns
        std::string goalKey = "(" + std::to_string(gx) + "," + std::to_string(gy) + ")";
        problematicGoals[goalKey]++;
        
        float timeoutRate = (m_stats.totalRequests > 0) ? 
            (static_cast<float>(m_stats.timeouts) / static_cast<float>(m_stats.totalRequests) * 100.0f) : 0.0f;
            
        PATHFIND_WARN("Pathfinding timeouts: " + std::to_string(m_stats.timeouts) + 
                      " of " + std::to_string(m_stats.totalRequests) + " requests (" + 
                      std::to_string(static_cast<int>(timeoutRate)) + "%). " +
                      "Last timeout: distance=" + std::to_string(baseDistance) + 
                      " tiles, iters=" + std::to_string(iterations) + "/" + std::to_string(dynamicMaxIters) +
                      ", ROI=" + std::to_string((roiMaxX-roiMinX+1) * (roiMaxY-roiMinY+1)) + " cells" +
                      ", start=(" + std::to_string(sx) + "," + std::to_string(sy) + ")" +
                      ", goal=" + goalKey);
                      
        if (problematicGoals[goalKey] >= 3) {
            PATHFIND_WARN("REPEATED PROBLEMATIC GOAL: " + goalKey + " failed " + 
                          std::to_string(problematicGoals[goalKey]) + " times");
        }
        
        // Flag suspicious boundary goals
        if (gx <= 1 || gy <= 1 || gx >= m_w-2 || gy >= m_h-2) {
            PATHFIND_WARN("BOUNDARY GOAL DETECTED: " + goalKey + " - near world edge");
        }
    }
    
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
