/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/AIDemoState.hpp"
#include "gameStates/LoadingState.hpp"
#include "SDL3/SDL_scancode.h"
#include "ai/behaviors/ChaseBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/WanderBehavior.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldManager.hpp"
#include "utils/Camera.hpp"
#include "world/WorldData.hpp"
#include <memory>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>



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

  // Camera zoom controls
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_LEFTBRACKET) && m_camera) {
    m_camera->zoomIn();  // [ key = zoom in (objects larger)
  }
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_RIGHTBRACKET) && m_camera) {
    m_camera->zoomOut();  // ] key = zoom out (objects smaller)
  }

  // NPC spawning controls
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_N)) {
    // Spawn all NPCs up to m_npcCount with standard behavior (Wander)
    if (m_npcsSpawned < m_npcCount) {
      // Set CollisionManager bounds once before spawning starts
      if (m_npcsSpawned == 0) {
        CollisionManager &collisionMgr = CollisionManager::Instance();
        if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
          collisionMgr.setWorldBounds(0.0f, 0.0f, m_worldWidth, m_worldHeight);
          GAMESTATE_INFO("CollisionManager bounds set to: " + std::to_string(m_worldWidth) +
                         " x " + std::to_string(m_worldHeight));
        }
      }

      int npcsToSpawn = m_npcCount - m_npcsSpawned;
      GAMESTATE_INFO("Spawning " + std::to_string(npcsToSpawn) + " NPCs with Wander behavior...");
      createNPCBatch(npcsToSpawn);
      m_npcsSpawned += npcsToSpawn;
      GAMESTATE_INFO("Spawned " + std::to_string(m_npcsSpawned) + " / " +
                     std::to_string(m_npcCount) + " NPCs (Standard behavior)");
    } else {
      GAMESTATE_INFO("Already spawned " + std::to_string(m_npcCount) + " NPCs (max reached)");
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_M)) {
    // Spawn 2000 NPCs with random behaviors (like EventDemoState)
    // Set CollisionManager bounds if this is the first spawn
    if (m_npcs.empty()) {
      CollisionManager &collisionMgr = CollisionManager::Instance();
      if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
        collisionMgr.setWorldBounds(0.0f, 0.0f, m_worldWidth, m_worldHeight);
        GAMESTATE_INFO("CollisionManager bounds set to: " + std::to_string(m_worldWidth) +
                       " x " + std::to_string(m_worldHeight));
      }
    }

    int previousCount = m_npcs.size();
    GAMESTATE_INFO("Spawning 2000 NPCs with random behaviors...");
    createNPCBatchWithRandomBehaviors(2000);
    int actualSpawned = m_npcs.size() - previousCount;
    GAMESTATE_INFO("Spawned " + std::to_string(actualSpawned) + " NPCs with random behaviors (Total: " +
                   std::to_string(m_npcs.size()) + ")");
  }
}

