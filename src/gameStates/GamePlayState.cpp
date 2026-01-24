/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/GamePlayState.hpp"
#include "entities/Player.hpp"
#include "utils/SceneRenderer.hpp"
#include "controllers/combat/CombatController.hpp"
#include "controllers/world/DayNightController.hpp"
#include "controllers/world/ItemController.hpp"
#include "controllers/world/WeatherController.hpp"
#include "controllers/render/ResourceRenderController.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "gameStates/LoadingState.hpp"
#include "gameStates/PauseState.hpp"
#include "managers/AIManager.hpp"
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
#include "utils/Camera.hpp"
#include "world/WorldData.hpp"
#include <algorithm>
#include <cmath>
#include <format>

bool GamePlayState::enter() {
  // Resume all game managers (may be paused from menu states)
  GameEngine::Instance().setGlobalPause(false);

  // Reset transition flag when entering state
  m_transitioningToLoading = false;

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
    const auto &gameEngine = GameEngine::Instance();
    auto &gameTimeMgr = GameTimeManager::Instance();

    // Initialize resource handles first
    initializeResourceHandles();

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

    // Create scene renderer for pixel-perfect zoomed rendering
    m_sceneRenderer = std::make_unique<HammerEngine::SceneRenderer>();

    // Register controllers with the registry
    m_controllers.add<WeatherController>();
    m_controllers.add<DayNightController>();
    m_controllers.add<CombatController>(mp_Player);
    m_controllers.add<ItemController>(mp_Player);
    m_controllers.add<ResourceRenderController>();

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
    initializeCombatHUD();

    // Cache EventManager reference for event subscriptions
    auto &eventMgr = EventManager::Instance();

    // Subscribe to TimePeriodChangedEvent for day/night overlay rendering
    m_dayNightEventToken = eventMgr.registerHandlerWithToken(
        EventTypeId::Time,
        [this](const EventData &data) { onTimePeriodChanged(data); });
    m_dayNightSubscribed = true;

    // Subscribe to WeatherChangedEvent for ambient particle management
    m_weatherEventToken = eventMgr.registerHandlerWithToken(
        EventTypeId::Weather,
        [this](const EventData &data) { onWeatherChanged(data); });
    m_weatherSubscribed = true;

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

    // Update combat HUD (health/stamina bars, target frame)
    updateCombatHUD();
  }

  // Update camera (follows player automatically)
  updateCamera(deltaTime);

  // Update resource animations (dropped items bobbing, etc.) - camera-based culling
  if (auto* resourceCtrl = m_controllers.get<ResourceRenderController>(); resourceCtrl && m_camera) {
    resourceCtrl->update(deltaTime, *m_camera);
  }

  // Update day/night overlay interpolation
  updateDayNightOverlay(deltaTime);

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

