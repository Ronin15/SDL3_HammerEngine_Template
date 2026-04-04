/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/GamePlayState.hpp"
#include "entities/Player.hpp"
#include "controllers/combat/CombatController.hpp"
#include "controllers/social/SocialController.hpp"
#include "controllers/world/DayNightController.hpp"
#include "controllers/world/HarvestController.hpp"
#include "controllers/world/ItemController.hpp"
#include "controllers/world/WeatherController.hpp"
#include "controllers/render/ResourceRenderController.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "gameStates/GameOverState.hpp"
#include "gameStates/LoadingState.hpp"
#include "gameStates/PauseState.hpp"
#include "events/HarvestResourceEvent.hpp"
#include "events/EntityEvents.hpp"
#include "managers/AIManager.hpp"
#include "managers/BackgroundSimulationManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/GameTimeManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/ParticleManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/UIConstants.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include "managers/ProjectileManager.hpp"
#include "core/WorkerBudget.hpp"
#include "utils/Camera.hpp"
#include "world/WorldData.hpp"
#include <algorithm>
#include <cmath>
#include <format>

#include "gpu/GPURenderer.hpp"
#include "gpu/SpriteBatch.hpp"
#include "utils/GPUSceneRecorder.hpp"

// Constructor/destructor defined here where GPUSceneRecorder is complete (for unique_ptr)
GamePlayState::GamePlayState()
    : m_transitioningToLoading{false}, m_transitioningToGameOver{false},
      mp_Player{nullptr}, m_inventoryVisible{false}, m_initialized{false},
      m_dayNightEventToken{}, m_weatherEventToken{}, m_harvestEventToken{} {}

GamePlayState::~GamePlayState() = default;

bool GamePlayState::enter() {
  // Cache GameEngine reference at function start
  auto &gameEngine = GameEngine::Instance();

  // Resume all game managers (may be paused from menu states)
  gameEngine.setGlobalPause(false);

  // Reset transition flag when entering state
  m_transitioningToLoading = false;
  m_transitioningToGameOver = false;

  // Check if world needs to be loaded
  if (!m_worldLoaded) {
    GAMEPLAY_INFO("World not loaded yet - will transition to LoadingState on "
                  "first update");
    m_needsLoading = true;
    m_worldLoaded = true; // Mark as loaded to prevent loop on re-entry
    return true;          // Will transition to loading screen in update()
  }

  // World is loaded - proceed with normal initialization
  GAMEPLAY_INFO("World already loaded - initializing gameplay");

  try {
    // Local references for init-only managers (not cached as members)
    auto &gameTimeMgr = GameTimeManager::Instance();

    // Create player and position at screen center
    mp_Player = std::make_shared<Player>();
    mp_Player->ensurePhysicsBodyRegistered();
    mp_Player->initializeInventory();

    // Position player at screen center
    Vector2D const screenCenter(gameEngine.getLogicalWidth() / 2.0,
                                gameEngine.getLogicalHeight() / 2.0);
    mp_Player->setPosition(screenCenter);

    // Set player handle in AIManager for collision culling reference point
    AIManager::Instance().setPlayerHandle(mp_Player->getHandle());

    // Initialize the inventory UI
    initializeInventoryUI();

    // Initialize camera (world already loaded)
    initializeCamera();

    // Create GPU scene recorder for coordinated GPU rendering
    m_gpuSceneRecorder = std::make_unique<HammerEngine::GPUSceneRecorder>();

    // Register controllers with the registry
    m_controllers.add<WeatherController>();
    m_controllers.add<DayNightController>();
    m_controllers.add<CombatController>(mp_Player);
    m_controllers.add<ItemController>(mp_Player);
    m_controllers.add<HarvestController>(mp_Player);
    m_controllers.add<ResourceRenderController>();

    // Social controller (handles both relationship management and trade UI)
    m_controllers.add<SocialController>(mp_Player);

    // Enable automatic weather changes
    gameTimeMgr.enableAutoWeather(true);
#ifdef NDEBUG
    // Release: normal pacing
    gameTimeMgr.setWeatherCheckInterval(4.0f);
    gameTimeMgr.setTimeScale(60.0f);
#else
    // Debug: faster changes for testing seasons/weather
    gameTimeMgr.setWeatherCheckInterval(1.0f);
    gameTimeMgr.setTimeScale(3600.0f);
#endif

    // Cache UI manager reference for better performance
    auto &ui = UIManager::Instance();

    // Create event log for time/weather messages
    ui.createEventLog("gameplay_event_log",
                      {10, ui.getLogicalHeight() - 200, 730, 180}, 7);
    UIPositioning eventLogPos;
    eventLogPos.mode = UIPositionMode::BOTTOM_ALIGNED;
    eventLogPos.offsetX = 10;
    eventLogPos.offsetY = 20;
    eventLogPos.fixedHeight = 180;
    eventLogPos.widthPercent = UIConstants::EVENT_LOG_WIDTH_PERCENT;
    ui.setComponentPositioning("gameplay_event_log", eventLogPos);

    // Create time status label at top-right of screen (no panel, just label)
    int const barHeight = UIConstants::STATUS_BAR_HEIGHT;
    int labelPadding = UIConstants::STATUS_BAR_LABEL_PADDING;

    ui.createLabel("gameplay_time_label",
                   {labelPadding, 6, ui.getLogicalWidth() - 2 * labelPadding,
                    barHeight - 12},
                   "");

    // Right-align the text within the label
    ui.setLabelAlignment("gameplay_time_label", UIAlignment::CENTER_RIGHT);

    // Full-width positioning for resize handling
    UIPositioning labelPos;
    labelPos.mode = UIPositionMode::TOP_ALIGNED;
    labelPos.offsetX = labelPadding;
    labelPos.offsetY = 6;                    // Small vertical offset from top
    labelPos.fixedWidth = -2 * labelPadding; // Full width minus margins
    labelPos.fixedHeight = barHeight - 12;
    ui.setComponentPositioning("gameplay_time_label", labelPos);

    // Pre-allocate status buffer for zero per-frame allocations
    m_statusBuffer.reserve(256);

    // Create FPS counter label (top-left, initially hidden, toggled with F2)
    ui.createLabel("gameplay_fps", {labelPadding, 6, 120, barHeight - 12},
                   "FPS: --");
    ui.setComponentVisible("gameplay_fps", false);
    UIPositioning fpsPos;
    fpsPos.mode = UIPositionMode::TOP_ALIGNED;
    fpsPos.offsetX = labelPadding;
    fpsPos.offsetY = 6;
    fpsPos.fixedWidth = 120;
    fpsPos.fixedHeight = barHeight - 12;
    ui.setComponentPositioning("gameplay_fps", fpsPos);

    // Subscribe all controllers at once
    m_controllers.subscribeAll();

    // Initialize combat HUD (health/stamina bars, target frame)
    UIManager::Instance().createCombatHUD();

    registerEventHandlers();

    // Mark as initialized for future pause/resume cycles
    m_initialized = true;

    GAMEPLAY_INFO("GamePlayState initialization complete");
  } catch (const std::exception &e) {
    GAMEPLAY_ERROR(std::format(
        "Exception during GamePlayState initialization: {}", e.what()));
    return false;
  }

  return true;
}

