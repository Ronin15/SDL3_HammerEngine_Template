/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/GamePlayState.hpp"
#include "core/GameEngine.hpp"
#include "core/GameTime.hpp"
#include "core/Logger.hpp"
#include "gameStates/PauseState.hpp"
#include "gameStates/LoadingState.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/ParticleManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldManager.hpp"
#include "world/WorldData.hpp"
#include "controllers/world/WeatherController.hpp"
#include "controllers/world/TimeController.hpp"
#include "controllers/world/DayNightController.hpp"
#include "managers/UIConstants.hpp"
#include "utils/Camera.hpp"
#include <algorithm>
#include <cmath>
#include <random>


bool GamePlayState::enter() {
  // Cache manager pointers for render hot path (always valid after GameEngine init)
  mp_particleMgr = &ParticleManager::Instance();
  mp_worldMgr = &WorldManager::Instance();
  mp_uiMgr = &UIManager::Instance();

  // Reset transition flag when entering state
  m_transitioningToLoading = false;

  // Check if world needs to be loaded
  if (!m_worldLoaded) {
    GAMEPLAY_INFO("World not loaded yet - will transition to LoadingState on first update");
    m_needsLoading = true;
    m_worldLoaded = true;  // Mark as loaded to prevent loop on re-entry
    return true;  // Will transition to loading screen in update()
  }

  // World is loaded - proceed with normal initialization
  GAMEPLAY_INFO("World already loaded - initializing gameplay");

  try {
    const auto &gameEngine = GameEngine::Instance();

    // Initialize resource handles first
    initializeResourceHandles();

    // Create player and position at screen center
    mp_Player = std::make_shared<Player>();
    mp_Player->ensurePhysicsBodyRegistered();
    mp_Player->initializeInventory();

    // Position player at screen center
    Vector2D screenCenter(gameEngine.getLogicalWidth() / 2.0, gameEngine.getLogicalHeight() / 2.0);
    mp_Player->setPosition(screenCenter);

    // Initialize the inventory UI
    initializeInventoryUI();

    // Initialize camera (world already loaded)
    initializeCamera();

    // Subscribe to automatic weather events (GameTime → WeatherController → ParticleManager)
    WeatherController::Instance().subscribe();

    // Enable automatic weather changes
    GameTime::Instance().enableAutoWeather(true);
#ifdef NDEBUG
    // Release: normal pacing (4 game hours between weather checks)
    GameTime::Instance().setWeatherCheckInterval(4.0f);
#else
    // Debug: faster weather changes for testing (1 game hour)
    GameTime::Instance().setWeatherCheckInterval(1.0f);
#endif

    // Create event log for time/weather messages
    auto& ui = UIManager::Instance();
    ui.createEventLog("gameplay_event_log",
        {10, ui.getLogicalHeight() - 200, 730, 180}, 7);
    UIPositioning eventLogPos;
    eventLogPos.mode = UIPositionMode::BOTTOM_ALIGNED;
    eventLogPos.offsetX = 10;
    eventLogPos.offsetY = 20;
    eventLogPos.fixedHeight = 180;
    eventLogPos.widthPercent = UIConstants::EVENT_LOG_WIDTH_PERCENT;
    ui.setComponentPositioning("gameplay_event_log", eventLogPos);

    // Subscribe to time events for event log display
    TimeController::Instance().subscribe("gameplay_event_log");

    // Create time status label at top-right of screen (no panel, just label)
    int barHeight = UIConstants::STATUS_BAR_HEIGHT;
    int labelPadding = UIConstants::STATUS_BAR_LABEL_PADDING;

    ui.createLabel("gameplay_time_label",
        {labelPadding, 6, ui.getLogicalWidth() - 2 * labelPadding, barHeight - 12}, "");

    // Right-align the text within the label
    ui.setLabelAlignment("gameplay_time_label", UIAlignment::CENTER_RIGHT);

    // Full-width positioning for resize handling
    UIPositioning labelPos;
    labelPos.mode = UIPositionMode::TOP_ALIGNED;
    labelPos.offsetX = labelPadding;
    labelPos.offsetY = 6;  // Small vertical offset from top
    labelPos.fixedWidth = -2 * labelPadding;  // Full width minus margins
    labelPos.fixedHeight = barHeight - 12;
    ui.setComponentPositioning("gameplay_time_label", labelPos);

    // Connect status label with extended format mode (zero allocation)
    auto& timeController = TimeController::Instance();
    timeController.setStatusLabel("gameplay_time_label");
    timeController.setStatusFormatMode(TimeController::StatusFormatMode::Extended);

    // Subscribe to day/night visual effects (controller dispatches events)
    DayNightController::Instance().subscribe();

    // Subscribe to TimePeriodChangedEvent for day/night overlay rendering
    auto& eventMgr = EventManager::Instance();
    m_dayNightEventToken = eventMgr.registerHandlerWithToken(
        EventTypeId::Time,
        [this](const EventData& data) { onTimePeriodChanged(data); }
    );
    m_dayNightSubscribed = true;

    // Subscribe to WeatherChangedEvent for ambient particle management
    m_weatherEventToken = eventMgr.registerHandlerWithToken(
        EventTypeId::Weather,
        [this](const EventData& data) { onWeatherChanged(data); }
    );
    m_weatherSubscribed = true;

    // Mark as initialized for future pause/resume cycles
    m_initialized = true;

    GAMEPLAY_INFO("GamePlayState initialization complete");
  } catch (const std::exception& e) {
    GAMEPLAY_ERROR("Exception during GamePlayState initialization: " + std::string(e.what()));
    return false;
  }

  return true;
}

