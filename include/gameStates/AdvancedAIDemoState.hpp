/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef ADVANCED_AI_DEMO_STATE_HPP
#define ADVANCED_AI_DEMO_STATE_HPP

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

class AdvancedAIDemoState : public GameState {
public:

    ~AdvancedAIDemoState() override;

    void update(float deltaTime) override;
    void render() override;
    void handleInput() override;

    bool enter() override;
    bool exit() override;

    std::string getName() const override { return "AdvancedAIDemo"; }

    // Get the player entity for AI behaviors to access
    EntityPtr getPlayer() const { return m_player; }

private:
    // Methods
    void setupAdvancedAIBehaviors();
    void createAdvancedNPCs();
    void setupCombatAttributes();
    void updateCombatSystem(float deltaTime);

    // Members
    std::vector<NPCPtr> m_npcs{};
    PlayerPtr m_player{};

    std::string m_textureID {""};  // Texture ID as loaded by TextureManager from res/img directory

    // Advanced demo settings optimized for behavior showcasing
    int m_idleNPCCount{3};      // Small group for idle demonstration
    int m_fleeNPCCount{5};      // Enough to show fleeing patterns
    int m_followNPCCount{4};    // Moderate group for following behavior
    int m_guardNPCCount{6};     // Strategic positions for guarding
    int m_attackNPCCount{4};    // Combat-focused group
    int m_totalNPCCount{22};    // Total optimized for advanced behavior showcase
    
    float m_worldWidth{800.0f};
    float m_worldHeight{600.0f};

    // Combat system attributes (architecturally integrated)
    struct CombatAttributes {
        float health{100.0f};
        float maxHealth{100.0f};
        float attackDamage{10.0f};
        float attackRange{80.0f};
        float attackCooldown{1.0f};
        float lastAttackTime{0.0f};
        bool isDead{false};
    };

    // Combat state tracking
    std::unordered_map<EntityPtr, CombatAttributes> m_combatAttributes;
    float m_gameTime{0.0f};

    // AI pause state
    bool m_aiPaused{false};
    bool m_previousGlobalPauseState{false};  // Store previous global pause state to restore on exit
};

#endif // ADVANCED_AI_DEMO_STATE_HPP