void GamePlayState::render(SDL_Renderer *renderer, float interpolationAlpha) {
  // Cache manager references for better performance
  ParticleManager &particleMgr = ParticleManager::Instance();
  WorldManager &worldMgr = WorldManager::Instance();
  UIManager &ui = UIManager::Instance();

  // ========== UPDATE DIRTY CHUNKS (before SceneRenderer pipeline) ==========
  // All world content renders to intermediate texture at 1x, then composited with zoom
  const bool worldActive = m_camera && m_sceneRenderer && worldMgr.isInitialized() && worldMgr.hasActiveWorld();

  if (worldActive) {
    // Get camera parameters for chunk visibility calculation
    float zoom = m_camera->getZoom();
    const auto& viewport = m_camera->getViewport();
    float rawCameraX = 0.0f;
    float rawCameraY = 0.0f;
    m_camera->getRenderOffset(rawCameraX, rawCameraY, interpolationAlpha);
    float viewWidth = viewport.width / zoom;
    float viewHeight = viewport.height / zoom;

    // Update dirty chunk textures BEFORE beginScene (no render target conflicts)
    worldMgr.updateDirtyChunks(renderer, rawCameraX, rawCameraY, viewWidth, viewHeight);
  }

  // ========== BEGIN SCENE (to SceneRenderer's intermediate target) ==========
  HammerEngine::SceneRenderer::SceneContext ctx;
  if (worldActive) {
    ctx = m_sceneRenderer->beginScene(renderer, *m_camera, interpolationAlpha);
  }

  if (ctx) {
    // Background particles (rain, snow behind everything)
    if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
      particleMgr.renderBackground(renderer, ctx.cameraX, ctx.cameraY,
                                   interpolationAlpha);
    }

    // Render tiles (pixel-perfect alignment)
    worldMgr.render(renderer, ctx.flooredCameraX, ctx.flooredCameraY,
                    ctx.viewWidth, ctx.viewHeight);

    // Render world resources (dropped items, harvestables, containers)
    if (auto* resourceCtrl = m_controllers.get<ResourceRenderController>(); resourceCtrl && m_camera) {
      resourceCtrl->renderDroppedItems(renderer, *m_camera, ctx.cameraX, ctx.cameraY, interpolationAlpha);
      resourceCtrl->renderHarvestables(renderer, *m_camera, ctx.cameraX, ctx.cameraY, interpolationAlpha);
      resourceCtrl->renderContainers(renderer, *m_camera, ctx.cameraX, ctx.cameraY, interpolationAlpha);
    }

    // Render player (sub-pixel smoothness from entity's own interpolation)
    if (mp_Player) {
      mp_Player->render(renderer, ctx.cameraX, ctx.cameraY, interpolationAlpha);
    }

    // Render world-space and foreground particles (after player)
    if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
      particleMgr.render(renderer, ctx.cameraX, ctx.cameraY, interpolationAlpha);
      particleMgr.renderForeground(renderer, ctx.cameraX, ctx.cameraY,
                                   interpolationAlpha);
    }
  }

  // ========== END SCENE (composite with zoom) ==========
  // This composites all world content and resets render scale to 1.0
  if (worldActive && m_sceneRenderer) {
    m_sceneRenderer->endScene(renderer);
  }

  // Render day/night overlay tint (at 1.0 scale, after zoom reset)
  if (m_camera) {
    const auto &viewport = m_camera->getViewport();
    renderDayNightOverlay(renderer, static_cast<int>(viewport.width),
                          static_cast<int>(viewport.height));
  }

  // Update FPS display if visible (zero-allocation, only when changed)
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

  // Render UI components (no camera transformation)
  ui.render(renderer);
}
bool GamePlayState::exit() {
  // Cache manager references for better performance
  AIManager &aiMgr = AIManager::Instance();
  EntityDataManager &edm = EntityDataManager::Instance();
  CollisionManager &collisionMgr = CollisionManager::Instance();
  PathfinderManager &pathfinderMgr = PathfinderManager::Instance();
  ParticleManager &particleMgr = ParticleManager::Instance();
  auto &ui = UIManager::Instance();
  WorldManager &worldMgr = WorldManager::Instance();
  GameTimeManager &gameTimeMgr = GameTimeManager::Instance();

  if (m_transitioningToLoading) {
    // Transitioning to LoadingState - do cleanup but preserve m_worldLoaded
    // flag This prevents infinite loop when returning from LoadingState

    // Reset the flag after using it
    m_transitioningToLoading = false;

    // Clean up managers (same as full exit)
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

    if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
      particleMgr.prepareForStateTransition();
    }

    // Clean up camera and scene renderer
    m_camera.reset();
    m_sceneRenderer.reset();

    // Unload world (LoadingState will reload it)
    if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
      worldMgr.unloadWorld();
      // CRITICAL: DO NOT reset m_worldLoaded here - keep it true to prevent
      // infinite loop when LoadingState returns to this state
    }

    // Clean up UI
    ui.prepareForStateTransition();

    // Reset player
    mp_Player = nullptr;

    // Reset initialized flag so state re-initializes after loading
    m_initialized = false;

    // Keep m_worldLoaded = true to remember we've already been through loading
    return true;
  }

  // Full exit (going to main menu, other states, or shutting down)

  // Use manager prepareForStateTransition methods for deterministic cleanup
  // CRITICAL: PathfinderManager MUST be cleaned BEFORE EDM
  // Pending path tasks hold captured edmIndex values - they must complete or
  // see the transition flag before EDM clears its data
  if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
    pathfinderMgr.prepareForStateTransition();
  }

  aiMgr.prepareForStateTransition();
  edm.prepareForStateTransition();

  // Clean collision state before other systems
  if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
    collisionMgr.prepareForStateTransition();
  }

  // Simple particle cleanup
  if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
    particleMgr.prepareForStateTransition();
  }

  // Clean up camera and scene renderer first to stop world rendering
  m_camera.reset();
  m_sceneRenderer.reset();

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

  // Unsubscribe all controllers at once
  m_controllers.unsubscribeAll();
  gameTimeMgr.enableAutoWeather(false);

  // Stop ambient particles before unsubscribing
  stopAmbientParticles();

  // Unsubscribe from event handlers
  if (m_dayNightSubscribed || m_weatherSubscribed) {
    auto &eventMgr = EventManager::Instance();

    if (m_dayNightSubscribed) {
      eventMgr.removeHandler(m_dayNightEventToken);
      m_dayNightSubscribed = false;
    }

    if (m_weatherSubscribed) {
      eventMgr.removeHandler(m_weatherEventToken);
      m_weatherSubscribed = false;
    }
  }

  // Reset initialization flag for next fresh start
  m_initialized = false;

  return true;
}

