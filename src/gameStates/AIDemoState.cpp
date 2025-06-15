/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/AIDemoState.hpp"
#include "core/Logger.hpp"
#include "managers/AIManager.hpp"
#include "SDL3/SDL_scancode.h"
#include "ai/behaviors/WanderBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/ChaseBehavior.hpp"
#include "core/GameEngine.hpp"
#include "managers/UIManager.hpp"
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

        // Chase behavior cleanup is now handled by AIManager

        // Reset AI behaviors first to clear entity references
        // Don't call unassignBehaviorFromEntity here - it uses shared_from_this()
        // Cache AIManager reference for better performance
        AIManager& aiMgr = AIManager::Instance();
        aiMgr.resetBehaviors();

        // Clear NPCs without calling clean() on them
        m_npcs.clear();

        // Clean up player
        m_player.reset();

        GAMESTATE_INFO("Exiting AIDemoState in destructor...");
    } catch (const std::exception& e) {
        GAMESTATE_ERROR("Exception in AIDemoState destructor: " + std::string(e.what()));
    } catch (...) {
        GAMESTATE_ERROR("Unknown exception in AIDemoState destructor");
    }
}

void AIDemoState::handleInput() {
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
        std::cout << "Forge Game Engine - AI " << (m_aiPaused ? "PAUSED" : "RESUMED") << std::endl;
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
        std::cout << "Forge Game Engine - Preparing to exit AIDemoState...\n";
        const GameEngine& gameEngine = GameEngine::Instance();
        gameEngine.getGameStateManager()->setState("MainMenuState");
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_1)) {
        // Assign Wander behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to WANDER behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            // Queue the behavior assignment for batch processing
            aiMgr.queueBehaviorAssignment(npc, "Wander");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_2)) {
        // Assign Patrol behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to PATROL behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            // Queue the behavior assignment for batch processing
            aiMgr.queueBehaviorAssignment(npc, "Patrol");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_3)) {
        // Assign Chase behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to CHASE behavior\n";

        // Chase behavior target is automatically maintained by AIManager
        // No manual target updates needed
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            // Queue the behavior assignment for batch processing
            aiMgr.queueBehaviorAssignment(npc, "Chase");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_4)) {
        // Assign SmallWander behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to SMALL WANDER behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            // Queue the behavior assignment for batch processing
            aiMgr.queueBehaviorAssignment(npc, "SmallWander");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_5)) {
        // Assign LargeWander behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to LARGE WANDER behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            // Queue the behavior assignment for batch processing
            aiMgr.queueBehaviorAssignment(npc, "LargeWander");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_6)) {
        // Assign EventWander behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to EVENT WANDER behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            // Queue the behavior assignment for batch processing
            aiMgr.queueBehaviorAssignment(npc, "EventWander");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_7)) {
        // Assign RandomPatrol behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to RANDOM PATROL behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            // Queue the behavior assignment for batch processing
            aiMgr.queueBehaviorAssignment(npc, "RandomPatrol");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_8)) {
        // Assign CirclePatrol behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to CIRCLE PATROL behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            // Queue the behavior assignment for batch processing
            aiMgr.queueBehaviorAssignment(npc, "CirclePatrol");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_9)) {
        // Assign EventTarget behavior to all NPCs
        std::cout << "Forge Game Engine - Switching all NPCs to EVENT TARGET behavior\n";
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            // Queue the behavior assignment for batch processing
            aiMgr.queueBehaviorAssignment(npc, "EventTarget");
        }
    }
}