void GamePlayState::update(float deltaTime) {
  // Cache manager references for better performance
  GameTimeManager &gameTimeMgr = GameTimeManager::Instance();
  auto &ui = UIManager::Instance();

  // Check if we need to transition to loading screen (do this in update, not
  // enter)
  if (m_needsLoading) {
    m_needsLoading = false; // Clear flag

    GAMEPLAY_INFO("Transitioning to LoadingState for world generation");

    // Create world configuration for gameplay
    HammerEngine::WorldGenerationConfig config;
    config.width = 200; // Standard gameplay world
    config.height = 200;
    config.seed = static_cast<int>(std::time(nullptr));
    config.elevationFrequency = 0.018f;  // Lower frequency = larger biome regions
    config.humidityFrequency = 0.012f;
    config.waterLevel = 0.28f;
    config.mountainLevel = 0.72f;

    // Configure LoadingState and transition to it
    auto *loadingState = dynamic_cast<LoadingState *>(
        mp_stateManager->getState("LoadingState").get());
    if (loadingState) {
      loadingState->configure("GamePlayState", config);
      // Set flag before transitioning to preserve m_worldLoaded in exit()
      m_transitioningToLoading = true;
      // Use changeState (called from update) to properly exit and re-enter
      mp_stateManager->changeState("LoadingState");
    }

    return; // Don't continue with rest of update
  }

  // Update game time (advances calendar, dispatches time events)
  gameTimeMgr.update(deltaTime);

  // Update player if it exists
  if (mp_Player) {
    mp_Player->update(deltaTime);

    // Update all IUpdatable controllers (combat cooldowns, stamina regen, etc.)
    m_controllers.updateAll(deltaTime);

    // Update data-driven NPCs (animations handled by NPCRenderController)
    m_npcRenderCtrl.update(deltaTime);

    // Update combat HUD (health/stamina bars, target frame)
    auto& combatCtrl = *m_controllers.get<CombatController>();
    UIManager::Instance().updateCombatHUD(
        mp_Player->getHealth(),
        mp_Player->getStamina(),
        combatCtrl.hasActiveTarget(),
        "Target",
        combatCtrl.getTargetHealth());

    if (!mp_Player->isAlive() && !m_transitioningToGameOver) {
      m_transitioningToGameOver = true;

      if (!mp_stateManager->hasState("GameOverState")) {
        mp_stateManager->addState(std::make_unique<GameOverState>());
      }

      mp_stateManager->changeState("GameOverState");
      return;
    }
  }

  // Update camera (follows player automatically)
  updateCamera(deltaTime);

  // Update resource animations (dropped items bobbing, etc.) - camera-based culling
  if (auto* resourceCtrl = m_controllers.get<ResourceRenderController>(); resourceCtrl && m_camera) {
    resourceCtrl->update(deltaTime, *m_camera);
  }

  // Update day/night controller (handles GPU lighting via shader)
  if (auto* dayNightCtrl = m_controllers.get<DayNightController>()) {
    dayNightCtrl->update(deltaTime);
  }

  // Update time status bar only when events fire (event-driven, not per-frame)
  if (m_statusBarDirty) {
    m_statusBarDirty = false;
    m_statusBuffer.clear();
    std::format_to(std::back_inserter(m_statusBuffer),
                   "Day {} {}, Year {} | {} {} | {} | {}F | {}",
                   gameTimeMgr.getDayOfMonth(),
                   gameTimeMgr.getCurrentMonthName(), gameTimeMgr.getGameYear(),
                   gameTimeMgr.formatCurrentTime(),
                   gameTimeMgr.getTimeOfDayName(), gameTimeMgr.getSeasonName(),
                   static_cast<int>(gameTimeMgr.getCurrentTemperature()),
                   m_controllers.get<WeatherController>()->getCurrentWeatherString());
    ui.setText("gameplay_time_label", m_statusBuffer);
  }

  // Update UI
  if (!ui.isShutdown()) {
    ui.update(deltaTime);
  }

  // Inventory display is now updated automatically via data binding.
}