std::string GamePlayState::getName() const { return "GamePlayState"; }

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
  ui.setComponentVisible("hud_target_panel", false);
  ui.setComponentVisible("hud_target_name", false);
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

  // Combat - spacebar to attack
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_SPACE) && mp_Player) {
    m_controllers.get<CombatController>()->tryAttack();
  }

  // Item interaction - E to pickup/harvest
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_E) && mp_Player) {
    auto& itemCtrl = *m_controllers.get<ItemController>();
    if (!itemCtrl.attemptPickup()) {
      itemCtrl.attemptHarvest();
    }
  }

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

  // Resource addition demo keys
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_1)) {
    addDemoResource(m_goldHandle, 10);
  }
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_2)) {
    addDemoResource(m_healthPotionHandle, 1);
  }
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_3)) {
    addDemoResource(m_ironOreHandle, 5);
  }
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_4)) {
    addDemoResource(m_woodHandle, 3);
  }
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_5)) {
    removeDemoResource(m_goldHandle, 5);
  }

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
        const auto *tile = worldMgr.getTileAt(tileX, tileY);
        // Log tile information for debugging
        GAMEPLAY_DEBUG_IF(
            tile,
            std::format("Clicked tile ({}, {}) - Biome: {}, Obstacle: {}",
                        tileX, tileY, tile ? static_cast<int>(tile->biome) : 0,
                        tile ? static_cast<int>(tile->obstacleType) : 0));
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
  constexpr int inventoryHeight = 400;
  constexpr int panelMarginRight = 20; // 20px from right edge
  constexpr int panelMarginTop = 170;  // 170px from top
  constexpr int childInset = 10;       // Children are 10px inside panel
  constexpr int childWidth = inventoryWidth - (childInset * 2); // 260px

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
                 "Capacity: 0/50");
  ui.setComponentVisible("gameplay_inventory_status", false);
  ui.setComponentPositioning("gameplay_inventory_status",
                             {UIPositionMode::TOP_RIGHT,
                              panelMarginRight + childInset,
                              panelMarginTop + 75, childWidth, 25});

  // Inventory list: 110px below panel top
  ui.createList("gameplay_inventory_list",
                {inventoryX + childInset, inventoryY + 110, childWidth, 270});
  ui.setComponentVisible("gameplay_inventory_list", false);
  ui.setComponentPositioning("gameplay_inventory_list",
                             {UIPositionMode::TOP_RIGHT,
                              panelMarginRight + childInset,
                              panelMarginTop + 110, childWidth, 270});

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

void GamePlayState::addDemoResource(HammerEngine::ResourceHandle resourceHandle,
                                    int quantity) {
  if (!mp_Player) {
    return;
  }

  if (!resourceHandle.isValid()) {
    return;
  }

  mp_Player->addToInventory(resourceHandle, quantity);
}

