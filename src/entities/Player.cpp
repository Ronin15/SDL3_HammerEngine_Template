/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/Player.hpp"
#include "SDL3/SDL_surface.h"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "entities/playerStates/PlayerIdleState.hpp"
#include "entities/playerStates/PlayerRunningState.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "managers/EventManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/TextureManager.hpp"
#include <SDL3/SDL.h>

Player::Player() {
  // Initialize player properties
  m_position =
      Vector2D(400, 300); // Start position in the middle of a typical screen
  m_velocity = Vector2D(0, 0);
  m_acceleration = Vector2D(0, 0);
  m_textureID =
      "player"; // Texture ID as loaded by TextureManager from res/img directory

  // Animation properties
  m_currentFrame = 1;    // Start with first frame
  m_currentRow = 1;      // In TextureManager::drawFrame, rows start at 1
  m_numFrames = 2;       // Number of frames in the animation
  m_animSpeed = 100;     // Animation speed in milliseconds
  m_spriteSheetRows = 1; // Number of rows in the sprite sheet
  m_lastFrameTime =
      SDL_GetTicks();     // Track when we last changed animation frame
  m_flip = SDL_FLIP_NONE; // Default flip direction

  // Set width and height based on texture dimensions if the texture is loaded
  loadDimensionsFromTexture();

  // Setup state manager and add states
  setupStates();

  // Setup inventory system - NOTE: Do NOT call setupInventory() here
  // because it can trigger shared_this() during construction.
  // Call setupInventory() after construction completes.
  // Set default state
  changeState("idle");

  // std::cout << "Hammer Game Engine - Player created" << "\n";
}

// Helper method to get dimensions from the loaded texture
void Player::loadDimensionsFromTexture() {
  // Default dimensions in case texture loading fails
  m_width = 128;
  m_height = 128;    // Set height equal to the sprite sheet row height
  m_frameWidth = 64; // Default frame width (width/numFrames)

  // Cache TextureManager reference for better performance
  const TextureManager &texMgr = TextureManager::Instance();

  // Get the texture from TextureManager
  if (texMgr.isTextureInMap(m_textureID)) {
    auto texture = texMgr.getTexture(m_textureID);
    if (texture != nullptr) {
      float width = 0.0f;
      float height = 0.0f;
      // Query the texture to get its width and height
      // SDL3 uses SDL_GetTextureSize which returns float dimensions and returns
      // a bool
      if (SDL_GetTextureSize(texture.get(), &width, &height)) {
        PLAYER_DEBUG("Original texture dimensions: " + std::to_string(width) +
                     "x" + std::to_string(height));

        // Store original dimensions for full sprite sheet
        m_width = static_cast<int>(width);
        m_height = static_cast<int>(height);

        // Calculate frame dimensions based on sprite sheet layout
        m_frameWidth = m_width / m_numFrames;           // Width per frame
        int frameHeight = m_height / m_spriteSheetRows; // Height per row

        // Update height to be the height of a single frame
        m_height = frameHeight;

        PLAYER_DEBUG("Loaded texture dimensions: " + std::to_string(m_width) +
                     "x" + std::to_string(height));
        PLAYER_DEBUG("Frame dimensions: " + std::to_string(m_frameWidth) + "x" +
                     std::to_string(frameHeight));
        PLAYER_DEBUG("Sprite layout: " + std::to_string(m_numFrames) +
                     " columns x " + std::to_string(m_spriteSheetRows) +
                     " rows");
      } else {
        PLAYER_ERROR("Failed to query texture dimensions: " +
                     std::string(SDL_GetError()));
      }
    }
  } else {
    PLAYER_ERROR("Texture '" + m_textureID + "' not found in TextureManager");
  }
}

void Player::setupStates() {
  // Create and add states
  m_stateManager.addState("idle", std::make_unique<PlayerIdleState>(*this));
  m_stateManager.addState("running",
                          std::make_unique<PlayerRunningState>(*this));
}

Player::~Player() {
  // Don't call virtual functions from destructors
  // Instead of calling clean(), directly handle cleanup here

  PLAYER_DEBUG("Cleaning up player resources");
  PLAYER_DEBUG("Player resources cleaned!");
}

