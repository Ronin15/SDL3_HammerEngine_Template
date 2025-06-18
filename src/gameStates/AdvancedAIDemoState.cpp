/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/AdvancedAIDemoState.hpp"
#include "core/Logger.hpp"
#include "managers/AIManager.hpp"
#include "SDL3/SDL_scancode.h"
#include "ai/behaviors/IdleBehavior.hpp"
#include "ai/behaviors/FleeBehavior.hpp"
#include "ai/behaviors/FollowBehavior.hpp"
#include "ai/behaviors/GuardBehavior.hpp"
#include "ai/behaviors/AttackBehavior.hpp"
#include "core/GameEngine.hpp"
#include "managers/UIManager.hpp"
#include "managers/InputManager.hpp"
#include <SDL3/SDL.h>

#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <iomanip>
#include <unordered_map>

AdvancedAIDemoState::~AdvancedAIDemoState() {
    // Don't call virtual functions from destructors

    try {
        // Note: Proper cleanup should already have happened in exit()
        // This destructor is just a safety measure in case exit() wasn't called

        // Reset AI behaviors first to clear entity references
        AIManager& aiMgr = AIManager::Instance();
        aiMgr.resetBehaviors();

        // Clear combat attributes
        m_combatAttributes.clear();

        // Clear NPCs without calling clean() on them
        m_npcs.clear();

        // Clean up player
        m_player.reset();

        GAMESTATE_INFO("Exiting AdvancedAIDemoState in destructor...");
    } catch (const std::exception& e) {
        GAMESTATE_ERROR("Exception in AdvancedAIDemoState destructor: " + std::string(e.what()));
    } catch (...) {
        GAMESTATE_ERROR("Unknown exception in AdvancedAIDemoState destructor");
    }
}

void AdvancedAIDemoState::handleInput() {
    InputManager& inputMgr = InputManager::Instance();

    // Use InputManager's new event-driven key press detection
    if (inputMgr.wasKeyPressed(SDL_SCANCODE_SPACE)) {
        // Toggle pause/resume
        m_aiPaused = !m_aiPaused;

        // Set global AI pause state in AIManager
        AIManager& aiMgr = AIManager::Instance();
        aiMgr.setGlobalPause(m_aiPaused);

        // Also send messages for behaviors that need them
        std::string message = m_aiPaused ? "pause" : "resume";
        aiMgr.broadcastMessage(message, true);

        // Simple feedback
        std::cout << "Hammer Game Engine - Advanced AI " << (m_aiPaused ? "PAUSED" : "RESUMED") << std::endl;
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
        std::cout << "Hammer Game Engine - Preparing to exit AdvancedAIDemoState...\n";
        const GameEngine& gameEngine = GameEngine::Instance();
        gameEngine.getGameStateManager()->setState("MainMenuState");
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_1)) {
        // Assign Idle behavior to all NPCs
        std::cout << "Hammer Game Engine - Switching all NPCs to IDLE behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            aiMgr.queueBehaviorAssignment(npc, "Idle");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_2)) {
        // Assign Flee behavior to all NPCs
        std::cout << "Hammer Game Engine - Switching all NPCs to FLEE behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            aiMgr.queueBehaviorAssignment(npc, "Flee");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_3)) {
        // Assign Follow behavior to all NPCs
        std::cout << "Hammer Game Engine - Switching all NPCs to FOLLOW behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            aiMgr.queueBehaviorAssignment(npc, "Follow");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_4)) {
        // Assign Guard behavior to all NPCs
        std::cout << "Hammer Game Engine - Switching all NPCs to GUARD behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            aiMgr.queueBehaviorAssignment(npc, "Guard");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_5)) {
        // Assign Attack behavior to all NPCs
        std::cout << "Hammer Game Engine - Switching all NPCs to ATTACK behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            aiMgr.queueBehaviorAssignment(npc, "Attack");
        }
    }
}

