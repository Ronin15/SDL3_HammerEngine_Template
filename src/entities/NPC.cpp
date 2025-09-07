/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/NPC.hpp"

#include "core/GameEngine.hpp"
#include "core/Logger.hpp"

#include "managers/CollisionManager.hpp"
#include "managers/WorldManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/TextureManager.hpp"
#include "utils/Camera.hpp"

#include <random>
#include <set>

NPC::NPC(const std::string &textureID, const Vector2D &startPosition,
         int frameWidth, int frameHeight)
    : Entity(), m_frameWidth(frameWidth), m_frameHeight(frameHeight) {
  // Initialize entity properties
  m_position = startPosition;
  m_velocity = Vector2D(0, 0);
  m_acceleration = Vector2D(0, 0);
  m_textureID = textureID;

  // Animation properties
  m_currentFrame = 1;    // Start with first frame
  m_currentRow = 1;      // In TextureManager::drawFrame, rows start at 1
  m_numFrames = 2;       // Default to 2 frames for simple animation
  m_animSpeed = 100;     // Default animation speed in milliseconds
  m_spriteSheetRows = 1; // Default number of rows in the sprite sheet
  m_lastFrameTime =
      SDL_GetTicks();     // Track when we last changed animation frame
  m_flip = SDL_FLIP_NONE; // Default flip direction

  // Load dimensions from texture if not provided
  if (m_frameWidth <= 0 || m_frameHeight <= 0) {
    loadDimensionsFromTexture();
  } else {
    m_width = m_frameWidth;
    m_height = m_frameHeight;
  }

  // Set default wander area to world bounds (can be changed later via setWanderArea)
  float worldMinX, worldMinY, worldMaxX, worldMaxY;
  if (WorldManager::Instance().getWorldBounds(worldMinX, worldMinY, worldMaxX, worldMaxY)) {
    // WorldManager returns bounds in PIXELS; apply directly
    m_minX = worldMinX;
    m_minY = worldMinY;
    m_maxX = worldMaxX;
    m_maxY = worldMaxY;
  } else {
    // Fallback to reasonable world bounds if WorldManager not available yet
    m_minX = 0.0f;
    m_minY = 0.0f;
    m_maxX = 2048.0f;  // Larger default world area
    m_maxY = 2048.0f;
  }

  // Bounds are enforced centrally by AIManager/PathfinderManager

  // Initialize inventory system - NOTE: Do NOT call setupInventory() here
  // because it can trigger shared_this() during construction.
  // Call initializeInventory() after construction completes.
  // Collision registration happens in NPC::create() after construction
  // NPC_DEBUG("NPC created at position: " + m_position.toString());
}

NPC::~NPC() {
  // IMPORTANT: Do not call shared_from_this() or any methods that use it in a
  // destructor The AIManager unassignment should happen in clean() or
  // beforeDestruction(), not here

  // Destructor - avoid any cleanup that might cause double-free

  // Note: Entity pointers should already be unassigned from AIManager
  // in AIDemoState::exit() or via the clean() method before destruction
}

void NPC::loadDimensionsFromTexture() {
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
        // Store original dimensions for full sprite sheet
        m_width = static_cast<int>(width);
        m_height = static_cast<int>(height);

        // Calculate frame dimensions based on sprite sheet layout
        m_frameWidth = m_width / m_numFrames;           // Width per frame
        int frameHeight = m_height / m_spriteSheetRows; // Height per row

        // Update height to be the height of a single frame
        m_height = frameHeight;

        // Sync new dimensions to collision body if already registered
        CollisionManager::Instance().resizeBody(getID(), m_frameWidth * 0.5f, m_height * 0.5f);
      } else {
        NPC_ERROR("Failed to query NPC texture dimensions: " +
                  std::string(SDL_GetError()));
      }
    }
  } else {
    NPC_ERROR("NPC texture '" + m_textureID + "' not found in TextureManager");
  }
}

// State management removed - handled by AI Manager

