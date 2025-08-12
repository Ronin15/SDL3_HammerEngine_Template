/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/GamePlayState.hpp"
#include "core/GameEngine.hpp"
#include "gameStates/PauseState.hpp"
#include "managers/FontManager.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldManager.hpp"
#include "world/WorldData.hpp"
#include "utils/Camera.hpp"
#include <algorithm>
#include <iostream>
#include <random>


bool GamePlayState::enter() {
  // Reset transition flag when entering state
  m_transitioningToPause = false;
  
  // Check if already initialized (resuming from pause)
  if (m_initialized) {
    // No longer need to unpause GameLoop - PauseState simply pops off the stack
    // GameStateManager will resume updating this state automatically
    
    return true;
  }
  
  // Initialize resource handles first
  initializeResourceHandles();
  
  // Create player and position at screen center
  mp_Player = std::make_shared<Player>();
  mp_Player->initializeInventory();
  
  // Position player at screen center
  const auto &gameEngine = GameEngine::Instance();
  Vector2D screenCenter(gameEngine.getLogicalWidth() / 2, gameEngine.getLogicalHeight() / 2);
  mp_Player->setPosition(screenCenter);

  // Initialize the inventory UI
  initializeInventoryUI();
  
  // Initialize world and camera
  initializeWorld();
  initializeCamera();

  // Mark as initialized for future pause/resume cycles
  m_initialized = true;

  return true;
}