bool GamePlayState::exit() {
  // Cache manager references for better performance
  AIManager &aiMgr = AIManager::Instance();
  BackgroundSimulationManager &bgSimMgr = BackgroundSimulationManager::Instance();
  EntityDataManager &edm = EntityDataManager::Instance();
  CollisionManager &collisionMgr = CollisionManager::Instance();
  PathfinderManager &pathfinderMgr = PathfinderManager::Instance();
  ParticleManager &particleMgr = ParticleManager::Instance();
  auto &ui = UIManager::Instance();
  WorldManager &worldMgr = WorldManager::Instance();
  GameTimeManager &gameTimeMgr = GameTimeManager::Instance();
  auto &wrm = WorldResourceManager::Instance();
  auto &eventMgr = EventManager::Instance();

  if (m_transitioningToLoading) {
    // Transitioning to LoadingState - do cleanup but preserve m_worldLoaded
    // flag This prevents infinite loop when returning from LoadingState

    // Reset the flag after using it
    m_transitioningToLoading = false;
    m_transitioningToGameOver = false;

    // Clear NPCs before manager cleanup (NPCs hold EDM indices)
    m_npcRenderCtrl.clearSpawnedNPCs();

    // Unsubscribe event handlers before clearing controllers
    // (handlers capture `this` and call m_controllers.get<>() which returns
    // nullptr after clear)
    unregisterEventHandlers();

    aiMgr.prepareForStateTransition();
    ProjectileManager::Instance().prepareForStateTransition();
    bgSimMgr.prepareForStateTransition();
    worldMgr.prepareForStateTransition();

    if (wrm.isInitialized()) {
      wrm.prepareForStateTransition();
    }

    eventMgr.prepareForStateTransition();

    if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
      collisionMgr.prepareForStateTransition();
    }

    if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
      pathfinderMgr.prepareForStateTransition();
    }

    edm.prepareForStateTransition();
    HammerEngine::WorkerBudgetManager::Instance().prepareForStateTransition();

    if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
      particleMgr.prepareForStateTransition();
    }

    WorldManager::Instance().setActiveCamera(nullptr);
    if (mp_Player) {
      mp_Player->setCamera(nullptr);
    }

    // Clean up camera and GPU scene recorder
    m_camera.reset();

    // Unload world (LoadingState will reload it)
    if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
      worldMgr.unloadWorld();
      // CRITICAL: DO NOT reset m_worldLoaded here - keep it true to prevent
      // infinite loop when LoadingState returns to this state
    }

    // Clean up UI
    ui.prepareForStateTransition();

    // Destroy all controllers so re-entry creates fresh instances with valid refs
    m_controllers.clear();

    // Reset player
    mp_Player = nullptr;

    // Reset initialized flag so state re-initializes after loading
    m_initialized = false;

    // Keep m_worldLoaded = true to remember we've already been through loading
    return true;
  }

  // Full exit (going to main menu, other states, or shutting down)
  m_transitioningToGameOver = false;

  // Clear NPCs before manager cleanup (NPCs hold EDM indices)
  m_npcRenderCtrl.clearSpawnedNPCs();

  // Unsubscribe from event handlers before EventManager teardown.
  unregisterEventHandlers();

  aiMgr.prepareForStateTransition();
  ProjectileManager::Instance().prepareForStateTransition();
  bgSimMgr.prepareForStateTransition();
  worldMgr.prepareForStateTransition();

  if (wrm.isInitialized()) {
    wrm.prepareForStateTransition();
  }

  eventMgr.prepareForStateTransition();

  if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
    collisionMgr.prepareForStateTransition();
  }

  if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
    pathfinderMgr.prepareForStateTransition();
  }

  edm.prepareForStateTransition();
  HammerEngine::WorkerBudgetManager::Instance().prepareForStateTransition();

  // Simple particle cleanup
  if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
    particleMgr.prepareForStateTransition();
  }

  WorldManager::Instance().setActiveCamera(nullptr);
  if (mp_Player) {
    mp_Player->setCamera(nullptr);
  }

  // Clean up camera and GPU scene recorder first to stop world rendering
  m_camera.reset();

  // Unload the world when fully exiting gameplay
  if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
    worldMgr.unloadWorld();
    // CRITICAL: Only reset m_worldLoaded when actually unloading a world
    // This prevents infinite loop when transitioning to LoadingState (no world
    // yet)
    m_worldLoaded = false;
  }

  // Full UI cleanup using standard pattern
  ui.prepareForStateTransition();

  // Reset player
  mp_Player = nullptr;

  // Destroy all controllers so re-entry creates fresh instances with valid refs
  m_controllers.clear();
  gameTimeMgr.enableAutoWeather(false);

  // Stop ambient particles after event teardown so no new weather callbacks fire.
  stopAmbientParticles();

  // Reset initialization flag for next fresh start
  m_initialized = false;

  return true;
}

std::string GamePlayState::getName() const { return "GamePlayState"; }

