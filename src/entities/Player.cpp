/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/Player.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "entities/playerStates/PlayerAttackingState.hpp"
#include "entities/playerStates/PlayerDyingState.hpp"
#include "entities/playerStates/PlayerHurtState.hpp"
#include "entities/playerStates/PlayerIdleState.hpp"
#include "entities/playerStates/PlayerRunningState.hpp"

#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/TextureManager.hpp"
#include "managers/WorldManager.hpp"

#include <algorithm>
#include <cmath>
#include <format>

Player::Player() : Entity() {
  // Register with EntityDataManager FIRST - data must exist before any state
  // setup This establishes the single source of truth for all entity data
  auto &edm = EntityDataManager::Instance();
  if (edm.isInitialized()) {
    // Use default half-sizes, will be updated in ensurePhysicsBodyRegistered
    EntityHandle handle =
        edm.registerPlayer(getID(), m_initialPosition, 16.0f, 16.0f);
    setHandle(handle);
  }

  m_textureID =
      "player"; // Texture ID as loaded by TextureManager from res/img directory

  // Animation properties
  m_currentFrame = 1;    // Start with first frame
  m_currentRow = 1;      // In TextureManager::drawFrame, rows start at 1
  m_numFrames = 2;       // Number of frames in the animation
  m_animSpeed = 100;     // Animation speed in milliseconds
  m_spriteSheetRows = 1; // Number of rows in the sprite sheet
  m_animationAccumulator = 0.0f; // deltaTime accumulator for animation timing
  m_flip = SDL_FLIP_NONE;        // Default flip direction

  // Set width and height based on texture dimensions if the texture is loaded
  loadDimensionsFromTexture();

  // Initialize animation system
  initializeAnimationMap();

  // Setup state manager and add states
  setupStates();

  // Setup inventory system - NOTE: Do NOT call setupInventory() here
  // because it can trigger shared_this() during construction.
  // Call setupInventory() after construction completes.
  // Set default state (now safe - EntityDataManager handle is valid)
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
        PLAYER_DEBUG(
            std::format("Original texture dimensions: {}x{}", width, height));

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
        CollisionManager::Instance().updateCollisionBodySize(getID(),
                                                             newHalfSize);

        PLAYER_DEBUG(
            std::format("Loaded texture dimensions: {}x{}", m_width, height));
        PLAYER_DEBUG(
            std::format("Frame dimensions: {}x{}", m_frameWidth, frameHeight));
        PLAYER_DEBUG(std::format("Sprite layout: {} columns x {} rows",
                                 m_numFrames, m_spriteSheetRows));
      } else {
        PLAYER_ERROR(std::format("Failed to query texture dimensions: {}",
                                 SDL_GetError()));
      }
    }
  } else {
    PLAYER_ERROR(
        std::format("Texture '{}' not found in TextureManager", m_textureID));
  }
}

void Player::setupStates() {
  // Create and add states
  m_stateManager.addState("idle", std::make_unique<PlayerIdleState>(*this));
  m_stateManager.addState("running",
                          std::make_unique<PlayerRunningState>(*this));
  m_stateManager.addState("attacking",
                          std::make_unique<PlayerAttackingState>(*this));
  m_stateManager.addState("hurt", std::make_unique<PlayerHurtState>(*this));
  m_stateManager.addState("dying", std::make_unique<PlayerDyingState>(*this));
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
    PLAYER_ERROR(std::format("Player state not found: {}", stateName));
  }
}

std::string Player::getCurrentStateName() const {
  return m_stateManager.getCurrentStateName();
}

