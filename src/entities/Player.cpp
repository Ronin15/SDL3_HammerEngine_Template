/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/Player.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "entities/playerStates/PlayerIdleState.hpp"
#include "entities/playerStates/PlayerRunningState.hpp"

#include "managers/EventManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/TextureManager.hpp"
#include "managers/WorldManager.hpp"
#include "utils/Camera.hpp"

#include <algorithm>

Player::Player() : Entity() {
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

  // PLAYER_DEBUG("Player created");
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

        // Sync new dimensions to collision body if already registered
        Vector2D newHalfSize(m_frameWidth * 0.5f, m_height * 0.5f);
        CollisionManager::Instance().updateCollisionBodySizeSOA(getID(), newHalfSize);

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
  // State machine handles input and sets velocity
  m_stateManager.update(deltaTime);

  // Sync velocity to collision body - let collision system handle movement integration
  // This prevents micro-bouncing from double integration (entity + physics)
  auto &cm = CollisionManager::Instance();
  cm.updateCollisionBodyVelocitySOA(m_id, m_velocity);

  // If the texture dimensions haven't been loaded yet, try loading them
  if (m_frameWidth == 0 &&
      TextureManager::Instance().isTextureInMap(m_textureID)) {
    loadDimensionsFromTexture();
  }
}

