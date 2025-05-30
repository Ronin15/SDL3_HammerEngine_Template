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
#include <sstream>
#include <iomanip>

AIDemoState::~AIDemoState() {
    // Don't call virtual functions from destructors

    try {
        // Note: Proper cleanup should already have happened in exit()
        // This destructor is just a safety measure in case exit() wasn't called

        // Safe cleanup for any remaining chase behavior references
        if (m_chaseBehavior) {
            m_chaseBehavior->setTarget(nullptr);
            m_chaseBehavior = nullptr;
        }

        // Reset AI behaviors first to clear entity references
        // Don't call unassignBehaviorFromEntity here - it uses shared_from_this()
        AIManager::Instance().resetBehaviors();

        // Clear NPCs without calling clean() on them
        m_npcs.clear();

        // Clean up player
        m_player.reset();

        std::cout << "Forge Game Engine - Exiting AIDemoState in destructor...\n";
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - Exception in AIDemoState destructor: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Forge Game Engine - Unknown exception in AIDemoState destructor" << std::endl;
    }
}

bool AIDemoState::enter() {
    std::cout << "Forge Game Engine - Entering AIDemoState...\n";

    try {
        // Setup window size
        m_worldWidth = GameEngine::Instance().getWindowWidth();
        m_worldHeight = GameEngine::Instance().getWindowHeight();

        //Texture has to be loaded by NPC or Player can't be loaded here
        setupAIBehaviors();

        // Create player first (the chase behavior will need it)
        m_player = std::make_shared<Player>();
        if (!m_player) {
            std::cerr << "Forge Game Engine - ERROR: Failed to create player!\n";
            return false;
        }
        m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

        // Set player reference in AIManager for distance optimization
        AIManager::Instance().setPlayerForDistanceOptimization(m_player);

        // Configure priority multiplier for proper distance progression (1.0 = full distance thresholds)
        AIManager::Instance().configurePriorityMultiplier(1.0f);

        // Create NPCs with AI behaviors
        createNPCs();

        // Initialize frame rate counter
        m_lastFrameTime = std::chrono::steady_clock::now();
        m_frameTimes.clear();
        m_frameCount = 0;
        m_currentFPS = 0.0f;
        m_averageFPS = 0.0f;

        // Log status
        std::cout << "Forge Game Engine - Created " << m_npcs.size() << " NPCs with AI behaviors\n";

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - ERROR: Exception in AIDemoState::enter(): " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "Forge Game Engine - ERROR: Unknown exception in AIDemoState::enter()" << std::endl;
        return false;
    }
}

bool AIDemoState::exit() {
    std::cout << "Forge Game Engine - Exiting AIDemoState...\n";

    // First clear entity references from behaviors
    if (AIManager::Instance().hasBehavior("Chase")) {
        auto chaseBehaviorPtr = AIManager::Instance().getBehavior("Chase");
        auto chaseBehavior = std::dynamic_pointer_cast<ChaseBehavior>(chaseBehaviorPtr);
        if (chaseBehavior) {
            chaseBehavior->setTarget(nullptr);
        }
    }

    // First unregister all NPCs from AIManager before calling clean()
    std::cout << "Forge Game Engine - Unregistering NPCs from AIManager before cleanup\n";
    for (auto& npc : m_npcs) {
        if (npc) {
            // Unregister from AIManager first to avoid shared_from_this() issues
            AIManager::Instance().unregisterEntityFromUpdates(npc);

            // Unassign behavior if it has one
            if (AIManager::Instance().entityHasBehavior(npc)) {
                AIManager::Instance().unassignBehaviorFromEntity(npc);
            }

            // Now safe to call clean() and stop movement
            npc->clean();
            npc->setVelocity(Vector2D(0, 0));
        }
    }

    // Send release message to all behaviors
    AIManager::Instance().broadcastMessage("release_entities", true);
    AIManager::Instance().processMessageQueue();

    // Reset all AI behaviors to clear entity references
    // This ensures behaviors release their entity references before the entities are destroyed
    AIManager::Instance().resetBehaviors();

    // Clean up NPCs
    m_npcs.clear();

    // Clean up player
    if (m_player) {
        m_player.reset();
    }

    // Null out our behavior reference
    m_chaseBehavior = nullptr;

    std::cout << "Forge Game Engine - AIDemoState exit complete\n";
    return true;
}