void GamePlayState::removeDemoResource(
    HammerEngine::ResourceHandle resourceHandle, int quantity) {
  if (!mp_Player) {
    return;
  }

  if (!resourceHandle.isValid()) {
    return;
  }

  mp_Player->removeFromInventory(resourceHandle, quantity);
}

void GamePlayState::initializeResourceHandles() {
  // Resolve resource names to handles once during initialization (resource
  // handle system compliance)
  const auto &templateManager = ResourceTemplateManager::Instance();

  // Only perform name-based lookups during initialization, not at runtime
  auto goldResource = templateManager.getResourceByName("gold");
  m_goldHandle =
      goldResource ? goldResource->getHandle() : HammerEngine::ResourceHandle();

  auto healthPotionResource =
      templateManager.getResourceByName("health_potion");
  m_healthPotionHandle = healthPotionResource
                             ? healthPotionResource->getHandle()
                             : HammerEngine::ResourceHandle();

  auto ironOreResource = templateManager.getResourceByName("iron_ore");
  m_ironOreHandle = ironOreResource ? ironOreResource->getHandle()
                                    : HammerEngine::ResourceHandle();

  auto woodResource = templateManager.getResourceByName("wood");
  m_woodHandle =
      woodResource ? woodResource->getHandle() : HammerEngine::ResourceHandle();
}

void GamePlayState::initializeCamera() {
  const auto &gameEngine = GameEngine::Instance();

  // Initialize camera at player's position to avoid any interpolation jitter
  Vector2D playerPosition =
      mp_Player ? mp_Player->getPosition() : Vector2D(0, 0);

  // Create camera starting at player position with logical viewport dimensions
  m_camera = std::make_unique<HammerEngine::Camera>(
      playerPosition.getX(), playerPosition.getY(),
      static_cast<float>(gameEngine.getLogicalWidth()),
      static_cast<float>(gameEngine.getLogicalHeight()));

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
  const auto &visuals = periodEvent->getVisuals();

  // Set target values - interpolation happens in updateDayNightOverlay()
  m_dayNightTargetR = static_cast<float>(visuals.overlayR);
  m_dayNightTargetG = static_cast<float>(visuals.overlayG);
  m_dayNightTargetB = static_cast<float>(visuals.overlayB);
  m_dayNightTargetA = static_cast<float>(visuals.overlayA);

  // Track current period for weather change handling
  m_currentTimePeriod = periodEvent->getPeriod();

  // Update ambient particles for the new time period
  updateAmbientParticles(m_currentTimePeriod);

  // Add event log entry for the time period change
  UIManager::Instance().addEventLogEntry(
      "gameplay_event_log",
      std::string(m_controllers.get<DayNightController>()->getCurrentPeriodDescription()));

  GAMEPLAY_DEBUG("Day/night transition started to period: " +
                 std::string(periodEvent->getPeriodName()));
}

void GamePlayState::updateDayNightOverlay(float deltaTime) {
  // Calculate interpolation speed based on transition duration
  // Using exponential smoothing for natural-feeling transitions
  float lerpFactor =
      1.0f - std::exp(-deltaTime * (3.0f / DAY_NIGHT_TRANSITION_DURATION));

  // Interpolate each channel toward target
  m_dayNightOverlayR += (m_dayNightTargetR - m_dayNightOverlayR) * lerpFactor;
  m_dayNightOverlayG += (m_dayNightTargetG - m_dayNightOverlayG) * lerpFactor;
  m_dayNightOverlayB += (m_dayNightTargetB - m_dayNightOverlayB) * lerpFactor;
  m_dayNightOverlayA += (m_dayNightTargetA - m_dayNightOverlayA) * lerpFactor;
}

