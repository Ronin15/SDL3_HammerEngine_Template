/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef AI_DEMO_STATE_HPP
#define AI_DEMO_STATE_HPP

#include "gameStates/GameState.hpp"
#include "controllers/render/NPCRenderController.hpp"
#include "entities/EntityHandle.hpp"
#include "entities/Player.hpp"
#include "utils/Camera.hpp"

#include <memory>
#include <vector>

// Forward declarations with smart pointer types
class Player;
using PlayerPtr = std::shared_ptr<Player>;

class AIDemoState : public GameState {
public:

    ~AIDemoState() override;

    void update(float deltaTime) override;
    void render(SDL_Renderer* renderer, float interpolationAlpha = 1.0f) override;
    void handleInput() override;

    bool enter() override;
    bool exit() override;

    std::string getName() const override { return "AIDemoState"; }

    // Get the player entity for AI behaviors to access
    EntityPtr getPlayer() const { return m_player; }

private:
    // Methods
    void setupAIBehaviors();
    void initializeCamera();
    void updateCamera(float deltaTime);

    // Data-driven NPC rendering (velocity-based animation)
    NPCRenderController m_npcRenderCtrl{};

    // Player entity
    PlayerPtr m_player{};

    std::string m_textureID {""};  // Texture ID as loaded by TextureManager from res/img directory

    // Demo settings
    float m_worldWidth{800.0f};
    float m_worldHeight{600.0f};

    // Track whether world has been loaded (prevents re-entering LoadingState)
    bool m_worldLoaded{false};

    // Track if we need to transition to loading screen on first update
    bool m_needsLoading{false};

    // Track if we're transitioning to LoadingState (prevents infinite loop)
    bool m_transitioningToLoading{false};

    // Track if state is fully initialized (after returning from LoadingState)
    bool m_initialized{false};

    // Camera for world navigation
    std::unique_ptr<HammerEngine::Camera> m_camera{nullptr};

    // AI pause state
    bool m_aiPaused{false};
    bool m_previousGlobalPauseState{false};  // Store previous global pause state to restore on exit

    // Status display optimization - zero per-frame allocations (C++20 type-safe)
    std::string m_statusBuffer{};
    int m_lastDisplayedFPS{-1};
    size_t m_lastDisplayedEntityCount{0};
    bool m_lastDisplayedPauseState{false};

    // Cached entity count (updated in update(), used in render())
    size_t m_cachedEntityCount{0};

    // Render scale caching - avoid GPU state changes when zoom unchanged
    float m_lastRenderedZoom{1.0f};
};

#endif // AI_DEMO_STATE_HPP