void NPC::update(float deltaTime) {
  // The AI drives velocity directly; apply it without additional damping.
  // Integrate intended motion
  Vector2D prevPosition = m_position;
  
  // Safety check: ensure deltaTime is reasonable
  if (deltaTime <= 0.0f || deltaTime > 0.1f) {
    deltaTime = 1.0f / 60.0f; // Fallback to 60 FPS
  }
  
  Vector2D newPosition = m_position + m_velocity * deltaTime;
  
  // Bounds handled centrally by AIManager
  setPosition(newPosition);
  // Sync velocity change if adjusted by bounce
  setVelocity(m_velocity);
  m_acceleration = Vector2D(0, 0);
  
  // Position sync is handled by setPosition() calls - no need for periodic checks
  // This prevents visual glitching from position corrections during rendering

  // Area constraints handling removed; behaviors and managers coordinate movement

  // --- Animation ---
  Uint64 currentTime = SDL_GetTicks();
  // Use actual final displacement to decide if we are moving (after all bounds checks)
  Vector2D finalPosition = m_position;
  float moveDist2 = (finalPosition - prevPosition).lengthSquared();
  
  // Diagnostic: Check for stuck entities with velocity but no movement
  float velocityMagnitude = m_velocity.length();
  if (velocityMagnitude > 1.0f && moveDist2 <= 0.01f) {
    // NPC has velocity but isn't moving - this indicates a stuck condition
    // Use instance-specific throttling instead of global static
    if (currentTime - m_lastStuckLogTime > 5000) { // Log every 5 seconds per NPC
      AI_DEBUG("NPC " + std::to_string(getID()) + " stuck: velocity=" + 
               std::to_string(velocityMagnitude) + ", movement=" + std::to_string(std::sqrt(moveDist2)) +
               ", pos=(" + std::to_string(m_position.getX()) + "," + std::to_string(m_position.getY()) + ")");
      m_lastStuckLogTime = currentTime;
    }
  }
  
  if (moveDist2 > 0.04f) { // ~> 0.2px per frame at 60Hz
    if (currentTime > m_lastFrameTime + m_animSpeed) {
      m_currentFrame = (m_currentFrame + 1) % m_numFrames;
      m_lastFrameTime = currentTime;
    }
    // Smooth flip: require sufficient lateral speed and a minimum interval
    const float flipSpeedThreshold = 15.0f; // px/s
    if (std::abs(m_velocity.getX()) > flipSpeedThreshold) {
      int newSign = (m_velocity.getX() < 0) ? -1 : 1;
      if (newSign != m_lastFlipSign) {
        if (currentTime - m_lastFlipTime >= 300) { // ms
          m_flip = (newSign < 0) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
          m_lastFlipSign = newSign;
          m_lastFlipTime = currentTime;
        }
      } else {
        m_flip = (newSign < 0) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
      }
    }
  } else {
    m_currentFrame = 0;
  }

  // If the texture dimensions haven't been loaded yet, try loading them
  if (m_frameWidth == 0 &&
      TextureManager::Instance().isTextureInMap(m_textureID)) {
    loadDimensionsFromTexture();
  }
}

void NPC::render(const HammerEngine::Camera *camera) {
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

  // Render the NPC with the current animation frame using float precision
  texMgr.drawFrameF(m_textureID,
                    renderX, // Keep float precision for smooth camera movement
                    renderY, // Keep float precision for smooth camera movement
                    m_frameWidth, m_height, m_currentRow, m_currentFrame,
                    renderer, m_flip);
}

void NPC::clean() {
  // This method is called before the object is destroyed,
  // but we need to be very careful about double-cleanup

  static std::set<void *> cleanedNPCs;

  // Check if this NPC has already been cleaned
  if (cleanedNPCs.find(this) != cleanedNPCs.end()) {
    return; // Already cleaned, avoid double-free
  }

  // Mark this NPC as cleaned
  cleanedNPCs.insert(this);

  // Note: AIManager cleanup (unregisterEntityFromUpdates,
  // unassignBehaviorFromEntity) should be handled externally before calling
  // clean() to avoid shared_from_this() issues during destruction. This method
  // now only handles internal NPC state cleanup.

  // Reset velocity and internal state
  m_velocity = Vector2D(0, 0);
  m_acceleration = Vector2D(0, 0);

  // Clean up inventory
  if (m_inventory) {
    m_inventory->clearInventory();
  }

  // Remove from collision system
  CollisionManager::Instance().removeBody(getID());
}

void NPC::setVelocity(const Vector2D& velocity) {
  m_velocity = velocity;
  auto &cm = CollisionManager::Instance();
  if (!cm.isSyncing()) {
    cm.setVelocity(getID(), velocity);
  }
}

void NPC::setPosition(const Vector2D& position) {
  m_position = position;
  auto &cm = CollisionManager::Instance();
  if (!cm.isSyncing()) {
    cm.setKinematicPose(getID(), position);
  }
}