bool AIDemoState::enter() {
    std::cout << "Forge Game Engine - Entering AIDemoState...\n";

    try {
        // Cache GameEngine reference for better performance
        const GameEngine& gameEngine = GameEngine::Instance();

        // Setup window size
        m_worldWidth = gameEngine.getWindowWidth();
        m_worldHeight = gameEngine.getWindowHeight();

        //Texture has to be loaded by NPC or Player can't be loaded here
        setupAIBehaviors();

        // Create player first (the chase behavior will need it)
        m_player = std::make_shared<Player>();
        m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

        // Cache AIManager reference for better performance
        AIManager& aiMgr = AIManager::Instance();

        // Set player reference in AIManager for distance optimization
        aiMgr.setPlayerForDistanceOptimization(m_player);

        // Create and register chase behavior - behaviors can get player via getPlayerReference()
        auto chaseBehavior = std::make_unique<ChaseBehavior>(120.0f, 500.0f, 50.0f);
        aiMgr.registerBehavior("Chase", std::move(chaseBehavior));
        std::cout << "Forge Game Engine - Chase behavior registered (will use AIManager::getPlayerReference())\n";

        // Configure priority multiplier for proper distance progression (1.0 = full distance thresholds)
        aiMgr.configurePriorityMultiplier(1.0f);

        // Create NPCs with AI behaviors
        createNPCs();

        // Create simple HUD UI
        auto& ui = UIManager::Instance();
        ui.createTitle("ai_title", {0, 5, gameEngine.getWindowWidth(), 25}, "AI Demo State");
        ui.setTitleAlignment("ai_title", UIAlignment::CENTER_CENTER);
        ui.createLabel("ai_instructions_line1", {10, 40, gameEngine.getWindowWidth() - 20, 20},
                       "Controls: [B] Exit | [SPACE] Pause/Resume | [1] Wander | [2] Patrol | [3] Chase");
        ui.createLabel("ai_instructions_line2", {10, 75, gameEngine.getWindowWidth() - 20, 20},
                       "Advanced: [4] Small | [5] Large | [6] Event | [7] Random | [8] Circle | [9] Target");
        ui.createLabel("ai_status", {10, 110, 400, 20}, "FPS: -- | Entities: -- | AI: RUNNING");

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

    // Clean up player
    if (m_player) {
        m_player.reset();
    }

    // Clean up UI components using simplified method
    auto& ui = UIManager::Instance();
    ui.prepareForStateTransition();

    // Always restore AI to unpaused state when exiting the demo state
    // This prevents the global pause from affecting other states
    aiMgr.setGlobalPause(false);
    m_aiPaused = false;

    std::cout << "Forge Game Engine - AIDemoState exit complete\n";
    return true;
}

void AIDemoState::update([[maybe_unused]] float deltaTime) {
    try {
        // Update player
        if (m_player) {
            m_player->update(deltaTime);
        }

        // AI Manager is updated globally by GameEngine for optimal performance
        // This hybrid architecture provides several benefits:
        // 1. Consistent 60 FPS updates for all 10K+ AI entities across all states
        // 2. Optimized threading with worker budget allocation
        // 3. No redundant updates when switching between states
        // 4. Better cache performance from centralized batch processing
        // Entity updates are handled by AIManager::update() in GameEngine
        // No need to manually update NPCs or AIManager here

        // Handle user input for the demo
    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - ERROR: Exception in AIDemoState::update(): " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Forge Game Engine - ERROR: Unknown exception in AIDemoState::update()" << std::endl;
    }

    // Game logic only - UI updates moved to render() for thread safety
}

void AIDemoState::render(float deltaTime) {
    // Render all NPCs
    for (auto& npc : m_npcs) {
        npc->render();
    }

    // Render player
    if (m_player) {
        m_player->render();
    }

    // Update and render UI components through UIManager using cached renderer for cleaner API
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        ui.update(deltaTime); // Use actual deltaTime from update cycle

        // Update status display
        const auto& gameEngine = GameEngine::Instance();
        auto& aiManager = AIManager::Instance();
        std::stringstream status;
        status << "FPS: " << std::fixed << std::setprecision(1) << gameEngine.getCurrentFPS()
               << " | Entities: " << m_npcs.size()
               << " | AI: " << (aiManager.isGloballyPaused() ? "PAUSED" : "RUNNING");
        ui.setText("ai_status", status.str());
    }
    ui.render(); // Uses cached renderer from GameEngine
}

void AIDemoState::setupAIBehaviors() {
    std::cout << "AIDemoState: Setting up AI behaviors using EventDemoState implementation...\n";

    // Cache AIManager reference for better performance
    AIManager& aiMgr = AIManager::Instance();

    if (!aiMgr.hasBehavior("Wander")) {
        auto wanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 80.0f);
        wanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("Wander", std::move(wanderBehavior));
        std::cout << "AIDemoState: Registered Wander behavior\n";
    }

    if (!aiMgr.hasBehavior("SmallWander")) {
        auto smallWanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::SMALL_AREA, 60.0f);
        smallWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("SmallWander", std::move(smallWanderBehavior));
        std::cout << "AIDemoState: Registered SmallWander behavior\n";
    }

    if (!aiMgr.hasBehavior("LargeWander")) {
        auto largeWanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::LARGE_AREA, 100.0f);
        largeWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("LargeWander", std::move(largeWanderBehavior));
        std::cout << "AIDemoState: Registered LargeWander behavior\n";
    }

    if (!aiMgr.hasBehavior("EventWander")) {
        auto eventWanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::EVENT_TARGET, 70.0f);
        eventWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("EventWander", std::move(eventWanderBehavior));
        std::cout << "AIDemoState: Registered EventWander behavior\n";
    }

    if (!aiMgr.hasBehavior("Patrol")) {
        auto patrolBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 75.0f, true);
        patrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("Patrol", std::move(patrolBehavior));
        std::cout << "AIDemoState: Registered Patrol behavior\n";
    }

    if (!aiMgr.hasBehavior("RandomPatrol")) {
        auto randomPatrolBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::RANDOM_AREA, 85.0f, false);
        randomPatrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("RandomPatrol", std::move(randomPatrolBehavior));
        std::cout << "AIDemoState: Registered RandomPatrol behavior\n";
    }

    if (!aiMgr.hasBehavior("CirclePatrol")) {
        auto circlePatrolBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::CIRCULAR_AREA, 90.0f, false);
        circlePatrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("CirclePatrol", std::move(circlePatrolBehavior));
        std::cout << "AIDemoState: Registered CirclePatrol behavior\n";
    }

    if (!aiMgr.hasBehavior("EventTarget")) {
        auto eventTargetBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::EVENT_TARGET, 95.0f, false);
        eventTargetBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("EventTarget", std::move(eventTargetBehavior));
        std::cout << "AIDemoState: Registered EventTarget behavior\n";
    }

    // Chase behavior will be set up after player is created in enter() method
    // This ensures the player reference is available for behaviors to use

    std::cout << "AIDemoState: AI behaviors setup complete.\n";
}



void AIDemoState::createNPCs() {
    // Cache AIManager reference for better performance
    AIManager& aiMgr = AIManager::Instance();

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
                // Use default 100ms animation timing

                // Set wander area to keep NPCs on screen
                npc->setWanderArea(0, 0, m_worldWidth, m_worldHeight);

                // Register with AIManager for centralized entity updates with priority and behavior
                aiMgr.registerEntityForUpdates(npc, 5, "Wander");

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