bool AdvancedAIDemoState::enter() {
    std::cout << "Hammer Game Engine - Entering AdvancedAIDemoState...\n";

    try {
        // Cache GameEngine reference for better performance
        const GameEngine& gameEngine = GameEngine::Instance();

        // Setup world size using logical dimensions for proper cross-platform rendering
        m_worldWidth = gameEngine.getLogicalWidth();
        m_worldHeight = gameEngine.getLogicalHeight();

        // Initialize game time
        m_gameTime = 0.0f;

        // Setup advanced AI behaviors
        setupAdvancedAIBehaviors();

        // Create player first (required for flee/follow/attack behaviors)
        m_player = std::make_shared<Player>();
        m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

        // Setup combat attributes for player
        setupCombatAttributes();

        // Cache AIManager reference for better performance
        AIManager& aiMgr = AIManager::Instance();

        // Set player reference in AIManager for advanced behaviors
        aiMgr.setPlayerForDistanceOptimization(m_player);

        // Configure priority multiplier for proper advanced behavior progression
        aiMgr.configurePriorityMultiplier(1.2f); // Slightly higher for advanced behaviors

        // Create NPCs with optimized counts for behavior showcasing
        createAdvancedNPCs();

        // Create advanced HUD UI
        auto& ui = UIManager::Instance();
        ui.createTitle("advanced_ai_title", {0, 5, gameEngine.getLogicalWidth(), 25}, "Advanced AI Demo State");
        ui.setTitleAlignment("advanced_ai_title", UIAlignment::CENTER_CENTER);
        ui.createLabel("advanced_ai_instructions_line1", {10, 40, gameEngine.getLogicalWidth() - 20, 20},
                       "Advanced AI Demo: [B] Exit | [SPACE] Pause/Resume | [1] Idle | [2] Flee | [3] Follow");
        ui.createLabel("advanced_ai_instructions_line2", {10, 75, gameEngine.getLogicalWidth() - 20, 20},
                       "Combat & Social: [4] Guard | [5] Attack");
        ui.createLabel("advanced_ai_status", {10, 110, 400, 20}, "FPS: -- | NPCs: -- | AI: RUNNING | Combat: ON");

        // Log status
        std::cout << "Hammer Game Engine - Created " << m_npcs.size() << " NPCs with advanced AI behaviors\n";
        std::cout << "Hammer Game Engine - Combat system initialized with health/damage attributes\n";

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Hammer Game Engine - ERROR: Exception in AdvancedAIDemoState::enter(): " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "Hammer Game Engine - ERROR: Unknown exception in AdvancedAIDemoState::enter()" << std::endl;
        return false;
    }
}

bool AdvancedAIDemoState::exit() {
    std::cout << "Hammer Game Engine - Exiting AdvancedAIDemoState...\n";

    // Cache AIManager reference for better performance
    AIManager& aiMgr = AIManager::Instance();

    // Use the new prepareForStateTransition method for safer cleanup
    aiMgr.prepareForStateTransition();

    // Clean up NPCs
    for (auto& npc : m_npcs) {
        if (npc) {
            npc->clean();
            npc->setVelocity(Vector2D(0, 0));
        }
    }
    m_npcs.clear();

    // Clear combat attributes
    m_combatAttributes.clear();

    // Clean up player
    if (m_player) {
        m_player.reset();
    }

    // Clean up UI components using simplified method
    auto& ui = UIManager::Instance();
    ui.prepareForStateTransition();

    // Always restore AI to unpaused state when exiting the demo state
    aiMgr.setGlobalPause(false);
    m_aiPaused = false;

    std::cout << "Hammer Game Engine - AdvancedAIDemoState exit complete\n";
    return true;
}

void AdvancedAIDemoState::update(float deltaTime) {
    try {
        // Update game time for combat system
        m_gameTime += deltaTime;

        // Update player
        if (m_player) {
            m_player->update(deltaTime);
        }

        // Update combat system
        updateCombatSystem(deltaTime);

        // AI Manager is updated globally by GameEngine for optimal performance
        // Entity updates are handled by AIManager::update() in GameEngine

    } catch (const std::exception& e) {
        std::cerr << "Hammer Game Engine - ERROR: Exception in AdvancedAIDemoState::update(): " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Hammer Game Engine - ERROR: Unknown exception in AdvancedAIDemoState::update()" << std::endl;
    }
}

