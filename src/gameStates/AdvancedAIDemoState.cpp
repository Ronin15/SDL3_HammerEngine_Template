/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/AdvancedAIDemoState.hpp"
#include "ai/behaviors/AttackBehavior.hpp"
#include "ai/behaviors/FleeBehavior.hpp"
#include "ai/behaviors/FollowBehavior.hpp"
#include "ai/behaviors/GuardBehavior.hpp"
#include "ai/behaviors/IdleBehavior.hpp"
#include "controllers/combat/CombatController.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "gameStates/LoadingState.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/ParticleManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldManager.hpp"

#include <cmath>
#include <cstddef>
#include <format>

AdvancedAIDemoState::~AdvancedAIDemoState() {
  // Don't call virtual functions from destructors

  try {
    // Note: Proper cleanup should already have happened in exit()
    // This destructor is just a safety measure in case exit() wasn't called

    // Reset AI behaviors first to clear entity references
    AIManager &aiMgr = AIManager::Instance();
    aiMgr.resetBehaviors();

    // Clear NPCs via NPCRenderController
    m_npcRenderCtrl.clearSpawnedNPCs();

    // Clean up player
    m_player.reset();

    GAMESTATE_INFO("Exiting AdvancedAIDemoState in destructor...");
  } catch (const std::exception &e) {
    GAMESTATE_ERROR(std::format(
        "Exception in AdvancedAIDemoState destructor: {}", e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in AdvancedAIDemoState destructor");
  }
}

void AdvancedAIDemoState::handleInput() {
  // Cache manager references for better performance
  InputManager &inputMgr = InputManager::Instance();
  AIManager &aiMgr = AIManager::Instance();

  // Use InputManager's new event-driven key press detection
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_SPACE)) {
    // Toggle pause/resume
    m_aiPaused = !m_aiPaused;

    // Set global AI pause state in AIManager
    aiMgr.setGlobalPause(m_aiPaused);

    // Also send messages for behaviors that need them
    std::string message = m_aiPaused ? "pause" : "resume";
    aiMgr.broadcastMessage(message, true);

    // Simple feedback
    GAMESTATE_INFO(
        std::format("Advanced AI {}", m_aiPaused ? "PAUSED" : "RESUMED"));
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
    GAMESTATE_INFO("Preparing to exit AdvancedAIDemoState...");
    mp_stateManager->changeState("MainMenuState");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_1)) {
    // Assign Idle behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to IDLE behavior");
    EntityDataManager &edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Idle");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_2)) {
    // Assign Flee behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to FLEE behavior");
    EntityDataManager &edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Flee");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_3)) {
    // Assign Follow behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to FOLLOW behavior");
    EntityDataManager &edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Follow");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_4)) {
    // Assign Guard behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to GUARD behavior");
    EntityDataManager &edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Guard");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_5)) {
    // Assign Attack behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to ATTACK behavior");
    EntityDataManager &edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Attack");
      }
    }
  }

  // Camera zoom controls
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_LEFTBRACKET) && m_camera) {
    m_camera->zoomIn(); // [ key = zoom in (objects larger)
  }
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_RIGHTBRACKET) && m_camera) {
    m_camera->zoomOut(); // ] key = zoom out (objects smaller)
  }
}

