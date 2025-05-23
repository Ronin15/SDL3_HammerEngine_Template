/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/AIDemoState.hpp"
#include "managers/AIManager.hpp"
#include "SDL3/SDL_scancode.h"
#include "ai/behaviors/WanderBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/ChaseBehavior.hpp"
#include "core/GameEngine.hpp"
#include "managers/FontManager.hpp"
#include "managers/InputManager.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <memory>
#include <random>

AIDemoState::~AIDemoState() {
    // Don't call virtual functions from destructors
    
    // Clean up NPCs
    m_npcs.clear();

    // Clean up player
    m_player.reset();

    // Reset all AI behaviors, but keep the manager initialized
    AIManager::Instance().resetBehaviors();
    
    std::cout << "Forge Game Engine - Exiting AIDemoState in destructor...\n";
}

bool AIDemoState::enter() {
    std::cout << "Forge Game Engine - Entering AIDemoState...\n";

    // Setup window size
    m_worldWidth = GameEngine::Instance().getWindowWidth();
    m_worldHeight = GameEngine::Instance().getWindowHeight();

    //Texture has to be loaded by NPC or Player can't be loaded here
    setupAIBehaviors();

    // Create player first (the chase behavior will need it)
    m_player = std::make_unique<Player>();
    m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

    // Create NPCs with AI behaviors
    createNPCs();

    // Log status
    std::cout << "Forge Game Engine - Created " << m_npcs.size() << " NPCs with AI behaviors\n";

    return true;
}

bool AIDemoState::exit() {
    std::cout << "Forge Game Engine - Exiting AIDemoState...\n";

    // Clean up NPCs
    m_npcs.clear();

    // Clean up player
    m_player.reset();

    // Reset all AI behaviors, but keep the manager initialized
    AIManager::Instance().resetBehaviors();

    return true;
}

void AIDemoState::update() {
    // No need to track frame count now that we've removed logging

    // Update player
    if (m_player) {
        m_player->update();
    }

    // NPCs are updated through AIManager, but we still check for removals or other status changes here
    for (size_t i = 0; i < m_npcs.size(); i++) {
        auto& npc = m_npcs[i];
        if (!npc) continue;

        // Call the NPC's update method to handle animation and other entity-specific logic
        npc->update();

        // Note: Letting NPCs go off-screen is now managed by the behaviors
        // They'll handle the reset logic when they go far enough off-screen
    }

    // Handle user input for the demo
    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_B)) {
        GameEngine::Instance().getGameStateManager()->setState("MainMenuState");
    }

    // Toggle AI behaviors
    static int lastKey = 0;

    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_1) && lastKey != 1) {
        // Assign Wander behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to WANDER behavior\n";
        for (auto& npc : m_npcs) {
            AIManager::Instance().assignBehaviorToEntity(npc.get(), "Wander");
        }
        lastKey = 1;
    } else if (InputManager::Instance().isKeyDown(SDL_SCANCODE_2) && lastKey != 2) {
        // Assign Patrol behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to PATROL behavior\n";
        for (auto& npc : m_npcs) {
            AIManager::Instance().assignBehaviorToEntity(npc.get(), "Patrol");
        }
        lastKey = 2;
    } else if (InputManager::Instance().isKeyDown(SDL_SCANCODE_3) && lastKey != 3) {
        // Assign Chase behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to CHASE behavior\n";

        // Make sure chase behavior has the current player target
        auto chaseBehavior = dynamic_cast<ChaseBehavior*>(AIManager::Instance().getBehavior("Chase"));
        if (chaseBehavior && m_player) {
            chaseBehavior->setTarget(m_player.get());

        }

        for (auto& npc : m_npcs) {
            AIManager::Instance().assignBehaviorToEntity(npc.get(), "Chase");
        }
        lastKey = 3;
    }

    // Reset key state if no behavior key is pressed
    if (!InputManager::Instance().isKeyDown(SDL_SCANCODE_1) &&
        !InputManager::Instance().isKeyDown(SDL_SCANCODE_2) &&
        !InputManager::Instance().isKeyDown(SDL_SCANCODE_3)) {
        lastKey = 0;
    }

    // Pause/Resume AI
    static bool wasSpacePressed = false;
    bool isSpacePressed = InputManager::Instance().isKeyDown(SDL_SCANCODE_SPACE);

    if (isSpacePressed && !wasSpacePressed) {
        // Toggle pause/resume
        static bool aiPaused = false;
        aiPaused = !aiPaused;

        // Send appropriate message to all entities
        AIManager::Instance().broadcastMessage(aiPaused ? "pause" : "resume");
    }

    wasSpacePressed = isSpacePressed;
}