void AIDemoState::update() {
    try {
        // Update frame rate counter
        updateFrameRate();

        // Update player
        if (m_player) {
            m_player->update();
        }

        // Let AIManager handle all NPC updates (movement + AI logic with distance optimization)
        AIManager::Instance().updateManagedEntities();

        // Check for any NPC-specific status changes if needed
        for (size_t i = 0; i < m_npcs.size(); i++) {
            auto& npc = m_npcs[i];
            if (!npc) continue;

            try {
                // Entity updates now handled by AIManager - just check status here if needed
            } catch (const std::exception& e) {
                std::cerr << "Forge Game Engine - ERROR: Exception checking NPC " << i << ": " << e.what() << std::endl;
                continue;
            }

            // Note: Letting NPCs go off-screen is now managed by the behaviors
            // They'll handle the reset logic when they go far enough off-screen
        }

        // Handle user input for the demo
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - ERROR: Exception in AIDemoState::update(): " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Forge Game Engine - ERROR: Unknown exception in AIDemoState::update()" << std::endl;
    }
    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_B)) {
        std::cout << "Forge Game Engine - Preparing to exit AIDemoState...\n";

        // First call clean() on all NPCs to properly handle unassignment
        for (auto& npc : m_npcs) {
            if (npc) {
                // Unregister from AIManager entity updates
                AIManager::Instance().unregisterEntityFromUpdates(npc);
                // Call clean() which will handle unassignment safely
                npc->clean();
                // Also stop the entity's movement
                npc->setVelocity(Vector2D(0, 0));
            }
        }

        // Set chase behavior target to nullptr to avoid dangling reference
        if (AIManager::Instance().hasBehavior("Chase")) {
            auto chaseBehaviorPtr = AIManager::Instance().getBehavior("Chase");
            auto chaseBehavior = std::dynamic_pointer_cast<ChaseBehavior>(chaseBehaviorPtr);
            if (chaseBehavior) {
                std::cout << "Forge Game Engine - Clearing chase behavior target...\n";
                chaseBehavior->setTarget(nullptr);

                // Ensure chase behavior has no references to any entities
                chaseBehavior->clean(nullptr);
            }
        }

        // Make sure all AI behavior references are cleared
        AIManager::Instance().broadcastMessage("release_entities", true);

        // Force a flush of the message queue to ensure all messages are processed
        AIManager::Instance().processMessageQueue();

        std::cout << "Forge Game Engine - Transitioning to MainMenuState...\n";
        GameEngine::Instance().getGameStateManager()->setState("MainMenuState");
    }

    // Toggle AI behaviors
    static int lastKey = 0;

    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_1) && lastKey != 1) {
        // Assign Wander behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to WANDER behavior\n";
        for (auto& npc : m_npcs) {
            // First make sure we call clean() to properly unassign any existing behavior
            npc->clean();
            // Register with AIManager for entity updates and assign the new behavior
            AIManager::Instance().registerEntityForUpdates(npc, 5);
            AIManager::Instance().assignBehaviorToEntity(npc, "Wander");
        }
        lastKey = 1;
    } else if (InputManager::Instance().isKeyDown(SDL_SCANCODE_2) && lastKey != 2) {
        // Assign Patrol behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to PATROL behavior\n";
        for (auto& npc : m_npcs) {
            // First make sure we call clean() to properly unassign any existing behavior
            npc->clean();
            // Register with AIManager for entity updates and assign the new behavior
            AIManager::Instance().registerEntityForUpdates(npc, 5);
            AIManager::Instance().assignBehaviorToEntity(npc, "Patrol");
        }
        lastKey = 2;
    } else if (InputManager::Instance().isKeyDown(SDL_SCANCODE_3) && lastKey != 3) {
        // Assign Chase behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to CHASE behavior\n";

        // Make sure chase behavior has the current player target
        auto chaseBehaviorPtr = AIManager::Instance().getBehavior("Chase");
        auto chaseBehavior = std::dynamic_pointer_cast<ChaseBehavior>(chaseBehaviorPtr);
        if (chaseBehavior && m_player) {
            chaseBehavior->setTarget(m_player);
        }

        for (auto& npc : m_npcs) {
            // First make sure we call clean() to properly unassign any existing behavior
            npc->clean();
            // Register with AIManager for entity updates and assign the new behavior
            AIManager::Instance().registerEntityForUpdates(npc, 5);
            AIManager::Instance().assignBehaviorToEntity(npc, "Chase");
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

    // Render frame rate
    std::stringstream fpsText;
    fpsText << "FPS: " << std::fixed << std::setprecision(1) << m_currentFPS
            << " (Avg: " << std::setprecision(1) << m_averageFPS << ")"
            << " - Entity Count: " << m_npcs.size();

    FontManager::Instance().drawText(fpsText.str(),
                                "fonts_Arial",
                                GameEngine::Instance().getWindowWidth() / 2,     // Center horizontally
                                50,
                                {255, 255, 0, 255},  // Yellow color for better visibility
                                GameEngine::Instance().getRenderer());
}

void AIDemoState::setupAIBehaviors() {
    std::cout << "Forge Game Engine - Setting up AI behaviors...\n";

    // Clean up any existing behaviors first
    AIManager::Instance().resetBehaviors();

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

void AIDemoState::updateFrameRate() {
    // Calculate time since last frame
    auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float, std::chrono::milliseconds::period>(currentTime - m_lastFrameTime).count();
    m_lastFrameTime = currentTime;

    // Convert to seconds for FPS calculation
    float deltaTimeSeconds = deltaTime / 1000.0f;

    // Skip extreme values that might be from debugging pauses
    if (deltaTimeSeconds > 0.0f && deltaTimeSeconds < 1.0f) {
        // Calculate current FPS
        m_currentFPS = 1.0f / deltaTimeSeconds;

        // Add to rolling average
        m_frameTimes.push_back(m_currentFPS);

        // Keep only the last MAX_FRAME_SAMPLES frames
        if (m_frameTimes.size() > MAX_FRAME_SAMPLES) {
            m_frameTimes.pop_front();
        }

        // Calculate average FPS
        float sum = 0.0f;
        for (float fps : m_frameTimes) {
            sum += fps;
        }
        m_averageFPS = sum / m_frameTimes.size();
    }

    // Increment frame counter
    m_frameCount++;
}

void AIDemoState::createNPCs() {
    try {
        // Random number generation for positioning
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> xDist(50.0f, m_worldWidth - 50.0f);
        std::uniform_real_distribution<float> yDist(50.0f, m_worldHeight - 50.0f);

        // Create NPCs
        for (int i = 0; i < m_npcCount; ++i) {
            try {
                // Create NPC with random position
                Vector2D position(xDist(gen), yDist(gen));
                auto npc = std::make_shared<NPC>("npc", position, 64, 64);

                if (!npc) {
                    std::cerr << "Forge Game Engine - ERROR: Failed to create NPC " << i << "\n";
                    continue;
                }

                // Set animation properties (adjust based on your actual sprite sheet)
                npc->setAnimSpeed(150);

                // Set wander area to keep NPCs on screen
                npc->setWanderArea(0, 0, m_worldWidth, m_worldHeight);

                // Register with AIManager for centralized entity updates with priority
                AIManager::Instance().registerEntityForUpdates(npc, 5);

                // Assign default behavior (Wander)
                AIManager::Instance().assignBehaviorToEntity(npc, "Wander");

                // Add to collection
                m_npcs.push_back(npc);
            } catch (const std::exception& e) {
                std::cerr << "Forge Game Engine - ERROR: Exception creating NPC " << i << ": " << e.what() << std::endl;
                continue;
            }
        }

        // Set player as the chase target for the chase behavior - do this last
        if (AIManager::Instance().hasBehavior("Chase")) {
            auto chaseBehaviorPtr = AIManager::Instance().getBehavior("Chase");
            auto chaseBehavior = std::dynamic_pointer_cast<ChaseBehavior>(chaseBehaviorPtr);
            if (chaseBehavior && m_player) {
                // Store the behavior for easier cleanup BEFORE setting target
                m_chaseBehavior = chaseBehavior;
                chaseBehavior->setTarget(m_player);
                std::cout << "Forge Game Engine - Chase behavior target set to player\n";
            } else {
                std::cerr << "Forge Game Engine - Could not set chase target - "
                          << (chaseBehavior ? "Player is null" : "ChaseBehavior is null") << std::endl;
            }
        } else {
            std::cerr << "Forge Game Engine - Chase behavior not found when setting target" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - ERROR: Exception in createNPCs(): " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Forge Game Engine - ERROR: Unknown exception in createNPCs()" << std::endl;
    }
}
