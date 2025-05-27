/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef AI_DEMO_STATE_HPP
#define AI_DEMO_STATE_HPP

#include "gameStates/GameState.hpp"
#include "entities/NPC.hpp"
#include "entities/Player.hpp"
#include <chrono>
#include <deque>
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

    void update() override;
    void render() override;

    bool enter() override;
    bool exit() override;

    std::string getName() const override { return "AIDemo"; }

    // Get the player entity for AI behaviors to access
    EntityPtr getPlayer() const { return m_player; }

private:
    // Methods
    void setupAIBehaviors();
    void createNPCs();
    void updateFrameRate();  // FPS counter update method

    // Members
    std::vector<NPCPtr> m_npcs{};
    PlayerPtr m_player{};
    // Shared pointer to the chase behavior for cleanup
    std::shared_ptr<class ChaseBehavior> m_chaseBehavior{nullptr};

    std::string m_textureID {""};  // Texture ID as loaded by TextureManager from res/img directory

    // Demo settings
    int m_npcCount{5000};  // Number of NPCs to create for the demo (balanced for performance)
    float m_worldWidth{800.0f};
    float m_worldHeight{600.0f};

    // Frame rate counter
    std::chrono::steady_clock::time_point m_lastFrameTime{};
    std::deque<float> m_frameTimes{};
    int m_frameCount{0};
    float m_currentFPS{0.0f};
    float m_averageFPS{0.0f};
    static constexpr int MAX_FRAME_SAMPLES{60}; // Number of frames to average
};

#endif // AI_DEMO_STATE_HPP