void Player::update(float deltaTime) {
  // Store position for render interpolation (must be first!)
  storePositionForInterpolation();

  // State machine handles input and sets velocity
  m_stateManager.update(deltaTime);

  // MOVEMENT INTEGRATION: Apply velocity to position (same as AIManager does
  // for NPCs) This is the core physics step that makes the player move Use
  // getPosition()/getVelocity() to read from EntityDataManager (single source
  // of truth)
  Vector2D currentVel = getVelocity();
  Vector2D newPos = getPosition() + (currentVel * deltaTime);

  // WORLD BOUNDS CONSTRAINT: Clamp player position to stay within world
  // boundaries PERFORMANCE: Use cached bounds instead of calling
  // WorldManager::Instance() every frame Auto-invalidate cache when world
  // version changes (new world loaded/generated)
  const uint64_t currentWorldVersion =
      WorldManager::Instance().getWorldVersion();
  if (!m_worldBoundsCached || m_cachedWorldVersion != currentWorldVersion) {
    refreshWorldBoundsCache();
  }

  // Always clamp if bounds are valid (maxX > minX)
  if (m_cachedWorldMaxX > m_cachedWorldMinX &&
      m_cachedWorldMaxY > m_cachedWorldMinY) {
    // Account for player half-size to prevent center from going out of bounds
    const float halfWidth = m_frameWidth * 0.5f;
    const float halfHeight = m_height * 0.5f;

    // Store original position before clamping
    const float originalX = newPos.getX();
    const float originalY = newPos.getY();

    // Clamp position to world bounds (with player size offset)
    const float clampedX = std::clamp(originalX, m_cachedWorldMinX + halfWidth,
                                      m_cachedWorldMaxX - halfWidth);
    const float clampedY = std::clamp(originalY, m_cachedWorldMinY + halfHeight,
                                      m_cachedWorldMaxY - halfHeight);

    // Update position and stop velocity if we hit a boundary
    if (clampedX != originalX) {
      newPos.setX(clampedX);
      currentVel.setX(0.0f); // Stop horizontal movement at edge
    }
    if (clampedY != originalY) {
      newPos.setY(clampedY);
      currentVel.setY(0.0f); // Stop vertical movement at edge
    }

    // Write velocity back if it was modified by boundary collision
    if (clampedX != originalX || clampedY != originalY) {
      setVelocity(currentVel);
    }
  }

  // Write position to EntityDataManager (single source of truth)
  setPosition(newPos);

  // Update collision body with new position and velocity
  auto &cm = CollisionManager::Instance();
  cm.updateCollisionBodyPosition(m_id, newPos);
  cm.updateCollisionBodyVelocity(m_id, currentVel);

  // If the texture dimensions haven't been loaded yet, try loading them
  if (m_frameWidth == 0 &&
      TextureManager::Instance().isTextureInMap(m_textureID)) {
    loadDimensionsFromTexture();
  }
}

void Player::render(SDL_Renderer *renderer, float cameraX, float cameraY,
                    float interpolationAlpha) {
  // Get interpolated position for smooth rendering between fixed timestep
  // updates
  Vector2D interpPos = getInterpolatedPosition(interpolationAlpha);
  renderAtPosition(renderer, interpPos, cameraX, cameraY);
}

void Player::renderAtPosition(SDL_Renderer *renderer, const Vector2D &interpPos,
                              float cameraX, float cameraY) {
  // Cache texture on first render (like WorldManager pattern - no hash lookup
  // per frame)
  if (!m_cachedTexture) {
    m_cachedTexture = TextureManager::Instance().getTexturePtr(m_textureID);
    if (!m_cachedTexture)
      return;
  }

  // Convert world coords to screen coords using passed camera offset
  // Pixel snap to prevent shimmer during diagonal camera movement
  float renderX = std::floor(interpPos.getX() - cameraX - (m_frameWidth / 2.0f));
  float renderY = std::floor(interpPos.getY() - cameraY - (m_height / 2.0f));

  // Direct SDL call with cached texture - no hash lookup!
  SDL_FRect srcRect = {static_cast<float>(m_frameWidth * m_currentFrame),
                       static_cast<float>(m_height * (m_currentRow - 1)),
                       static_cast<float>(m_frameWidth),
                       static_cast<float>(m_height)};
  SDL_FRect destRect = {renderX, renderY, static_cast<float>(m_frameWidth),
                        static_cast<float>(m_height)};
  SDL_FPoint center = {m_frameWidth / 2.0f, m_height / 2.0f};

  SDL_RenderTextureRotated(renderer, m_cachedTexture, &srcRect, &destRect, 0.0,
                           &center, m_flip);
}

void Player::clean() {
  // Clean up any resources
  PLAYER_DEBUG("Cleaning up player resources");

  // Destroy EDM inventory
  auto &edm = EntityDataManager::Instance();
  if (edm.isInitialized() && m_inventoryIndex != INVALID_INVENTORY_INDEX) {
    edm.destroyInventory(m_inventoryIndex);
    m_inventoryIndex = INVALID_INVENTORY_INDEX;
  }

  // Clear equipped items
  m_equippedItems.clear();

  // Unregister from EntityDataManager
  if (edm.isInitialized()) {
    edm.unregisterEntity(getID());
  }
}