void GamePlayState::registerEventHandlers() {
  auto &eventMgr = EventManager::Instance();

  m_dayNightEventToken = eventMgr.registerHandlerWithToken(
      EventTypeId::Time,
      [this](const EventData &data) { onTimePeriodChanged(data); });
  m_dayNightSubscribed = true;

  m_weatherEventToken = eventMgr.registerHandlerWithToken(
      EventTypeId::Weather,
      [this](const EventData &data) {
        ParticleManager::Instance().handleWeatherEvent(data);
        onWeatherChanged(data);
      });
  m_weatherSubscribed = true;

  m_harvestEventToken = eventMgr.registerHandlerWithToken(
      EventTypeId::Harvest, [](const EventData &data) {
        if (!data.isActive() || !data.event) {
          return;
        }

        auto harvestEvent =
            std::dynamic_pointer_cast<HarvestResourceEvent>(data.event);
        if (!harvestEvent) {
          return;
        }

        WorldManager::Instance().handleHarvestResource(
            harvestEvent->getEntityId(), harvestEvent->getTargetX(),
            harvestEvent->getTargetY());
      });
  m_harvestSubscribed = true;
}

void GamePlayState::unregisterEventHandlers() {
  auto &eventMgr = EventManager::Instance();

  if (m_dayNightSubscribed) {
    eventMgr.removeHandler(m_dayNightEventToken);
    m_dayNightSubscribed = false;
  }

  if (m_weatherSubscribed) {
    eventMgr.removeHandler(m_weatherEventToken);
    m_weatherSubscribed = false;
  }

  if (m_harvestSubscribed) {
    eventMgr.removeHandler(m_harvestEventToken);
    m_harvestSubscribed = false;
  }
}

void GamePlayState::pause() {
  // Hide gameplay UI when paused (PauseState overlays on top)
  auto &ui = UIManager::Instance();
  ui.setComponentVisible("gameplay_event_log", false);
  ui.setComponentVisible("gameplay_time_label", false);
  ui.setComponentVisible("gameplay_fps", false);

  // Hide combat HUD components
  ui.setComponentVisible("hud_health_label", false);
  ui.setComponentVisible("hud_health_bar", false);
  ui.setComponentVisible("hud_stamina_label", false);
  ui.setComponentVisible("hud_stamina_bar", false);
  ui.setComponentVisible("hud_target_name", false);
  ui.setComponentVisible("hud_target_hp_label", false);
  ui.setComponentVisible("hud_target_health", false);

  // Also hide inventory components if visible
  if (m_inventoryVisible) {
    ui.setComponentVisible("gameplay_inventory_panel", false);
    ui.setComponentVisible("gameplay_inventory_title", false);
    ui.setComponentVisible("gameplay_inventory_status", false);
    ui.setComponentVisible("gameplay_inventory_list", false);
  }

  // Stop player movement to prevent drift during pause
  if (mp_Player) {
    mp_Player->setVelocity(Vector2D(0, 0));
  }

  // Suspend all controllers (unsubscribe from events during pause)
  m_controllers.suspendAll();

  GAMEPLAY_INFO("GamePlayState paused");
}

void GamePlayState::resume() {
  // Show gameplay UI when resuming from pause
  auto &ui = UIManager::Instance();
  ui.setComponentVisible("gameplay_event_log", true);
  ui.setComponentVisible("gameplay_time_label", true);

  // Restore FPS counter visibility if it was enabled
  if (m_fpsVisible) {
    ui.setComponentVisible("gameplay_fps", true);
  }

  // Show combat HUD components (always visible during gameplay)
  ui.setComponentVisible("hud_health_label", true);
  ui.setComponentVisible("hud_health_bar", true);
  ui.setComponentVisible("hud_stamina_label", true);
  ui.setComponentVisible("hud_stamina_bar", true);
  // Target frame visibility controlled by updateCombatHUD() based on
  // hasActiveTarget()

  // Restore inventory visibility state
  if (m_inventoryVisible) {
    ui.setComponentVisible("gameplay_inventory_panel", true);
    ui.setComponentVisible("gameplay_inventory_title", true);
    ui.setComponentVisible("gameplay_inventory_status", true);
    ui.setComponentVisible("gameplay_inventory_list", true);
  }

  // Resume all controllers (re-subscribe to events after pause)
  m_controllers.resumeAll();

  GAMEPLAY_INFO("GamePlayState resumed");
}