void Player::changeState(const std::string &stateName) {
  if (m_stateManager.hasState(stateName)) {
    m_stateManager.setState(stateName);
  } else {
    PLAYER_ERROR("Player state not found: " + stateName);
  }
}

std::string Player::getCurrentStateName() const {
  return m_stateManager.getCurrentStateName();
}

void Player::update(float deltaTime) {
  // Let the state machine handle ALL movement and input logic
  m_stateManager.update(deltaTime);

  // Apply velocity to position
  m_position += m_velocity * deltaTime;

  // If the texture dimensions haven't been loaded yet, try loading them
  if (m_frameWidth == 0 &&
      TextureManager::Instance().isTextureInMap(m_textureID)) {
    loadDimensionsFromTexture();
  }
}

void Player::render() {
  // Cache manager references for better performance
  TextureManager &texMgr = TextureManager::Instance();
  SDL_Renderer *renderer = GameEngine::Instance().getRenderer();

  // Calculate centered position for rendering (IDENTICAL to NPCs)
  int renderX = static_cast<int>(m_position.getX() - (m_frameWidth / 2.0f));
  int renderY = static_cast<int>(m_position.getY() - (m_height / 2.0f));

  // Render the Player with the current animation frame (IDENTICAL to NPCs)
  texMgr.drawFrame(m_textureID,
                   renderX,        // Center horizontally
                   renderY,        // Center vertically
                   m_frameWidth,   // Use the calculated frame width
                   m_height,       // Height stays the same
                   m_currentRow,   // Current animation row
                   m_currentFrame, // Current animation frame
                   renderer, m_flip);
}

void Player::clean() {
  // Clean up any resources
  PLAYER_DEBUG("Cleaning up player resources");

  // Clean up inventory
  if (m_inventory) {
    m_inventory->clearInventory();
  }

  // Clear equipped items
  m_equippedItems.clear();
}

void Player::initializeInventory() {
  // Create inventory with 50 slots (can be made configurable)
  m_inventory = std::make_unique<InventoryComponent>(this, 50);

  // Set up resource change callback
  m_inventory->setResourceChangeCallback(
      [this](HammerEngine::ResourceHandle resourceHandle, int oldQuantity,
             int newQuantity) {
        onResourceChanged(resourceHandle, oldQuantity, newQuantity);
      });

  // Initialize equipment slots
  m_equippedItems["weapon"] = "";
  m_equippedItems["helmet"] = "";
  m_equippedItems["chest"] = "";
  m_equippedItems["legs"] = "";
  m_equippedItems["boots"] = "";
  m_equippedItems["gloves"] = "";
  m_equippedItems["ring"] = "";
  m_equippedItems["necklace"] = "";

  // Give player some starting resources (only use resources that exist)
  addResource("gold", 100);
  addResource("health_potion", 3);
  // Note: mana_potion doesn't exist in default resources

  PLAYER_DEBUG("Player inventory initialized with " +
               std::to_string(m_inventory->getMaxSlots()) + " slots");
}

void Player::onResourceChanged(HammerEngine::ResourceHandle resourceHandle,
                               int oldQuantity, int newQuantity) {
  // Get resource name for the event
  auto resourceTemplate =
      ResourceTemplateManager::Instance().getResourceTemplate(resourceHandle);
  std::string resourceId =
      resourceTemplate ? resourceTemplate->getName() : "Unknown Resource";

  // Create and fire resource change event
  auto resourceEvent = std::make_shared<ResourceChangeEvent>(
      shared_this(), resourceId, oldQuantity, newQuantity, "player_action");

  // Send event to EventManager if available
  // Note: We don't directly access EventManager here to avoid tight coupling
  // Instead, the event system should be notified through the inventory callback

  PLAYER_DEBUG("Resource changed: " + resourceId + " from " +
               std::to_string(oldQuantity) + " to " +
               std::to_string(newQuantity));
}

// Resource management methods
bool Player::addResource(const std::string &resourceId, int quantity) {
  if (!m_inventory) {
    PLAYER_ERROR("Player::addResource - Inventory not initialized");
    return false;
  }

  return m_inventory->addResource(resourceId, quantity);
}

