/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/AdvancedAIDemoState.hpp"
#include "core/Logger.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/WorldManager.hpp"
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

#include <sstream>
#include <iomanip>
#include <cmath>



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
        GAMESTATE_INFO("Advanced AI " + std::string(m_aiPaused ? "PAUSED" : "RESUMED"));
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
        GAMESTATE_INFO("Preparing to exit AdvancedAIDemoState...");
        const GameEngine& gameEngine = GameEngine::Instance();
        gameEngine.getGameStateManager()->changeState("MainMenuState");
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_1)) {
        // Assign Idle behavior to all NPCs
        GAMESTATE_INFO("Switching all NPCs to IDLE behavior");
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            aiMgr.queueBehaviorAssignment(npc, "Idle");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_2)) {
        // Assign Flee behavior to all NPCs
        GAMESTATE_INFO("Switching all NPCs to FLEE behavior");
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            aiMgr.queueBehaviorAssignment(npc, "Flee");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_3)) {
        // Assign Follow behavior to all NPCs
        GAMESTATE_INFO("Switching all NPCs to FOLLOW behavior");
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            aiMgr.queueBehaviorAssignment(npc, "Follow");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_4)) {
        // Assign Guard behavior to all NPCs
        GAMESTATE_INFO("Switching all NPCs to GUARD behavior");
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            aiMgr.queueBehaviorAssignment(npc, "Guard");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_5)) {
        // Assign Attack behavior to all NPCs
        GAMESTATE_INFO("Switching all NPCs to ATTACK behavior");
        AIManager& aiMgr = AIManager::Instance();
        for (auto& npc : m_npcs) {
            aiMgr.queueBehaviorAssignment(npc, "Attack");
        }
    }

    // Camera zoom controls
    if (inputMgr.wasKeyPressed(SDL_SCANCODE_LEFTBRACKET) && m_camera) {
        m_camera->zoomIn();  // [ key = zoom in (objects larger)
    }
    if (inputMgr.wasKeyPressed(SDL_SCANCODE_RIGHTBRACKET) && m_camera) {
        m_camera->zoomOut();  // ] key = zoom out (objects smaller)
    }
}

bool AdvancedAIDemoState::enter() {
    GAMESTATE_INFO("Entering AdvancedAIDemoState...");

    try {
        // Cache GameEngine reference for better performance
        const GameEngine& gameEngine = GameEngine::Instance();

        // Setup world size using actual world bounds instead of screen dimensions
        float worldMinX, worldMinY, worldMaxX, worldMaxY;
        if (WorldManager::Instance().getWorldBounds(worldMinX, worldMinY, worldMaxX, worldMaxY)) {
            m_worldWidth = worldMaxX;
            m_worldHeight = worldMaxY;
        } else {
            // Fallback to screen dimensions if world not loaded
            m_worldWidth = gameEngine.getLogicalWidth();
            m_worldHeight = gameEngine.getLogicalHeight();
        }

        // Initialize game time
        m_gameTime = 0.0f;

        // Create player first (required for flee/follow/attack behaviors)
        m_player = std::make_shared<Player>();
        m_player->ensurePhysicsBodyRegistered();
        m_player->initializeInventory(); // Initialize inventory after construction
        m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

        // Initialize world and camera BEFORE behavior setup
        // WorldGeneratedEvent is fired but processed asynchronously
        initializeWorld();

        // Reposition player to world center now that world is loaded
        m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

        initializeCamera();

        // Initialize PathfinderManager for Follow behavior pathfinding
        PathfinderManager& pathfinderMgr = PathfinderManager::Instance();
        if (!pathfinderMgr.isInitialized()) {
            pathfinderMgr.init();
            GAMESTATE_INFO("PathfinderManager initialized for Follow behavior");
        }

        // Manually trigger grid rebuild now that world is loaded
        // This ensures the pathfinding grid is ready before NPCs start following
        pathfinderMgr.rebuildGrid();
        GAMESTATE_INFO("PathfinderManager grid rebuild initiated");

        // Cache AIManager reference for better performance
        AIManager& aiMgr = AIManager::Instance();

        // Set player reference in AIManager BEFORE registering behaviors
        // This ensures Follow/Flee/Attack behaviors can access the player target
        // Explicitly cast PlayerPtr to EntityPtr to ensure proper conversion
        EntityPtr playerAsEntity = std::static_pointer_cast<Entity>(m_player);
        aiMgr.setPlayerForDistanceOptimization(playerAsEntity);

        // Setup advanced AI behaviors AFTER world is initialized and player is set
        // This ensures Guard behavior uses correct world dimensions and Follow has player target
        setupAdvancedAIBehaviors();

        // Setup combat attributes for player
        setupCombatAttributes();

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
        GAMESTATE_INFO("Created " + std::to_string(m_npcs.size()) + " NPCs with advanced AI behaviors");
        GAMESTATE_INFO("Combat system initialized with health/damage attributes");

        return true;
    } catch (const std::exception& e) {
        GAMESTATE_ERROR("Exception in AdvancedAIDemoState::enter(): " + std::string(e.what()));
        return false;
    } catch (...) {
        GAMESTATE_ERROR("Unknown exception in AdvancedAIDemoState::enter()");
        return false;
    }
}

