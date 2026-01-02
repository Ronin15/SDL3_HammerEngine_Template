/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef AI_DEMO_STATE_HPP
#define AI_DEMO_STATE_HPP

#include "gameStates/GameState.hpp"
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
    void createNPCBatch(int count);  // Create a batch of NPCs with standard behavior
    void createNPCBatchWithRandomBehaviors(int count);  // Create NPCs with random behaviors
    void initializeCamera();
    void updateCamera(float deltaTime);

    // Members - stored by handle ID for O(1) lookup
    std::unordered_map<uint32_t, NPCPtr> m_npcsById{};
    PlayerPtr m_player{};

    std::string m_textureID {""};  // Texture ID as loaded by TextureManager from res/img directory

    // Demo settings
    int m_npcCount{2000};  // Number of NPCs to create for the demo (balanced for performance)
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

    // Batch NPC spawning to reduce per-frame overhead
    int m_npcsSpawned{0};
    int m_npcsPerBatch{30};        // Spawn 30 NPCs per batch
    int m_spawnInterval{10};       // Spawn every 10 frames
    int m_framesSinceLastSpawn{0}; // Frame counter for spawn timing

    // Cached manager pointers for render hot path (resolved in enter())
    EntityDataManager* mp_edm{nullptr};
    WorldManager* mp_worldMgr{nullptr};
    UIManager* mp_uiMgr{nullptr};
    ParticleManager* mp_particleMgr{nullptr};

    // Status display optimization - zero per-frame allocations (C++20 type-safe)
    std::string m_statusBuffer{};
    int m_lastDisplayedFPS{-1};
    size_t m_lastDisplayedEntityCount{0};
    bool m_lastDisplayedPauseState{false};

    // Render scale caching - avoid GPU state changes when zoom unchanged
    float m_lastRenderedZoom{1.0f};
};

#endif // AI_DEMO_STATE_HPP