void GamePlayState::handleInput() {
  // Cache manager references for better performance
  const InputManager &inputMgr = InputManager::Instance();
  auto &ui = UIManager::Instance();
  GameTimeManager &gameTimeMgr = GameTimeManager::Instance();
  GameEngine &gameEngine = GameEngine::Instance();

  // Use InputManager's new event-driven key press detection
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_P)) {
    // Create PauseState if it doesn't exist
    if (!mp_stateManager->hasState("PauseState")) {
      mp_stateManager->addState(std::make_unique<PauseState>());
    }
    // pushState will call pause() which handles UI hiding and player velocity
    mp_stateManager->pushState("PauseState");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
    mp_stateManager->changeState("MainMenuState");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
    // gameEngine already cached at top of function
    gameEngine.setRunning(false);
  }

  // Inventory toggle
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_I)) {
    toggleInventoryDisplay();
  }

  // FPS counter toggle
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_F2)) {
    m_fpsVisible = !m_fpsVisible;
    ui.setComponentVisible("gameplay_fps", m_fpsVisible);
  }

  // Combat - F for melee attack
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_F) && mp_Player) {
    m_controllers.get<CombatController>()->tryAttack();
  }

  // Debug: R to spawn a hostile ranged NPC near player (test hook)
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_R) && mp_Player) {
    auto& edm = EntityDataManager::Instance();
    auto& aiMgr = AIManager::Instance();
    Vector2D playerPos = mp_Player->getPosition();
    Vector2D spawnPos = playerPos + Vector2D(150.0f, 0.0f);
    EntityHandle npc = edm.createNPCWithRaceClass(spawnPos, "Human", "Warrior",
                                                   Sex::Unknown, 1);  // faction 1 = Enemy
    if (npc.isValid()) {
      aiMgr.assignBehavior(npc, "RangedAttack");
    }
  }

  // Projectile - Space to fire projectile (test hook)
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_SPACE) && mp_Player) {
    auto& edm = EntityDataManager::Instance();
    Vector2D playerPos = mp_Player->getPosition();
    EntityHandle playerHandle = mp_Player->getHandle();

    // Direction based on player facing
    float dirX = (mp_Player->getFlip() == SDL_FLIP_HORIZONTAL) ? -1.0f : 1.0f;
    constexpr float PROJECTILE_SPEED = 300.0f;
    Vector2D velocity(dirX * PROJECTILE_SPEED, 0.0f);

    // Offset spawn slightly in front of player
    Vector2D spawnPos = playerPos + Vector2D(dirX * 20.0f, 0.0f);

    edm.createProjectile(spawnPos, velocity, playerHandle, 10.0f, 3.0f);
  }

  // Interaction - E to trade/pickup/harvest
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_E) && mp_Player) {
    auto& socialCtrl = *m_controllers.get<SocialController>();

    // If already trading, close the trade UI
    if (socialCtrl.isTrading()) {
      socialCtrl.closeTrade();
    } else {
      // Try to find a nearby merchant NPC to trade with
      bool openedTrade = false;
      auto& edm = EntityDataManager::Instance();
      auto& aiMgr = AIManager::Instance();
      Vector2D playerPos = mp_Player->getPosition();

      // Query nearby NPCs
      m_nearbyHandlesBuffer.clear();
      aiMgr.scanActiveHandlesInRadius(playerPos, 100.0f, m_nearbyHandlesBuffer, true);

      for (const auto& handle : m_nearbyHandlesBuffer) {
        if (!handle.isValid() || handle.getKind() != EntityKind::NPC) {
          continue;
        }
        // Check if this NPC is a merchant
        if (edm.isNPCMerchant(handle)) {
          if (socialCtrl.openTrade(handle)) {
            openedTrade = true;
            break;
          }
        }
      }

      // If no merchant found, try pickup/harvest
      if (!openedTrade) {
        auto& itemCtrl = *m_controllers.get<ItemController>();
        if (!itemCtrl.attemptPickup()) {
          // Try to start progress-based harvesting
          auto& harvestCtrl = *m_controllers.get<HarvestController>();
          harvestCtrl.startHarvest();
        }
      }
    }
  }
  // Note: HarvestController handles movement cancellation automatically in update()
  // via position-based detection (MOVEMENT_CANCEL_THRESHOLD)

  // Camera zoom controls
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_LEFTBRACKET) && m_camera) {
    m_camera->zoomIn(); // [ key = zoom in (objects larger)
  }
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_RIGHTBRACKET) && m_camera) {
    m_camera->zoomOut(); // ] key = zoom out (objects smaller)
  }

#ifndef NDEBUG
  // Debug time speed controls: < (comma) = normal speed, > (period) = max speed
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_COMMA)) {
    gameTimeMgr.setTimeScale(60.0f);
    GAMEPLAY_INFO("Time scale set to NORMAL (60x)");
    ui.addEventLogEntry("gameplay_event_log", "Time: NORMAL speed (60x)");
  }
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_PERIOD)) {
    gameTimeMgr.setTimeScale(3600.0f);
    GAMEPLAY_INFO("Time scale set to MAX (3600x)");
    ui.addEventLogEntry("gameplay_event_log", "Time: MAX speed (3600x)");
  }
#endif

  // Mouse input for world interaction
  if (inputMgr.getMouseButtonState(LEFT) && m_camera) {
    Vector2D const mousePos = inputMgr.getMousePosition();

    if (!ui.isClickOnUI(mousePos)) {
      Vector2D worldPos = m_camera->screenToWorld(mousePos);
      int const tileX =
          static_cast<int>(worldPos.getX() / HammerEngine::TILE_SIZE);
      int const tileY =
          static_cast<int>(worldPos.getY() / HammerEngine::TILE_SIZE);

      auto &worldMgr = WorldManager::Instance();
      if (worldMgr.isValidPosition(tileX, tileY)) {
        const auto biome = worldMgr.getTileBiomeAt(tileX, tileY);
        const auto obstacleType = worldMgr.getTileObstacleTypeAt(tileX, tileY);
        // Log tile information for debugging
        GAMEPLAY_DEBUG_IF(
            biome.has_value() && obstacleType.has_value(),
            std::format("Clicked tile ({}, {}) - Biome: {}, Obstacle: {}",
                        tileX, tileY, static_cast<int>(*biome),
                        static_cast<int>(*obstacleType)));
      }
    }
  }
}

