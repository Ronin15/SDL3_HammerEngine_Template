/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef AI_DEMO_STATE_HPP
#define AI_DEMO_STATE_HPP

#include "gameStates/GameState.hpp"
#include "entities/NPC.hpp"
#include "entities/Player.hpp"

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
    void render(double alpha) override;
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


    // Members
    std::vector<NPCPtr> m_npcs{};
    PlayerPtr m_player{};

    std::string m_textureID {""};  // Texture ID as loaded by TextureManager from res/img directory

    // Demo settings
    int m_npcCount{10000};  // Number of NPCs to create for the demo (balanced for performance)
    float m_worldWidth{800.0f};
    float m_worldHeight{600.0f};



    // AI pause state
    bool m_aiPaused{false};
    bool m_previousGlobalPauseState{false};  // Store previous global pause state to restore on exit
};

#endif // AI_DEMO_STATE_HPP
