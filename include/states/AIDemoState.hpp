/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef AI_DEMO_STATE_HPP
#define AI_DEMO_STATE_HPP

#include "states/GameState.hpp"
#include "entities/NPC.hpp"
#include "entities/Player.hpp"
// AIManager.hpp is included by AIDemoState.cpp
#include <memory>
#include <vector>

class AIDemoState : public GameState {
public:

    ~AIDemoState();

    void update() override;
    void render() override;

    bool enter() override;
    bool exit() override;

    std::string getName() const override { return "AIDemo"; }

private:
    // Methods
    void setupAIBehaviors();
    void createNPCs();

    // Members
    std::vector<std::unique_ptr<NPC>> m_npcs;
    std::unique_ptr<Player> m_player;

    // Demo settings
    int m_npcCount{5};
    float m_worldWidth{800.0f};
    float m_worldHeight{600.0f};
};

#endif // AI_DEMO_STATE_HPP
