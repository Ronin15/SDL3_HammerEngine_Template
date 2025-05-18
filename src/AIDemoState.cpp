/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "AIDemoState.hpp"
#include "AIManager.hpp"
#include "SDL3/SDL_scancode.h"
#include "WanderBehavior.hpp"
#include "PatrolBehavior.hpp"
#include "ChaseBehavior.hpp"
#include "GameEngine.hpp"
#include "TextureManager.hpp"
#include "FontManager.hpp"
#include "InputHandler.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <memory>
#include <random>

AIDemoState::AIDemoState() : m_infoPanel{20, 10, 300, 150} {
    // Initialize with default values
    m_infoText = "AI Demo: Press [B] to exit to main menu\n Press [1-3] to switch behaviors\n Press [SPACE] to pause/resume AI\n [1] Wander [2] Patrol [3] Chase";
}

AIDemoState::~AIDemoState() {
    exit();
}

bool AIDemoState::enter() {
    std::cout << "Entering AIDemoState...\n";

    // Setup window size
    m_worldWidth = GameEngine::Instance().getWindowWidth();
    m_worldHeight = GameEngine::Instance().getWindowHeight();


    // If npc_sprite.png doesn't exist, use the player sprite as a fallback
    // In a real implementation, you should create a dedicated NPC sprite
    // with multiple animation frames in res/img/npc_sprite.png
    TextureManager::Instance().load("res/img/player.png", "npc", GameEngine::Instance().getRenderer());

    // Create AI behaviors
    // Setup AI behaviors
    setupAIBehaviors();

    // Create player first (the chase behavior will need it)
    m_player = std::make_unique<Player>();
    m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

    // Create NPCs with AI behaviors
    createNPCs();

    // Log status
    std::cout << "Created " << m_npcs.size() << " NPCs with AI behaviors\n";

    return true;
}

bool AIDemoState::exit() {
    std::cout << "Exiting AIDemoState...\n";

    // Clean up NPCs
    m_npcs.clear();

    // Clean up player
    m_player.reset();

    // Clean up AI Manager
    AIManager::Instance().clean();

    return true;
}

void AIDemoState::update() {
    // Update player
    if (m_player) {
        m_player->update();
    }

    // Log the player position periodically for debugging chase behavior
    static int frameCount = 0;
    if (frameCount++ % 60 == 0 && m_player) { // Log every ~1 second
        std::cout << "Player position: (" 
                  << m_player->getPosition().getX() << ", " 
                  << m_player->getPosition().getY() << ")\n";
    }

    // NPCs are updated through AIManager, but we still check for removals or other status changes here
    for (size_t i = 0; i < m_npcs.size(); i++) {
        auto& npc = m_npcs[i];
        if (!npc) continue;
    
        // Store previous position for movement debugging
        Vector2D prevPos = npc->getPosition();
    
        // Call the NPC's update method to handle animation and other entity-specific logic
        npc->update();
    
        // Log NPC position and movement periodically
        if (frameCount % 60 == 0) { // Same as player logging frequency
            Vector2D currPos = npc->getPosition();
            Vector2D velocity = npc->getVelocity();
            std::cout << "NPC " << i << " position: (" 
                      << currPos.getX() << ", " << currPos.getY() 
                      << "), velocity: (" << velocity.getX() << ", " << velocity.getY() << ")\n";
                  
            // Check if entity has moved since last frame
            if ((currPos - prevPos).length() < 0.1f && velocity.length() > 0.1f) {
                std::cout << "WARNING: NPC " << i << " has velocity but didn't move!\n";
            }
        }
    
        // Make sure NPCs stay on screen by checking bounds
        Vector2D pos = npc->getPosition();
        if (pos.getX() < 0 || pos.getY() < 0 || 
            pos.getX() > m_worldWidth || pos.getY() > m_worldHeight) {
            // Log and reset to center if off-screen
            std::cout << "NPC " << i << " went off-screen! Resetting position.\n";
            npc->setPosition(Vector2D(m_worldWidth/2, m_worldHeight/2));
        }
    }

    // Handle user input for the demo
    if (InputHandler::Instance().isKeyDown(SDL_SCANCODE_B)) {
        GameEngine::Instance().getGameStateManager()->setState("MainMenuState");
    }

    // Toggle AI behaviors
    static int lastKey = 0;

    if (InputHandler::Instance().isKeyDown(SDL_SCANCODE_1) && lastKey != 1) {
        // Assign Wander behavior to all NPCs
        std::cout << "Switching all NPCs to WANDER behavior\n";
        for (auto& npc : m_npcs) {
            AIManager::Instance().assignBehaviorToEntity(npc.get(), "Wander");
        }
        lastKey = 1;
    } else if (InputHandler::Instance().isKeyDown(SDL_SCANCODE_2) && lastKey != 2) {
        // Assign Patrol behavior to all NPCs
        std::cout << "Switching all NPCs to PATROL behavior\n";
        for (auto& npc : m_npcs) {
            AIManager::Instance().assignBehaviorToEntity(npc.get(), "Patrol");
        }
        lastKey = 2;
    } else if (InputHandler::Instance().isKeyDown(SDL_SCANCODE_3) && lastKey != 3) {
        // Assign Chase behavior to all NPCs
        std::cout << "Switching all NPCs to CHASE behavior\n";
    
        // Make sure chase behavior has the current player target
        auto chaseBehavior = dynamic_cast<ChaseBehavior*>(AIManager::Instance().getBehavior("Chase"));
        if (chaseBehavior && m_player) {
            chaseBehavior->setTarget(m_player.get());
            std::cout << "Updated chase target to player at (" 
                      << m_player->getPosition().getX() << ", " 
                      << m_player->getPosition().getY() << ")\n";
        }
    
        for (auto& npc : m_npcs) {
            AIManager::Instance().assignBehaviorToEntity(npc.get(), "Chase");
        }
        lastKey = 3;
    }

    // Reset key state if no behavior key is pressed
    if (!InputHandler::Instance().isKeyDown(SDL_SCANCODE_1) && 
        !InputHandler::Instance().isKeyDown(SDL_SCANCODE_2) && 
        !InputHandler::Instance().isKeyDown(SDL_SCANCODE_3)) {
        lastKey = 0;
    }

    // Pause/Resume AI
    static bool wasSpacePressed = false;
    bool isSpacePressed = InputHandler::Instance().isKeyDown(SDL_SCANCODE_SPACE);

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
    if (m_showDebugInfo) {
        SDL_SetRenderDrawColor(GameEngine::Instance().getRenderer(), 0, 0, 0, 200);
        SDL_RenderFillRect(GameEngine::Instance().getRenderer(), &m_infoPanel);

        FontManager::Instance().drawText(m_infoText,
                                    "fonts_Arial",
                                    m_infoPanel.x + 10,
                                    m_infoPanel.y + 10,
                                    {255, 255, 255, 255},
                                    GameEngine::Instance().getRenderer());
    }
}