bool AdvancedAIDemoState::exit() {
    GAMESTATE_INFO("Exiting AdvancedAIDemoState...");

    // Cache AIManager reference for better performance
    AIManager& aiMgr = AIManager::Instance();

    // Use prepareForStateTransition methods for deterministic cleanup
    aiMgr.prepareForStateTransition();

    // Clean collision state
    CollisionManager &collisionMgr = CollisionManager::Instance();
    if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
      collisionMgr.prepareForStateTransition();
    }

    // Clean pathfinding state for fresh start
    PathfinderManager& pathfinderMgr = PathfinderManager::Instance();
    if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
      pathfinderMgr.prepareForStateTransition();
    }

    // Clear NPCs (AIManager::prepareForStateTransition already handled cleanup)
    m_npcs.clear();

    // Clear combat attributes
    m_combatAttributes.clear();

    // Clean up player
    if (m_player) {
        m_player.reset();
    }

    // Clean up camera first to stop world rendering
    m_camera.reset();

    // Clean up UI components using simplified method
    auto& ui = UIManager::Instance();
    ui.prepareForStateTransition();

    // Unload the world when fully exiting, but only if there's actually a world loaded
    // This matches EventDemoState's safety pattern and prevents crashes
    WorldManager& worldMgr = WorldManager::Instance();
    if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
        worldMgr.unloadWorld();
    }

    // Always restore AI to unpaused state when exiting the demo state
    aiMgr.setGlobalPause(false);
    m_aiPaused = false;

    GAMESTATE_INFO("AdvancedAIDemoState exit complete");
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

        // Update camera (follows player automatically)
        updateCamera(deltaTime);

        // Update combat system
        updateCombatSystem(deltaTime);

        // AI Manager is updated globally by GameEngine for optimal performance
        // Entity updates are handled by AIManager::update() in GameEngine

    } catch (const std::exception& e) {
        GAMESTATE_ERROR("Exception in AdvancedAIDemoState::update(): " + std::string(e.what()));
    } catch (...) {
        GAMESTATE_ERROR("Unknown exception in AdvancedAIDemoState::update()");
    }
}

