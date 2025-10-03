/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/AIDemoState.hpp"
#include "SDL3/SDL_scancode.h"
#include "ai/behaviors/ChaseBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/WanderBehavior.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldManager.hpp"
#include <memory>
#include <random>
#include <sstream>
#include <iomanip>



AIDemoState::~AIDemoState() {
  // Don't call virtual functions from destructors

  try {
    // Note: Proper cleanup should already have happened in exit()
    // This destructor is just a safety measure in case exit() wasn't called
    // Reset AI behaviors first to clear entity references
    // Don't call unassignBehaviorFromEntity here - it uses shared_from_this()
    // Clear NPCs without calling clean() on them
    m_npcs.clear();

    // Clean up player
    m_player.reset();

    GAMESTATE_INFO("Exiting AIDemoState in destructor...");
  } catch (const std::exception &e) {
    GAMESTATE_ERROR("Exception in AIDemoState destructor: " +
                    std::string(e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in AIDemoState destructor");
  }
}

void AIDemoState::handleInput() {
  InputManager &inputMgr = InputManager::Instance();

  // Use InputManager's new event-driven key press detection
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_SPACE)) {
    // Toggle pause/resume
    m_aiPaused = !m_aiPaused;

    // Set global AI pause state in AIManager
    AIManager &aiMgr = AIManager::Instance();
    aiMgr.setGlobalPause(m_aiPaused);

    // Also send messages for behaviors that need them
    std::string message = m_aiPaused ? "pause" : "resume";
    aiMgr.broadcastMessage(message, true);

    // Simple feedback
    GAMESTATE_INFO("AI " + std::string(m_aiPaused ? "PAUSED" : "RESUMED"));
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
    GAMESTATE_INFO("Preparing to exit AIDemoState...");
    const GameEngine &gameEngine = GameEngine::Instance();
    gameEngine.getGameStateManager()->changeState("MainMenuState");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_1)) {
    // Assign Wander behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to WANDER behavior");
    AIManager &aiMgr = AIManager::Instance();
    for (auto &npc : m_npcs) {
      // Queue the behavior assignment for batch processing
      aiMgr.queueBehaviorAssignment(npc, "Wander");
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_2)) {
    // Assign Patrol behavior to all NPCs
    GAMESTATE_INFO("Switching " + std::to_string(m_npcs.size()) +
                   " NPCs to PATROL behavior (batched processing)...");
    AIManager &aiMgr = AIManager::Instance();
    for (auto &npc : m_npcs) {
      // Queue the behavior assignment for batch processing
      aiMgr.queueBehaviorAssignment(npc, "Patrol");
    }
    GAMESTATE_INFO("Patrol assignments queued. Processing "
                   "instantly in parallel for optimal performance.");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_3)) {
    // Assign Chase behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to CHASE behavior");

    // Chase behavior target is automatically maintained by AIManager
    // No manual target updates needed
    AIManager &aiMgr = AIManager::Instance();
    for (auto &npc : m_npcs) {
      // Queue the behavior assignment for batch processing
      aiMgr.queueBehaviorAssignment(npc, "Chase");
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_4)) {
    // Assign SmallWander behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to SMALL WANDER behavior");
    AIManager &aiMgr = AIManager::Instance();
    for (auto &npc : m_npcs) {
      // Queue the behavior assignment for batch processing
      aiMgr.queueBehaviorAssignment(npc, "SmallWander");
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_5)) {
    // Assign LargeWander behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to LARGE WANDER behavior");
    AIManager &aiMgr = AIManager::Instance();
    for (auto &npc : m_npcs) {
      // Queue the behavior assignment for batch processing
      aiMgr.queueBehaviorAssignment(npc, "LargeWander");
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_6)) {
    // Assign EventWander behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to EVENT WANDER behavior");
    AIManager &aiMgr = AIManager::Instance();
    for (auto &npc : m_npcs) {
      // Queue the behavior assignment for batch processing
      aiMgr.queueBehaviorAssignment(npc, "EventWander");
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_7)) {
    // Assign RandomPatrol behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to RANDOM PATROL behavior");
    AIManager &aiMgr = AIManager::Instance();
    for (auto &npc : m_npcs) {
      // Queue the behavior assignment for batch processing
      aiMgr.queueBehaviorAssignment(npc, "RandomPatrol");
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_8)) {
    // Assign CirclePatrol behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to CIRCLE PATROL behavior");
    AIManager &aiMgr = AIManager::Instance();
    for (auto &npc : m_npcs) {
      // Queue the behavior assignment for batch processing
      aiMgr.queueBehaviorAssignment(npc, "CirclePatrol");
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_9)) {
    // Assign EventTarget behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to EVENT TARGET behavior");
    AIManager &aiMgr = AIManager::Instance();
    for (auto &npc : m_npcs) {
      // Queue the behavior assignment for batch processing
      aiMgr.queueBehaviorAssignment(npc, "EventTarget");
    }
  }
}