void GamePlayState::initializeInventoryUI() {
  auto &ui = UIManager::Instance();
  const auto &gameEngine = GameEngine::Instance();
  int const windowWidth = gameEngine.getLogicalWidth();

  // Create inventory panel (initially hidden) matching EventDemoState layout
  // Using TOP_RIGHT positioning - UIManager handles all resize repositioning
  constexpr int inventoryWidth = 280;
  constexpr int panelMarginRight = 20; // 20px from right edge
  constexpr int panelMarginTop = 170;  // 170px from top
  constexpr int childInset = 10;       // Children are 10px inside panel
  constexpr int childWidth = inventoryWidth - (childInset * 2); // 260px
  constexpr int headerHeight = 110;    // Title + status label area
  constexpr int itemHeight = 20;       // Height per inventory item row
  constexpr int bottomPadding = 15;    // Padding below list

  // Calculate inventory panel size based on slot count
  uint32_t invIdx = mp_Player->getInventoryIndex();
  const auto& invData = EntityDataManager::Instance().getInventoryData(invIdx);
  int slotCount = static_cast<int>(invData.maxSlots);

  // Calculate list and panel heights dynamically
  int listHeight = slotCount * itemHeight;
  int inventoryHeight = headerHeight + listHeight + bottomPadding;

  // Initial position (will be auto-repositioned by UIManager)
  int inventoryX = windowWidth - inventoryWidth - panelMarginRight;
  int inventoryY = panelMarginTop;

  ui.createPanel("gameplay_inventory_panel",
                 {inventoryX, inventoryY, inventoryWidth, inventoryHeight});
  ui.setComponentVisible("gameplay_inventory_panel", false);
  ui.setComponentPositioning("gameplay_inventory_panel",
                             {UIPositionMode::TOP_RIGHT, panelMarginRight,
                              panelMarginTop, inventoryWidth, inventoryHeight});

  // Title: 25px below panel top, inset by 10px
  ui.createTitle("gameplay_inventory_title",
                 {inventoryX + childInset, inventoryY + 25, childWidth, 35},
                 "Player Inventory");
  ui.setComponentVisible("gameplay_inventory_title", false);
  ui.setComponentPositioning("gameplay_inventory_title",
                             {UIPositionMode::TOP_RIGHT,
                              panelMarginRight + childInset,
                              panelMarginTop + 25, childWidth, 35});

  // Status label: 75px below panel top
  ui.createLabel("gameplay_inventory_status",
                 {inventoryX + childInset, inventoryY + 75, childWidth, 25},
                 "Capacity: 0/20");
  ui.setComponentVisible("gameplay_inventory_status", false);
  ui.setComponentPositioning("gameplay_inventory_status",
                             {UIPositionMode::TOP_RIGHT,
                              panelMarginRight + childInset,
                              panelMarginTop + 75, childWidth, 25});

  // Inventory list: 110px below panel top, height based on slot count
  ui.createList("gameplay_inventory_list",
                {inventoryX + childInset, inventoryY + 110, childWidth, listHeight});
  ui.setComponentVisible("gameplay_inventory_list", false);
  ui.setComponentPositioning("gameplay_inventory_list",
                             {UIPositionMode::TOP_RIGHT,
                              panelMarginRight + childInset,
                              panelMarginTop + 110, childWidth, listHeight});

  // --- DATA BINDING SETUP ---
  // Bind the inventory capacity label to a function that gets the data
  ui.bindText("gameplay_inventory_status", [this]() -> std::string {
    if (!mp_Player) {
      return "Capacity: 0/0";
    }
    uint32_t invIdx = mp_Player->getInventoryIndex();
    if (invIdx == INVALID_INVENTORY_INDEX) {
      return "Capacity: 0/0";
    }
    const auto& inv = EntityDataManager::Instance().getInventoryData(invIdx);
    return std::format("Capacity: {}/{}", inv.usedSlots, inv.maxSlots);
  });

  // Bind the inventory list - populates provided buffers (zero-allocation
  // pattern)
  ui.bindList(
      "gameplay_inventory_list",
      [this](std::vector<std::string> &items,
             std::vector<std::pair<std::string, int>> &sortedResources) {
        if (!mp_Player) {
          items.push_back("(Empty)");
          return;
        }

        uint32_t invIdx = mp_Player->getInventoryIndex();
        if (invIdx == INVALID_INVENTORY_INDEX) {
          items.push_back("(Empty)");
          return;
        }

        auto allResources = EntityDataManager::Instance().getInventoryResources(invIdx);

        if (allResources.empty()) {
          items.push_back("(Empty)");
          return;
        }

        // Build sorted list using reusable buffer provided by UIManager
        sortedResources.reserve(allResources.size());
        for (const auto &[resourceHandle, quantity] : allResources) {
          if (quantity > 0) {
            auto resourceTemplate =
                ResourceTemplateManager::Instance().getResourceTemplate(
                    resourceHandle);
            std::string resourceName =
                resourceTemplate ? resourceTemplate->getName() : "Unknown";
            sortedResources.emplace_back(std::move(resourceName), quantity);
          }
        }
        std::sort(sortedResources.begin(), sortedResources.end());

        items.reserve(sortedResources.size());
        for (const auto &[resourceId, quantity] : sortedResources) {
          items.push_back(std::format("{} x{}", resourceId, quantity));
        }

        if (items.empty()) {
          items.push_back("(Empty)");
        }
      });
}

void GamePlayState::toggleInventoryDisplay() {
  auto &ui = UIManager::Instance();
  m_inventoryVisible = !m_inventoryVisible;

  ui.setComponentVisible("gameplay_inventory_panel", m_inventoryVisible);
  ui.setComponentVisible("gameplay_inventory_title", m_inventoryVisible);
  ui.setComponentVisible("gameplay_inventory_status", m_inventoryVisible);
  ui.setComponentVisible("gameplay_inventory_list", m_inventoryVisible);
}

