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
#include <vector>

// Forward declarations with smart pointer types
class NPC;
using NPCPtr = std::shared_ptr<NPC>;

class Player;
using PlayerPtr = std::shared_ptr<Player>;

class AIDemoState : public GameState {
public:

    ~AIDemoState() override;

    void update(float deltaTime) override;
    void render() override;
    void handleInput() override;

    bool enter() override;
    bool exit() override;

    std::string getName() const override { return "AIDemo"; }

    // Get the player entity for AI behaviors to access
    EntityPtr getPlayer() const { return m_player; }

private:
    // Methods
    void setupAIBehaviors();
    void createNPCs();
    void createNPCBatch(int count);  // Create a batch of NPCs gradually
    void initializeWorld();
    void initializeCamera();
    void updateCamera(float deltaTime);
    void applyCameraTransformation();

    // Members
    std::vector<NPCPtr> m_npcs{};
    PlayerPtr m_player{};

    std::string m_textureID {""};  // Texture ID as loaded by TextureManager from res/img directory

    // Demo settings
    int m_npcCount{10000};  // Number of NPCs to create for the demo (balanced for performance)
    float m_worldWidth{800.0f};
    float m_worldHeight{600.0f};

    // Camera for world navigation
    std::unique_ptr<HammerEngine::Camera> m_camera{nullptr};

    // Camera transformation state (calculated in update, used in render)
    float m_cameraOffsetX{0.0f};
    float m_cameraOffsetY{0.0f};

    // AI pause state
    bool m_aiPaused{false};
    bool m_previousGlobalPauseState{false};  // Store previous global pause state to restore on exit

    // Batch NPC spawning to reduce per-frame overhead
    int m_npcsSpawned{0};
    int m_npcsPerBatch{30};        // Spawn 30 NPCs per batch
    int m_spawnInterval{10};       // Spawn every 10 frames
    int m_framesSinceLastSpawn{0}; // Frame counter for spawn timing
};

#endif // AI_DEMO_STATE_HPP