void GamePlayState::update([[maybe_unused]] float deltaTime) {
  // Check if we need to transition to loading screen (do this in update, not enter)
  if (m_needsLoading) {
    m_needsLoading = false;  // Clear flag

    GAMEPLAY_INFO("Transitioning to LoadingState for world generation");

    // Create world configuration for gameplay
    HammerEngine::WorldGenerationConfig config;
    config.width = 100;   // Standard gameplay world
    config.height = 100;
    config.seed = static_cast<int>(std::time(nullptr));
    config.elevationFrequency = 0.05f;
    config.humidityFrequency = 0.03f;
    config.waterLevel = 0.3f;
    config.mountainLevel = 0.7f;

    // Configure LoadingState and transition to it
    const auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    if (gameStateManager) {
      auto* loadingState = dynamic_cast<LoadingState*>(gameStateManager->getState("LoadingState").get());
      if (loadingState) {
        loadingState->configure("GamePlayState", config);
        // Set flag before transitioning to preserve m_worldLoaded in exit()
        m_transitioningToLoading = true;
        // Use changeState (called from update) to properly exit and re-enter
        gameStateManager->changeState("LoadingState");
      }
    }

    return;  // Don't continue with rest of update
  }

  // Update game time (advances calendar, dispatches time events)
  GameTime::Instance().update(deltaTime);

  // Update player if it exists
  if (mp_Player) {
    mp_Player->update(deltaTime);
  }

  // Update camera (follows player automatically)
  updateCamera(deltaTime);

  // Update day/night overlay interpolation
  updateDayNightOverlay(deltaTime);

  // Update UI
  auto &ui = UIManager::Instance();
  if (!ui.isShutdown()) {
    ui.update(deltaTime);
  }

  // Inventory display is now updated automatically via data binding.
}