void GamePlayState::renderDayNightOverlay(SDL_Renderer *renderer, int width,
                                          int height) {
  // Skip if no tint (alpha near 0)
  if (m_dayNightOverlayA < 0.5f) {
    return;
  }

  // Blend mode already set globally by GameEngine at init
  SDL_SetRenderDrawColor(renderer, static_cast<uint8_t>(m_dayNightOverlayR),
                         static_cast<uint8_t>(m_dayNightOverlayG),
                         static_cast<uint8_t>(m_dayNightOverlayB),
                         static_cast<uint8_t>(m_dayNightOverlayA));

  SDL_FRect const rect = {0, 0, static_cast<float>(width),
                          static_cast<float>(height)};
  SDL_RenderFillRect(renderer, &rect);
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

void GamePlayState::initializeCombatHUD() {
  auto &ui = UIManager::Instance();

  // Combat HUD layout constants (top-left positioning)
  constexpr int hudMarginLeft = 20;
  constexpr int hudMarginTop = 40; // Below time label area
  constexpr int labelWidth = 30;
  constexpr int barWidth = 150;
  constexpr int barHeight = 20;
  constexpr int rowSpacing = 25;
  constexpr int labelBarGap = 5;

  // Row positions
  int healthRowY = hudMarginTop;
  int staminaRowY = hudMarginTop + rowSpacing;
  int targetRowY =
      hudMarginTop + rowSpacing * 2 + 10; // Extra spacing before target

  // --- Player Health Bar ---
  ui.createLabel("hud_health_label",
                 {hudMarginLeft, healthRowY, labelWidth, barHeight}, "HP");
  UIPositioning healthLabelPos;
  healthLabelPos.mode = UIPositionMode::TOP_ALIGNED;
  healthLabelPos.offsetX = hudMarginLeft;
  healthLabelPos.offsetY = healthRowY;
  healthLabelPos.fixedWidth = labelWidth;
  healthLabelPos.fixedHeight = barHeight;
  ui.setComponentPositioning("hud_health_label", healthLabelPos);

  ui.createProgressBar("hud_health_bar",
                       {hudMarginLeft + labelWidth + labelBarGap, healthRowY,
                        barWidth, barHeight},
                       0.0f, 100.0f);
  UIPositioning healthBarPos;
  healthBarPos.mode = UIPositionMode::TOP_ALIGNED;
  healthBarPos.offsetX = hudMarginLeft + labelWidth + labelBarGap;
  healthBarPos.offsetY = healthRowY;
  healthBarPos.fixedWidth = barWidth;
  healthBarPos.fixedHeight = barHeight;
  ui.setComponentPositioning("hud_health_bar", healthBarPos);

  // Set green fill color for health bar
  UIStyle healthStyle;
  healthStyle.backgroundColor = {40, 40, 40, 255};
  healthStyle.borderColor = {180, 180, 180, 255};
  healthStyle.hoverColor = {50, 200, 50, 255}; // Green fill
  healthStyle.borderWidth = 1;
  ui.setStyle("hud_health_bar", healthStyle);

  // --- Player Stamina Bar ---
  ui.createLabel("hud_stamina_label",
                 {hudMarginLeft, staminaRowY, labelWidth, barHeight}, "SP");
  UIPositioning staminaLabelPos;
  staminaLabelPos.mode = UIPositionMode::TOP_ALIGNED;
  staminaLabelPos.offsetX = hudMarginLeft;
  staminaLabelPos.offsetY = staminaRowY;
  staminaLabelPos.fixedWidth = labelWidth;
  staminaLabelPos.fixedHeight = barHeight;
  ui.setComponentPositioning("hud_stamina_label", staminaLabelPos);

  ui.createProgressBar("hud_stamina_bar",
                       {hudMarginLeft + labelWidth + labelBarGap, staminaRowY,
                        barWidth, barHeight},
                       0.0f, 100.0f);
  UIPositioning staminaBarPos;
  staminaBarPos.mode = UIPositionMode::TOP_ALIGNED;
  staminaBarPos.offsetX = hudMarginLeft + labelWidth + labelBarGap;
  staminaBarPos.offsetY = staminaRowY;
  staminaBarPos.fixedWidth = barWidth;
  staminaBarPos.fixedHeight = barHeight;
  ui.setComponentPositioning("hud_stamina_bar", staminaBarPos);

  // Set yellow fill color for stamina bar
  UIStyle staminaStyle;
  staminaStyle.backgroundColor = {40, 40, 40, 255};
  staminaStyle.borderColor = {180, 180, 180, 255};
  staminaStyle.hoverColor = {255, 200, 50, 255}; // Yellow fill
  staminaStyle.borderWidth = 1;
  ui.setStyle("hud_stamina_bar", staminaStyle);

  // --- Target Frame (NPC health when attacked) ---
  constexpr int targetPanelWidth = 190;
  constexpr int targetPanelHeight = 50;

  ui.createPanel("hud_target_panel", {hudMarginLeft, targetRowY,
                                      targetPanelWidth, targetPanelHeight});
  UIPositioning targetPanelPos;
  targetPanelPos.mode = UIPositionMode::TOP_ALIGNED;
  targetPanelPos.offsetX = hudMarginLeft;
  targetPanelPos.offsetY = targetRowY;
  targetPanelPos.fixedWidth = targetPanelWidth;
  targetPanelPos.fixedHeight = targetPanelHeight;
  ui.setComponentPositioning("hud_target_panel", targetPanelPos);
  ui.setComponentVisible("hud_target_panel", false);

  ui.createLabel("hud_target_name",
                 {hudMarginLeft + 5, targetRowY + 5, targetPanelWidth - 10, 18},
                 "");
  UIPositioning targetNamePos;
  targetNamePos.mode = UIPositionMode::TOP_ALIGNED;
  targetNamePos.offsetX = hudMarginLeft + 5;
  targetNamePos.offsetY = targetRowY + 5;
  targetNamePos.fixedWidth = targetPanelWidth - 10;
  targetNamePos.fixedHeight = 18;
  ui.setComponentPositioning("hud_target_name", targetNamePos);
  ui.setComponentVisible("hud_target_name", false);

  ui.createProgressBar(
      "hud_target_health",
      {hudMarginLeft + 5, targetRowY + 26, targetPanelWidth - 10, 18}, 0.0f,
      100.0f);
  UIPositioning targetHealthPos;
  targetHealthPos.mode = UIPositionMode::TOP_ALIGNED;
  targetHealthPos.offsetX = hudMarginLeft + 5;
  targetHealthPos.offsetY = targetRowY + 26;
  targetHealthPos.fixedWidth = targetPanelWidth - 10;
  targetHealthPos.fixedHeight = 18;
  ui.setComponentPositioning("hud_target_health", targetHealthPos);
  ui.setComponentVisible("hud_target_health", false);

  // Set red fill color for target health bar
  UIStyle targetHealthStyle;
  targetHealthStyle.backgroundColor = {40, 40, 40, 255};
  targetHealthStyle.borderColor = {180, 180, 180, 255};
  targetHealthStyle.hoverColor = {200, 50, 50, 255}; // Red fill
  targetHealthStyle.borderWidth = 1;
  ui.setStyle("hud_target_health", targetHealthStyle);

  // Initialize bars with player stats
  if (mp_Player) {
    ui.setValue("hud_health_bar", mp_Player->getHealth());
    ui.setValue("hud_stamina_bar", mp_Player->getStamina());
  }

  GAMEPLAY_INFO("Combat HUD initialized");
}

void GamePlayState::updateCombatHUD() {
  if (!mp_Player) {
    return;
  }

  auto &ui = UIManager::Instance();
  auto &combatCtrl = *m_controllers.get<CombatController>();

  // Update player health and stamina bars
  ui.setValue("hud_health_bar", mp_Player->getHealth());
  ui.setValue("hud_stamina_bar", mp_Player->getStamina());

  // Update target frame visibility and content (data-driven via EDM)
  if (combatCtrl.hasActiveTarget()) {
    EntityHandle targetHandle = combatCtrl.getTargetedHandle();
    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(targetHandle);
    if (idx != SIZE_MAX) {
      const auto& charData = edm.getCharacterDataByIndex(idx);
      ui.setComponentVisible("hud_target_panel", true);
      ui.setComponentVisible("hud_target_name", true);
      ui.setComponentVisible("hud_target_health", true);
      ui.setText("hud_target_name", "Target");  // Data-driven NPCs don't have names
      ui.setValue("hud_target_health", charData.health);
    }
  } else {
    ui.setComponentVisible("hud_target_panel", false);
    ui.setComponentVisible("hud_target_name", false);
    ui.setComponentVisible("hud_target_health", false);
  }
}