void GamePlayState::initializeCamera() {
  // Initialize camera at player's position to avoid any interpolation jitter
  Vector2D playerPosition =
      mp_Player ? mp_Player->getPosition() : Vector2D(0, 0);

  // Create camera with position, then sync viewport from the active GPU renderer.
  m_camera = std::make_unique<HammerEngine::Camera>();
  m_camera->setPosition(playerPosition);
  m_camera->syncViewportWithEngine();

  GAMEPLAY_INFO(std::format("Camera initialized: pos=({}, {}), viewport={}x{}",
                                 playerPosition.getX(), playerPosition.getY(),
                                 m_camera->getViewport().width, m_camera->getViewport().height));

  // Configure camera to follow player
  if (mp_Player) {
    // Set target and enable follow mode
    std::weak_ptr<Entity> playerAsEntity =
        std::static_pointer_cast<Entity>(mp_Player);
    m_camera->setTarget(playerAsEntity);
    m_camera->setMode(HammerEngine::Camera::Mode::Follow);

    // Set up camera configuration for smooth following
    // Using exponential smoothing for smooth, responsive follow
    HammerEngine::Camera::Config config;
    config.followSpeed = 5.0f;      // Speed of camera interpolation
    config.deadZoneRadius = 0.0f;   // No dead zone - always follow
    config.smoothingFactor = 0.85f; // Smoothing factor (0-1, higher = smoother)
    config.clampToWorldBounds = true; // Keep camera within world bounds
    m_camera->setConfig(config);

    // Provide camera to player for screen-to-world coordinate conversion
    mp_Player->setCamera(m_camera.get());

    // Camera auto-synchronizes world bounds on update
  }

  // Register camera with WorldManager for chunk texture updates
  WorldManager::Instance().setActiveCamera(m_camera.get());
}

void GamePlayState::updateCamera(float deltaTime) {
  // Defensive null check (camera always initialized in enter(), but kept for
  // safety)
  if (m_camera) {
    // Sync viewport with current window size (handles resize events)
    m_camera->syncViewportWithEngine();

    // Update camera position and following logic
    m_camera->update(deltaTime);
  }
}

// Removed setupCameraForWorld(): camera manages world bounds itself

void GamePlayState::onTimePeriodChanged(const EventData &data) {
  if (!data.event) {
    return;
  }

  auto timeEvent = std::static_pointer_cast<TimeEvent>(data.event);
  TimeEventType eventType = timeEvent->getTimeEventType();

  // Mark status bar dirty on any time event (hour, day, season changes)
  m_statusBarDirty = true;

  // Only process visual changes for TimePeriodChangedEvent
  if (eventType != TimeEventType::TimePeriodChanged) {
    return;
  }

  auto periodEvent =
      std::static_pointer_cast<TimePeriodChangedEvent>(data.event);
  // Track current period for weather change handling
  m_currentTimePeriod = periodEvent->getPeriod();

  // Update ambient particles for the new time period
  updateAmbientParticles(m_currentTimePeriod);

  // Add event log entry for the time period change
  UIManager::Instance().addEventLogEntry(
      "gameplay_event_log",
      std::string(m_controllers.get<DayNightController>()->getCurrentPeriodDescription()));

  GAMEPLAY_DEBUG(std::format("Day/night transition started to period: {}",
                             periodEvent->getPeriodName()));
}

void GamePlayState::updateAmbientParticles(TimePeriod period) {
  // Only spawn ambient particles during clear weather
  if (m_controllers.get<WeatherController>()->getCurrentWeather() != WeatherType::Clear) {
    if (m_ambientParticlesActive) {
      stopAmbientParticles();
    }
    return;
  }

  // OPTIMIZATION: Only stop/start particles if the period actually changed
  // This avoids particle thrashing when called repeatedly with the same period
  if (m_ambientParticlesActive && period == m_lastAmbientPeriod) {
    return; // No change, particles already running for this period
  }

  const auto &gameEngine = GameEngine::Instance();
  Vector2D screenCenter(gameEngine.getLogicalWidth() / 2.0f,
                        gameEngine.getLogicalHeight() / 2.0f);

  // Stop existing ambient particles only when period changed
  if (m_ambientParticlesActive) {
    stopAmbientParticles();
  }

  // Cache ParticleManager reference for multiple effect calls
  auto &particleMgr = ParticleManager::Instance();

  // Start appropriate particles for the new period
  switch (period) {
  case TimePeriod::Morning:
    // Light dust motes in morning sunlight
    m_ambientDustEffectId = particleMgr.playIndependentEffect(
        ParticleEffectType::AmbientDust, screenCenter, 0.6f, -1.0f, "ambient");
    break;

  case TimePeriod::Day:
    // Subtle dust particles during the day
    m_ambientDustEffectId = particleMgr.playIndependentEffect(
        ParticleEffectType::AmbientDust, screenCenter, 0.4f, -1.0f, "ambient");
    break;

  case TimePeriod::Evening:
    // Golden dust in evening light
    m_ambientDustEffectId = particleMgr.playIndependentEffect(
        ParticleEffectType::AmbientDust, screenCenter, 0.8f, -1.0f, "ambient");
    break;

  case TimePeriod::Night:
    // Fireflies at night
    m_ambientFireflyEffectId = particleMgr.playIndependentEffect(
        ParticleEffectType::AmbientFirefly, screenCenter, 1.0f, -1.0f,
        "ambient");
    break;
  }

  m_lastAmbientPeriod = period;
  m_ambientParticlesActive = true;
}