bool AIDemoState::enter() {
  GAMESTATE_INFO("Entering AIDemoState...");

  try {
    // Cache GameEngine reference for better performance
    const GameEngine &gameEngine = GameEngine::Instance();

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

    // Texture has to be loaded by NPC or Player can't be loaded here
    setupAIBehaviors();

    // Create player first (the chase behavior will need it)
    m_player = std::make_shared<Player>();
    m_player->ensurePhysicsBodyRegistered();
    m_player->initializeInventory(); // Initialize inventory after construction
    m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

    // Cache AIManager reference for better performance
    AIManager &aiMgr = AIManager::Instance();

    // Set player reference in AIManager for distance optimization
    aiMgr.setPlayerForDistanceOptimization(m_player);

    // Create and register chase behavior - behaviors can get player via
    // getPlayerReference()
    auto chaseBehavior = std::make_unique<ChaseBehavior>(90.0f, 500.0f, 50.0f);
    aiMgr.registerBehavior("Chase", std::move(chaseBehavior));
    GAMESTATE_INFO("Chase behavior registered (will use AIManager::getPlayerReference())");

    // Configure priority multiplier for proper distance progression (1.0 = full
    // distance thresholds)
    aiMgr.configurePriorityMultiplier(1.0f);

    // Create NPCs with AI behaviors
    createNPCs();

    // Create simple HUD UI
    auto &ui = UIManager::Instance();
    ui.createTitle("ai_title", {0, 5, gameEngine.getLogicalWidth(), 25},
                   "AI Demo State");
    ui.setTitleAlignment("ai_title", UIAlignment::CENTER_CENTER);
    ui.createLabel("ai_instructions_line1",
                   {10, 40, gameEngine.getLogicalWidth() - 20, 20},
                   "Controls: [B] Exit | [SPACE] Pause/Resume | [1] Wander | "
                   "[2] Patrol | [3] Chase");
    ui.createLabel("ai_instructions_line2",
                   {10, 75, gameEngine.getLogicalWidth() - 20, 20},
                   "Advanced: [4] Small | [5] Large | [6] Event | [7] Random | "
                   "[8] Circle | [9] Target");
    ui.createLabel("ai_status", {10, 110, 400, 20},
                   "FPS: -- | Entities: -- | AI: RUNNING");

    // Log status
    GAMESTATE_INFO("Created " + std::to_string(m_npcs.size()) +
              " NPCs with AI behaviors");

    return true;
  } catch (const std::exception &e) {
    GAMESTATE_ERROR("Exception in AIDemoState::enter(): " + std::string(e.what()));
    return false;
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in AIDemoState::enter()");
    return false;
  }
}

bool AIDemoState::exit() {
  GAMESTATE_INFO("Exiting AIDemoState...");

  // Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();

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

  // Clean up player
  if (m_player) {
    m_player.reset();
  }

  // Clean up UI components using simplified method
  auto &ui = UIManager::Instance();
  ui.prepareForStateTransition();

  // Always restore AI to unpaused state when exiting the demo state
  // This prevents the global pause from affecting other states
  aiMgr.setGlobalPause(false);
  m_aiPaused = false;

  GAMESTATE_INFO("AIDemoState exit complete");
  return true;
}

