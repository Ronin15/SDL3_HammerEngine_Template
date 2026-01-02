/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef ADVANCED_AI_DEMO_STATE_HPP
#define ADVANCED_AI_DEMO_STATE_HPP

#include "gameStates/GameState.hpp"
#include "controllers/ControllerRegistry.hpp"
#include "entities/NPC.hpp"
#include "entities/Player.hpp"
#include "utils/Camera.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

// Forward declarations with smart pointer types
class NPC;
using NPCPtr = std::shared_ptr<NPC>;

class Player;
using PlayerPtr = std::shared_ptr<Player>;

// Forward declarations for cached manager pointers
class EntityDataManager;
class WorldManager;
class UIManager;
class ParticleManager;
class CombatController;

class AdvancedAIDemoState : public GameState {
public:

    ~AdvancedAIDemoState() override;

    void update(float deltaTime) override;
    void render(SDL_Renderer* renderer, float interpolationAlpha = 1.0f) override;
    void handleInput() override;

    bool enter() override;
    bool exit() override;

    std::string getName() const override { return "AdvancedAIDemoState"; }

    // Get the player entity for AI behaviors to access
    EntityPtr getPlayer() const { return m_player; }

private:
    // Methods
    void setupAdvancedAIBehaviors();
    void createAdvancedNPCs();
    void initializeCamera();
    void updateCamera(float deltaTime);

    // Members - stored by handle ID for O(1) lookup
    std::unordered_map<uint32_t, NPCPtr> m_npcsById{};
    PlayerPtr m_player{};
    std::unique_ptr<HammerEngine::Camera> m_camera;

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
    CombatController* mp_combatCtrl{nullptr};

    // AI pause state
    bool m_aiPaused{false};
    bool m_previousGlobalPauseState{false};  // Store previous global pause state to restore on exit

    // Cached manager pointers for render hot path (resolved in enter())
    EntityDataManager* mp_edm{nullptr};
    WorldManager* mp_worldMgr{nullptr};
    UIManager* mp_uiMgr{nullptr};
    ParticleManager* mp_particleMgr{nullptr};

    // Status display optimization - zero per-frame allocations (C++20 type-safe)
    std::string m_statusBuffer{};
    int m_lastDisplayedFPS{-1};
    size_t m_lastDisplayedNPCCount{0};
    bool m_lastDisplayedPauseState{false};

    // Render scale caching - avoid GPU state changes when zoom unchanged
    float m_lastRenderedZoom{1.0f};
};

#endif // ADVANCED_AI_DEMO_STATE_HPP