void AdvancedAIDemoState::render() {
    // Get renderer using the standard pattern
    auto& gameEngine = GameEngine::Instance();
    SDL_Renderer* renderer = gameEngine.getRenderer();

    // Calculate camera view rect ONCE for all rendering to ensure perfect synchronization
    HammerEngine::Camera::ViewRect cameraView{0.0f, 0.0f, 0.0f, 0.0f};
    if (m_camera) {
        cameraView = m_camera->getViewRect();
    }

    // Set render scale for zoom (scales all world/entity rendering automatically)
    float zoom = m_camera ? m_camera->getZoom() : 1.0f;
    SDL_SetRenderScale(renderer, zoom, zoom);

    // Render world first (background layer) using unified camera position
    if (m_camera) {
        auto& worldMgr = WorldManager::Instance();
        if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
            // Use the camera view for world rendering
            worldMgr.render(renderer,
                           cameraView.x,
                           cameraView.y,
                           gameEngine.getLogicalWidth(),
                           gameEngine.getLogicalHeight());
        }
    }

    // Render all NPCs using camera-aware rendering
    for (auto& npc : m_npcs) {
        npc->render(m_camera.get());

        // Render health bars for NPCs with combat attributes
        auto it = m_combatAttributes.find(npc);
        if (it != m_combatAttributes.end() && !it->second.isDead) {
            //TODO:
            // Note: Health bar rendering would be implemented with the graphics system
            // const auto& combat = it->second;
            // Vector2D pos = npc->getPosition();
            // float healthPercent = combat.health / combat.maxHealth;
        }
    }

    // Render player using camera-aware rendering
    if (m_player) {
        m_player->render(m_camera.get());

        // Render player health bar
        auto it = m_combatAttributes.find(m_player);
        if (it != m_combatAttributes.end()) {
            //TODO:
            // Player health bar rendering would go here
        }
    }

    // Update and render UI components
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        ui.update(0.0); // UI updates are not time-dependent in this state

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
    GAMESTATE_INFO("AdvancedAIDemoState: Setting up advanced AI behaviors...");

    // Cache AIManager reference for better performance
    AIManager& aiMgr = AIManager::Instance();
    //TODO: Need to see if we can move all of these behaviors into the AIManager and just have events trigger NPCs creation with behavior
    // Register Idle behavior
    if (!aiMgr.hasBehavior("Idle")) {
        auto idleBehavior = std::make_unique<IdleBehavior>(IdleBehavior::IdleMode::LIGHT_FIDGET, 50.0f);
        aiMgr.registerBehavior("Idle", std::move(idleBehavior));
        GAMESTATE_INFO("AdvancedAIDemoState: Registered Idle behavior");
    }

    // Register Flee behavior
    if (!aiMgr.hasBehavior("Flee")) {
    auto fleeBehavior = std::make_unique<FleeBehavior>(60.0f, 150.0f, 200.0f); // speed, detection range, safe distance
        aiMgr.registerBehavior("Flee", std::move(fleeBehavior));
        GAMESTATE_INFO("AdvancedAIDemoState: Registered Flee behavior");
    }

    // Register Follow behavior
    if (!aiMgr.hasBehavior("Follow")) {
        auto followBehavior = std::make_unique<FollowBehavior>(80.0f, 50.0f, 300.0f); // follow speed, follow distance, max distance
        followBehavior->setStopWhenTargetStops(false); // Always follow, even if player is stationary
        aiMgr.registerBehavior("Follow", std::move(followBehavior));
        GAMESTATE_INFO("AdvancedAIDemoState: Registered Follow behavior");
    }

    // Register Guard behavior
    if (!aiMgr.hasBehavior("Guard")) {
        auto guardBehavior = std::make_unique<GuardBehavior>(Vector2D(m_worldWidth/2, m_worldHeight/2), 150.0f, 200.0f); // position, guard radius, alert radius
        aiMgr.registerBehavior("Guard", std::move(guardBehavior));
        GAMESTATE_INFO("AdvancedAIDemoState: Registered Guard behavior");
    }

    // Register Attack behavior
    if (!aiMgr.hasBehavior("Attack")) {
    auto attackBehavior = std::make_unique<AttackBehavior>(80.0f, 1.0f, 63.75f); // range, cooldown, speed
        aiMgr.registerBehavior("Attack", std::move(attackBehavior));
        GAMESTATE_INFO("AdvancedAIDemoState: Registered Attack behavior");
    }

    GAMESTATE_INFO("AdvancedAIDemoState: Advanced AI behaviors setup complete.");
}

void AdvancedAIDemoState::createAdvancedNPCs() {
    //TODO: Make an event for advanced NPC's
    // Cache AIManager reference for better performance
    AIManager& aiMgr = AIManager::Instance();

    try {
        // Random number generation for positioning near player
        std::random_device rd;
        std::mt19937 gen(rd());

        // Player is at world center - spawn NPCs in a reasonable radius around player
        Vector2D playerPos = m_player->getPosition();
        float spawnRadius = 400.0f; // Spawn within ~400 pixels of player (visible on screen)
        std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159f);
        std::uniform_real_distribution<float> radiusDist(100.0f, spawnRadius);

        // Create NPCs with optimized counts for behavior demonstration
        for (int i = 0; i < m_totalNPCCount; ++i) {
            try {
                // Create NPC with strategic positioning near player for behavior showcase
                Vector2D position;

                // Position all NPCs in a circle around the player for easy visibility
                float angle = angleDist(gen);
                float distance = radiusDist(gen);

                position = Vector2D(
                    playerPos.getX() + distance * std::cos(angle),
                    playerPos.getY() + distance * std::sin(angle)
                );

                auto npc = NPC::create("npc", position);

                // Set wander area to keep NPCs on screen
                npc->setWanderArea(0, 0, m_worldWidth, m_worldHeight);

                // Initialize with Follow behavior by default for smooth movement demonstration
                aiMgr.registerEntityForUpdates(npc, 5, "Follow");
                //TODO: should be moved into the entity and NPC class based on what type of entity.
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
                GAMESTATE_ERROR("Exception creating advanced NPC " + std::to_string(i) + ": " + std::string(e.what()));
                continue;
            }
        }

        GAMESTATE_INFO("AdvancedAIDemoState: Created " + std::to_string(m_npcs.size()) + " NPCs with combat attributes");
    } catch (const std::exception& e) {
        GAMESTATE_ERROR("Exception in createAdvancedNPCs(): " + std::string(e.what()));
    } catch (...) {
        GAMESTATE_ERROR("Unknown exception in createAdvancedNPCs()");
    }
}