void AIDemoState::update(float deltaTime) {
  try {
    // Update player
    if (m_player) {
      m_player->update(deltaTime);
    }

    // AI Manager is updated globally by GameEngine for optimal performance
    // Entity updates are handled by AIManager::update() in GameEngine
    // No need to manually update NPCs or AIManager here

  } catch (const std::exception &e) {
    GAMESTATE_ERROR("Exception in AIDemoState::update(): " + std::string(e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in AIDemoState::update()");
  }
}

void AIDemoState::render() {

  // Render all NPCs
  for (auto &npc : m_npcs) {
    npc->render(nullptr);  // No camera transformation needed in AI demo
  }

  // Render player
  if (m_player) {
    m_player->render(nullptr);  // No camera transformation needed in AI demo
  }

  // Update and render UI components through UIManager using cached renderer for
  // cleaner API
  auto &ui = UIManager::Instance();
  if (!ui.isShutdown()) {
    ui.update(0.0); // UI updates are not time-dependent in this state

    // Update status display
    const auto &gameEngine = GameEngine::Instance();
    auto &aiManager = AIManager::Instance();
    std::stringstream status;
    status << "FPS: " << std::fixed << std::setprecision(1)
           << gameEngine.getCurrentFPS() << " | Entities: " << m_npcs.size()
           << " | AI: "
           << (aiManager.isGloballyPaused() ? "PAUSED" : "RUNNING");
    ui.setText("ai_status", status.str());
  }
  ui.render(); // Uses cached renderer from GameEngine
}

void AIDemoState::setupAIBehaviors() {
  GAMESTATE_INFO("AIDemoState: Setting up AI behaviors using EventDemoState implementation...");
  //TODO: need to move all availible behaviors into the AIManager and Event Manager NPC creation with behavior
  // Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();

  if (!aiMgr.hasBehavior("Wander")) {
    auto wanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::MEDIUM_AREA, 60.0f);
    aiMgr.registerBehavior("Wander", std::move(wanderBehavior));
    GAMESTATE_INFO("AIDemoState: Registered Wander behavior");
  }

  if (!aiMgr.hasBehavior("SmallWander")) {
    auto smallWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::SMALL_AREA, 45.0f);
    aiMgr.registerBehavior("SmallWander", std::move(smallWanderBehavior));
    GAMESTATE_INFO("AIDemoState: Registered SmallWander behavior");
  }

  if (!aiMgr.hasBehavior("LargeWander")) {
    auto largeWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::LARGE_AREA, 75.0f);
    aiMgr.registerBehavior("LargeWander", std::move(largeWanderBehavior));
    GAMESTATE_INFO("AIDemoState: Registered LargeWander behavior");
  }

  if (!aiMgr.hasBehavior("EventWander")) {
    auto eventWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::EVENT_TARGET, 52.5f);
    aiMgr.registerBehavior("EventWander", std::move(eventWanderBehavior));
    GAMESTATE_INFO("AIDemoState: Registered EventWander behavior");
  }

  if (!aiMgr.hasBehavior("Patrol")) {
    auto patrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 75.0f, true);
    aiMgr.registerBehavior("Patrol", std::move(patrolBehavior));
    GAMESTATE_INFO("AIDemoState: Registered Patrol behavior");
  }

  if (!aiMgr.hasBehavior("RandomPatrol")) {
    auto randomPatrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::RANDOM_AREA, 85.0f, false);
    aiMgr.registerBehavior("RandomPatrol", std::move(randomPatrolBehavior));
    GAMESTATE_INFO("AIDemoState: Registered RandomPatrol behavior");
  }

  if (!aiMgr.hasBehavior("CirclePatrol")) {
    auto circlePatrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::CIRCULAR_AREA, 90.0f, false);
    aiMgr.registerBehavior("CirclePatrol", std::move(circlePatrolBehavior));
    GAMESTATE_INFO("AIDemoState: Registered CirclePatrol behavior");
  }

  if (!aiMgr.hasBehavior("EventTarget")) {
    auto eventTargetBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::EVENT_TARGET, 95.0f, false);
    aiMgr.registerBehavior("EventTarget", std::move(eventTargetBehavior));
    GAMESTATE_INFO("AIDemoState: Registered EventTarget behavior");
  }

  // Chase behavior will be set up after player is created in enter() method
  // This ensures the player reference is available for behaviors to use

  GAMESTATE_INFO("AIDemoState: AI behaviors setup complete.");
}

void AIDemoState::createNPCs() {
  // Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();

  try {
    // Random number generation for positioning
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> xDist(50.0f, m_worldWidth - 50.0f);
    std::uniform_real_distribution<float> yDist(50.0f, m_worldHeight - 50.0f);

    // Step 1: Create all NPCs and store them locally first to avoid race conditions.
    std::vector<std::shared_ptr<NPC>> local_npcs;
    local_npcs.reserve(m_npcCount);

    for (int i = 0; i < m_npcCount; ++i) {
      try {
        // Create NPC with random position
        Vector2D position(xDist(gen), yDist(gen));
        auto npc = NPC::create("npc", position);
        npc->initializeInventory(); // Initialize inventory after construction

        // Set animation properties (adjust based on your actual sprite sheet)
        // Use default 100ms animation timing

        // Set wander area to keep NPCs on screen
        npc->setWanderArea(0, 0, m_worldWidth, m_worldHeight);

        local_npcs.push_back(npc);
      } catch (const std::exception &e) {
        GAMESTATE_ERROR("Exception creating NPC " + std::to_string(i) + ": " + std::string(e.what()));
        continue;
      }
    }

    // Step 2: Now that the vector is stable, add them to the member variable.
    m_npcs = local_npcs;

    // Step 3: Register all NPCs with the AIManager in a separate loop.
    for (const auto& npc : m_npcs) {
        // Register with AIManager for centralized entity updates with priority
        // and behavior
        aiMgr.registerEntityForUpdates(npc, 5, "Wander");
    }


    // Chase behavior target is now automatically handled by AIManager
    // No manual setup needed - target is set during
    // setupChaseBehaviorWithTarget()
  } catch (const std::exception &e) {
    GAMESTATE_ERROR("Exception in createNPCs(): " + std::string(e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in createNPCs()");
  }
}
