/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef RESOURCE_RENDER_CONTROLLER_HPP
#define RESOURCE_RENDER_CONTROLLER_HPP

/**
 * @file ResourceRenderController.hpp
 * @brief Unified rendering controller for dropped items, containers, and harvestables
 *
 * Renders resources using data from EntityDataManager:
 * - DroppedItems: Bobbing animation, frame cycling
 * - Containers: Open/closed state rendering
 * - Harvestables: Normal/depleted state rendering
 *
 * Usage:
 *   - Add as member in GameState
 *   - Call update(deltaTime) in GameState::update()
 *   - Call render methods in GameState::render()
 */

#include "controllers/ControllerBase.hpp"
#include "controllers/IUpdatable.hpp"
#include <vector>

struct SDL_Renderer;

namespace HammerEngine {
class Camera;
}

class ResourceRenderController : public ControllerBase, public IUpdatable {
public:
    ResourceRenderController() = default;
    ~ResourceRenderController() override = default;

    // Non-copyable, non-movable
    ResourceRenderController(const ResourceRenderController&) = delete;
    ResourceRenderController& operator=(const ResourceRenderController&) = delete;
    ResourceRenderController(ResourceRenderController&&) = delete;
    ResourceRenderController& operator=(ResourceRenderController&&) = delete;

    // ControllerBase interface
    void subscribe() override {}  // No events needed
    [[nodiscard]] std::string_view getName() const override { return "ResourceRenderController"; }

    // IUpdatable - updates all resource animations
    void update(float deltaTime) override;

    /**
     * @brief Render all visible dropped items using spatial query
     * @param renderer SDL renderer from GameState::render()
     * @param camera Camera for viewport and position info
     * @param alpha Interpolation alpha for smooth rendering (0.0-1.0)
     */
    void renderDroppedItems(SDL_Renderer* renderer, const HammerEngine::Camera& camera, float alpha);

    /**
     * @brief Render all visible containers using spatial query
     * @param renderer SDL renderer from GameState::render()
     * @param camera Camera for viewport and position info
     * @param alpha Interpolation alpha for smooth rendering (0.0-1.0)
     */
    void renderContainers(SDL_Renderer* renderer, const HammerEngine::Camera& camera, float alpha);

    /**
     * @brief Render all visible harvestables using spatial query
     * @param renderer SDL renderer from GameState::render()
     * @param camera Camera for viewport and position info
     * @param alpha Interpolation alpha for smooth rendering (0.0-1.0)
     */
    void renderHarvestables(SDL_Renderer* renderer, const HammerEngine::Camera& camera, float alpha);

    /**
     * @brief Clear all spawned resources (cleanup for state transitions)
     * Queries EDM for all resource indices and destroys them.
     */
    void clearAll();

private:
    // Update helpers (combined for efficiency - single iteration per entity kind)
    void updateDroppedItemAnimations(float deltaTime);
    void updateContainerStates(float deltaTime);
    void updateHarvestableStates(float deltaTime);

    // Reusable buffers for spatial queries (avoid per-frame allocations)
    std::vector<size_t> m_visibleItemIndices;
    std::vector<size_t> m_visibleContainerIndices;
    std::vector<size_t> m_visibleHarvestableIndices;

    // Animation constants
    static constexpr float BOB_SPEED = 3.0f;           // Radians per second for bobbing
    static constexpr float TWO_PI = 6.28318530718f;    // 2 * PI for wrapping
};

#endif // RESOURCE_RENDER_CONTROLLER_HPP
