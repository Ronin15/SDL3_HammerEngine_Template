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
#include "core/Logger.hpp"

namespace HammerEngine {

PathfindingGrid::PathfindingGrid(int width, int height, float cellSize, const Vector2D& worldOffset)
    : m_w(width), m_h(height), m_cell(cellSize), m_offset(worldOffset) {
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
    
    // Reject goals too close to boundaries (likely problematic)
    if (gx <= 2 || gy <= 2 || gx >= m_w-3 || gy >= m_h-3) {
        m_stats.totalRequests++;
        m_stats.invalidGoals++;
        static uint32_t boundaryGoalCount = 0;
        if (++boundaryGoalCount % 10 == 0) {
            PATHFIND_WARN("Rejecting boundary goal: world(" + std::to_string(goal.getX()) + "," + std::to_string(goal.getY()) + 
                          ") -> grid(" + std::to_string(gx) + "," + std::to_string(gy) + ") - too close to edge");
        }
        return PathfindingResult::INVALID_GOAL;
    }
    // Nudge start/goal if blocked (common after collision resolution)
    int nsx = sx, nsy = sy, ngx = gx, ngy = gy;
    bool startOk = !isBlocked(sx, sy) || findNearestOpen(sx, sy, 4, nsx, nsy);
    bool goalOk  = !isBlocked(gx, gy) || findNearestOpen(gx, gy, 6, ngx, ngy);
    if (!startOk || !goalOk) { PATHFIND_DEBUG("findPath(): start or goal blocked"); return PathfindingResult::NO_PATH_FOUND; }
    sx = nsx; sy = nsy; gx = ngx; gy = ngy;

    const int W = m_w, H = m_h;
    const size_t N = static_cast<size_t>(W * H);
    const float INF = std::numeric_limits<float>::infinity();

    std::vector<float> gScore(N, INF), fScore(N, INF);
    std::vector<uint8_t> closed(N, 0);
    std::vector<int> parent(N, -1);
    auto idx = [&](int x, int y){ return y * W + x; };
    auto h = [&](int x, int y){
        // Octile distance with terrain complexity adjustment  
        int dx = std::abs(x - gx); int dy = std::abs(y - gy);
        int dmin = std::min(dx, dy); int dmax = std::max(dx, dy);
        float baseDistance = m_costDiagonal * dmin + m_costStraight * (dmax - dmin);
        // Scale heuristic slightly lower for heavily blocked terrain to encourage exploration
        return baseDistance * 0.95f;
    };

    // Restrict search to a reasonable ROI around start/goal to prevent
    // exploring the whole map on unreachable goals
    int dxGoal = std::abs(sx - gx), dyGoal = std::abs(sy - gy);
    int baseDistance = std::max(dxGoal, dyGoal);
    // More generous ROI: allow detours up to 1.5x the direct distance + base margin
    int r = std::max(20, static_cast<int>(baseDistance * 1.5f + 16));
    int roiMinX = std::max(0, sx - r);
    int roiMaxX = std::min(W - 1, sx + r);
    int roiMinY = std::max(0, sy - r);
    int roiMaxY = std::min(H - 1, sy + r);

    struct Node { int x; int y; float f; };
    struct Cmp { bool operator()(const Node& a, const Node& b) const { return a.f > b.f; } };
    std::priority_queue<Node, std::vector<Node>, Cmp> open;

    size_t sIdx = static_cast<size_t>(idx(sx, sy));
    gScore[sIdx] = 0.0f;
    fScore[sIdx] = static_cast<float>(h(sx, sy));
    open.push({sx, sy, fScore[sIdx]});

    int iterations = 0;
    const int dirs = m_allowDiagonal ? 8 : 4;
    const int dx8[8] = {1,-1,0,0, 1,1,-1,-1};
    const int dy8[8] = {0,0,1,-1, 1,-1,1,-1};

    // More generous dynamic iteration limits with adaptive scaling  
    int baseIters = std::max(2000, baseDistance * 150); // Higher base for complex terrain
    int dynamicMaxIters = std::min(m_maxIterations, baseIters + 1000); // Large 1000 iteration buffer

    while (!open.empty() && iterations++ < dynamicMaxIters) {
        Node cur = open.top(); open.pop();
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
            if (!inBounds(nx, ny) || isBlocked(nx, ny)) continue;
            if (nx < roiMinX || nx > roiMaxX || ny < roiMinY || ny > roiMaxY) continue;
            // No-corner-cutting: if moving diagonally, both orthogonal neighbors must be open
            if (m_allowDiagonal && i >= 4) {
                int ox = cur.x + dx8[i]; int oy = cur.y;
                int px = cur.x; int py = cur.y + dy8[i];
                if (isBlocked(ox, oy) || isBlocked(px, py)) continue;
            }
            float step = (i < 4) ? m_costStraight : m_costDiagonal;
            size_t nIndex = static_cast<size_t>(idx(nx, ny));
            if (closed[nIndex]) continue;
            float weight = (nIndex < m_weight.size() && m_weight[nIndex] > 0.0f) ? m_weight[nIndex] : 1.0f;
            float tentative = gScore[static_cast<size_t>(idx(cur.x, cur.y))] + step * weight;
            if (tentative < gScore[nIndex]) {
                parent[nIndex] = idx(cur.x, cur.y);
                gScore[nIndex] = tentative;
                fScore[nIndex] = tentative + h(nx, ny);
                open.push({nx, ny, fScore[nIndex]});
            }
        }
    }
    
    // Update statistics for timeout case
    m_stats.totalRequests++;
    m_stats.timeouts++;
    m_stats.totalIterations += iterations;
    
    // Diagnostic logging for timeout patterns
    static uint32_t timeoutCount = 0;
    static std::unordered_map<std::string, int> problematicGoals;
    
    if (++timeoutCount % 10 == 0) {
        std::string goalKey = "(" + std::to_string(gx) + "," + std::to_string(gy) + ")";
        problematicGoals[goalKey]++;
        
        PATHFIND_WARN("Pathfinding timeouts: " + std::to_string(m_stats.timeouts) + 
                      " of " + std::to_string(m_stats.totalRequests) + " requests. " +
                      "Last timeout: distance=" + std::to_string(baseDistance) + 
                      " tiles, iters=" + std::to_string(iterations) + "/" + std::to_string(dynamicMaxIters) +
                      ", start=(" + std::to_string(sx) + "," + std::to_string(sy) + ")" +
                      ", goal=" + goalKey);
                      
        if (problematicGoals[goalKey] >= 3) {
            PATHFIND_WARN("REPEATED PROBLEMATIC GOAL: " + goalKey + " failed " + 
                          std::to_string(problematicGoals[goalKey]) + " times");
        }
        
        // Flag suspicious boundary goals
        if (gx <= 2 || gy <= 2 || gx >= m_w-3 || gy >= m_h-3) {
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
