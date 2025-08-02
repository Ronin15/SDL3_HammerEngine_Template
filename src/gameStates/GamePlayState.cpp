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
#include <iostream>
#include <random>

bool GamePlayState::enter() {
  // Create the player
  mp_Player = std::make_shared<Player>();
  mp_Player->initializeInventory(); // Initialize after construction

  // Initialize the inventory UI first
  initializeInventoryUI();

  // Initialize resource handles (resolve names to handles once during
  // initialization)
  initializeResourceHandles();
  
  // Initialize camera for world navigation
  initializeCamera();
  
  // Initialize and load a new world
  auto& worldManager = HammerEngine::WorldManager::Instance();
  if (!worldManager.isInitialized()) {
    if (!worldManager.init()) {
      std::cerr << "Failed to initialize WorldManager" << std::endl;
      return false;
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
    // Continue anyway - game can function without world
  } else {
    std::cout << "Successfully loaded world with seed: " << config.seed << std::endl;
    
    // Setup camera to work with the world
    setupCameraForWorld();
  }

  return true;
}

void GamePlayState::update([[maybe_unused]] float deltaTime) {
  // std::cout << "Updating GAME State\n";

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

  // Update inventory display
  updateInventoryUI();
}

void GamePlayState::render(double alpha) {
  // std::cout << "Rendering GAME State\n";

  // Cache manager references for better performance
  FontManager &fontMgr = FontManager::Instance();
  const auto &gameEngine = GameEngine::Instance();

  SDL_Color fontColor = {200, 200, 200, 255};
  fontMgr.drawText("Game State with Inventory Demo <-> [P] Pause <-> [B] Main "
                   "Menu <-> [I] Toggle Inventory <-> [1-5] Add Items",
                   "fonts_Arial",
                   gameEngine.getLogicalWidth() / 2, // Center horizontally
                   20, fontColor, gameEngine.getRenderer());

  mp_Player->render(alpha);

  // Render UI components
  auto &ui = UIManager::Instance();
  ui.render(gameEngine.getRenderer());
}
bool GamePlayState::exit() {
  std::cout << "Hammer Game Engine - Exiting GAME State\n";

  // Unload the world when exiting gameplay
  auto& worldManager = HammerEngine::WorldManager::Instance();
  if (worldManager.isInitialized() && worldManager.hasActiveWorld()) {
    worldManager.unloadWorld();
    std::cout << "World unloaded from GamePlayState\n";
  }

  // Clean up UI components
  auto &ui = UIManager::Instance();
  ui.removeComponentsWithPrefix("gameplay_");

  // Only clear specific textures if we're not transitioning to pause state
  if (!m_transitioningToPause) {
    // Reset player
    mp_Player = nullptr;
    // TODO need to evaluate if this entire block is needed. I want to keep all
    // texture in the MAP
    //  and not clear any as they may be needed. left over from testing but not
    //  hurting anything currently.
    std::cout << "Hammer Game Engine - reset player pointer to null, not going "
                 "to pause\n";
  } else {
    std::cout << "Hammer Game Engine - Not clearing player and textures, "
                 "transitioning to pause\n";
  }

  // GamePlayState doesn't create UI components, so no UI cleanup needed

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
      std::cout << "Hammer Game Engine - Created PAUSE State\n";
    }
    m_transitioningToPause = true; // Set flag before transitioning
    gameStateManager->pushState("PauseState");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
    std::cout << "Hammer Game Engine - Transitioning to MainMenuState...\n";
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
}

void GamePlayState::initializeInventoryUI() {
  auto &ui = UIManager::Instance();
  const auto &gameEngine = GameEngine::Instance();
  int windowWidth = gameEngine.getLogicalWidth();

  // Create inventory panel (initially hidden)
  int inventoryWidth = 300;
  int inventoryHeight = 420; // Increased height to accommodate better spacing
  int inventoryX = windowWidth - inventoryWidth - 20;
  int inventoryY = 60;

  ui.createPanel("gameplay_inventory_panel",
                 {inventoryX, inventoryY, inventoryWidth, inventoryHeight});
  ui.createTitle("gameplay_inventory_title",
                 {inventoryX + 10, inventoryY + 10, inventoryWidth - 20, 30},
                 "Player Inventory");

  // Create inventory list for displaying items with better spacing
  ui.createList("gameplay_inventory_list",
                {inventoryX + 10, inventoryY + 50, inventoryWidth - 20, 260});

  // Create resource status labels with better vertical spacing
  ui.createLabel("gameplay_gold_label",
                 {inventoryX + 10, inventoryY + 330, inventoryWidth - 20, 20},
                 "Gold: 0");
  ui.createLabel("gameplay_potions_label",
                 {inventoryX + 10, inventoryY + 355, inventoryWidth - 20, 20},
                 "Health Potions: 0");
  ui.createLabel("gameplay_total_items",
                 {inventoryX + 10, inventoryY + 380, inventoryWidth - 20, 20},
                 "Total Items: 0");

  // Hide inventory initially
  ui.setComponentVisible("gameplay_inventory_panel", false);
  ui.setComponentVisible("gameplay_inventory_title", false);
  ui.setComponentVisible("gameplay_inventory_list", false);
  ui.setComponentVisible("gameplay_gold_label", false);
  ui.setComponentVisible("gameplay_potions_label", false);
  ui.setComponentVisible("gameplay_total_items", false);
}