void GamePlayState::update([[maybe_unused]] float deltaTime) {
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

  // Cache manager references for better performance
  FontManager &fontMgr = FontManager::Instance();

  // Get camera view area for culling and rendering
  HammerEngine::Camera::ViewRect viewRect = m_camera->getViewRect();

  // Render world using camera coordinate transformations
  auto &worldMgr = WorldManager::Instance();
  if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
    worldMgr.render(renderer, 
                   viewRect.x, viewRect.y,  // Camera view area
                   viewRect.width, viewRect.height);
  }

  // Render player using camera coordinate transformations
  if (mp_Player) {
    mp_Player->render(m_camera.get());  // Pass camera for coordinate transformation
  }

  // Render UI components (no camera transformation)
  SDL_Color fontColor = {200, 200, 200, 255};
  fontMgr.drawText("Game State with Inventory Demo <-> [P] Pause <-> [B] Main "
                   "Menu <-> [I] Toggle Inventory <-> [1-5] Add Items",
                   "fonts_Arial",
                   gameEngine.getLogicalWidth() / 2, // Center horizontally
                   20, fontColor, renderer);

  auto &ui = UIManager::Instance();
  ui.render(renderer);
}
bool GamePlayState::exit() {
  if (m_transitioningToPause) {
    // Transitioning to pause - PRESERVE ALL GAMEPLAY DATA
    // PauseState will overlay on top, and GameStateManager will only update PauseState
    
    // Reset the flag after using it
    m_transitioningToPause = false;
    
    // Return early - NO cleanup when going to pause, keep m_initialized = true
    return true;
  }

  // Full exit (going to main menu, other states, or shutting down)
  
  // Unload the world when fully exiting gameplay
  auto& worldManager = WorldManager::Instance();
  if (worldManager.isInitialized() && worldManager.hasActiveWorld()) {
    worldManager.unloadWorld();
  }

  // Full UI cleanup using standard pattern
  auto &ui = UIManager::Instance();
  ui.prepareForStateTransition();
  
  // Reset player
  mp_Player = nullptr;

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
            int tileX = static_cast<int>(worldPos.getX() / 32);
            int tileY = static_cast<int>(worldPos.getY() / 32);

            auto& worldMgr = WorldManager::Instance();
            if (worldMgr.isValidPosition(tileX, tileY)) {
                const auto* tile = worldMgr.getTileAt(tileX, tileY);
                if (tile) {
                    // Log tile information for now
                    // Later, this could trigger events or actions
                    std::cout << "Clicked tile (" << tileX << ", " << tileY << ") - Biome: " 
                              << static_cast<int>(tile->biome) << ", Obstacle: " 
                              << static_cast<int>(tile->obstacleType) << std::endl;
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
  
  ui.createTitle("gameplay_inventory_title",
                 {inventoryX + 10, inventoryY + 25, inventoryWidth - 20, 35},
                 "Player Inventory");
  ui.setComponentVisible("gameplay_inventory_title", false);
  
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
      auto* inventory = mp_Player->getInventory();
      int used = inventory->getUsedSlots();
      int max = inventory->getMaxSlots();
      return "Capacity: " + std::to_string(used) + "/" + std::to_string(max);
  });

  // Bind the inventory list to a function that gets the items, sorts them, and returns them
  ui.bindList("gameplay_inventory_list", [this]() -> std::vector<std::string> {
      if (!mp_Player || !mp_Player->getInventory()) {
          return {"(Empty)"};
      }
      
      auto* inventory = mp_Player->getInventory();
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

void GamePlayState::initializeWorld() {
  // Initialize and load a new world following EventDemoState pattern
  auto& worldManager = WorldManager::Instance();
  if (!worldManager.isInitialized()) {
    if (!worldManager.init()) {
      std::cerr << "Failed to initialize WorldManager" << std::endl;
      return;
    }
  }
  
  // Create a default world configuration
  // TODO: Make this configurable or load from settings
  HammerEngine::WorldGenerationConfig config;
  config.width = 100;
  config.height = 100;
  
  // Generate a random seed for world variety
  std::random_device rd;
  config.seed = rd();
  
  // Set reasonable defaults for world generation
  config.elevationFrequency = 0.05f;
  config.humidityFrequency = 0.03f;
  config.waterLevel = 0.3f;
  config.mountainLevel = 0.7f;
  
  if (!worldManager.loadNewWorld(config)) {
    std::cerr << "Failed to load new world in GamePlayState" << std::endl;
    // Continue anyway like EventDemoState - game can function without world
  }
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
  if (mp_Player && m_camera) {
    // DISABLE EVENT FIRING for testing jitter
    m_camera->setEventFiringEnabled(false);
    
    // Set target and enable follow mode
    std::weak_ptr<Entity> playerAsEntity = std::static_pointer_cast<Entity>(mp_Player);
    m_camera->setTarget(playerAsEntity);
    m_camera->setMode(HammerEngine::Camera::Mode::Follow);
    
    // Set up camera configuration for smooth following (RESTORED ORIGINAL SMOOTH SETTINGS)
    HammerEngine::Camera::Config config;
    config.followSpeed = 2.5f;         // Original smooth settings
    config.deadZoneRadius = 0.0f;      // No dead zone - always follow
    config.smoothingFactor = 0.85f;    // Original exponential smoothing
    config.maxFollowDistance = 9999.0f; // No distance limit
    config.clampToWorldBounds = false; // DISABLE clamping for testing jitter
    m_camera->setConfig(config);
    
    // Set up world bounds for camera (called after world is loaded)
    setupCameraForWorld();
  }
}

void GamePlayState::updateCamera(float deltaTime) {
  if (m_camera) {
    m_camera->update(deltaTime);
  }
}

void GamePlayState::setupCameraForWorld() {
  if (!m_camera) {
    return;
  }
  
  // Get actual world bounds from WorldManager
  WorldManager& worldManager = WorldManager::Instance();
  
  HammerEngine::Camera::Bounds worldBounds;
  float minX, minY, maxX, maxY;
  
  if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
    // Convert tile coordinates to pixel coordinates (WorldManager returns tile coords)
    // TileRenderer uses 32px per tile
    const float TILE_SIZE = 32.0f;
    worldBounds.minX = minX * TILE_SIZE;
    worldBounds.minY = minY * TILE_SIZE;
    worldBounds.maxX = maxX * TILE_SIZE;
    worldBounds.maxY = maxY * TILE_SIZE;
  } else {
    // Fall back to default bounds if no world is loaded
    worldBounds.minX = 0.0f;
    worldBounds.minY = 0.0f;
    worldBounds.maxX = 3200.0f;  // 100 tiles * 32px = 3200px
    worldBounds.maxY = 3200.0f;  // 100 tiles * 32px = 3200px
  }
  
  m_camera->setWorldBounds(worldBounds);
}

void GamePlayState::applyCameraTransformation() {
  if (!m_camera) {
    return;
  }
  
  // Calculate camera offset for later use in rendering
  auto viewRect = m_camera->getViewRect();
  m_cameraOffsetX = viewRect.x;
  m_cameraOffsetY = viewRect.y;
}