void GamePlayState::render(SDL_Renderer* renderer, float interpolationAlpha) {
  // Get GameEngine for logical dimensions (renderer now passed as parameter)
  auto &gameEngine = GameEngine::Instance();

  // Get camera view rect and INTERPOLATED render offset for this frame
  // All rendering (tiles, entities, particles) uses the SAME offset
  // Interpolation enables smooth camera at any refresh rate with fixed 60Hz updates
  HammerEngine::Camera::ViewRect viewRect{0.0f, 0.0f, 0.0f, 0.0f};
  float renderCamX = 0.0f;
  float renderCamY = 0.0f;
  float zoom = 1.0f;
  if (m_camera) {
    viewRect = m_camera->getViewRect();
    m_camera->getRenderOffset(renderCamX, renderCamY, interpolationAlpha);  // Interpolated offset
    zoom = m_camera->getZoom();
  }

  // Set render scale for zoom only when changed (avoids GPU state change overhead)
  if (zoom != m_lastRenderedZoom) {
    SDL_SetRenderScale(renderer, zoom, zoom);
    m_lastRenderedZoom = zoom;
  }

  // Render background particles (rain, snow) BEFORE world - use cached pointer
  // mp_particleMgr guaranteed valid between enter() and exit(), but check shutdown state
  if (mp_particleMgr->isInitialized() && !mp_particleMgr->isShutdown()) {
    mp_particleMgr->renderBackground(renderer, renderCamX, renderCamY);
  }

  // Render world using pixel-snapped camera coordinates - use cached pointer
  // mp_worldMgr guaranteed valid between enter() and exit()
  if (m_camera && mp_worldMgr->isInitialized() && mp_worldMgr->hasActiveWorld()) {
    mp_worldMgr->render(renderer,
                       renderCamX, renderCamY,  // Pixel-snapped camera
                       viewRect.width, viewRect.height);
  }

  // Render player using smooth camera coordinates (allows sub-pixel motion)
  if (mp_Player && m_camera) {
    mp_Player->render(m_camera.get());
  }

  // Render world-space and foreground particles (after player) - use cached pointer
  // mp_particleMgr guaranteed valid between enter() and exit(), but check shutdown state
  if (mp_particleMgr->isInitialized() && !mp_particleMgr->isShutdown()) {
    mp_particleMgr->render(renderer, renderCamX, renderCamY);
    mp_particleMgr->renderForeground(renderer, renderCamX, renderCamY);
  }

  // Render day/night overlay tint (after particles, before UI)
  renderDayNightOverlay(renderer,
      gameEngine.getLogicalWidth(), gameEngine.getLogicalHeight());

  // Reset render scale to 1.0 for UI rendering only when needed (UI should not be zoomed)
  if (m_lastRenderedZoom != 1.0f) {
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    m_lastRenderedZoom = 1.0f;
  }

  // Render UI components (no camera transformation) - use cached pointer
  // mp_uiMgr guaranteed valid between enter() and exit()
  mp_uiMgr->render(renderer);
}
bool GamePlayState::exit() {
  if (m_transitioningToLoading) {
    // Transitioning to LoadingState - do cleanup but preserve m_worldLoaded flag
    // This prevents infinite loop when returning from LoadingState

    // Reset the flag after using it
    m_transitioningToLoading = false;

    // Clean up managers (same as full exit)
    AIManager& aiMgr = AIManager::Instance();
    aiMgr.prepareForStateTransition();

    CollisionManager& collisionMgr = CollisionManager::Instance();
    if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
      collisionMgr.prepareForStateTransition();
    }

    PathfinderManager& pathfinderMgr = PathfinderManager::Instance();
    if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
      pathfinderMgr.prepareForStateTransition();
    }

    ParticleManager& particleMgr = ParticleManager::Instance();
    if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
      particleMgr.prepareForStateTransition();
    }

    // Clean up camera
    m_camera.reset();

    // Unload world (LoadingState will reload it)
    auto& worldManager = WorldManager::Instance();
    if (worldManager.isInitialized() && worldManager.hasActiveWorld()) {
      worldManager.unloadWorld();
      // CRITICAL: DO NOT reset m_worldLoaded here - keep it true to prevent infinite loop
      // when LoadingState returns to this state
    }

    // Clean up UI
    auto &ui = UIManager::Instance();
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
  AIManager& aiMgr = AIManager::Instance();
  aiMgr.prepareForStateTransition();

  // Clean collision state before other systems
  CollisionManager& collisionMgr = CollisionManager::Instance();
  if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
    collisionMgr.prepareForStateTransition();
  }

  // Clean pathfinding state for fresh start
  PathfinderManager& pathfinderMgr = PathfinderManager::Instance();
  if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
    pathfinderMgr.prepareForStateTransition();
  }

  // Simple particle cleanup
  ParticleManager& particleMgr = ParticleManager::Instance();
  if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
    particleMgr.prepareForStateTransition();
  }

  // Clean up camera first to stop world rendering
  m_camera.reset();

  // Unload the world when fully exiting gameplay
  auto& worldManager = WorldManager::Instance();
  if (worldManager.isInitialized() && worldManager.hasActiveWorld()) {
    worldManager.unloadWorld();
    // CRITICAL: Only reset m_worldLoaded when actually unloading a world
    // This prevents infinite loop when transitioning to LoadingState (no world yet)
    m_worldLoaded = false;
  }

  // Full UI cleanup using standard pattern
  auto &ui = UIManager::Instance();
  ui.prepareForStateTransition();

  // Reset player
  mp_Player = nullptr;

  // Clear cached manager pointers
  mp_particleMgr = nullptr;
  mp_worldMgr = nullptr;
  mp_uiMgr = nullptr;

  // Unsubscribe from automatic weather events and disable auto-weather
  WeatherController::Instance().unsubscribe();
  GameTime::Instance().enableAutoWeather(false);

  // Stop ambient particles before unsubscribing
  stopAmbientParticles();

  // Unsubscribe from day/night visual effects
  DayNightController::Instance().unsubscribe();

  // Unsubscribe from TimePeriodChangedEvent
  if (m_dayNightSubscribed) {
    EventManager::Instance().removeHandler(m_dayNightEventToken);
    m_dayNightSubscribed = false;
  }

  // Unsubscribe from WeatherChangedEvent
  if (m_weatherSubscribed) {
    EventManager::Instance().removeHandler(m_weatherEventToken);
    m_weatherSubscribed = false;
  }

  // Reset status format mode and unsubscribe from time events
  TimeController::Instance().setStatusFormatMode(TimeController::StatusFormatMode::Default);
  TimeController::Instance().unsubscribe();

  // Reset initialization flag for next fresh start
  m_initialized = false;

  return true;
}

