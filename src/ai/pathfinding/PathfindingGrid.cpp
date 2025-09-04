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
#include <memory>
#include <unordered_map>
#include <stdexcept>
#include "core/Logger.hpp"

namespace HammerEngine {

// NodePool will be thread_local within findPath function

PathfindingGrid::PathfindingGrid(int width, int height, float cellSize, const Vector2D& worldOffset, bool createCoarseGrid)
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
    
    // Initialize coarse grid for hierarchical pathfinding (4x larger cells)
    if (createCoarseGrid) {
        initializeCoarseGrid();
    }
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
    
    // Update coarse grid for hierarchical pathfinding
    if (m_coarseGrid) {
        updateCoarseGrid();
        
        // Log coarse grid statistics for debugging
        int coarseW = m_coarseGrid->getWidth();
        int coarseH = m_coarseGrid->getHeight();
        int coarseBlockedCount = 0;
        for (int cy = 0; cy < coarseH; ++cy) {
            for (int cx = 0; cx < coarseW; ++cx) {
                if (m_coarseGrid->isBlocked(cx, cy)) {
                    coarseBlockedCount++;
                }
            }
        }
        float coarseBlockedPercent = (coarseW * coarseH > 0) ? 
            (100.0f * coarseBlockedCount) / (coarseW * coarseH) : 0.0f;
        
        PATHFIND_INFO("Coarse grid updated: " + std::to_string(coarseW) + "x" + std::to_string(coarseH) +
                     ", blocked=" + std::to_string(coarseBlockedCount) + "/" + std::to_string(coarseW * coarseH) +
                     " (" + std::to_string(coarseBlockedPercent) + "% blocked)");
    }
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
    
