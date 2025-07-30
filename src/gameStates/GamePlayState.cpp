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
#include <iostream>

bool GamePlayState::enter() {
  // Create the player
  mp_Player = std::make_shared<Player>();
  mp_Player->initializeInventory(); // Initialize after construction

  // Initialize the inventory UI first
  initializeInventoryUI();

  // Initialize resource handles (resolve names to handles once during
  // initialization)
  initializeResourceHandles();

  return true;
}

void GamePlayState::update([[maybe_unused]] float deltaTime) {
  // std::cout << "Updating GAME State\n";

  // Update player if it exists
  if (mp_Player) {
    mp_Player->update(deltaTime);
  }

  // Update UI
  auto &ui = UIManager::Instance();
  if (!ui.isShutdown()) {
    ui.update(deltaTime);
  }

  // Update inventory display
  updateInventoryUI();
}

void GamePlayState::render([[maybe_unused]] float deltaTime) {
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

  mp_Player->render();

  // Render UI components
  auto &ui = UIManager::Instance();
  ui.render(gameEngine.getRenderer());
}
bool GamePlayState::exit() {
  std::cout << "Hammer Game Engine - Exiting GAME State\n";

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
    gameStateManager->setState("PauseState");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
    std::cout << "Hammer Game Engine - Transitioning to MainMenuState...\n";
    const auto &gameEngine = GameEngine::Instance();
    gameEngine.getGameStateManager()->setState("MainMenuState");
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