std::string GamePlayState::getName() const { return "GamePlayState"; }

void GamePlayState::pause() {
  // Hide gameplay UI when paused (PauseState overlays on top)
  auto& ui = UIManager::Instance();
  ui.setComponentVisible("gameplay_event_log", false);
  ui.setComponentVisible("gameplay_time_label", false);

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

  GAMEPLAY_INFO("GamePlayState paused");
}

void GamePlayState::resume() {
  // Show gameplay UI when resuming from pause
  auto& ui = UIManager::Instance();
  ui.setComponentVisible("gameplay_event_log", true);
  ui.setComponentVisible("gameplay_time_label", true);

  // Restore inventory visibility state
  if (m_inventoryVisible) {
    ui.setComponentVisible("gameplay_inventory_panel", true);
    ui.setComponentVisible("gameplay_inventory_title", true);
    ui.setComponentVisible("gameplay_inventory_status", true);
    ui.setComponentVisible("gameplay_inventory_list", true);
  }

  GAMEPLAY_INFO("GamePlayState resumed");
}

void GamePlayState::handleInput() {
  const auto &inputMgr = InputManager::Instance();

  // Use InputManager's new event-driven key press detection
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_P)) {
    // Create PauseState if it doesn't exist
    auto &gameEngine = GameEngine::Instance();
    auto *gameStateManager = gameEngine.getGameStateManager();
    if (!gameStateManager->hasState("PauseState")) {
      gameStateManager->addState(std::make_unique<PauseState>());
    }
    // pushState will call pause() which handles UI hiding and player velocity
    gameStateManager->pushState("PauseState");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
    const auto &gameEngine = GameEngine::Instance();
    gameEngine.getGameStateManager()->changeState("MainMenuState");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
    auto &gameEngine = GameEngine::Instance();
    gameEngine.setRunning(false);
  }

  // Inventory toggle
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_I)) {
    toggleInventoryDisplay();
  }

  // Camera zoom controls
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_LEFTBRACKET) && m_camera) {
    m_camera->zoomIn();  // [ key = zoom in (objects larger)
  }
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_RIGHTBRACKET) && m_camera) {
    m_camera->zoomOut();  // ] key = zoom out (objects smaller)
  }

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
        Vector2D mousePos = inputMgr.getMousePosition();
        auto& ui = UIManager::Instance();

        if (!ui.isClickOnUI(mousePos)) {
            Vector2D worldPos = m_camera->screenToWorld(mousePos);
            int tileX = static_cast<int>(worldPos.getX() / HammerEngine::TILE_SIZE);
            int tileY = static_cast<int>(worldPos.getY() / HammerEngine::TILE_SIZE);

            auto& worldMgr = WorldManager::Instance();
            if (worldMgr.isValidPosition(tileX, tileY)) {
                const auto* tile = worldMgr.getTileAt(tileX, tileY);
                if (tile) {
                    // Log tile information for debugging
                    GAMEPLAY_DEBUG("Clicked tile (" + std::to_string(tileX) +
                                   ", " + std::to_string(tileY) +
                                   ") - Biome: " +
                                   std::to_string(static_cast<int>(tile->biome)) +
                                   ", Obstacle: " +
                                   std::to_string(
                                       static_cast<int>(tile->obstacleType)));
                }
            }
        }
    }
}

