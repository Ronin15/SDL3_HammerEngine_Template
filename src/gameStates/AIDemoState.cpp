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
#include <numeric>
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

        // Chase behavior cleanup is now handled by AIManager

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
        m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

        // Set player reference in AIManager for distance optimization
        AIManager::Instance().setPlayerForDistanceOptimization(m_player);

        // Create and register chase behavior - behaviors can get player via getPlayerReference()
        auto chaseBehavior = std::make_unique<ChaseBehavior>(2.0f, 500.0f, 50.0f);
        AIManager::Instance().registerBehavior("Chase", std::move(chaseBehavior));
        std::cout << "Forge Game Engine - Chase behavior registered (will use AIManager::getPlayerReference())\n";

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
            // Chase behavior cleanup handled by AIManager
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

    // Chase behavior cleanup is now handled by AIManager

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

        // Update AI Manager
        AIManager::Instance().update();

        // Entity updates are now handled by AIManager::update()
        // No need to manually update NPCs here

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
                std::cout << "Forge Game Engine - Chase behavior cleanup handled by AIManager...\n";

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
            // Queue the behavior assignment for batch processing
            AIManager::Instance().queueBehaviorAssignment(npc, "Wander");
        }
        lastKey = 1;
    } else if (InputManager::Instance().isKeyDown(SDL_SCANCODE_2) && lastKey != 2) {
        // Assign Patrol behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to PATROL behavior\n";
        for (auto& npc : m_npcs) {
            // Queue the behavior assignment for batch processing
            AIManager::Instance().queueBehaviorAssignment(npc, "Patrol");
        }
        lastKey = 2;
    } else if (InputManager::Instance().isKeyDown(SDL_SCANCODE_3) && lastKey != 3) {
        // Assign Chase behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to CHASE behavior\n";

        // Chase behavior target is automatically maintained by AIManager
        // No manual target updates needed

        for (auto& npc : m_npcs) {
            // Queue the behavior assignment for batch processing
            AIManager::Instance().queueBehaviorAssignment(npc, "Chase");
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
    bool isSpacePressed = InputManager::Instance().isKeyDown(SDL_SCANCODE_SPACE);

    if (isSpacePressed && !m_wasSpacePressed) {
        // Toggle pause/resume
        m_aiPaused = !m_aiPaused;

        // Set global AI pause state in AIManager
        AIManager::Instance().setGlobalPause(m_aiPaused);

        // Also send messages for behaviors that need them
        std::string message = m_aiPaused ? "pause" : "resume";
        AIManager::Instance().broadcastMessage(message, true);

        // Simple feedback
        std::cout << "Forge Game Engine - AI " << (m_aiPaused ? "PAUSED" : "RESUMED") << std::endl;
    }

    m_wasSpacePressed = isSpacePressed;
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

    // Render frame rate and AI status
    bool globallyPaused = AIManager::Instance().isGloballyPaused();
    std::stringstream fpsText;
    fpsText << "FPS: " << std::fixed << std::setprecision(1) << m_currentFPS
            << " (Avg: " << std::setprecision(1) << m_averageFPS << ")"
            << " - Entity Count: " << m_npcs.size()
            << " - AI: " << (globallyPaused ? "PAUSED" : "RUNNING");

    FontManager::Instance().drawText(fpsText.str(),
                                "fonts_Arial",
                                GameEngine::Instance().getWindowWidth() / 2,     // Center horizontally
                                50,
                                globallyPaused ? SDL_Color{255, 100, 100, 255} : SDL_Color{255, 255, 255, 255},
                                GameEngine::Instance().getRenderer());
}

void AIDemoState::setupAIBehaviors() {
    std::cout << "AIDemoState: Setting up AI behaviors using EventDemoState implementation...\n";

    if (!AIManager::Instance().hasBehavior("Wander")) {
        auto wanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f);
        wanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        AIManager::Instance().registerBehavior("Wander", std::move(wanderBehavior));
        std::cout << "AIDemoState: Registered Wander behavior\n";
    }

    if (!AIManager::Instance().hasBehavior("SmallWander")) {
        auto smallWanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::SMALL_AREA, 1.5f);
        smallWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        AIManager::Instance().registerBehavior("SmallWander", std::move(smallWanderBehavior));
        std::cout << "AIDemoState: Registered SmallWander behavior\n";
    }

    if (!AIManager::Instance().hasBehavior("LargeWander")) {
        auto largeWanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::LARGE_AREA, 2.5f);
        largeWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        AIManager::Instance().registerBehavior("LargeWander", std::move(largeWanderBehavior));
        std::cout << "AIDemoState: Registered LargeWander behavior\n";
    }

    if (!AIManager::Instance().hasBehavior("EventWander")) {
        auto eventWanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::EVENT_TARGET, 2.0f);
        eventWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        AIManager::Instance().registerBehavior("EventWander", std::move(eventWanderBehavior));
        std::cout << "AIDemoState: Registered EventWander behavior\n";
    }

    if (!AIManager::Instance().hasBehavior("Patrol")) {
        auto patrolBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 1.5f, true);
        patrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        AIManager::Instance().registerBehavior("Patrol", std::move(patrolBehavior));
        std::cout << "AIDemoState: Registered Patrol behavior\n";
    }

    if (!AIManager::Instance().hasBehavior("RandomPatrol")) {
        auto randomPatrolBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f, false);
        randomPatrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        AIManager::Instance().registerBehavior("RandomPatrol", std::move(randomPatrolBehavior));
        std::cout << "AIDemoState: Registered RandomPatrol behavior\n";
    }

    if (!AIManager::Instance().hasBehavior("CirclePatrol")) {
        auto circlePatrolBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::CIRCULAR_AREA, 1.8f, false);
        circlePatrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        AIManager::Instance().registerBehavior("CirclePatrol", std::move(circlePatrolBehavior));
        std::cout << "AIDemoState: Registered CirclePatrol behavior\n";
    }

    if (!AIManager::Instance().hasBehavior("EventTarget")) {
        auto eventTargetBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::EVENT_TARGET, 2.2f, false);
        eventTargetBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        AIManager::Instance().registerBehavior("EventTarget", std::move(eventTargetBehavior));
        std::cout << "AIDemoState: Registered EventTarget behavior\n";
    }

    // Chase behavior will be set up after player is created in enter() method
    // This ensures the player reference is available for behaviors to use

    std::cout << "AIDemoState: AI behaviors setup complete.\n";
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

        // Calculate average FPS using std::accumulate
        float sum = std::accumulate(m_frameTimes.begin(), m_frameTimes.end(), 0.0f);
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

        // Chase behavior target is now automatically handled by AIManager
        // No manual setup needed - target is set during setupChaseBehaviorWithTarget()
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - ERROR: Exception in createNPCs(): " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Forge Game Engine - ERROR: Unknown exception in createNPCs()" << std::endl;
    }
}
