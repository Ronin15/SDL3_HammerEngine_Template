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
            bool blocked = tile.obstacleType != ObstacleType::NONE || tile.isWater;
            m_blocked[static_cast<size_t>(y * m_w + x)] = blocked ? 1 : 0;
            if (blocked) ++blockedCount;
            // Optionally set weights per biome if needed later
        }
    }
    PATHFIND_INFO("Grid rebuilt: " + std::to_string(m_w) + "x" + std::to_string(m_h) +
                  ", blocked=" + std::to_string(blockedCount));
}

PathfindingResult PathfindingGrid::findPath(const Vector2D& start, const Vector2D& goal,
                                            std::vector<Vector2D>& outPath) {
    outPath.clear();
    auto [sx, sy] = worldToGrid(start);
    auto [gx, gy] = worldToGrid(goal);
    if (!inBounds(sx, sy)) { PATHFIND_WARN("findPath(): invalid start grid"); return PathfindingResult::INVALID_START; }
    if (!inBounds(gx, gy)) { PATHFIND_WARN("findPath(): invalid goal grid"); return PathfindingResult::INVALID_GOAL; }
    if (isBlocked(sx, sy) || isBlocked(gx, gy)) { PATHFIND_DEBUG("findPath(): start or goal blocked"); return PathfindingResult::NO_PATH_FOUND; }

    const int W = m_w, H = m_h;
    const size_t N = static_cast<size_t>(W * H);
    const float INF = std::numeric_limits<float>::infinity();

    std::vector<float> gScore(N, INF), fScore(N, INF);
    std::vector<int> parent(N, -1);
    auto idx = [&](int x, int y){ return y * W + x; };
    auto h = [&](int x, int y){
        // Octile distance
        int dx = std::abs(x - gx); int dy = std::abs(y - gy);
        int dmin = std::min(dx, dy); int dmax = std::max(dx, dy);
        return m_costDiagonal * dmin + m_costStraight * (dmax - dmin);
    };

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

    while (!open.empty() && iterations++ < m_maxIterations) {
        Node cur = open.top(); open.pop();
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
            PATHFIND_DEBUG("Path found in " + std::to_string(iterations) +
                           " iters, length=" + std::to_string(outPath.size()));
            return PathfindingResult::SUCCESS;
        }
        for (int i = 0; i < dirs; ++i) {
            int nx = cur.x + dx8[i];
            int ny = cur.y + dy8[i];
            if (!inBounds(nx, ny) || isBlocked(nx, ny)) continue;
            float step = (i < 4) ? m_costStraight : m_costDiagonal;
            size_t nIndex = static_cast<size_t>(idx(nx, ny));
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
    PATHFIND_WARN("findPath(): timeout after " + std::to_string(iterations) + " iterations");
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