bool AdvancedAIDemoState::enter() {
  // Resume all game managers (may be paused from menu states)
  GameEngine::Instance().setGlobalPause(false);

  GAMESTATE_INFO("Entering AdvancedAIDemoState...");

  // Reset transition flag when entering state
  m_transitioningToLoading = false;

  // Check if already initialized (resuming after LoadingState)
  if (m_initialized) {
    GAMESTATE_INFO("Already initialized - resuming AdvancedAIDemoState");
    return true; // Skip all loading logic
  }

  // Check if world needs to be loaded
  if (!m_worldLoaded) {
    GAMESTATE_INFO("World not loaded yet - will transition to LoadingState on "
                   "first update");
    m_needsLoading = true;
    m_worldLoaded = true; // Mark as loaded to prevent loop on re-entry
    return true;          // Will transition to loading screen in update()
  }

  // World is loaded - proceed with normal initialization
  GAMESTATE_INFO("World already loaded - initializing advanced AI demo");

  try {
    // Cache GameEngine reference for better performance
    const GameEngine &gameEngine = GameEngine::Instance();
    auto &worldManager = WorldManager::Instance();

    // Setup world size using actual world bounds from loaded world
    float worldMinX, worldMinY, worldMaxX, worldMaxY;
    if (worldManager.getWorldBounds(worldMinX, worldMinY, worldMaxX,
                                    worldMaxY)) {
      m_worldWidth = worldMaxX;
      m_worldHeight = worldMaxY;
      GAMESTATE_INFO(std::format("World dimensions: {} x {} pixels",
                                 m_worldWidth, m_worldHeight));
    } else {
      // Fallback to screen dimensions if world bounds unavailable
      m_worldWidth = gameEngine.getLogicalWidth();
      m_worldHeight = gameEngine.getLogicalHeight();
    }

    // Create player first (required for flee/follow/attack behaviors)
    m_player = std::make_shared<Player>();
    m_player->ensurePhysicsBodyRegistered();
    m_player->initializeInventory(); // Initialize inventory after construction
    m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

    // Initialize camera (world is already loaded by LoadingState)
    initializeCamera();

    // Initialize PathfinderManager for Follow behavior pathfinding
    PathfinderManager &pathfinderMgr = PathfinderManager::Instance();
    if (!pathfinderMgr.isInitialized()) {
      pathfinderMgr.init();
      GAMESTATE_INFO("PathfinderManager initialized for Follow behavior");
    }

    // Manually trigger grid rebuild now that world is loaded
    // This ensures the pathfinding grid is ready before NPCs start following
    pathfinderMgr.rebuildGrid();
    GAMESTATE_INFO("PathfinderManager grid rebuild initiated");

    // Cache AIManager reference for better performance
    AIManager &aiMgr = AIManager::Instance();

    // Set player reference in AIManager BEFORE registering behaviors
    // This ensures Follow/Flee/Attack behaviors can access the player target
    // Explicitly cast PlayerPtr to EntityPtr to ensure proper conversion
    EntityPtr playerAsEntity = std::static_pointer_cast<Entity>(m_player);
    aiMgr.setPlayerHandle(playerAsEntity->getHandle());

    // Setup advanced AI behaviors AFTER world is initialized and player is set
    // This ensures Guard behavior uses correct world dimensions and Follow has
    // player target
    setupAdvancedAIBehaviors();

    // Register CombatController (follows GamePlayState pattern)
    m_controllers.add<CombatController>(m_player);
    m_controllers.subscribeAll();

    // Create NPCs with optimized counts for behavior showcasing
    createAdvancedNPCs();

    // Create advanced HUD UI (matches EventDemoState spacing pattern)
    auto &ui = UIManager::Instance();
    ui.createTitle("advanced_ai_title",
                   {0, UIConstants::TITLE_TOP_OFFSET,
                    gameEngine.getLogicalWidth(),
                    UIConstants::DEFAULT_TITLE_HEIGHT},
                   "Advanced AI Demo State");
    ui.setTitleAlignment("advanced_ai_title", UIAlignment::CENTER_CENTER);
    // Set auto-repositioning: top-aligned, full width
    ui.setComponentPositioning("advanced_ai_title",
                               {UIPositionMode::TOP_ALIGNED, 0,
                                UIConstants::TITLE_TOP_OFFSET, -1,
                                UIConstants::DEFAULT_TITLE_HEIGHT});

    ui.createLabel(
        "advanced_ai_instructions_line1",
        {UIConstants::INFO_LABEL_MARGIN_X, UIConstants::INFO_FIRST_LINE_Y,
         gameEngine.getLogicalWidth() - 2 * UIConstants::INFO_LABEL_MARGIN_X,
         UIConstants::INFO_LABEL_HEIGHT},
        "Advanced AI Demo: [B] Exit | [SPACE] Pause/Resume | [1] Idle | [2] "
        "Flee | [3] Follow");
    // Set auto-repositioning: top-aligned, full width minus margins
    ui.setComponentPositioning(
        "advanced_ai_instructions_line1",
        {UIPositionMode::TOP_ALIGNED, UIConstants::INFO_LABEL_MARGIN_X,
         UIConstants::INFO_FIRST_LINE_Y, -2 * UIConstants::INFO_LABEL_MARGIN_X,
         UIConstants::INFO_LABEL_HEIGHT});

    const int line2Y = UIConstants::INFO_FIRST_LINE_Y +
                       UIConstants::INFO_LABEL_HEIGHT +
                       UIConstants::INFO_LINE_SPACING;
    ui.createLabel(
        "advanced_ai_instructions_line2",
        {UIConstants::INFO_LABEL_MARGIN_X, line2Y,
         gameEngine.getLogicalWidth() - 2 * UIConstants::INFO_LABEL_MARGIN_X,
         UIConstants::INFO_LABEL_HEIGHT},
        "Combat & Social: [4] Guard | [5] Attack");
    // Set auto-repositioning: top-aligned, full width minus margins
    ui.setComponentPositioning("advanced_ai_instructions_line2",
                               {UIPositionMode::TOP_ALIGNED,
                                UIConstants::INFO_LABEL_MARGIN_X, line2Y,
                                -2 * UIConstants::INFO_LABEL_MARGIN_X,
                                UIConstants::INFO_LABEL_HEIGHT});

    const int statusY = line2Y + UIConstants::INFO_LABEL_HEIGHT +
                        UIConstants::INFO_LINE_SPACING +
                        UIConstants::INFO_STATUS_SPACING;
    ui.createLabel("advanced_ai_status",
                   {UIConstants::INFO_LABEL_MARGIN_X, statusY, 400,
                    UIConstants::INFO_LABEL_HEIGHT},
                   "FPS: -- | NPCs: -- | AI: RUNNING | Combat: ON");
    // Set auto-repositioning: top-aligned with calculated position (fixes
    // fullscreen transition)
    ui.setComponentPositioning("advanced_ai_status",
                               {UIPositionMode::TOP_ALIGNED,
                                UIConstants::INFO_LABEL_MARGIN_X, statusY, 400,
                                UIConstants::INFO_LABEL_HEIGHT});

    // Log status
    GAMESTATE_INFO(std::format("Created {} NPCs with advanced AI behaviors",
                               EntityDataManager::Instance().getIndicesByKind(EntityKind::NPC).size()));
    GAMESTATE_INFO("CombatController registered");

    // Pre-allocate status buffer to avoid per-frame allocations
    m_statusBuffer.reserve(64);

    // Mark as fully initialized to prevent re-entering loading logic
    m_initialized = true;

    return true;
  } catch (const std::exception &e) {
    GAMESTATE_ERROR(
        std::format("Exception in AdvancedAIDemoState::enter(): {}", e.what()));
    return false;
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in AdvancedAIDemoState::enter()");
    return false;
  }
}