void AdvancedAIDemoState::render(float deltaTime) {
    // Render all NPCs
    for (auto& npc : m_npcs) {
        npc->render();
        
        // Render health bars for NPCs with combat attributes
        auto it = m_combatAttributes.find(npc);
        if (it != m_combatAttributes.end() && !it->second.isDead) {
            // Note: Health bar rendering would be implemented with the graphics system
            // const auto& combat = it->second;
            // Vector2D pos = npc->getPosition();
            // float healthPercent = combat.health / combat.maxHealth;
        }
    }

    // Render player
    if (m_player) {
        m_player->render();
        
        // Render player health bar
        auto it = m_combatAttributes.find(m_player);
        if (it != m_combatAttributes.end()) {
            // Player health bar rendering would go here
        }
    }

    // Update and render UI components
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        ui.update(deltaTime);

        // Update status display with combat information
        const auto& gameEngine = GameEngine::Instance();
        auto& aiManager = AIManager::Instance();
        std::stringstream status;
        status << "FPS: " << std::fixed << std::setprecision(1) << gameEngine.getCurrentFPS()
               << " | NPCs: " << m_npcs.size()
               << " | AI: " << (aiManager.isGloballyPaused() ? "PAUSED" : "RUNNING")
               << " | Combat: ON";
        ui.setText("advanced_ai_status", status.str());
    }
    ui.render();
}

void AdvancedAIDemoState::setupAdvancedAIBehaviors() {
    std::cout << "AdvancedAIDemoState: Setting up advanced AI behaviors...\n";

    // Cache AIManager reference for better performance
    AIManager& aiMgr = AIManager::Instance();

    // Register Idle behavior
    if (!aiMgr.hasBehavior("Idle")) {
        auto idleBehavior = std::make_unique<IdleBehavior>(IdleBehavior::IdleMode::LIGHT_FIDGET, 50.0f);
        aiMgr.registerBehavior("Idle", std::move(idleBehavior));
        std::cout << "AdvancedAIDemoState: Registered Idle behavior\n";
    }

    // Register Flee behavior
    if (!aiMgr.hasBehavior("Flee")) {
        auto fleeBehavior = std::make_unique<FleeBehavior>(80.0f, 150.0f, 200.0f); // speed, detection range, safe distance
        aiMgr.registerBehavior("Flee", std::move(fleeBehavior));
        std::cout << "AdvancedAIDemoState: Registered Flee behavior\n";
    }

    // Register Follow behavior
    if (!aiMgr.hasBehavior("Follow")) {
        auto followBehavior = std::make_unique<FollowBehavior>(75.0f, 50.0f, 90.0f); // follow speed, follow distance, max distance
        aiMgr.registerBehavior("Follow", std::move(followBehavior));
        std::cout << "AdvancedAIDemoState: Registered Follow behavior\n";
    }

    // Register Guard behavior
    if (!aiMgr.hasBehavior("Guard")) {
        auto guardBehavior = std::make_unique<GuardBehavior>(Vector2D(m_worldWidth/2, m_worldHeight/2), 150.0f, 200.0f); // position, guard radius, alert radius
        aiMgr.registerBehavior("Guard", std::move(guardBehavior));
        std::cout << "AdvancedAIDemoState: Registered Guard behavior\n";
    }

    // Register Attack behavior
    if (!aiMgr.hasBehavior("Attack")) {
        auto attackBehavior = std::make_unique<AttackBehavior>(80.0f, 1.0f, 85.0f); // range, cooldown, speed
        aiMgr.registerBehavior("Attack", std::move(attackBehavior));
        std::cout << "AdvancedAIDemoState: Registered Attack behavior\n";
    }

    std::cout << "AdvancedAIDemoState: Advanced AI behaviors setup complete.\n";
}

