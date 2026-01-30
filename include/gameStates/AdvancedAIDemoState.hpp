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
#include "utils/Camera.hpp"
#include "utils/WorldRenderPipeline.hpp"

#include <memory>
#include <vector>

// Forward declarations with smart pointer types
class Player;
using PlayerPtr = std::shared_ptr<Player>;

#ifdef USE_SDL3_GPU
namespace HammerEngine {
class GPUSceneRenderer;
}
#endif

class AdvancedAIDemoState : public GameState {
public:
    AdvancedAIDemoState();  // Defined in .cpp for unique_ptr with forward-declared types
    ~AdvancedAIDemoState() override;

    void update(float deltaTime) override;
    void render(SDL_Renderer* renderer, float interpolationAlpha = 1.0f) override;
    void handleInput() override;

    bool enter() override;
    bool exit() override;

    std::string getName() const override { return "AdvancedAIDemoState"; }

#ifdef USE_SDL3_GPU
    // GPU rendering support
    void recordGPUVertices(HammerEngine::GPURenderer& gpuRenderer,
                           float interpolationAlpha) override;
    void renderGPUScene(HammerEngine::GPURenderer& gpuRenderer,
                        SDL_GPURenderPass* scenePass,
                        float interpolationAlpha) override;
    void renderGPUUI(HammerEngine::GPURenderer& gpuRenderer,
                     SDL_GPURenderPass* swapchainPass) override;
    bool supportsGPURendering() const override { return true; }
#endif

    // Get the player entity for AI behaviors to access
    EntityPtr getPlayer() const { return m_player; }

private:
    // Methods
    void setupAdvancedAIBehaviors();
    void createAdvancedNPCs();
    void initializeCamera();
    void updateCamera(float deltaTime);
    void initializeCombatHUD();
    void updateCombatHUD();

    // Data-driven NPC rendering (velocity-based animation)
    NPCRenderController m_npcRenderCtrl{};

    // Player entity
    PlayerPtr m_player{};
    std::unique_ptr<HammerEngine::Camera> m_camera;

    // World render pipeline for coordinated chunk management and scene rendering
    std::unique_ptr<HammerEngine::WorldRenderPipeline> m_renderPipeline{nullptr};

#ifdef USE_SDL3_GPU
    // GPU scene renderer for coordinated GPU rendering
    std::unique_ptr<HammerEngine::GPUSceneRenderer> m_gpuSceneRenderer{nullptr};
#endif

    std::string m_textureID {""};  // Texture ID as loaded by TextureManager from res/img directory

    // Advanced demo settings optimized for behavior showcasing
    int m_idleNPCCount{4};      // Small group for idle demonstration
    int m_fleeNPCCount{7};      // Enough to show fleeing patterns
    int m_followNPCCount{5};    // Moderate group for following behavior
    int m_guardNPCCount{8};     // Strategic positions for guarding
    int m_attackNPCCount{6};    // Combat-focused group
    int m_totalNPCCount{30};    // Total optimized for advanced behavior showcase

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

    // Controller registry (follows GamePlayState pattern)
    ControllerRegistry m_controllers;

    // AI pause state
    bool m_aiPaused{false};
    bool m_previousGlobalPauseState{false};  // Store previous global pause state to restore on exit

    // Status display optimization - zero per-frame allocations (C++20 type-safe)
    std::string m_statusBuffer{};
    float m_lastDisplayedFPS{-1.0f};
    size_t m_lastDisplayedNPCCount{0};
    bool m_lastDisplayedPauseState{false};

    // Cached NPC count (updated in update(), used in render())
    size_t m_cachedNPCCount{0};
};

#endif // ADVANCED_AI_DEMO_STATE_HPP