bool AdvancedAIDemoState::exit() {
  GAMESTATE_INFO("Exiting AdvancedAIDemoState...");

  // Cache manager references for better performance
  AIManager &aiMgr = AIManager::Instance();
  EntityDataManager &edm = EntityDataManager::Instance();
  CollisionManager &collisionMgr = CollisionManager::Instance();
  PathfinderManager &pathfinderMgr = PathfinderManager::Instance();
  UIManager &ui = UIManager::Instance();
  WorldManager &worldMgr = WorldManager::Instance();

  if (m_transitioningToLoading) {
    // Transitioning to LoadingState - do cleanup but preserve m_worldLoaded
    // flag This prevents infinite loop when returning from LoadingState

    // Reset the flag after using it
    m_transitioningToLoading = false;

    // CRITICAL: Clear NPCs and player BEFORE prepareForStateTransition()
    // NPCs hold EDM indices - must be destroyed while EDM data is still valid
    m_npcRenderCtrl.clearSpawnedNPCs();
    if (m_player) {
      m_player.reset();
    }

    // Now safe to clear manager state
    // CRITICAL: PathfinderManager MUST be cleaned BEFORE EDM
    // Pending path tasks hold captured edmIndex values - they must complete or
    // see the transition flag before EDM clears its data
    if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
      pathfinderMgr.prepareForStateTransition();
    }

    aiMgr.prepareForStateTransition();
    edm.prepareForStateTransition();

    if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
      collisionMgr.prepareForStateTransition();
    }

    // Clean up camera
    m_camera.reset();

    // Clean up UI
    ui.prepareForStateTransition();

    // Unload world (LoadingState will reload it)
    if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
      worldMgr.unloadWorld();
      // CRITICAL: DO NOT reset m_worldLoaded here - keep it true to prevent
      // infinite loop when LoadingState returns to this state
    }

    // Restore AI to unpaused state
    aiMgr.setGlobalPause(false);
    m_aiPaused = false;

    // Reset initialized flag so state re-initializes after loading
    m_initialized = false;

    // Keep m_worldLoaded = true to remember we've already been through loading
    GAMESTATE_INFO(
        "AdvancedAIDemoState cleanup for LoadingState transition complete");
    return true;
  }

  // Full exit (going to main menu, other states, or shutting down)

  // CRITICAL: Clear NPCs and player BEFORE prepareForStateTransition()
  // NPCs hold EDM indices - must be destroyed while EDM data is still valid
  m_npcRenderCtrl.clearSpawnedNPCs();
  if (m_player) {
    m_player.reset();
  }

  // Now safe to clear manager state
  // CRITICAL: PathfinderManager MUST be cleaned BEFORE EDM
  // Pending path tasks hold captured edmIndex values - they must complete or
  // see the transition flag before EDM clears its data
  if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
    pathfinderMgr.prepareForStateTransition();
  }

  aiMgr.prepareForStateTransition();
  edm.prepareForStateTransition();

  // Clean collision state
  if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
    collisionMgr.prepareForStateTransition();
  }

  // Clean up camera first to stop world rendering
  m_camera.reset();

  // Clean up UI components using simplified method
  ui.prepareForStateTransition();

  // Unload the world when fully exiting, but only if there's actually a world
  // loaded This matches EventDemoState's safety pattern and prevents crashes
  if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
    worldMgr.unloadWorld();
    // Reset m_worldLoaded when doing full exit (going to main menu, etc.)
    m_worldLoaded = false;
  }

  // Always restore AI to unpaused state when exiting the demo state
  aiMgr.setGlobalPause(false);
  m_aiPaused = false;

  // Reset initialization flag for next fresh start
  m_initialized = false;

  GAMESTATE_INFO("AdvancedAIDemoState exit complete");
  return true;
}