bool AIDemoState::enter() {
  GAMESTATE_INFO("Entering AIDemoState...");

  // Check if world needs to be loaded
  if (!m_worldLoaded) {
    GAMESTATE_INFO("World not loaded yet - will transition to LoadingState on first update");
    m_needsLoading = true;
    m_worldLoaded = true;  // Mark as loaded to prevent loop on re-entry
    return true;  // Will transition to loading screen in update()
  }

  // World is loaded - proceed with normal initialization
  GAMESTATE_INFO("World already loaded - initializing AI demo");

  try {
    // Cache GameEngine reference for better performance
    const GameEngine &gameEngine = GameEngine::Instance();
    auto& worldManager = WorldManager::Instance();

    // Update world dimensions from loaded world
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
      m_worldWidth = std::max(0.0f, maxX - minX);
      m_worldHeight = std::max(0.0f, maxY - minY);
      GAMESTATE_INFO("World dimensions: " + std::to_string(m_worldWidth) + " x " + std::to_string(m_worldHeight) + " pixels");
    } else {
      // Fallback to screen dimensions if world bounds unavailable
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

    // Create simple HUD UI
    auto &ui = UIManager::Instance();
    ui.createTitle("ai_title", {0, 5, gameEngine.getLogicalWidth(), 25},
                   "AI Demo State");
    ui.setTitleAlignment("ai_title", UIAlignment::CENTER_CENTER);
    ui.createLabel("ai_instructions_line1",
                   {10, 40, gameEngine.getLogicalWidth() - 20, 20},
                   "Controls: [B] Exit | [SPACE] Pause/Resume | [N] Spawn 2K Standard | "
                   "[M] Spawn 2K Random | [ ] Zoom");
    ui.createLabel("ai_instructions_line2",
                   {10, 75, gameEngine.getLogicalWidth() - 20, 20},
                   "Behaviors: [1] Wander | [2] Patrol | [3] Chase | [4] Small | [5] Large | "
                   "[6] Event | [7] Random | [8] Circle | [9] Target");
    ui.createLabel("ai_status", {10, 110, 400, 20},
                   "FPS: -- | Entities: -- | AI: RUNNING");

    // Initialize camera (world is already loaded by LoadingState)
    initializeCamera();

    // NPCs can be spawned using keyboard triggers (N for standard, M for random behaviors)
    m_npcsSpawned = 0;

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

  // Clean up camera first to stop world rendering
  m_camera.reset();

  // Clean up UI components using simplified method
  auto &ui = UIManager::Instance();
  ui.prepareForStateTransition();

  // Unload the world when fully exiting, but only if there's actually a world loaded
  // This matches EventDemoState's safety pattern and prevents crashes
  WorldManager &worldMgr = WorldManager::Instance();
  if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
    worldMgr.unloadWorld();
    // CRITICAL: Only reset m_worldLoaded when actually unloading a world
    // This prevents infinite loop when transitioning to LoadingState (no world yet)
    m_worldLoaded = false;
  }

  // Always restore AI to unpaused state when exiting the demo state
  // This prevents the global pause from affecting other states
  aiMgr.setGlobalPause(false);
  m_aiPaused = false;

  GAMESTATE_INFO("AIDemoState exit complete");
  return true;
}