void GamePlayState::stopAmbientParticles() {
  if (m_ambientDustEffectId != 0 || m_ambientFireflyEffectId != 0) {
    auto &particleMgr = ParticleManager::Instance();

    if (m_ambientDustEffectId != 0) {
      particleMgr.stopIndependentEffect(m_ambientDustEffectId);
      m_ambientDustEffectId = 0;
    }

    if (m_ambientFireflyEffectId != 0) {
      particleMgr.stopIndependentEffect(m_ambientFireflyEffectId);
      m_ambientFireflyEffectId = 0;
    }
  }

  m_ambientParticlesActive = false;
}

void GamePlayState::onWeatherChanged(const EventData &data) {
  if (!data.event) {
    return;
  }

  auto weatherEvent = std::static_pointer_cast<WeatherEvent>(data.event);
  WeatherType newWeather = weatherEvent->getWeatherType();

  // OPTIMIZATION: Skip if weather hasn't actually changed (deduplication)
  if (newWeather == m_lastWeatherType) {
    return;
  }
  m_lastWeatherType = newWeather;

  // Mark status bar dirty for weather display update
  m_statusBarDirty = true;

  // Re-evaluate ambient particles based on current time period and new weather
  updateAmbientParticles(m_currentTimePeriod);

  // Add event log entry for the weather change
  UIManager::Instance().addEventLogEntry(
      "gameplay_event_log",
      std::string(m_controllers.get<WeatherController>()->getCurrentWeatherDescription()));

  GAMEPLAY_DEBUG(weatherEvent->getWeatherTypeString());
}

void GamePlayState::recordGPUVertices(HammerEngine::GPURenderer &gpuRenderer,
                                      float interpolationAlpha) {
  // Skip if world not active or GPU scene recorder not initialized
  if (!m_camera || !m_gpuSceneRecorder) {
    return;
  }

  // Begin scene-data recording before the engine-owned scene pass opens
  auto ctx = m_gpuSceneRecorder->beginRecording(gpuRenderer, *m_camera, interpolationAlpha);
  if (!ctx) {
    return;
  }

  // Record world tile vertices (draws to ctx.spriteBatch)
  auto &worldMgr = WorldManager::Instance();
  worldMgr.recordGPU(*ctx.spriteBatch, ctx.cameraX, ctx.cameraY,
                     ctx.viewWidth, ctx.viewHeight, ctx.zoom);

  // Record world resources managed outside world tiles (dropped items, containers)
  if (auto* resourceCtrl = m_controllers.get<ResourceRenderController>()) {
    resourceCtrl->recordGPUDroppedItems(ctx, *m_camera);
    resourceCtrl->recordGPUContainers(ctx, *m_camera);
  }

  // Record NPCs after resources to preserve SDL render-order parity.
  m_npcRenderCtrl.recordGPU(ctx);

  // Record projectiles after NPCs (rendered on top of NPCs)
  m_projectileRenderCtrl.recordGPU(ctx);

  // End sprite batch recording before switching to entity batch
  m_gpuSceneRecorder->endSpriteBatch();

  // Record player vertices (uses entity batch - separate texture)
  if (mp_Player) {
    mp_Player->recordGPUVertices(gpuRenderer, ctx.cameraX, ctx.cameraY, interpolationAlpha);
  }

  // Record particle vertices (uses particle pool)
  auto &particleMgr = ParticleManager::Instance();
  if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
    particleMgr.recordGPUVertices(gpuRenderer, ctx.cameraX, ctx.cameraY, interpolationAlpha);
  }

  // End scene-data recording before UI vertices are recorded
  m_gpuSceneRecorder->endRecording();

  // Update FPS display if visible (must happen BEFORE recording UI vertices)
  auto &ui = UIManager::Instance();
  if (m_fpsVisible) {
    float const currentFPS = mp_stateManager->getCurrentFPS();
    // Only update UI text if FPS changed by more than 0.05 (avoids flicker)
    if (std::abs(currentFPS - m_lastDisplayedFPS) > 0.05f) {
      m_fpsBuffer.clear();
      std::format_to(std::back_inserter(m_fpsBuffer), "FPS: {:.1f}",
                     currentFPS);
      ui.setText("gameplay_fps", m_fpsBuffer);
      m_lastDisplayedFPS = currentFPS;
    }
  }

  // Record UI vertices (separate from scene)
  ui.recordGPUVertices(gpuRenderer);
}

void GamePlayState::renderGPUScene(HammerEngine::GPURenderer &gpuRenderer,
                                   SDL_GPURenderPass *scenePass,
                                   [[maybe_unused]] float interpolationAlpha) {
  if (!m_camera || !m_gpuSceneRecorder) {
    return;
  }

  // Render previously recorded scene data into the engine-owned scene pass
  m_gpuSceneRecorder->renderRecordedScene(gpuRenderer, scenePass);

  // Render player (entity batch)
  if (mp_Player) {
    mp_Player->renderGPU(gpuRenderer, scenePass);
  }

  // Render particles (particle pool)
  auto &particleMgr = ParticleManager::Instance();
  if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
    particleMgr.renderGPU(gpuRenderer, scenePass);
  }
}

void GamePlayState::renderGPUUI(HammerEngine::GPURenderer &gpuRenderer,
                                SDL_GPURenderPass *swapchainPass) {
  // Day/night lighting is handled by the composite shader (DayNightController updates GPURenderer)

  // Render UI
  auto &ui = UIManager::Instance();
  ui.renderGPU(gpuRenderer, swapchainPass);
}