void GamePlayState::initializeInventoryUI() {
  auto &ui = UIManager::Instance();
  const auto &gameEngine = GameEngine::Instance();
  int windowWidth = gameEngine.getLogicalWidth();

  // Create inventory panel (initially hidden) matching EventDemoState layout
  // Using TOP_RIGHT positioning - UIManager handles all resize repositioning
  constexpr int inventoryWidth = 280;
  constexpr int inventoryHeight = 400;
  constexpr int panelMarginRight = 20;  // 20px from right edge
  constexpr int panelMarginTop = 170;   // 170px from top
  constexpr int childInset = 10;        // Children are 10px inside panel
  constexpr int childWidth = inventoryWidth - (childInset * 2);  // 260px

  // Initial position (will be auto-repositioned by UIManager)
  int inventoryX = windowWidth - inventoryWidth - panelMarginRight;
  int inventoryY = panelMarginTop;

  ui.createPanel("gameplay_inventory_panel",
                 {inventoryX, inventoryY, inventoryWidth, inventoryHeight});
  ui.setComponentVisible("gameplay_inventory_panel", false);
  ui.setComponentPositioning("gameplay_inventory_panel",
                             {UIPositionMode::TOP_RIGHT, panelMarginRight, panelMarginTop,
                              inventoryWidth, inventoryHeight});

  // Title: 25px below panel top, inset by 10px
  ui.createTitle("gameplay_inventory_title",
                 {inventoryX + childInset, inventoryY + 25, childWidth, 35},
                 "Player Inventory");
  ui.setComponentVisible("gameplay_inventory_title", false);
  ui.setComponentPositioning("gameplay_inventory_title",
                             {UIPositionMode::TOP_RIGHT, panelMarginRight + childInset, panelMarginTop + 25,
                              childWidth, 35});

  // Status label: 75px below panel top
  ui.createLabel("gameplay_inventory_status",
                 {inventoryX + childInset, inventoryY + 75, childWidth, 25},
                 "Capacity: 0/50");
  ui.setComponentVisible("gameplay_inventory_status", false);
  ui.setComponentPositioning("gameplay_inventory_status",
                             {UIPositionMode::TOP_RIGHT, panelMarginRight + childInset, panelMarginTop + 75,
                              childWidth, 25});

  // Inventory list: 110px below panel top
  ui.createList("gameplay_inventory_list",
                {inventoryX + childInset, inventoryY + 110, childWidth, 270});
  ui.setComponentVisible("gameplay_inventory_list", false);
  ui.setComponentPositioning("gameplay_inventory_list",
                             {UIPositionMode::TOP_RIGHT, panelMarginRight + childInset, panelMarginTop + 110,
                              childWidth, 270});

  // --- DATA BINDING SETUP ---
  // Bind the inventory capacity label to a function that gets the data
  ui.bindText("gameplay_inventory_status", [this]() -> std::string {
      if (!mp_Player || !mp_Player->getInventory()) {
          return "Capacity: 0/0";
      }
      const auto* inventory = mp_Player->getInventory();
      int used = inventory->getUsedSlots();
      int max = inventory->getMaxSlots();
      return "Capacity: " + std::to_string(used) + "/" + std::to_string(max);
  });

  // Bind the inventory list to a function that gets the items, sorts them, and returns them
  ui.bindList("gameplay_inventory_list", [this]() -> std::vector<std::string> {
      if (!mp_Player || !mp_Player->getInventory()) {
          return {"(Empty)"};
      }

      const auto* inventory = mp_Player->getInventory();
      auto allResources = inventory->getAllResources();

      if (allResources.empty()) {
          return {"(Empty)"};
      }

      std::vector<std::string> items;
      std::vector<std::pair<std::string, int>> sortedResources;
      for (const auto& [resourceHandle, quantity] : allResources) {
          if (quantity > 0) {
              auto resourceTemplate = ResourceTemplateManager::Instance().getResourceTemplate(resourceHandle);
              std::string resourceName = resourceTemplate ? resourceTemplate->getName() : "Unknown";
              sortedResources.emplace_back(resourceName, quantity);
          }
      }
      std::sort(sortedResources.begin(), sortedResources.end());

      for (const auto& [resourceId, quantity] : sortedResources) {
          items.push_back(resourceId + " x" + std::to_string(quantity));
      }

      if (items.empty()) {
          return {"(Empty)"};
      }

      return items;
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

  mp_Player->getInventory()->addResource(resourceHandle, quantity);
}

void GamePlayState::removeDemoResource(
    HammerEngine::ResourceHandle resourceHandle, int quantity) {
  if (!mp_Player) {
    return;
  }

  if (!resourceHandle.isValid()) {
    return;
  }

  mp_Player->getInventory()->removeResource(resourceHandle, quantity);
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
  Vector2D playerPosition = mp_Player ? mp_Player->getPosition() : Vector2D(0, 0);

  // Create camera starting at player position with logical viewport dimensions
  m_camera = std::make_unique<HammerEngine::Camera>(
    playerPosition.getX(), playerPosition.getY(),
    static_cast<float>(gameEngine.getLogicalWidth()),
    static_cast<float>(gameEngine.getLogicalHeight())
  );

  // Configure camera to follow player
  if (mp_Player) {
    // Set target and enable follow mode
    std::weak_ptr<Entity> playerAsEntity = std::static_pointer_cast<Entity>(mp_Player);
    m_camera->setTarget(playerAsEntity);
    m_camera->setMode(HammerEngine::Camera::Mode::Follow);

    // Set up camera configuration for smooth following
    // Using critically damped spring (SmoothDamp) for smooth, non-oscillating follow
    HammerEngine::Camera::Config config;
    config.smoothTime = 0.12f;         // Time to reach target (lower = snappier, 0.1-0.3 typical)
    config.deadZoneRadius = 0.0f;      // No dead zone - always follow
    config.maxSpeed = 800.0f;          // Max camera speed in pixels/second
    config.clampToWorldBounds = true;  // Keep camera within world bounds
    m_camera->setConfig(config);

    // Camera auto-synchronizes world bounds on update
  }
}

void GamePlayState::updateCamera(float deltaTime) {
  // Defensive null check (camera always initialized in enter(), but kept for safety)
  if (m_camera) {
    // Sync viewport with current window size (handles resize events)
    m_camera->syncViewportWithEngine();

    // Update camera position and following logic
    m_camera->update(deltaTime);
  }
}

// Removed setupCameraForWorld(): camera manages world bounds itself

void GamePlayState::onTimePeriodChanged(const EventData& data) {
  if (!data.event) {
    return;
  }

  // Check if this is a TimePeriodChangedEvent
  auto timeEvent = std::static_pointer_cast<TimeEvent>(data.event);
  if (timeEvent->getTimeEventType() != TimeEventType::TimePeriodChanged) {
    return;
  }

  auto periodEvent = std::static_pointer_cast<TimePeriodChangedEvent>(data.event);
  const auto& visuals = periodEvent->getVisuals();

  // Set target values - interpolation happens in updateDayNightOverlay()
  m_dayNightTargetR = static_cast<float>(visuals.overlayR);
  m_dayNightTargetG = static_cast<float>(visuals.overlayG);
  m_dayNightTargetB = static_cast<float>(visuals.overlayB);
  m_dayNightTargetA = static_cast<float>(visuals.overlayA);

  // Track current period for weather change handling
  m_currentTimePeriod = periodEvent->getPeriod();

  // Update ambient particles for the new time period
  updateAmbientParticles(m_currentTimePeriod);

  GAMEPLAY_DEBUG("Day/night transition started to period: " +
                 std::string(periodEvent->getPeriodName()));
}

void GamePlayState::updateDayNightOverlay(float deltaTime) {
  // Calculate interpolation speed based on transition duration
  // Using exponential smoothing for natural-feeling transitions
  float lerpFactor = 1.0f - std::exp(-deltaTime * (3.0f / DAY_NIGHT_TRANSITION_DURATION));

  // Interpolate each channel toward target
  m_dayNightOverlayR += (m_dayNightTargetR - m_dayNightOverlayR) * lerpFactor;
  m_dayNightOverlayG += (m_dayNightTargetG - m_dayNightOverlayG) * lerpFactor;
  m_dayNightOverlayB += (m_dayNightTargetB - m_dayNightOverlayB) * lerpFactor;
  m_dayNightOverlayA += (m_dayNightTargetA - m_dayNightOverlayA) * lerpFactor;
}

void GamePlayState::renderDayNightOverlay(SDL_Renderer* renderer, int width, int height) {
  // Skip if no tint (alpha near 0)
  if (m_dayNightOverlayA < 0.5f) {
    return;
  }

  // Blend mode already set globally by GameEngine at init
  SDL_SetRenderDrawColor(renderer,
      static_cast<uint8_t>(m_dayNightOverlayR),
      static_cast<uint8_t>(m_dayNightOverlayG),
      static_cast<uint8_t>(m_dayNightOverlayB),
      static_cast<uint8_t>(m_dayNightOverlayA));

  SDL_FRect rect = {0, 0, static_cast<float>(width), static_cast<float>(height)};
  SDL_RenderFillRect(renderer, &rect);
}

void GamePlayState::updateAmbientParticles(TimePeriod period) {
  // Only spawn ambient particles during clear weather
  if (WeatherController::Instance().getCurrentWeather() != WeatherType::Clear) {
    stopAmbientParticles();
    return;
  }

  auto& pm = ParticleManager::Instance();
  const auto& gameEngine = GameEngine::Instance();
  Vector2D screenCenter(gameEngine.getLogicalWidth() / 2.0f,
                        gameEngine.getLogicalHeight() / 2.0f);

  // Stop existing ambient particles with fade-out transition
  stopAmbientParticles();

  // Start appropriate particles for the new period
  switch (period) {
    case TimePeriod::Morning:
      // Light dust motes in morning sunlight
      m_ambientDustEffectId = pm.playIndependentEffect(
          ParticleEffectType::AmbientDust, screenCenter, 0.6f, -1.0f, "ambient");
      break;

    case TimePeriod::Day:
      // Subtle dust particles during the day
      m_ambientDustEffectId = pm.playIndependentEffect(
          ParticleEffectType::AmbientDust, screenCenter, 0.4f, -1.0f, "ambient");
      break;

    case TimePeriod::Evening:
      // Golden dust in evening light
      m_ambientDustEffectId = pm.playIndependentEffect(
          ParticleEffectType::AmbientDust, screenCenter, 0.8f, -1.0f, "ambient");
      break;

    case TimePeriod::Night:
      // Fireflies at night
      m_ambientFireflyEffectId = pm.playIndependentEffect(
          ParticleEffectType::AmbientFirefly, screenCenter, 1.0f, -1.0f, "ambient");
      break;
  }
}

void GamePlayState::stopAmbientParticles() {
  auto& pm = ParticleManager::Instance();

  if (m_ambientDustEffectId != 0) {
    pm.stopIndependentEffect(m_ambientDustEffectId);
    m_ambientDustEffectId = 0;
  }

  if (m_ambientFireflyEffectId != 0) {
    pm.stopIndependentEffect(m_ambientFireflyEffectId);
    m_ambientFireflyEffectId = 0;
  }
}

void GamePlayState::onWeatherChanged(const EventData& data) {
  if (!data.event) {
    return;
  }

  // When weather changes, update ambient particles (they only show during clear weather)
  auto weatherEvent = std::static_pointer_cast<WeatherEvent>(data.event);

  // Re-evaluate ambient particles based on current time period and new weather
  updateAmbientParticles(m_currentTimePeriod);

  GAMEPLAY_DEBUG("Weather changed to: " + weatherEvent->getWeatherTypeString() +
                 " - updating ambient particles");
}