void Player::ensurePhysicsBodyRegistered() {
  // EDM-CENTRIC: Set collision layers directly in EDM
  // Movables are managed entirely by EDM - no CollisionManager storage entry
  // needed
  if (!hasValidHandle())
    return;

  auto &edm = EntityDataManager::Instance();
  size_t edmIdx = edm.getIndex(getHandle());
  if (edmIdx == SIZE_MAX)
    return;

  auto &hot = edm.getHotDataByIndex(edmIdx);

  // Player collides with everything except pets (pets pass through player)
  // Layer_Player is already set in registerPlayer(), just set mask
  hot.collisionMask = 0xFFFF & ~HammerEngine::CollisionLayer::Layer_Pet;
  hot.setCollisionEnabled(true);
}

void Player::setVelocity(const Vector2D &velocity) {
  // Update EntityDataManager (single source of truth) via base class
  // EDM-CENTRIC: No CollisionManager entry for movables
  Entity::setVelocity(velocity);
}

void Player::setPosition(const Vector2D &position) {
  // Update EntityDataManager (single source of truth) via base class
  // EDM-CENTRIC: No CollisionManager entry for movables
  Entity::setPosition(position);
}

void Player::initializeInventory() {
  // Create EDM inventory with 50 slots
  auto &edm = EntityDataManager::Instance();
  m_inventoryIndex = edm.createInventory(50, true);  // 50 slots, world-tracked

  if (m_inventoryIndex == INVALID_INVENTORY_INDEX) {
    PLAYER_ERROR("Failed to create player inventory");
    return;
  }

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
  if (goldResource) {
    edm.addToInventory(m_inventoryIndex, goldResource->getHandle(), 100);
  }

  auto healthPotionResource = templateManager.getResourceByName("health_potion");
  if (healthPotionResource) {
    edm.addToInventory(m_inventoryIndex, healthPotionResource->getHandle(), 3);
  }

  PLAYER_DEBUG(std::format("Player EDM inventory initialized with index {}", m_inventoryIndex));
}

void Player::onResourceChanged(HammerEngine::ResourceHandle resourceHandle,
                               int oldQuantity, int newQuantity) {
  [[maybe_unused]] const std::string resourceId = resourceHandle.toString();
  // Use EventManager hub to trigger a ResourceChange (no registration needed)
  EventManager::Instance().triggerResourceChange(
      getHandle(), resourceHandle, oldQuantity, newQuantity, "player_action",
      EventManager::DispatchMode::Deferred);

  PLAYER_DEBUG(std::format(
      "Resource changed: {} from {} to {} - event dispatched to EventManager",
      resourceId, oldQuantity, newQuantity));
}

// Resource management - delegates to EntityDataManager
bool Player::addToInventory(HammerEngine::ResourceHandle handle, int quantity) {
  if (m_inventoryIndex == INVALID_INVENTORY_INDEX) {
    PLAYER_WARN("Player::addToInventory - Inventory not initialized");
    return false;
  }
  int oldQty = EntityDataManager::Instance().getInventoryQuantity(m_inventoryIndex, handle);
  bool result = EntityDataManager::Instance().addToInventory(m_inventoryIndex, handle, quantity);
  if (result) {
    int newQty = EntityDataManager::Instance().getInventoryQuantity(m_inventoryIndex, handle);
    onResourceChanged(handle, oldQty, newQty);
  }
  return result;
}

bool Player::removeFromInventory(HammerEngine::ResourceHandle handle, int quantity) {
  if (m_inventoryIndex == INVALID_INVENTORY_INDEX) {
    PLAYER_WARN("Player::removeFromInventory - Inventory not initialized");
    return false;
  }
  int oldQty = EntityDataManager::Instance().getInventoryQuantity(m_inventoryIndex, handle);
  bool result = EntityDataManager::Instance().removeFromInventory(m_inventoryIndex, handle, quantity);
  if (result) {
    int newQty = EntityDataManager::Instance().getInventoryQuantity(m_inventoryIndex, handle);
    onResourceChanged(handle, oldQty, newQty);
  }
  return result;
}