void AdvancedAIDemoState::update(float deltaTime) {
  try {
    // Check if we need to transition to loading screen (do this in update, not
    // enter)
    if (m_needsLoading) {
      m_needsLoading = false; // Clear flag

      GAMESTATE_INFO("Transitioning to LoadingState for world generation");

      // Create world configuration for advanced AI demo
      HammerEngine::WorldGenerationConfig config;
      config.width = 350; // Medium-sized world for advanced AI showcase
      config.height = 350;
      config.seed = static_cast<int>(std::time(nullptr));
      config.elevationFrequency = 0.08f;
      config.humidityFrequency = 0.06f;
      config.waterLevel = 0.3f;
      config.mountainLevel = 0.7f;

      // Configure LoadingState and transition to it
      auto *loadingState = dynamic_cast<LoadingState *>(
          mp_stateManager->getState("LoadingState").get());
      if (loadingState) {
        loadingState->configure("AdvancedAIDemoState", config);
        // Set flag before transitioning to preserve m_worldLoaded in exit()
        m_transitioningToLoading = true;
        // Use changeState (called from update) to properly exit and re-enter
        mp_stateManager->changeState("LoadingState");
      } else {
        GAMESTATE_ERROR("LoadingState not found in GameStateManager");
      }

      return; // Don't continue with rest of update
    }

    // Update player
    if (m_player) {
      m_player->update(deltaTime);
    }

    // Cache manager references for better performance
    UIManager &ui = UIManager::Instance();

    // Update data-driven NPCs (animations handled by NPCRenderController)
    m_npcRenderCtrl.update(deltaTime);

    // Update camera (follows player automatically)
    updateCamera(deltaTime);

    // Update controllers (CombatController handles cooldowns, stamina regen)
    m_controllers.updateAll(deltaTime);

    // Update UI (moved from render path for consistent frame timing)
    if (!ui.isShutdown()) {
      ui.update(deltaTime);
    }

  } catch (const std::exception &e) {
    GAMESTATE_ERROR(std::format(
        "Exception in AdvancedAIDemoState::update(): {}", e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in AdvancedAIDemoState::update()");
  }
}

void AdvancedAIDemoState::render(SDL_Renderer *renderer,
                                 float interpolationAlpha) {
  // Cache manager references for better performance
  WorldManager &worldMgr = WorldManager::Instance();
  UIManager &ui = UIManager::Instance();

  // Camera offset with unified interpolation (single atomic read for sync)
  float renderCamX = 0.0f;
  float renderCamY = 0.0f;
  float viewWidth = 0.0f;
  float viewHeight = 0.0f;
  Vector2D playerInterpPos; // Position synced with camera

  // Set render scale for zoom only when changed (avoids GPU state change
  // overhead)
  float zoom = m_camera ? m_camera->getZoom() : 1.0f;
  if (zoom != m_lastRenderedZoom) {
    SDL_SetRenderScale(renderer, zoom, zoom);
    m_lastRenderedZoom = zoom;
  }

  if (m_camera) {
    // Returns the position used for offset - use it for player rendering
    playerInterpPos =
        m_camera->getRenderOffset(renderCamX, renderCamY, interpolationAlpha);

    // Derive view dimensions from viewport/zoom (no per-frame GameEngine calls)
    viewWidth = m_camera->getViewport().width / zoom;
    viewHeight = m_camera->getViewport().height / zoom;
  }

  // Render world first (background layer) using pixel-snapped camera
  if (m_camera && worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
    worldMgr.render(renderer, renderCamX, renderCamY, viewWidth, viewHeight);
  }

  // Render data-driven NPCs via NPCRenderController
  m_npcRenderCtrl.renderNPCs(renderer, renderCamX, renderCamY, interpolationAlpha);

  // Render player at the position camera used for offset calculation
  if (m_player) {
    // Use position camera returned - no separate atomic read
    m_player->renderAtPosition(renderer, playerInterpPos, renderCamX,
                               renderCamY);

    // TODO: Player health bar rendering using m_player->getHealth() /
    // m_player->getMaxHealth()
  }

  // Reset render scale to 1.0 for UI rendering only when needed (UI should not
  // be zoomed)
  if (m_lastRenderedZoom != 1.0f) {
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    m_lastRenderedZoom = 1.0f;
  }

  // Render UI components (update moved to update() for consistent frame timing)
  if (!ui.isShutdown()) {
    // Update status only when values change (C++20 type-safe, zero allocations)
    // Use m_aiPaused member instead of polling AIManager::isGloballyPaused()
    // every frame
    int currentFPS =
        static_cast<int>(std::lround(mp_stateManager->getCurrentFPS()));
    size_t npcCount = EntityDataManager::Instance().getIndicesByKind(EntityKind::NPC).size();

    if (currentFPS != m_lastDisplayedFPS ||
        npcCount != m_lastDisplayedNPCCount ||
        m_aiPaused != m_lastDisplayedPauseState) {

      m_statusBuffer.clear();
      std::format_to(std::back_inserter(m_statusBuffer),
                     "FPS: {} | NPCs: {} | AI: {} | Combat: ON", currentFPS,
                     npcCount, m_aiPaused ? "PAUSED" : "RUNNING");
      ui.setText("advanced_ai_status", m_statusBuffer);

      m_lastDisplayedFPS = currentFPS;
      m_lastDisplayedNPCCount = npcCount;
      m_lastDisplayedPauseState = m_aiPaused;
    }
  }
  ui.render(renderer);
}

void AdvancedAIDemoState::setupAdvancedAIBehaviors() {
  GAMESTATE_INFO("AdvancedAIDemoState: Setting up advanced AI behaviors...");

  // Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();
  // TODO: Need to see if we can move all of these behaviors into the AIManager
  // and just have events trigger NPCs creation with behavior
  //  Register Idle behavior
  if (!aiMgr.hasBehavior("Idle")) {
    auto idleBehavior = std::make_unique<IdleBehavior>(
        IdleBehavior::IdleMode::LIGHT_FIDGET, 50.0f);
    aiMgr.registerBehavior("Idle", std::move(idleBehavior));
    GAMESTATE_INFO("AdvancedAIDemoState: Registered Idle behavior");
  }

  // Register Flee behavior
  if (!aiMgr.hasBehavior("Flee")) {
    auto fleeBehavior = std::make_unique<FleeBehavior>(
        60.0f, 150.0f, 200.0f); // speed, detection range, safe distance
    aiMgr.registerBehavior("Flee", std::move(fleeBehavior));
    GAMESTATE_INFO("AdvancedAIDemoState: Registered Flee behavior");
  }

  // Register Follow behavior
  if (!aiMgr.hasBehavior("Follow")) {
    auto followBehavior = std::make_unique<FollowBehavior>(
        80.0f, 50.0f, 300.0f); // follow speed, follow distance, max distance
    followBehavior->setStopWhenTargetStops(
        false); // Always follow, even if player is stationary
    aiMgr.registerBehavior("Follow", std::move(followBehavior));
    GAMESTATE_INFO("AdvancedAIDemoState: Registered Follow behavior");
  }

  // Register Guard behavior
  if (!aiMgr.hasBehavior("Guard")) {
    auto guardBehavior = std::make_unique<GuardBehavior>(
        Vector2D(m_worldWidth / 2, m_worldHeight / 2), 150.0f,
        200.0f); // position, guard radius, alert radius
    aiMgr.registerBehavior("Guard", std::move(guardBehavior));
    GAMESTATE_INFO("AdvancedAIDemoState: Registered Guard behavior");
  }

  // Register Attack behavior
  if (!aiMgr.hasBehavior("Attack")) {
    auto attackBehavior = std::make_unique<AttackBehavior>(
        80.0f, 1.0f, 63.75f); // range, cooldown, speed
    aiMgr.registerBehavior("Attack", std::move(attackBehavior));
    GAMESTATE_INFO("AdvancedAIDemoState: Registered Attack behavior");
  }

  GAMESTATE_INFO("AdvancedAIDemoState: Advanced AI behaviors setup complete.");
}

void AdvancedAIDemoState::createAdvancedNPCs() {
  // TODO: Make an event for advanced NPC's
  //  Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();
  EntityDataManager &edm = EntityDataManager::Instance();

  try {
    // Random number generation for positioning near player
    std::random_device rd;
    std::mt19937 gen(rd());

    // Player is at world center - spawn NPCs in a reasonable radius around
    // player
    Vector2D playerPos = m_player->getPosition();
    float spawnRadius =
        400.0f; // Spawn within ~400 pixels of player (visible on screen)
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159f);
    std::uniform_real_distribution<float> radiusDist(100.0f, spawnRadius);

    // Create NPCs with optimized counts for behavior demonstration
    for (int i = 0; i < m_totalNPCCount; ++i) {
      try {
        // Create NPC with strategic positioning near player for behavior
        // showcase
        Vector2D position;

        // Position all NPCs in a circle around the player for easy visibility
        float angle = angleDist(gen);
        float distance = radiusDist(gen);

        position = Vector2D(playerPos.getX() + distance * std::cos(angle),
                            playerPos.getY() + distance * std::sin(angle));

        // Create data-driven NPC via EntityDataManager type registry
        EntityHandle handle = edm.createDataDrivenNPC(position, "Guard");

        if (!handle.isValid()) {
          GAMESTATE_ERROR(std::format("Failed to create data-driven NPC {}", i));
          continue;
        }

        // Register with AIManager (default to Follow behavior for all NPCs)
        aiMgr.registerEntity(handle, "Follow");

      } catch (const std::exception &e) {
        GAMESTATE_ERROR(
            std::format("Exception creating advanced NPC {}: {}", i, e.what()));
        continue;
      }
    }

    GAMESTATE_INFO(
        std::format("AdvancedAIDemoState: Created {} NPCs",
                    edm.getIndicesByKind(EntityKind::NPC).size()));
  } catch (const std::exception &e) {
    GAMESTATE_ERROR(
        std::format("Exception in createAdvancedNPCs(): {}", e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in createAdvancedNPCs()");
  }
}

void AdvancedAIDemoState::initializeCamera() {
  const auto &gameEngine = GameEngine::Instance();

  // Initialize camera at player's position to avoid any interpolation jitter
  Vector2D playerPosition = m_player ? m_player->getPosition() : Vector2D(0, 0);

  // Create camera starting at player position
  m_camera = std::make_unique<HammerEngine::Camera>(
      playerPosition.getX(), playerPosition.getY(), // Start at player position
      static_cast<float>(gameEngine.getLogicalWidth()),
      static_cast<float>(gameEngine.getLogicalHeight()));

  // Configure camera to follow player
  if (m_player) {
    // Disable camera event firing for consistency
    m_camera->setEventFiringEnabled(false);

    // Set target and enable follow mode
    std::weak_ptr<Entity> playerAsEntity =
        std::static_pointer_cast<Entity>(m_player);
    m_camera->setTarget(playerAsEntity);
    m_camera->setMode(HammerEngine::Camera::Mode::Follow);

    // Set up camera configuration for fast, smooth following
    // Using exponential smoothing for smooth, responsive follow
    HammerEngine::Camera::Config config;
    config.followSpeed = 5.0f;      // Speed of camera interpolation
    config.deadZoneRadius = 0.0f;   // No dead zone - always follow
    config.smoothingFactor = 0.85f; // Smoothing factor (0-1, higher = smoother)
    config.clampToWorldBounds = true; // Keep camera within world
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
