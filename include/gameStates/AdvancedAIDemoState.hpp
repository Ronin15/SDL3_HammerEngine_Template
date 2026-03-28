/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef ADVANCED_AI_DEMO_STATE_HPP
#define ADVANCED_AI_DEMO_STATE_HPP

#include "gameStates/GameState.hpp"
#include "controllers/ControllerRegistry.hpp"
#include "controllers/render/NPCRenderController.hpp"
#include "entities/EntityHandle.hpp"
#include "entities/Player.hpp"
#include "managers/EventManager.hpp"
#include "utils/Camera.hpp"

#include <memory>
#include <vector>

// Forward declarations with smart pointer types
class Player;
using PlayerPtr = std::shared_ptr<Player>;

namespace HammerEngine {
class GPUSceneRenderer;
}

class AdvancedAIDemoState : public GameState {
public:
    AdvancedAIDemoState();  // Defined in .cpp for unique_ptr with forward-declared types
    ~AdvancedAIDemoState() override;

    void update(float deltaTime) override;
    void handleInput() override;

    bool enter() override;
    bool exit() override;

    std::string getName() const override { return "AdvancedAIDemoState"; }

    // GPU rendering support
    void recordGPUVertices(HammerEngine::GPURenderer& gpuRenderer,
                           float interpolationAlpha) override;
    void renderGPUScene(HammerEngine::GPURenderer& gpuRenderer,
                        SDL_GPURenderPass* scenePass,
                        float interpolationAlpha) override;
    void renderGPUUI(HammerEngine::GPURenderer& gpuRenderer,
                     SDL_GPURenderPass* swapchainPass) override;
    bool supportsGPURendering() const override { return true; }

    // Get the player entity for AI behaviors to access
    EntityPtr getPlayer() const { return m_player; }

private:
    // Methods
    void setupTestVillage();  // Spawns merchant NPCs, guards, and villagers
    void initializeCamera();
    void updateCamera(float deltaTime);
    void registerEventHandlers();
    void unregisterEventHandlers();

    // Data-driven NPC rendering (velocity-based animation)
    NPCRenderController m_npcRenderCtrl{};

    // Player entity
    PlayerPtr m_player{};
    std::unique_ptr<HammerEngine::Camera> m_camera;

    // GPU scene renderer for coordinated GPU rendering
    std::unique_ptr<HammerEngine::GPUSceneRenderer> m_gpuSceneRenderer{nullptr};

    float m_worldWidth{800.0f};
    float m_worldHeight{600.0f};

    // Track whether world has been loaded (prevents re-entering LoadingState)
    bool m_worldLoaded{false};

    // Track if we need to transition to loading screen on first update
    bool m_needsLoading{false};

    // Track if we're transitioning to LoadingState (prevents infinite loop)
    bool m_transitioningToLoading{false};
    bool m_transitioningToGameOver{false};

    // Track if state is fully initialized (after returning from LoadingState)
    bool m_initialized{false};

    // Controller registry (follows GamePlayState pattern)
    ControllerRegistry m_controllers;

    // AI pause state
    bool m_aiPaused{false};

    // Status display optimization - zero per-frame allocations (C++20 type-safe)
    std::string m_statusBuffer{};
    float m_lastDisplayedFPS{-1.0f};
    size_t m_lastDisplayedNPCCount{0};
    bool m_lastDisplayedPauseState{false};

    // Cached NPC count (updated in update(), used in render())
    size_t m_cachedNPCCount{0};
};

#endif // ADVANCED_AI_DEMO_STATE_HPP