void AdvancedAIDemoState::createAdvancedNPCs() {
    // Cache AIManager reference for better performance
    AIManager& aiMgr = AIManager::Instance();

    try {
        // Random number generation for positioning
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> xDist(80.0f, m_worldWidth - 80.0f);
        std::uniform_real_distribution<float> yDist(80.0f, m_worldHeight - 80.0f);

        // Create NPCs with optimized counts for behavior demonstration
        for (int i = 0; i < m_totalNPCCount; ++i) {
            try {
                // Create NPC with strategic positioning for behavior showcase
                Vector2D position;
                
                // Position NPCs in different areas based on intended behavior
                if (i < m_idleNPCCount) {
                    // Idle NPCs in corners
                    position = Vector2D(
                        (i % 2 == 0) ? 100.0f : m_worldWidth - 100.0f,
                        (i < 2) ? 100.0f : m_worldHeight - 100.0f
                    );
                } else if (i < m_idleNPCCount + m_guardNPCCount) {
                    // Guard NPCs at strategic positions
                    int guardIndex = i - m_idleNPCCount;
                    position = Vector2D(
                        150.0f + (guardIndex * (m_worldWidth - 300.0f) / (m_guardNPCCount - 1)),
                        150.0f + (guardIndex % 2) * (m_worldHeight - 300.0f)
                    );
                } else {
                    // Other NPCs randomly positioned
                    position = Vector2D(xDist(gen), yDist(gen));
                }

                auto npc = std::make_shared<NPC>("npc", position, 64, 64);

                // Set animation properties for smooth movement
                // Use default 100ms animation timing for consistent animations

                // Set wander area to keep NPCs on screen
                npc->setWanderArea(0, 0, m_worldWidth, m_worldHeight);
                npc->setBoundsCheckEnabled(false);

                // Initialize with Follow behavior by default for smooth movement demonstration
                aiMgr.registerEntityForUpdates(npc, 5, "Follow");

                // Setup combat attributes for this NPC
                CombatAttributes combat;
                combat.health = 100.0f;
                combat.maxHealth = 100.0f;
                combat.attackDamage = 15.0f;
                combat.attackRange = 80.0f;
                combat.attackCooldown = 1.5f;
                combat.lastAttackTime = 0.0f;
                combat.isDead = false;
                
                m_combatAttributes[npc] = combat;

                // Add to collection
                m_npcs.push_back(npc);
            } catch (const std::exception& e) {
                std::cerr << "Hammer Game Engine - ERROR: Exception creating advanced NPC " << i << ": " << e.what() << std::endl;
                continue;
            }
        }

        std::cout << "AdvancedAIDemoState: Created " << m_npcs.size() << " NPCs with combat attributes\n";
    } catch (const std::exception& e) {
        std::cerr << "Hammer Game Engine - ERROR: Exception in createAdvancedNPCs(): " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Hammer Game Engine - ERROR: Unknown exception in createAdvancedNPCs()" << std::endl;
    }
}

void AdvancedAIDemoState::setupCombatAttributes() {
    // Setup player combat attributes
    if (m_player) {
        CombatAttributes playerCombat;
        playerCombat.health = 200.0f;
        playerCombat.maxHealth = 200.0f;
        playerCombat.attackDamage = 25.0f;
        playerCombat.attackRange = 100.0f;
        playerCombat.attackCooldown = 0.8f;
        playerCombat.lastAttackTime = 0.0f;
        playerCombat.isDead = false;
        
        m_combatAttributes[m_player] = playerCombat;
    }
}

void AdvancedAIDemoState::updateCombatSystem(float deltaTime) {
    // Simple combat system update
    // This is architecturally integrated but kept simple for demonstration
    
    for (auto& [entity, combat] : m_combatAttributes) {
        if (combat.isDead) continue;
        
        // Update attack cooldowns
        if (combat.lastAttackTime > 0) {
            combat.lastAttackTime -= deltaTime;
            if (combat.lastAttackTime < 0) {
                combat.lastAttackTime = 0;
            }
        }
        
        // Simple health regeneration for demo purposes
        if (combat.health < combat.maxHealth && combat.health > 0) {
            combat.health += 5.0f * deltaTime; // 5 HP per second regen
            if (combat.health > combat.maxHealth) {
                combat.health = combat.maxHealth;
            }
        }
        
        // Check if entity should be marked as dead
        if (combat.health <= 0 && !combat.isDead) {
            combat.isDead = true;
            // In a full implementation, this would trigger death animations/effects
        }
    }
}