void NPC::initializeInventory() {
  // Create inventory with 20 slots (smaller than player inventory)
  m_inventory = std::make_unique<InventoryComponent>(this, 20);

  // Set up resource change callback
  m_inventory->setResourceChangeCallback(
      [this](HammerEngine::ResourceHandle resourceHandle, int oldQuantity,
             int newQuantity) {
        onResourceChanged(resourceHandle, oldQuantity, newQuantity);
      });

  NPC_DEBUG("NPC inventory initialized with " +
            std::to_string(m_inventory->getMaxSlots()) + " slots");
}

void NPC::onResourceChanged(HammerEngine::ResourceHandle resourceHandle,
                            int oldQuantity, int newQuantity) {
  const std::string resourceId = resourceHandle.toString();
  // Use EventManager hub to trigger a ResourceChange (no registration needed)
  EventManager::Instance().triggerResourceChange(
      shared_this(), resourceHandle, oldQuantity, newQuantity, "npc_action",
      EventManager::DispatchMode::Deferred);
  NPC_DEBUG("Resource changed: " + resourceId + " from " +
            std::to_string(oldQuantity) + " to " + std::to_string(newQuantity) +
            " - change triggered via EventManager");
}

// Resource management methods - removed, use getInventory() directly with
// ResourceHandle

// Trading system - updated to use ResourceHandle (resource handle system
// compliance)
bool NPC::canTrade(HammerEngine::ResourceHandle resourceHandle,
                   int quantity) const {
  if (!m_canTrade || !m_inventory) {
    NPC_WARN(
        "NPC::canTrade - Trading not available or inventory not initialized");
    return false;
  }

  if (!resourceHandle.isValid()) {
    NPC_WARN("NPC::canTrade - Invalid resource handle");
    return false;
  }

  return m_inventory->hasResource(resourceHandle, quantity);
}

bool NPC::tradeWithPlayer(HammerEngine::ResourceHandle resourceHandle,
                          int quantity, InventoryComponent &playerInventory) {
  if (!canTrade(resourceHandle, quantity)) {
    NPC_WARN("NPC::tradeWithPlayer - Cannot trade resource (handle: " +
             resourceHandle.toString() + ")");
    return false;
  }

  if (!resourceHandle.isValid()) {
    NPC_ERROR("NPC::tradeWithPlayer - Invalid resource handle");
    return false;
  }

  if (quantity <= 0) {
    NPC_ERROR("NPC::tradeWithPlayer - Invalid quantity: " +
              std::to_string(quantity));
    return false;
  }

  // Simple trade: NPC gives item to player for free (could be enhanced with
  // currency exchange)
  if (m_inventory->transferTo(playerInventory, resourceHandle, quantity)) {
    NPC_DEBUG("NPC traded " + std::to_string(quantity) +
              " resources (handle: " + resourceHandle.toString() +
              ") to player");
    return true;
  }

  return false;
}

void NPC::initializeShopInventory() {
  m_canTrade = true;

  // Add some basic shop items using ResourceTemplateManager
  const auto &templateManager = ResourceTemplateManager::Instance();

  // Add health potions
  auto healthPotion = templateManager.getResourceByName("health_potion");
  if (healthPotion && m_inventory) {
    m_inventory->addResource(healthPotion->getHandle(), 10);
  }

  // Add mana potions
  auto manaPotion = templateManager.getResourceByName("mana_potion");
  if (manaPotion && m_inventory) {
    m_inventory->addResource(manaPotion->getHandle(), 8);
  }

  // Add iron sword
  auto ironSword = templateManager.getResourceByName("iron_sword");
  if (ironSword && m_inventory) {
    m_inventory->addResource(ironSword->getHandle(), 1);
  }

  // Add leather helmet
  auto leatherHelmet = templateManager.getResourceByName("leather_helmet");
  if (leatherHelmet && m_inventory) {
    m_inventory->addResource(leatherHelmet->getHandle(), 2);
  }

  NPC_DEBUG("NPC shop inventory initialized");
}