void AIDemoState::setupAIBehaviors() {
    std::cout << "Setting up AI behaviors...\n";
    
    // Clean up any existing behaviors first
    AIManager::Instance().clean();
    AIManager::Instance().init();
    
    // Create and register wander behavior
    auto wanderBehavior = std::make_unique<WanderBehavior>(2.0f, 3000.0f, 200.0f);
    std::cout << "Created WanderBehavior with speed 2.0, interval 3000, radius 200\n";
    AIManager::Instance().registerBehavior("Wander", std::move(wanderBehavior));

    // Create and register patrol behavior with screen-relative coordinates
    std::vector<Vector2D> patrolPoints;
    patrolPoints.push_back(Vector2D(m_worldWidth * 0.2f, m_worldHeight * 0.2f));
    patrolPoints.push_back(Vector2D(m_worldWidth * 0.8f, m_worldHeight * 0.2f));
    patrolPoints.push_back(Vector2D(m_worldWidth * 0.8f, m_worldHeight * 0.8f));
    patrolPoints.push_back(Vector2D(m_worldWidth * 0.2f, m_worldHeight * 0.8f));
    
    std::cout << "Created PatrolBehavior with " << patrolPoints.size() << " waypoints at corners of screen\n";
    auto patrolBehavior = std::make_unique<PatrolBehavior>(patrolPoints, 2.0f);
    AIManager::Instance().registerBehavior("Patrol", std::move(patrolBehavior));

    // Create and register chase behavior
    auto chaseBehavior = std::make_unique<ChaseBehavior>(nullptr, 3.0f, 300.0f, 50.0f);
    std::cout << "Created ChaseBehavior with speed 3.0, max range 300, min range 50\n";
    AIManager::Instance().registerBehavior("Chase", std::move(chaseBehavior));
    
    std::cout << "AI behaviors setup complete.\n";
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
            std::cout << "Chase behavior target set to player at position (" 
                      << m_player->getPosition().getX() << ", " 
                      << m_player->getPosition().getY() << ")\n";
        } else {
            std::cout << "ERROR: Could not set chase target - " 
                      << (chaseBehavior ? "Player is null" : "ChaseBehavior is null") << "\n";
        }
    } else {
        std::cout << "ERROR: Chase behavior not found when setting target\n";
    }
}