void AIDemoState::update(float deltaTime) {
  try {
    // Check if we need to transition to loading screen (do this in update, not enter)
    if (m_needsLoading) {
      m_needsLoading = false;  // Clear flag

      GAMESTATE_INFO("Transitioning to LoadingState for world generation");

      // Create world configuration for AI demo
      HammerEngine::WorldGenerationConfig config;
      config.width = 500;  // Massive world matching EventDemoState
      config.height = 500;
      config.seed = static_cast<int>(std::time(nullptr));
      config.elevationFrequency = 0.1f;
      config.humidityFrequency = 0.1f;
      config.waterLevel = 0.25f;
      config.mountainLevel = 0.75f;

      // Configure LoadingState and transition to it
      auto& gameEngine = GameEngine::Instance();
      auto* gameStateManager = gameEngine.getGameStateManager();
      if (gameStateManager) {
        auto* loadingState = dynamic_cast<LoadingState*>(gameStateManager->getState("LoadingState").get());
        if (loadingState) {
          loadingState->configure("AIDemoState", config);
          // Use changeState (called from update) to properly exit and re-enter
          gameStateManager->changeState("LoadingState");
        } else {
          GAMESTATE_ERROR("LoadingState not found in GameStateManager");
        }
      }

      return;  // Don't continue with rest of update
    }

    // Auto-spawning disabled - use keyboard triggers instead (N key for standard spawn, M key for random behaviors)
    // Collision bounds are set on first spawn via keyboard trigger

    // Update player
    if (m_player) {
      m_player->update(deltaTime);
    }

    // Update camera (follows player automatically)
    updateCamera(deltaTime);

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
  // Get renderer using the standard pattern (consistent with other states)
  auto &gameEngine = GameEngine::Instance();
  SDL_Renderer *renderer = gameEngine.getRenderer();

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
    auto &worldMgr = WorldManager::Instance();
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
  for (auto &npc : m_npcs) {
    npc->render(m_camera.get());
  }

  // Render player using camera-aware rendering
  if (m_player) {
    m_player->render(m_camera.get());
  }

  // Reset render scale to 1.0 for UI rendering (UI should not be zoomed)
  SDL_SetRenderScale(renderer, 1.0f, 1.0f);

  // Update and render UI components through UIManager using cached renderer for
  // cleaner API
  auto &ui = UIManager::Instance();
  if (!ui.isShutdown()) {
    ui.update(0.0); // UI updates are not time-dependent in this state

    // Update status display
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
        PatrolBehavior::PatrolMode::RANDOM_AREA, 75.0f, false);
    aiMgr.registerBehavior("Patrol", std::move(patrolBehavior));
    GAMESTATE_INFO("AIDemoState: Registered Patrol behavior (random area)");
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

void AIDemoState::createNPCBatch(int count) {
  // Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();
  WorldManager &worldMgr = WorldManager::Instance();
  const auto *worldData = worldMgr.getWorldData();

  if (!worldData) {
    GAMESTATE_ERROR("Cannot create NPCs - world data not available");
    return;
  }

  try {
    // Random number generation for positioning across the entire world
    static std::random_device rd;
    static std::mt19937 gen(rd());
    constexpr float tileSize = HammerEngine::TILE_SIZE;

    // Calculate tile range
    int maxTileX = static_cast<int>(m_worldWidth / tileSize) - 2;  // -2 for margin
    int maxTileY = static_cast<int>(m_worldHeight / tileSize) - 2;

    std::uniform_int_distribution<int> tileDistX(1, maxTileX);  // Start at 1 for margin
    std::uniform_int_distribution<int> tileDistY(1, maxTileY);

    // Create batch of NPCs
    int attempts = 0;
    int created = 0;
    const int maxAttempts = count * 10;  // Allow multiple attempts to find valid positions

    while (created < count && attempts < maxAttempts) {
      attempts++;

      // Pick a random tile
      int tileX = tileDistX(gen);
      int tileY = tileDistY(gen);

      // Check if tile is valid (not water, not a building)
      if (tileY >= 0 && tileY < static_cast<int>(worldData->grid.size()) &&
          tileX >= 0 && tileX < static_cast<int>(worldData->grid[tileY].size())) {

        const auto &tile = worldData->grid[tileY][tileX];

        // Only spawn on walkable ground (not water, not buildings)
        if (!tile.isWater && tile.obstacleType != HammerEngine::ObstacleType::BUILDING) {
          // Random position within the tile
          std::uniform_real_distribution<float> offsetDist(0.0f, tileSize);
          float x = tileX * tileSize + offsetDist(gen);
          float y = tileY * tileSize + offsetDist(gen);
          Vector2D position(x, y);

          try {
            auto npc = NPC::create("npc", position);
            npc->initializeInventory();
            npc->setWanderArea(0, 0, m_worldWidth, m_worldHeight);
            aiMgr.registerEntityForUpdates(npc, 5, "Wander");
            m_npcs.push_back(npc);
            created++;
          } catch (const std::exception &e) {
            GAMESTATE_ERROR("Exception creating NPC: " + std::string(e.what()));
          }
        }
      }
    }

    if (created < count) {
      GAMESTATE_WARN("Only created " + std::to_string(created) + " of " +
                     std::to_string(count) + " requested NPCs after " +
                     std::to_string(attempts) + " attempts");
    }

  } catch (const std::exception &e) {
    GAMESTATE_ERROR("Exception in createNPCBatch(): " + std::string(e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in createNPCBatch()");
  }
}

void AIDemoState::createNPCBatchWithRandomBehaviors(int count) {
  // Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();
  WorldManager &worldMgr = WorldManager::Instance();
  const auto *worldData = worldMgr.getWorldData();

  if (!worldData) {
    GAMESTATE_ERROR("Cannot create NPCs with random behaviors - world data not available");
    return;
  }

  try {
    // Random number generation for positioning across the entire world
    static std::random_device rd;
    static std::mt19937 gen(rd());
    constexpr float tileSize = HammerEngine::TILE_SIZE;

    // Calculate tile range
    int maxTileX = static_cast<int>(m_worldWidth / tileSize) - 2;  // -2 for margin
    int maxTileY = static_cast<int>(m_worldHeight / tileSize) - 2;

    std::uniform_int_distribution<int> tileDistX(1, maxTileX);  // Start at 1 for margin
    std::uniform_int_distribution<int> tileDistY(1, maxTileY);

    // Available behaviors for random assignment (matching EventDemoState variety)
    std::vector<std::string> behaviors = {
        "Wander", "SmallWander", "LargeWander", "EventWander",
        "Patrol", "RandomPatrol", "CirclePatrol", "EventTarget", "Chase"
    };
    std::uniform_int_distribution<size_t> behaviorDist(0, behaviors.size() - 1);

    // Create batch of NPCs
    int attempts = 0;
    int created = 0;
    const int maxAttempts = count * 10;  // Allow multiple attempts to find valid positions

    while (created < count && attempts < maxAttempts) {
      attempts++;

      // Pick a random tile
      int tileX = tileDistX(gen);
      int tileY = tileDistY(gen);

      // Check if tile is valid (not water, not a building)
      if (tileY >= 0 && tileY < static_cast<int>(worldData->grid.size()) &&
          tileX >= 0 && tileX < static_cast<int>(worldData->grid[tileY].size())) {

        const auto &tile = worldData->grid[tileY][tileX];

        // Only spawn on walkable ground (not water, not buildings)
        if (!tile.isWater && tile.obstacleType != HammerEngine::ObstacleType::BUILDING) {
          // Random position within the tile
          std::uniform_real_distribution<float> offsetDist(0.0f, tileSize);
          float x = tileX * tileSize + offsetDist(gen);
          float y = tileY * tileSize + offsetDist(gen);
          Vector2D position(x, y);

          try {
            auto npc = NPC::create("npc", position);
            npc->initializeInventory();
            npc->setWanderArea(0, 0, m_worldWidth, m_worldHeight);

            // Assign random behavior from the list
            std::string randomBehavior = behaviors[behaviorDist(gen)];
            aiMgr.registerEntityForUpdates(npc, rand() % 9 + 1, randomBehavior);

            m_npcs.push_back(npc);
            created++;
          } catch (const std::exception &e) {
            GAMESTATE_ERROR("Exception creating NPC with random behavior: " + std::string(e.what()));
          }
        }
      }
    }

    if (created < count) {
      GAMESTATE_WARN("Only created " + std::to_string(created) + " of " +
                     std::to_string(count) + " requested NPCs with random behaviors after " +
                     std::to_string(attempts) + " attempts");
    }

  } catch (const std::exception &e) {
    GAMESTATE_ERROR("Exception in createNPCBatchWithRandomBehaviors(): " + std::string(e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in createNPCBatchWithRandomBehaviors()");
  }
}

void AIDemoState::initializeCamera() {
  const auto &gameEngine = GameEngine::Instance();

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
    // Disable camera event firing for consistency with other demo states
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
    config.clampToWorldBounds = true; // Keep camera within world
    m_camera->setConfig(config);

    // Camera auto-synchronizes world bounds on update
  }
}

void AIDemoState::updateCamera(float deltaTime) {
  if (m_camera) {
    // Sync viewport with current window size (handles resize events)
    m_camera->syncViewportWithEngine();

    // Update camera position and following logic
    m_camera->update(deltaTime);
  }
}