void NPC::initializeLootDrops() {
  m_hasLootDrops = true;

  // Set up drop rates using ResourceTemplateManager to get handles
  const auto &templateManager = ResourceTemplateManager::Instance();

  // Set up drop rates (0.0 to 1.0) using handles
  auto goldResource = templateManager.getResourceByName("gold");
  if (goldResource) {
    m_dropRates[goldResource->getHandle()] = 0.8f; // 80% chance to drop gold
  }

  auto healthPotionResource =
      templateManager.getResourceByName("health_potion");
  if (healthPotionResource) {
    m_dropRates[healthPotionResource->getHandle()] =
        0.3f; // 30% chance to drop health potion
  }

  auto ironOreResource = templateManager.getResourceByName("iron_ore");
  if (ironOreResource) {
    m_dropRates[ironOreResource->getHandle()] =
        0.2f; // 20% chance to drop iron ore
  }

  auto oakWoodResource = templateManager.getResourceByName("oak_wood");
  if (oakWoodResource) {
    m_dropRates[oakWoodResource->getHandle()] =
        0.15f; // 15% chance to drop oak wood
  }

  NPC_DEBUG("NPC loot drops initialized with " +
            std::to_string(m_dropRates.size()) + " possible drops");
}

void NPC::dropLoot() {
  if (!m_hasLootDrops) {
    return;
  }

  // Random number generator
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_real_distribution<float> dis(0.0f, 1.0f);

  NPC_DEBUG("NPC dropping loot...");

  // Check each potential drop
  for (const auto &[itemHandle, dropRate] : m_dropRates) {
    if (dis(gen) <= dropRate) {
      // Determine quantity (simple random 1-3 for most items)
      int quantity = 1;

      // Get resource template to check if it's gold
      const auto &templateManager = ResourceTemplateManager::Instance();
      auto resourceTemplate = templateManager.getResourceTemplate(itemHandle);
      if (resourceTemplate && resourceTemplate->getName() == "gold") {
        quantity = 5 + (rand() % 15); // 5-19 gold
      } else {
        quantity = 1 + (rand() % 3); // 1-3 of other items
      }

      dropSpecificItem(itemHandle, quantity);
    }
  }
}
void NPC::dropSpecificItem(HammerEngine::ResourceHandle itemHandle,
                           int quantity) {
  // In a real implementation, you would create a physical item drop in the
  // world For now, we'll just log the drop
  NPC_DEBUG("NPC dropped " + std::to_string(quantity) +
            " items (handle: " + itemHandle.toString() + ")");

  // You could add the items to a world container, create pickup entities, etc.
  // This would integrate with your game's item pickup system
}

void NPC::setLootDropRate(HammerEngine::ResourceHandle itemHandle,
                          float dropRate) {
  if (!itemHandle.isValid()) {
    NPC_ERROR("NPC::setLootDropRate - Invalid resource handle");
    return;
  }

  if (dropRate < 0.0f || dropRate > 1.0f) {
    NPC_ERROR(
        "NPC::setLootDropRate - Drop rate must be between 0.0 and 1.0, got: " +
        std::to_string(dropRate));
    return;
  }

  m_dropRates[itemHandle] = dropRate;
  m_hasLootDrops = !m_dropRates.empty();
  NPC_DEBUG("Set drop rate for item (handle: " + itemHandle.toString() +
            ") to " + std::to_string(dropRate));
}
// Animation handling removed - TextureManager handles this functionality

void NPC::setWanderArea(float minX, float minY, float maxX, float maxY) {
  m_minX = minX;
  m_minY = minY;
  m_maxX = maxX;
  m_maxY = maxY;
}

void NPC::ensurePhysicsBodyRegistered() {
  auto &cm = CollisionManager::Instance();
  const float halfW = m_frameWidth > 0 ? m_frameWidth * 0.5f : 16.0f;
  const float halfH = m_height > 0 ? m_height * 0.5f : 16.0f;
  HammerEngine::AABB aabb(m_position.getX(), m_position.getY(), halfW, halfH);
  cm.addBody(getID(), aabb, HammerEngine::BodyType::KINEMATIC);
  cm.attachEntity(getID(), shared_this());
}

void NPC::setFaction(Faction f) {
  m_faction = f;
  auto &cm = CollisionManager::Instance();
  uint32_t layer = HammerEngine::CollisionLayer::Layer_Default;
  switch (m_faction) {
  case Faction::Enemy:
    layer = HammerEngine::CollisionLayer::Layer_Enemy;
    break;
  case Faction::Friendly:
    layer = HammerEngine::CollisionLayer::Layer_Default;
    break;
  case Faction::Neutral:
    layer = HammerEngine::CollisionLayer::Layer_Default;
    break;
  }
  cm.setBodyLayer(getID(), layer, 0xFFFFFFFFu);
}
