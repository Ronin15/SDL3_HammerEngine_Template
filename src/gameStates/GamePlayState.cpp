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
#include "world/WeatherController.hpp"
#include "world/TimeEventController.hpp"
#include "managers/UIConstants.hpp"
#include "utils/Camera.hpp"
#include <algorithm>

#include <random>


bool GamePlayState::enter() {
  // Reset transition flags when entering state
  m_transitioningToPause = false;
  m_transitioningToLoading = false;

  // Check if already initialized (resuming from pause)
  if (m_initialized) {
    // Resuming from pause - show UI components again
    auto& ui = UIManager::Instance();
    ui.setComponentVisible("gameplay_event_log", true);
    ui.setComponentVisible("gameplay_time_label", true);

    return true;
  }

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
    TimeEventController::Instance().subscribe("gameplay_event_log");

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
    auto& timeController = TimeEventController::Instance();
    timeController.setStatusLabel("gameplay_time_label");
    timeController.setStatusFormatMode(TimeEventController::StatusFormatMode::Extended);

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

  // Update UI
  auto &ui = UIManager::Instance();
  if (!ui.isShutdown()) {
    ui.update(deltaTime);
  }

  // Inventory display is now updated automatically via data binding.
}

void GamePlayState::render() {
  // Get renderer using the standard pattern (consistent with other states)
  auto &gameEngine = GameEngine::Instance();
  SDL_Renderer *renderer = gameEngine.getRenderer();

  // Calculate camera view rect ONCE for all rendering to ensure perfect synchronization
  HammerEngine::Camera::ViewRect viewRect{0.0f, 0.0f, 0.0f, 0.0f};
  if (m_camera) {
    viewRect = m_camera->getViewRect();
  }

  // Set render scale for zoom (scales all world/entity rendering automatically)
  float zoom = m_camera ? m_camera->getZoom() : 1.0f;
  SDL_SetRenderScale(renderer, zoom, zoom);

  // Render world using camera coordinate transformations
  if (m_camera) {
    auto &worldMgr = WorldManager::Instance();
    if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
      worldMgr.render(renderer,
                     viewRect.x, viewRect.y,  // Camera view area
                     viewRect.width, viewRect.height);
    }
  }

  // Render player using camera coordinate transformations
  if (mp_Player && m_camera) {
    mp_Player->render(m_camera.get());  // Pass camera for coordinate transformation
  }

  // Reset render scale to 1.0 for UI rendering (UI should not be zoomed)
  SDL_SetRenderScale(renderer, 1.0f, 1.0f);

  // Render UI components (no camera transformation)
  auto &ui = UIManager::Instance();
  ui.render(renderer);
}
bool GamePlayState::exit() {
  if (m_transitioningToPause) {
    // Transitioning to pause - PRESERVE ALL GAMEPLAY DATA
    // PauseState will overlay on top, and GameStateManager will only update PauseState

    // Hide UI components during pause overlay
    auto& ui = UIManager::Instance();
    ui.setComponentVisible("gameplay_event_log", false);
    ui.setComponentVisible("gameplay_time_label", false);

    // Reset the flag after using it
    m_transitioningToPause = false;

    // Return early - NO cleanup when going to pause, keep m_initialized = true
    return true;
  }

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

  // Unsubscribe from automatic weather events and disable auto-weather
  WeatherController::Instance().unsubscribe();
  GameTime::Instance().enableAutoWeather(false);

  // Reset status format mode and unsubscribe from time events
  TimeEventController::Instance().setStatusFormatMode(TimeEventController::StatusFormatMode::Default);
  TimeEventController::Instance().unsubscribe();

  // Reset initialization flag for next fresh start
  m_initialized = false;

  return true;
}

std::string GamePlayState::getName() const { return "GamePlayState"; }

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
    // Stop player movement to prevent jittering during pause
    if (mp_Player) {
      mp_Player->setVelocity(Vector2D(0, 0));
    }

    m_transitioningToPause = true; // Set flag before transitioning
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
  int inventoryWidth = 280;  // Match EventDemoState width
  int inventoryHeight = 400; // Match EventDemoState height
  int inventoryX = windowWidth - inventoryWidth - 20;
  int inventoryY = 170;  // Match EventDemoState Y position

  ui.createPanel("gameplay_inventory_panel",
                 {inventoryX, inventoryY, inventoryWidth, inventoryHeight});
  ui.setComponentVisible("gameplay_inventory_panel", false);
  // Set auto-repositioning: right-aligned with fixed offsetY from top
  ui.setComponentPositioning("gameplay_inventory_panel", {UIPositionMode::RIGHT_ALIGNED, 20, inventoryY - (ui.getLogicalHeight() - inventoryHeight) / 2, inventoryWidth, inventoryHeight});

  ui.createTitle("gameplay_inventory_title",
                 {inventoryX + 10, inventoryY + 25, inventoryWidth - 20, 35},
                 "Player Inventory");
  ui.setComponentVisible("gameplay_inventory_title", false);
  // Children will be repositioned in onWindowResize based on panel position

  ui.createLabel("gameplay_inventory_status",
                 {inventoryX + 10, inventoryY + 75, inventoryWidth - 20, 25},
                 "Capacity: 0/50");
  ui.setComponentVisible("gameplay_inventory_status", false);

  // Create inventory list for displaying items matching EventDemoState
  ui.createList("gameplay_inventory_list",
                {inventoryX + 10, inventoryY + 110, inventoryWidth - 20, 270});
  ui.setComponentVisible("gameplay_inventory_list", false);

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

void GamePlayState::onWindowResize(int newLogicalWidth,
                                    int newLogicalHeight) {
  // Panel auto-repositions via UIManager, but children need manual updates (they're relative to panel)
  auto &ui = UIManager::Instance();

  const int inventoryWidth = 280;

  // Get the panel's new position (auto-repositioned by UIManager)
  UIRect panelBounds = ui.getBounds("gameplay_inventory_panel");
  const int inventoryX = panelBounds.x;
  const int inventoryY = panelBounds.y;

  // Reposition children relative to panel's new position
  ui.setComponentBounds("gameplay_inventory_title",
                        {inventoryX + 10, inventoryY + 25,
                         inventoryWidth - 20, 35});

  ui.setComponentBounds("gameplay_inventory_status",
                        {inventoryX + 10, inventoryY + 75,
                         inventoryWidth - 20, 25});

  ui.setComponentBounds("gameplay_inventory_list",
                        {inventoryX + 10, inventoryY + 110,
                         inventoryWidth - 20, 270});

  GAMEPLAY_DEBUG("Repositioned inventory UI for new window size: " +
                 std::to_string(newLogicalWidth) + "x" +
                 std::to_string(newLogicalHeight) + " (panel auto-positioned)");
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
    HammerEngine::Camera::Config config;
    config.followSpeed = 8.0f;         // Faster follow for action gameplay
    config.deadZoneRadius = 0.0f;      // No dead zone - always follow
    config.smoothingFactor = 0.80f;    // Quicker response smoothing
    config.maxFollowDistance = 9999.0f; // No distance limit
    config.clampToWorldBounds = true;  // ENABLE clamping - player is now bounded so no jitter
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