void AIDemoState::render() {
    // Render all NPCs
    for (auto& npc : m_npcs) {
        npc->render();
    }

    // Render player
    if (m_player) {
        m_player->render();
    }

    // Render info panel
        FontManager::Instance().drawText("AI Demo: Press [B] to exit to main menu. Press [1-3] to switch behaviors. Press [SPACE] to pause/resume AI. [1] Wander [2] Patrol [3] Chase",
                                    "fonts_Arial",
                                    GameEngine::Instance().getWindowWidth() / 2,     // Center horizontally
                                    20,
                                    {255, 255, 255, 255},
                                    GameEngine::Instance().getRenderer());
}

void AIDemoState::setupAIBehaviors() {
    std::cout << "Forge Game Engine - Setting up AI behaviors...\n";

    // Clean up any existing behaviors first
    AIManager::Instance().resetBehaviors();
    // No need to call init() after clean() anymore as it now preserves initialization state

    // Create and register wander behavior
    auto wanderBehavior = std::make_unique<WanderBehavior>(2.0f, 3000.0f, 200.0f);
    wanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    wanderBehavior->setOffscreenProbability(0.2f); // 20% chance to wander offscreen
    std::cout << "Forge Game Engine - Created WanderBehavior with speed 2.0, interval 3000, radius 200, offscreen probability 0.2\n";
    AIManager::Instance().registerBehavior("Wander", std::move(wanderBehavior));

    // Create and register patrol behavior with screen-relative coordinates
    boost::container::small_vector<Vector2D, 10> patrolPoints;
    patrolPoints.push_back(Vector2D(m_worldWidth * 0.2f, m_worldHeight * 0.2f));
    patrolPoints.push_back(Vector2D(m_worldWidth * 0.8f, m_worldHeight * 0.2f));
    patrolPoints.push_back(Vector2D(m_worldWidth * 0.8f, m_worldHeight * 0.8f));
    patrolPoints.push_back(Vector2D(m_worldWidth * 0.2f, m_worldHeight * 0.8f));

    // Add one offscreen waypoint to force entities off-screen
    patrolPoints.push_back(Vector2D(-100.0f, m_worldHeight * 0.5f));  // Off the left side

    std::cout << "Forge Game Engine - Created PatrolBehavior with " << patrolPoints.size() << " waypoints at corners and one offscreen\n";

    auto patrolBehavior = std::make_unique<PatrolBehavior>(patrolPoints, 1.5f, true);  // Reduced speed to 1.5, enable offscreen
    patrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    AIManager::Instance().registerBehavior("Patrol", std::move(patrolBehavior));

    // Create and register chase behavior
    auto chaseBehavior = std::make_unique<ChaseBehavior>(nullptr, 2.0f, 500.0f, 50.0f);  // Reduced speed to 2.0, range to 500
    std::cout << "Forge Game Engine - Created ChaseBehavior with speed 2.0, max range 500, min range 50\n";
    AIManager::Instance().registerBehavior("Chase", std::move(chaseBehavior));

    std::cout << "Forge Game Engine - AI behaviors setup complete.\n";
}

void AIDemoState::createNPCs() {
    // Random number generation for positioning
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> xDist(50.0f, m_worldWidth - 50.0f);
    std::uniform_real_distribution<float> yDist(50.0f, m_worldHeight - 50.0f);

    // Create NPCs
    for (int i = 0; i < m_npcCount; ++i) {
        // Create NPC with random position
        Vector2D position(xDist(gen), yDist(gen));
        auto npc = std::make_unique<NPC>("npc", position, 64, 64);

        // Set animation properties (adjust based on your actual sprite sheet)
        npc->setAnimSpeed(150);

        // Set wander area to keep NPCs on screen
        npc->setWanderArea(0, 0, m_worldWidth, m_worldHeight);

        // Assign default behavior (Wander)
        AIManager::Instance().assignBehaviorToEntity(npc.get(), "Wander");

        // Add to collection
        m_npcs.push_back(std::move(npc));
    }

    // Set player as the chase target for the chase behavior
    if (AIManager::Instance().hasBehavior("Chase")) {
        auto chaseBehavior = dynamic_cast<ChaseBehavior*>(AIManager::Instance().getBehavior("Chase"));
        if (chaseBehavior && m_player) {
            chaseBehavior->setTarget(m_player.get());
        } else {
            std::cerr << "Forge Game Engine - Could not set chase target - "
                      << (chaseBehavior ? "Player is null" : "ChaseBehavior is null") << std::endl;
        }
    } else {
        std::cerr << "Forge Game Engine - Chase behavior not found when setting target" << std::endl;
    }
}
