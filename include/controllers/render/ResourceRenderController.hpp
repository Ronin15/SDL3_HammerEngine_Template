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

struct SDL_Renderer;

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
     * @brief Render all active dropped items
     * @param renderer SDL renderer from GameState::render()
     * @param cameraX Camera X offset for world-to-screen conversion
     * @param cameraY Camera Y offset for world-to-screen conversion
     * @param alpha Interpolation alpha for smooth rendering (0.0-1.0)
     */
    void renderDroppedItems(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha);

    /**
     * @brief Render all active containers
     * @param renderer SDL renderer from GameState::render()
     * @param cameraX Camera X offset for world-to-screen conversion
     * @param cameraY Camera Y offset for world-to-screen conversion
     * @param alpha Interpolation alpha for smooth rendering (0.0-1.0)
     */
    void renderContainers(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha);

    /**
     * @brief Render all active harvestables
     * @param renderer SDL renderer from GameState::render()
     * @param cameraX Camera X offset for world-to-screen conversion
     * @param cameraY Camera Y offset for world-to-screen conversion
     * @param alpha Interpolation alpha for smooth rendering (0.0-1.0)
     */
    void renderHarvestables(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha);

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

    // Animation constants
    static constexpr float BOB_SPEED = 3.0f;           // Radians per second for bobbing
    static constexpr float TWO_PI = 6.28318530718f;    // 2 * PI for wrapping
};

#endif // RESOURCE_RENDER_CONTROLLER_HPP