void GamePlayState::updateInventoryUI() {
  if (!mp_Player || !mp_Player->getInventory()) {
    return;
  }

  auto *inventory = mp_Player->getInventory();
  UIManager &ui = UIManager::Instance();

  int goldCount = 0;
  if (m_goldHandle.isValid()) {
    goldCount = inventory->getResourceQuantity(m_goldHandle);
  }

  int potionCount = 0;
  if (m_healthPotionHandle.isValid()) {
    potionCount = inventory->getResourceQuantity(m_healthPotionHandle);
  }

  size_t totalItems = inventory->getUsedSlots();

  ui.setText("gameplay_gold_label", "Gold: " + std::to_string(goldCount));
  ui.setText("gameplay_potions_label",
             "Health Potions: " + std::to_string(potionCount));
  ui.setText("gameplay_total_items",
             "Total Items: " + std::to_string(totalItems) + "/" +
                 std::to_string(inventory->getMaxSlots()));

  // Update inventory list
  ui.clearList("gameplay_inventory_list");
  auto allResources = inventory->getAllResources();
  for (const auto &[resourceHandle, quantity] : allResources) {
    if (quantity > 0) {
      auto resourceTemplate =
          ResourceTemplateManager::Instance().getResourceTemplate(
              resourceHandle);
      std::string displayName =
          resourceTemplate ? resourceTemplate->getName() : "Unknown Resource";
      ui.addListItem("gameplay_inventory_list",
                     displayName + " x" + std::to_string(quantity));
    }
  }
}

void GamePlayState::toggleInventoryDisplay() {
  auto &ui = UIManager::Instance();
  m_inventoryVisible = !m_inventoryVisible;

  ui.setComponentVisible("gameplay_inventory_panel", m_inventoryVisible);
  ui.setComponentVisible("gameplay_inventory_title", m_inventoryVisible);
  ui.setComponentVisible("gameplay_inventory_list", m_inventoryVisible);
  ui.setComponentVisible("gameplay_gold_label", m_inventoryVisible);
  ui.setComponentVisible("gameplay_potions_label", m_inventoryVisible);
  ui.setComponentVisible("gameplay_total_items", m_inventoryVisible);

  std::cout << "Inventory " << (m_inventoryVisible ? "shown" : "hidden")
            << std::endl;
}

void GamePlayState::addDemoResource(HammerEngine::ResourceHandle resourceHandle,
                                    int quantity) {
  if (!mp_Player) {
    std::cout << "Player not available for resource addition" << std::endl;
    return;
  }

  if (!resourceHandle.isValid()) {
    std::cout << "Invalid resource handle" << std::endl;
    return;
  }

  bool success =
      mp_Player->getInventory()->addResource(resourceHandle, quantity);
  if (success) {
    std::cout << "Added " << quantity
              << " resources (handle: " << resourceHandle.toString()
              << ") to player inventory" << std::endl;
  } else {
    std::cout << "Failed to add resources to inventory (might be full)"
              << std::endl;
  }
}

void GamePlayState::removeDemoResource(
    HammerEngine::ResourceHandle resourceHandle, int quantity) {
  if (!mp_Player) {
    std::cout << "Player not available for resource removal" << std::endl;
    return;
  }

  if (!resourceHandle.isValid()) {
    std::cout << "Invalid resource handle" << std::endl;
    return;
  }

  bool success =
      mp_Player->getInventory()->removeResource(resourceHandle, quantity);
  if (success) {
    std::cout << "Removed " << quantity
              << " resources (handle: " << resourceHandle.toString()
              << ") from player inventory" << std::endl;
  } else {
    std::cout
        << "Failed to remove resources from inventory (insufficient quantity)"
        << std::endl;
  }
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
  
  // Create camera with current screen dimensions
  m_camera = std::make_unique<HammerEngine::Camera>(
    0.0f, 0.0f, // Initial position
    static_cast<float>(gameEngine.getLogicalWidth()),
    static_cast<float>(gameEngine.getLogicalHeight())
  );
  
  // Configure camera to follow player
  if (mp_Player && m_camera) {
    m_camera->setMode(HammerEngine::Camera::Mode::Follow);
    // Cast Player to Entity since Player inherits from Entity
    std::weak_ptr<Entity> playerAsEntity = std::static_pointer_cast<Entity>(mp_Player);
    m_camera->setTarget(playerAsEntity);
    
    // Set up camera configuration for smooth following
    HammerEngine::Camera::Config config;
    config.followSpeed = 8.0f;        // Responsive following
    config.deadZoneRadius = 16.0f;    // Small dead zone for tight control
    config.smoothingFactor = 0.92f;   // Smooth interpolation
    config.clampToWorldBounds = true; // Keep camera within world
    m_camera->setConfig(config);
    
    // Set up world bounds based on current world (if available)
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
  HammerEngine::WorldManager& worldManager = HammerEngine::WorldManager::Instance();
  
  HammerEngine::Camera::Bounds worldBounds;
  float minX, minY, maxX, maxY;
  
  if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
    // Use actual world bounds
    worldBounds.minX = minX;
    worldBounds.minY = minY;
    worldBounds.maxX = maxX;
    worldBounds.maxY = maxY;
  } else {
    // Fall back to default bounds if no world is loaded
    worldBounds.minX = 0.0f;
    worldBounds.minY = 0.0f;
    worldBounds.maxX = 1000.0f;  // Default world width
    worldBounds.maxY = 1000.0f;  // Default world height
  }
  
  m_camera->setWorldBounds(worldBounds);
}