    // For very long distances, do a quick connectivity check (increased threshold to be less restrictive)
    if (directDistance > 200) {
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
        
        // If more than 70% of samples are in completely blocked areas, likely unreachable (balanced threshold)
        if (blockedSamples > (samples * 7) / 10) {
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
        // OPTIMIZED HEURISTIC: More focused search with perfect admissible heuristic
        int dx = std::abs(x - gx); int dy = std::abs(y - gy);
        int dmin = std::min(dx, dy); int dmax = std::max(dx, dy);
        float baseDistance = m_costDiagonal * dmin + m_costStraight * (dmax - dmin);
        // Perfect octile distance for optimal A* convergence
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

    // PERFORMANCE TUNED: Balanced iteration limits based on world complexity
    // With 35% blocked world, need more iterations for complex paths  
    int baseIters = std::max(2000, baseDistance * 50); // Generous base for complex terrain
    int dynamicMaxIters = std::min(m_maxIterations, baseIters + 8000); // Large buffer for blocked terrain
    
    // Performance-focused: Reasonable queue limits to balance memory and success rate
    
    // Memory protection for very complex pathfinding scenarios  
    // Increased queue size limit for better success rate with complex paths
    const size_t maxAbsoluteQueueSize = 15000;

    while (!open.empty() && iterations++ < dynamicMaxIters) {
        // Only terminate on truly excessive memory usage, not normal pathfinding complexity
        if (open.size() > maxAbsoluteQueueSize) {
            PATHFIND_DEBUG("Emergency termination: queue size " + std::to_string(open.size()) + 
                          " exceeded emergency limit (max: " + std::to_string(maxAbsoluteQueueSize) + ")");
            break;
        }
        
        // EARLY SUCCESS: Check if we're very close to the goal for quick termination
        if (iterations > 100 && iterations % 500 == 0 && !open.empty()) {
            NodePool::Node topNode = open.top();
            int distToGoal = std::abs(topNode.x - gx) + std::abs(topNode.y - gy);
            if (distToGoal <= 2) {
                // Very close to goal, increase iteration allowance for final push
                dynamicMaxIters = std::min(m_maxIterations, iterations + 1000);
            }
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

// ===== Hierarchical Pathfinding Implementation =====

void PathfindingGrid::initializeCoarseGrid() {
    // Skip coarse grid initialization if main grid is too small
    if (m_w < static_cast<int>(COARSE_GRID_MULTIPLIER) || m_h < static_cast<int>(COARSE_GRID_MULTIPLIER)) {
        PATHFIND_INFO("Grid too small for hierarchical pathfinding (" + std::to_string(m_w) + 
                     "x" + std::to_string(m_h) + "), skipping coarse grid");
        m_coarseGrid = nullptr;
        return;
    }
    
    // Create coarse grid with 4x larger cells for long-distance pathfinding
    float coarseCellSize = m_cell * COARSE_GRID_MULTIPLIER;
    int coarseWidth = std::max(1, static_cast<int>(m_w / COARSE_GRID_MULTIPLIER));
    int coarseHeight = std::max(1, static_cast<int>(m_h / COARSE_GRID_MULTIPLIER));
    
    // Validate coarse grid dimensions
    if (coarseWidth <= 0 || coarseHeight <= 0) {
        PATHFIND_WARN("Invalid coarse grid dimensions: " + std::to_string(coarseWidth) + 
                     "x" + std::to_string(coarseHeight));
        m_coarseGrid = nullptr;
        return;
    }
    
    try {
        m_coarseGrid = std::make_unique<PathfindingGrid>(coarseWidth, coarseHeight, 
                                                        coarseCellSize, m_offset, false);
        // Set more aggressive settings for coarse grid (speed over precision)
        m_coarseGrid->setMaxIterations(m_maxIterations / 4); // Quarter the iterations
        m_coarseGrid->setAllowDiagonal(true); // Always allow diagonal for speed
        
        PATHFIND_INFO("Hierarchical coarse grid initialized: " + std::to_string(coarseWidth) + 
                     "x" + std::to_string(coarseHeight) + ", cell size: " + std::to_string(coarseCellSize));
    }
    catch (const std::exception& e) {
        PATHFIND_WARN("Failed to initialize coarse grid: " + std::string(e.what()));
        m_coarseGrid = nullptr;
    }
}

void PathfindingGrid::updateCoarseGrid() {
    if (!m_coarseGrid) {
        return;
    }
    
    // Downsample fine grid to coarse grid (4x4 fine cells -> 1 coarse cell)
    int coarseW = m_coarseGrid->getWidth();
    int coarseH = m_coarseGrid->getHeight();
    
    for (int cy = 0; cy < coarseH; ++cy) {
        for (int cx = 0; cx < coarseW; ++cx) {
            // Sample 4x4 region in fine grid with improved strategy
            int blockedCount = 0;
            int totalCount = 0;
            float avgWeight = 0.0f;
            int sampleCount = 0;
            
            int fineStartX = cx * static_cast<int>(COARSE_GRID_MULTIPLIER);
            int fineStartY = cy * static_cast<int>(COARSE_GRID_MULTIPLIER);
            
            // Sample the 4x4 region
            for (int fy = fineStartY; fy < fineStartY + static_cast<int>(COARSE_GRID_MULTIPLIER) && fy < m_h; ++fy) {
                for (int fx = fineStartX; fx < fineStartX + static_cast<int>(COARSE_GRID_MULTIPLIER) && fx < m_w; ++fx) {
                    if (inBounds(fx, fy)) {
                        totalCount++;
                        if (isBlocked(fx, fy)) {
                            blockedCount++;
                        }
                        size_t fineIdx = static_cast<size_t>(fy * m_w + fx);
                        if (fineIdx < m_weight.size()) {
                            avgWeight += m_weight[fineIdx];
                            sampleCount++;
                        }
                    }
                }
            }
            
            // IMPROVED: Only block coarse cell if majority (>75%) of fine cells are blocked
            // This allows pathfinding through areas with partial obstacles
            bool coarseBlocked = (totalCount > 0) && (static_cast<float>(blockedCount) / static_cast<float>(totalCount) > 0.75f);
            
            // Update coarse grid cell using safe setter methods
            m_coarseGrid->setBlocked(cx, cy, coarseBlocked);
            if (sampleCount > 0) {
                // Increase weight for areas with some obstacles (but not fully blocked)
                float blockageRatio = (totalCount > 0) ? static_cast<float>(blockedCount) / static_cast<float>(totalCount) : 0.0f;
                float adjustedWeight = (avgWeight / static_cast<float>(sampleCount)) * (1.0f + blockageRatio * 2.0f);
                m_coarseGrid->setWeight(cx, cy, adjustedWeight);
            } else {
                m_coarseGrid->setWeight(cx, cy, 1.0f); // Default weight
            }
        }
    }
}

bool PathfindingGrid::shouldUseHierarchicalPathfinding(const Vector2D& start, const Vector2D& goal) const {
    if (!m_coarseGrid) {
        return false; // No coarse grid available
    }
    
    float distance = (goal - start).length();
    return distance > HIERARCHICAL_DISTANCE_THRESHOLD;
}

PathfindingResult PathfindingGrid::findPathHierarchical(const Vector2D& start, const Vector2D& goal,
                                                      std::vector<Vector2D>& outPath) {
    if (!m_coarseGrid) {
        // Fallback to regular pathfinding
        return findPath(start, goal, outPath);
    }
    
    // Note: Coarse grid is updated in rebuildFromWorld() and should be current
    
    // Step 1: Find coarse path on low-resolution grid
    std::vector<Vector2D> coarsePath;
    auto coarseResult = m_coarseGrid->findPath(start, goal, coarsePath);
    
    if (coarseResult != PathfindingResult::SUCCESS) {
        // Coarse pathfinding failed, try direct pathfinding as fallback
        static int failureCount = 0;
        failureCount++;
        
        // Only log every 10th failure to reduce spam
        if (failureCount % 10 == 1) {
            PATHFIND_INFO("Coarse pathfinding result: " + std::to_string(static_cast<int>(coarseResult)) + 
                         ", attempting direct pathfinding (" + std::to_string(failureCount) + " failures)");
        }
        return findPath(start, goal, outPath);
    }
    
    if (coarsePath.size() < 2) {
        // Coarse path too short (start == goal?), use direct pathfinding
        return findPath(start, goal, outPath);
    }
    
    // Step 2: Refine coarse path segments on fine grid
    return refineCoarsePath(coarsePath, start, goal, outPath);
}

PathfindingResult PathfindingGrid::refineCoarsePath(const std::vector<Vector2D>& coarsePath,
                                                  const Vector2D& start, const Vector2D& goal,
                                                  std::vector<Vector2D>& outPath) {
    outPath.clear();
    outPath.reserve(coarsePath.size() * 4); // Estimate refined path size
    
    // Add start point
    outPath.push_back(start);
    
    // Refine each segment of the coarse path
    Vector2D currentPoint = start;
    
    for (size_t i = 1; i < coarsePath.size(); ++i) {
        Vector2D segmentGoal = coarsePath[i];
        
        // Check if we can directly connect to this coarse waypoint
        if (hasLineOfSight(currentPoint, segmentGoal)) {
            // Direct line of sight - no need for detailed pathfinding
            outPath.push_back(segmentGoal);
            currentPoint = segmentGoal;
        }
        else {
            // Need detailed pathfinding for this segment
            std::vector<Vector2D> segmentPath;
            auto result = findPath(currentPoint, segmentGoal, segmentPath);
            
            if (result != PathfindingResult::SUCCESS || segmentPath.empty()) {
                PATHFIND_WARN("Segment refinement failed, using direct line");
                outPath.push_back(segmentGoal);
                currentPoint = segmentGoal;
            }
            else {
                // Add refined segment (skip first point to avoid duplicates)
                for (size_t j = 1; j < segmentPath.size(); ++j) {
                    outPath.push_back(segmentPath[j]);
                }
                currentPoint = segmentPath.back();
            }
        }
    }
    
    // Ensure we end at the exact goal
    if (outPath.empty() || (outPath.back() - goal).length() > m_cell * 0.5f) {
        outPath.push_back(goal);
    }
    
    // Apply path smoothing to the final result
    smoothPath(outPath);
    
    return PathfindingResult::SUCCESS;
}

void PathfindingGrid::setBlocked(int gx, int gy, bool blocked) {
    if (inBounds(gx, gy)) {
        size_t idx = static_cast<size_t>(gy * m_w + gx);
        if (idx < m_blocked.size()) {
            m_blocked[idx] = blocked ? 1 : 0;
        }
    }
}

void PathfindingGrid::setWeight(int gx, int gy, float weight) {
    if (inBounds(gx, gy)) {
        size_t idx = static_cast<size_t>(gy * m_w + gx);
        if (idx < m_weight.size()) {
            m_weight[idx] = weight;
        }
    }
}

} // namespace HammerEngine