void AdvancedAIDemoState::setupCombatAttributes() {
    //TODO: Again should be consolidated into Player or Entity class
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
    //TODO: Move to AIMAnager or maybe seperate Combat Manager?
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

void AdvancedAIDemoState::initializeWorld() {
    // Create world manager and generate a world for advanced AI demo
    WorldManager& worldManager = WorldManager::Instance();

    // Get UI and engine references for loading overlay
    auto& ui = UIManager::Instance();
    auto& gameEngine = GameEngine::Instance();
    SDL_Renderer* renderer = gameEngine.getRenderer();
    int windowWidth = gameEngine.getLogicalWidth();
    int windowHeight = gameEngine.getLogicalHeight();

    // Create loading overlay using existing UIManager components
    ui.createOverlay();
    ui.createTitle("loading_title", {0, windowHeight / 2 - 80, windowWidth, 40}, "Loading Advanced AI Demo World...");
    ui.setTitleAlignment("loading_title", UIAlignment::CENTER_CENTER);

    // Create progress bar in center of screen
    int progressBarWidth = 400;
    int progressBarHeight = 30;
    int progressBarX = (windowWidth - progressBarWidth) / 2;
    int progressBarY = windowHeight / 2;
    ui.createProgressBar("loading_progress", {progressBarX, progressBarY, progressBarWidth, progressBarHeight}, 0.0f, 100.0f);

    // Create status text as a TITLE below progress bar
    ui.createTitle("loading_status", {0, progressBarY + 50, windowWidth, 30}, "Initializing...");
    ui.setTitleAlignment("loading_status", UIAlignment::CENTER_CENTER);

    // Create world configuration matching AIDemoState for consistency
    // At 32 pixels per tile, this is 12800x12800 pixels
    HammerEngine::WorldGenerationConfig config;
    config.width = 400;  // Match AIDemoState world size
    config.height = 400; // Match AIDemoState world size
    config.seed = static_cast<int>(std::time(nullptr)); // Random seed for variety
    config.elevationFrequency = 0.1f;
    config.humidityFrequency = 0.1f;
    config.waterLevel = 0.25f;
    config.mountainLevel = 0.75f;

    // Create progress callback to update UI during world generation
    auto progressCallback = [&](float percent, const std::string& status) {
        ui.updateProgressBar("loading_progress", percent);
        ui.setText("loading_status", status);

        // Render the current frame to show progress updates
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        ui.render(renderer);
        SDL_RenderPresent(renderer);
    };

    if (!worldManager.loadNewWorld(config, progressCallback)) {
        GAMESTATE_ERROR("Failed to load new world in AdvancedAIDemoState");
        // Continue anyway - advanced AI demo can function without world
    } else {
        GAMESTATE_INFO("Successfully loaded advanced AI demo world with seed: " + std::to_string(config.seed));

        // Update demo world dimensions to match generated world (pixels)
        float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
        if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
            m_worldWidth = std::max(0.0f, maxX - minX);
            m_worldHeight = std::max(0.0f, maxY - minY);
        }
    }

    // Cleanup loading UI
    ui.removeOverlay();
    ui.removeComponent("loading_title");
    ui.removeComponent("loading_progress");
    ui.removeComponent("loading_status");
}

void AdvancedAIDemoState::initializeCamera() {
    const auto& gameEngine = GameEngine::Instance();

    // Initialize camera at player's position to avoid any interpolation jitter
    Vector2D playerPosition = m_player ? m_player->getPosition() : Vector2D(0, 0);

    // Create camera starting at player position
    m_camera = std::make_unique<HammerEngine::Camera>(
        playerPosition.getX(), playerPosition.getY(), // Start at player position
        static_cast<float>(gameEngine.getLogicalWidth()),
        static_cast<float>(gameEngine.getLogicalHeight())
    );

    // Configure camera to follow player
    if (m_player) {
        // Disable camera event firing for consistency
        m_camera->setEventFiringEnabled(false);

        // Set target and enable follow mode
        std::weak_ptr<Entity> playerAsEntity = std::static_pointer_cast<Entity>(m_player);
        m_camera->setTarget(playerAsEntity);
        m_camera->setMode(HammerEngine::Camera::Mode::Follow);

        // Set up camera configuration for fast, smooth following
        HammerEngine::Camera::Config config;
        config.followSpeed = 8.0f;         // Faster follow for action gameplay
        config.deadZoneRadius = 0.0f;      // No dead zone - always follow
        config.smoothingFactor = 0.80f;    // Quicker response smoothing
        config.maxFollowDistance = 9999.0f; // No distance limit
        config.clampToWorldBounds = true;   // Keep camera within world
        m_camera->setConfig(config);

        // Camera auto-synchronizes world bounds on update
    }
}

void AdvancedAIDemoState::updateCamera(float deltaTime) {
    if (m_camera) {
        // Sync viewport with current window size (handles resize events)
        m_camera->syncViewportWithEngine();

        // Update camera position and following logic
        m_camera->update(deltaTime);
    }
}