int Player::getInventoryQuantity(HammerEngine::ResourceHandle handle) const {
  if (m_inventoryIndex == INVALID_INVENTORY_INDEX) {
    return 0;
  }
  return EntityDataManager::Instance().getInventoryQuantity(m_inventoryIndex, handle);
}

bool Player::hasInInventory(HammerEngine::ResourceHandle handle, int quantity) const {
  if (m_inventoryIndex == INVALID_INVENTORY_INDEX) {
    return false;
  }
  return EntityDataManager::Instance().hasInInventory(m_inventoryIndex, handle, quantity);
}

// Equipment management
bool Player::equipItem(HammerEngine::ResourceHandle itemHandle) {
  if (m_inventoryIndex == INVALID_INVENTORY_INDEX) {
    PLAYER_WARN("Player::equipItem - Inventory not initialized");
    return false;
  }

  if (!itemHandle.isValid()) {
    PLAYER_ERROR("Player::equipItem - Invalid resource handle");
    return false;
  }

  auto &edm = EntityDataManager::Instance();
  if (!edm.hasInInventory(m_inventoryIndex, itemHandle, 1)) {
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
  if (edm.removeFromInventory(m_inventoryIndex, itemHandle, 1)) {
    m_equippedItems[slotName] = itemHandle;
    PLAYER_DEBUG(std::format("Equipped item (handle: {}) in slot: {}",
                             itemHandle.toString(), slotName));
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
    PLAYER_WARN(std::format(
        "Player::unequipItem - No item equipped in slot: {}", slotName));
    return false; // Nothing equipped in this slot
  }

  HammerEngine::ResourceHandle itemHandle = it->second;

  // Try to add back to inventory
  auto &edm = EntityDataManager::Instance();
  if (edm.addToInventory(m_inventoryIndex, itemHandle, 1)) {
    it->second = HammerEngine::ResourceHandle{}; // Set to invalid handle
    PLAYER_DEBUG(std::format("Unequipped item (handle: {}) from slot: {}",
                             itemHandle.toString(), slotName));
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
  if (m_inventoryIndex == INVALID_INVENTORY_INDEX) {
    PLAYER_WARN("Player::consumeItem - Inventory not initialized");
    return false;
  }

  if (!itemHandle.isValid()) {
    PLAYER_ERROR("Player::consumeItem - Invalid resource handle");
    return false;
  }

  auto &edm = EntityDataManager::Instance();
  if (!edm.hasInInventory(m_inventoryIndex, itemHandle, 1)) {
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
  if (edm.removeFromInventory(m_inventoryIndex, itemHandle, 1)) {
    PLAYER_DEBUG(
        std::format("Consumed item (handle: {})", itemHandle.toString()));
    // Here you would apply the item's effects (healing, buffs, etc.)
    return true;
  }

  return false;
}

void Player::refreshWorldBoundsCache() {
  const auto &worldMgr = WorldManager::Instance();
  worldMgr.getWorldBounds(m_cachedWorldMinX, m_cachedWorldMinY,
                          m_cachedWorldMaxX, m_cachedWorldMaxY);
  m_cachedWorldVersion = worldMgr.getWorldVersion();
  m_worldBoundsCached = true;
  PLAYER_DEBUG(
      std::format("World bounds cached: ({}, {}) to ({}, {}), version: {}",
                  m_cachedWorldMinX, m_cachedWorldMinY, m_cachedWorldMaxX,
                  m_cachedWorldMaxY, m_cachedWorldVersion));
}

// Animation abstraction methods

void Player::initializeAnimationMap() {
  // Default animation configuration mapping names to sprite sheet details
  // Current player sprite sheet: 1 row, 2 frames (shared for idle/running)
  // Can be extended when player sprite sheet is expanded with more rows
  m_animationMap = {
      {"idle", {0, 2, 150, true}}, // Row 0, 2 frames, 150ms, looping
      {"running",
       {0, 2, 100, true}}, // Row 0, 2 frames, 100ms, looping (same row as idle)
      {"attacking",
       {0, 2, 80, false}}, // Row 0, 2 frames, 80ms, play once (placeholder)
      {"hurt",
       {0, 2, 100, false}}, // Row 0, 2 frames, 100ms, play once (placeholder)
      {"dying",
       {0, 2, 120, false}} // Row 0, 2 frames, 120ms, play once (placeholder)
  };
}

void Player::playAnimation(const std::string &animName) {
  // Skip if already playing this animation - prevents jitter on state re-entry
  if (m_currentAnimationName == animName) {
    return;
  }

  auto it = m_animationMap.find(animName);
  if (it != m_animationMap.end()) {
    const auto &config = it->second;
    m_currentRow = config.row + 1; // TextureManager uses 1-based rows
    m_numFrames = config.frameCount;
    m_animSpeed = config.speed;
    m_animationLoops = config.loop;
    m_currentFrame = 0;
    m_animationAccumulator = 0.0f;     // Reset deltaTime accumulator
    m_currentAnimationName = animName; // Track current animation
  } else {
    PLAYER_WARN(std::format("Player animation not found: {}", animName));
  }
}

// Combat system methods - all stats stored in EntityDataManager::CharacterData

float Player::getHealth() const {
  return EntityDataManager::Instance().getCharacterData(m_handle).health;
}

float Player::getMaxHealth() const {
  return EntityDataManager::Instance().getCharacterData(m_handle).maxHealth;
}

float Player::getStamina() const {
  return EntityDataManager::Instance().getCharacterData(m_handle).stamina;
}

float Player::getMaxStamina() const {
  return EntityDataManager::Instance().getCharacterData(m_handle).maxStamina;
}

float Player::getAttackDamage() const {
  return EntityDataManager::Instance().getCharacterData(m_handle).attackDamage;
}

float Player::getAttackRange() const {
  return EntityDataManager::Instance().getCharacterData(m_handle).attackRange;
}

bool Player::isAlive() const {
  return EntityDataManager::Instance().getCharacterData(m_handle).health > 0.0f;
}

bool Player::canAttack(float staminaCost) const {
  return EntityDataManager::Instance().getCharacterData(m_handle).stamina >=
         staminaCost;
}

void Player::takeDamage(float damage, const Vector2D &knockback) {
  auto &charData = EntityDataManager::Instance().getCharacterData(m_handle);

  if (charData.health <= 0.0f) {
    return; // Already dead
  }

  charData.health = std::max(0.0f, charData.health - damage);

  // Apply knockback via transform
  if (knockback.length() > 0.001f) {
    auto &transform = EntityDataManager::Instance().getTransform(m_handle);
    transform.position = transform.position + knockback;
  }

  // Handle death or hurt
  if (charData.health <= 0.0f) {
    die();
  } else {
    changeState("hurt");
  }

  PLAYER_DEBUG(std::format("Player took {} damage, health: {}/{}", damage,
                           charData.health, charData.maxHealth));
}

void Player::heal(float amount) {
  auto &charData = EntityDataManager::Instance().getCharacterData(m_handle);

  if (charData.health <= 0.0f) {
    return; // Can't heal dead player
  }

  float oldHealth = charData.health;
  charData.health = std::min(charData.maxHealth, charData.health + amount);

  PLAYER_DEBUG(std::format("Player healed {} HP: {}/{} -> {}/{}", amount,
                           oldHealth, charData.maxHealth, charData.health,
                           charData.maxHealth));
}

void Player::die() {
  Vector2D pos = getPosition();
  PLAYER_INFO(
      std::format("Player died at position ({}, {})", pos.getX(), pos.getY()));

  changeState("dying");

  // Note: Game over / respawn logic would be handled by GamePlayState or
  // similar
}

void Player::setMaxHealth(float maxHealth) {
  auto &charData = EntityDataManager::Instance().getCharacterData(m_handle);
  charData.maxHealth = maxHealth;
  charData.health = std::min(charData.health, charData.maxHealth);
}

void Player::setMaxStamina(float maxStamina) {
  auto &charData = EntityDataManager::Instance().getCharacterData(m_handle);
  charData.maxStamina = maxStamina;
  charData.stamina = std::min(charData.stamina, charData.maxStamina);
}

void Player::consumeStamina(float amount) {
  auto &charData = EntityDataManager::Instance().getCharacterData(m_handle);
  charData.stamina = std::max(0.0f, charData.stamina - amount);
}

void Player::restoreStamina(float amount) {
  auto &charData = EntityDataManager::Instance().getCharacterData(m_handle);
  charData.stamina = std::min(charData.maxStamina, charData.stamina + amount);
}