bool Player::removeResource(const std::string &resourceId, int quantity) {
  if (!m_inventory) {
    PLAYER_ERROR("Player::removeResource - Inventory not initialized");
    return false;
  }

  return m_inventory->removeResource(resourceId, quantity);
}

bool Player::hasResource(const std::string &resourceId,
                         int minimumQuantity) const {
  if (!m_inventory) {
    return false;
  }

  return m_inventory->hasResource(resourceId, minimumQuantity);
}

int Player::getResourceQuantity(const std::string &resourceId) const {
  if (!m_inventory) {
    return 0;
  }

  return m_inventory->getResourceQuantity(resourceId);
}

// Equipment management
bool Player::equipItem(const std::string &itemId) {
  if (!m_inventory || !hasResource(itemId, 1)) {
    PLAYER_WARN("Player::equipItem - Item not available: " + itemId);
    return false;
  }

  // Find the resource handle and get the template
  auto handle = m_inventory->getResourceHandle(itemId);
  if (!handle.isValid()) {
    PLAYER_ERROR("Player::equipItem - Unknown item: " + itemId);
    return false;
  }

  const auto *resourceManager = &ResourceTemplateManager::Instance();
  auto itemTemplate = resourceManager->getResourceTemplate(handle);
  if (!itemTemplate) {
    PLAYER_ERROR("Player::equipItem - Cannot get template for item: " + itemId);
    return false;
  }

  // Check if it's equipment
  if (itemTemplate->getType() != ResourceType::Equipment) {
    PLAYER_WARN("Player::equipItem - Item is not equipment: " + itemId);
    return false;
  }

  // Determine equipment slot (simplified - in a real game you'd check
  // Equipment::EquipmentSlot)
  std::string slotName =
      "weapon"; // Default, should be determined from item properties

  // Unequip existing item in that slot
  if (!m_equippedItems[slotName].empty()) {
    unequipItem(slotName);
  }

  // Remove item from inventory and equip it
  if (removeResource(itemId, 1)) {
    m_equippedItems[slotName] = itemId;
    PLAYER_DEBUG("Equipped item: " + itemId + " in slot: " + slotName);
    return true;
  }

  return false;
}

bool Player::unequipItem(const std::string &slotName) {
  auto it = m_equippedItems.find(slotName);
  if (it == m_equippedItems.end() || it->second.empty()) {
    return false; // Nothing equipped in this slot
  }

  std::string itemId = it->second;

  // Try to add back to inventory
  if (addResource(itemId, 1)) {
    it->second.clear();
    PLAYER_DEBUG("Unequipped item: " + itemId + " from slot: " + slotName);
    return true;
  }

  PLAYER_WARN("Player::unequipItem - Could not add item back to inventory: " +
              itemId);
  return false;
}

std::string Player::getEquippedItem(const std::string &slotName) const {
  auto it = m_equippedItems.find(slotName);
  return (it != m_equippedItems.end()) ? it->second : "";
}

// Crafting and consumption
bool Player::canCraft(const std::string &recipeId) const {
  // Simplified crafting check - in a real game you'd have a proper recipe
  // system
  (void)recipeId; // Suppress unused parameter warning
  return false;   // Not implemented yet
}

bool Player::craftItem(const std::string &recipeId) {
  // Simplified crafting - in a real game you'd have a proper recipe system
  (void)recipeId; // Suppress unused parameter warning
  return false;   // Not implemented yet
}

bool Player::consumeItem(const std::string &itemId) {
  if (!hasResource(itemId, 1)) {
    return false;
  }

  // Get item template to check if it's consumable
  auto handle = m_inventory->getResourceHandle(itemId);
  if (!handle.isValid()) {
    PLAYER_WARN("Player::consumeItem - Unknown item: " + itemId);
    return false;
  }

  const auto *resourceManager = &ResourceTemplateManager::Instance();
  auto itemTemplate = resourceManager->getResourceTemplate(handle);
  if (!itemTemplate || !itemTemplate->isConsumable()) {
    PLAYER_WARN("Player::consumeItem - Item is not consumable: " + itemId);
    return false;
  }

  // Remove the item and apply its effects
  if (removeResource(itemId, 1)) {
    PLAYER_DEBUG("Consumed item: " + itemId);
    // Here you would apply the item's effects (healing, buffs, etc.)
    return true;
  }

  return false;
}