void Player::render(const HammerEngine::Camera* camera) {
  // Cache manager references for better performance
  TextureManager &texMgr = TextureManager::Instance();
  SDL_Renderer *renderer = GameEngine::Instance().getRenderer();

  // Determine render position based on camera
  Vector2D renderPosition;
  if (camera) {
    // Transform world position to screen coordinates using camera
    renderPosition = camera->worldToScreen(m_position);
  } else {
    // No camera transformation - render at world coordinates directly
    renderPosition = m_position;
  }

  // Calculate centered position for rendering (preserve float precision)
  float renderX = renderPosition.getX() - (m_frameWidth / 2.0f);
  float renderY = renderPosition.getY() - (m_height / 2.0f);

  // Render the Player with the current animation frame using float precision
  texMgr.drawFrameF(m_textureID,
                    renderX,        // Keep float precision for smooth camera movement
                    renderY,        // Keep float precision for smooth camera movement
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

  // Remove collision body
  CollisionManager::Instance().removeCollisionBodySOA(getID());
}

void Player::ensurePhysicsBodyRegistered() {
  // Register a dynamic body for the player and attach this entity
  auto &cm = CollisionManager::Instance();
  const float halfW = m_frameWidth > 0 ? m_frameWidth * 0.5f : 16.0f;
  const float halfH = m_height > 0 ? m_height * 0.5f : 16.0f;
  HammerEngine::AABB aabb(m_position.getX(), m_position.getY(), halfW, halfH);

  // Use new SOA-based collision system
  cm.addCollisionBodySOA(getID(), aabb.center, aabb.halfSize, HammerEngine::BodyType::DYNAMIC,
                        HammerEngine::CollisionLayer::Layer_Player, 0xFFFFFFFFu);
  // Process queued command to ensure body exists before attaching
  cm.processPendingCommands();
  // Attach entity reference to SOA storage
  cm.attachEntity(getID(), shared_this());
}

void Player::setVelocity(const Vector2D& velocity) {
  m_velocity = velocity;
  auto &cm = CollisionManager::Instance();
  cm.updateCollisionBodyVelocitySOA(getID(), velocity);
}

void Player::setPosition(const Vector2D& position) {
  m_position = position;
  auto &cm = CollisionManager::Instance();
  cm.updateCollisionBodyPositionSOA(getID(), position);
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

  // Initialize equipment slots with invalid handles
  m_equippedItems["weapon"] = HammerEngine::ResourceHandle{};
  m_equippedItems["helmet"] = HammerEngine::ResourceHandle{};
  m_equippedItems["chest"] = HammerEngine::ResourceHandle{};
  m_equippedItems["legs"] = HammerEngine::ResourceHandle{};
  m_equippedItems["boots"] = HammerEngine::ResourceHandle{};
  m_equippedItems["gloves"] = HammerEngine::ResourceHandle{};
  m_equippedItems["ring"] = HammerEngine::ResourceHandle{};
  m_equippedItems["necklace"] = HammerEngine::ResourceHandle{};

  // Give player some starting resources using ResourceTemplateManager
  const auto &templateManager = ResourceTemplateManager::Instance();

  auto goldResource = templateManager.getResourceByName("gold");
  if (goldResource && m_inventory) {
    m_inventory->addResource(goldResource->getHandle(), 100);
  }

  auto healthPotionResource =
      templateManager.getResourceByName("health_potion");
  if (healthPotionResource && m_inventory) {
    m_inventory->addResource(healthPotionResource->getHandle(), 3);
  }
  // Note: mana_potion doesn't exist in default resources

  if (m_inventory) {
    PLAYER_DEBUG("Player inventory initialized with " +
                 std::to_string(m_inventory->getMaxSlots()) + " slots");
  }
}

void Player::onResourceChanged(HammerEngine::ResourceHandle resourceHandle,
                               int oldQuantity, int newQuantity) {
  [[maybe_unused]] const std::string resourceId = resourceHandle.toString();
  // Use EventManager hub to trigger a ResourceChange (no registration needed)
  EventManager::Instance().triggerResourceChange(
      shared_this(), resourceHandle, oldQuantity, newQuantity, "player_action",
      EventManager::DispatchMode::Deferred);

  PLAYER_DEBUG("Resource changed: " + resourceId + " from " +
               std::to_string(oldQuantity) + " to " +
               std::to_string(newQuantity) +
               " - event dispatched to EventManager");
}

// Resource management methods - removed, use getInventory() directly with
// ResourceHandle

// Equipment management
bool Player::equipItem(HammerEngine::ResourceHandle itemHandle) {
  if (!m_inventory) {
    PLAYER_WARN("Player::equipItem - Inventory not initialized");
    return false;
  }

  if (!itemHandle.isValid()) {
    PLAYER_ERROR("Player::equipItem - Invalid resource handle");
    return false;
  }

  if (!m_inventory->hasResource(itemHandle, 1)) {
    PLAYER_WARN("Player::equipItem - Item not available (handle: " +
                itemHandle.toString() + ")");
    return false;
  }

  const auto &templateManager = ResourceTemplateManager::Instance();
  auto itemTemplate = templateManager.getResourceTemplate(itemHandle);
  if (!itemTemplate) {
    PLAYER_ERROR("Player::equipItem - Cannot get template for item handle: " +
                 itemHandle.toString());
    return false;
  }

  // Check if it's equipment
  if (itemTemplate->getType() != ResourceType::Equipment) {
    PLAYER_WARN("Player::equipItem - Item is not equipment (handle: " +
                itemHandle.toString() + ")");
    return false;
  }

  // Determine equipment slot (simplified - in a real game you'd check
  // Equipment::EquipmentSlot)
  std::string slotName =
      "weapon"; // Default, should be determined from item properties

  // Unequip existing item in that slot
  if (m_equippedItems[slotName].isValid()) {
    unequipItem(slotName);
  }

  // Remove item from inventory and equip it
  if (m_inventory->removeResource(itemHandle, 1)) {
    m_equippedItems[slotName] = itemHandle;
    PLAYER_DEBUG("Equipped item (handle: " + itemHandle.toString() +
                 ") in slot: " + slotName);
    return true;
  }

  return false;
}

bool Player::unequipItem(const std::string &slotName) {
  if (slotName.empty()) {
    PLAYER_ERROR("Player::unequipItem - Slot name cannot be empty");
    return false;
  }

  auto it = m_equippedItems.find(slotName);
  if (it == m_equippedItems.end() || !it->second.isValid()) {
    PLAYER_WARN("Player::unequipItem - No item equipped in slot: " + slotName);
    return false; // Nothing equipped in this slot
  }

  HammerEngine::ResourceHandle itemHandle = it->second;

  // Try to add back to inventory
  if (m_inventory->addResource(itemHandle, 1)) {
    it->second = HammerEngine::ResourceHandle{}; // Set to invalid handle
    PLAYER_DEBUG("Unequipped item (handle: " + itemHandle.toString() +
                 ") from slot: " + slotName);
    return true;
  }

  PLAYER_WARN(
      "Player::unequipItem - Could not add item back to inventory (handle: " +
      itemHandle.toString() + ")");
  return false;
}

HammerEngine::ResourceHandle
Player::getEquippedItem(const std::string &slotName) const {
  if (slotName.empty()) {
    PLAYER_ERROR("Player::getEquippedItem - Slot name cannot be empty");
    return HammerEngine::ResourceHandle{};
  }

  auto it = m_equippedItems.find(slotName);
  return (it != m_equippedItems.end()) ? it->second
                                       : HammerEngine::ResourceHandle{};
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

bool Player::consumeItem(HammerEngine::ResourceHandle itemHandle) {
  if (!m_inventory) {
    PLAYER_WARN("Player::consumeItem - Inventory not initialized");
    return false;
  }

  if (!itemHandle.isValid()) {
    PLAYER_ERROR("Player::consumeItem - Invalid resource handle");
    return false;
  }

  if (!m_inventory->hasResource(itemHandle, 1)) {
    PLAYER_WARN("Player::consumeItem - Item not available (handle: " +
                itemHandle.toString() + ")");
    return false;
  }

  const auto &templateManager = ResourceTemplateManager::Instance();
  auto itemTemplate = templateManager.getResourceTemplate(itemHandle);
  if (!itemTemplate || !itemTemplate->isConsumable()) {
    PLAYER_WARN("Player::consumeItem - Item is not consumable (handle: " +
                itemHandle.toString() + ")");
    return false;
  }

  // Remove the item and apply its effects
  if (m_inventory->removeResource(itemHandle, 1)) {
    PLAYER_DEBUG("Consumed item (handle: " + itemHandle.toString() + ")");
    // Here you would apply the item's effects (healing, buffs, etc.)
    return true;
  }

  return false;
}
