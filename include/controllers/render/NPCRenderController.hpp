/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef NPC_RENDER_CONTROLLER_HPP
#define NPC_RENDER_CONTROLLER_HPP

/**
 * @file NPCRenderController.hpp
 * @brief Velocity-based NPC rendering controller (data-driven approach)
 *
 * Renders NPCs using NPCRenderData from EntityDataManager.
 * Animation is velocity-based: Idle when stationary, Moving when velocity > threshold.
 * No NPC class needed - all data comes from EDM.
 *
 * Usage:
 *   - Add as member in GameState
 *   - Call update(deltaTime) in GameState::update()
 *   - Call renderNPCs(renderer, camX, camY, alpha) in GameState::render()
 */

#include "controllers/ControllerBase.hpp"
#include "controllers/IUpdatable.hpp"

struct SDL_Renderer;

class NPCRenderController : public ControllerBase, public IUpdatable {
public:
    NPCRenderController() = default;
    ~NPCRenderController() override = default;

    // Non-copyable, non-movable
    NPCRenderController(const NPCRenderController&) = delete;
    NPCRenderController& operator=(const NPCRenderController&) = delete;
    NPCRenderController(NPCRenderController&&) = delete;
    NPCRenderController& operator=(NPCRenderController&&) = delete;

    // ControllerBase interface
    void subscribe() override {}  // No events needed
    [[nodiscard]] std::string_view getName() const override { return "NPCRenderController"; }

    // IUpdatable - advances animation frames based on velocity
    void update(float deltaTime) override;

    /**
     * @brief Render all active NPCs
     * @param renderer SDL renderer from GameState::render()
     * @param cameraX Camera X offset for world-to-screen conversion
     * @param cameraY Camera Y offset for world-to-screen conversion
     * @param alpha Interpolation alpha for smooth rendering (0.0-1.0)
     */
    void renderNPCs(SDL_Renderer* renderer, float cameraX, float cameraY, float alpha);

    /**
     * @brief Clear all spawned NPCs (cleanup for state transitions)
     * Queries EDM for NPC indices and destroys them.
     */
    void clearSpawnedNPCs();

private:
    static constexpr float MOVEMENT_THRESHOLD = 15.0f;  // Velocity threshold for Moving/Idle
};

#endif // NPC_RENDER_CONTROLLER_HPP
