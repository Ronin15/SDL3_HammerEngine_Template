/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef ADVANCED_AI_DEMO_STATE_HPP
#define ADVANCED_AI_DEMO_STATE_HPP

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

class AdvancedAIDemoState : public GameState {
public:

    ~AdvancedAIDemoState() override;

    void update(float deltaTime) override;
    void render() override;
    void handleInput() override;

    bool enter() override;
    bool exit() override;

    std::string getName() const override { return "AdvancedAIDemoState"; }
    void onWindowResize(int newLogicalWidth, int newLogicalHeight) override;

    // Get the player entity for AI behaviors to access
    EntityPtr getPlayer() const { return m_player; }

private:
    // Methods
    void setupAdvancedAIBehaviors();
    void createAdvancedNPCs();
    void setupCombatAttributes();
    void updateCombatSystem(float deltaTime);
    void initializeCamera();
    void updateCamera(float deltaTime);

    // Members
    std::vector<NPCPtr> m_npcs{};
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