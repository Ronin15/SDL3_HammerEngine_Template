/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/NPC.hpp"

#include "core/GameEngine.hpp"
#include "core/Logger.hpp"

// NPC Animation States
#include "entities/npcStates/NPCIdleState.hpp"
#include "entities/npcStates/NPCWalkingState.hpp"
#include "entities/npcStates/NPCAttackingState.hpp"
#include "entities/npcStates/NPCRecoveringState.hpp"
#include "entities/npcStates/NPCHurtState.hpp"
#include "entities/npcStates/NPCDyingState.hpp"

#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/TextureManager.hpp"
#include "managers/WorldManager.hpp"

#include <cmath>
#include <format>
#include <random>

NPC::NPC(const std::string &textureID, const Vector2D &startPosition,
         int frameWidth, int frameHeight, NPCType type)
    : Entity(), m_frameWidth(frameWidth), m_frameHeight(frameHeight), m_npcType(type) {
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
  m_animationAccumulator = 0.0f;  // deltaTime accumulator for animation timing
  m_flip = SDL_FLIP_NONE; // Default flip direction

  // Load dimensions from texture if not provided
  if (m_frameWidth <= 0 || m_frameHeight <= 0) {
    loadDimensionsFromTexture();
  } else {
    m_width = m_frameWidth;
    m_height = m_frameHeight;
  }

  // Initialize animation system
  initializeAnimationMap();
  setupAnimationStates();

  // Set default animation state
  setAnimationState("Idle");

  // Set default wander area to world bounds (can be changed later via
  // setWanderArea)
  float worldMinX, worldMinY, worldMaxX, worldMaxY;
  if (WorldManager::Instance().getWorldBounds(worldMinX, worldMinY, worldMaxX,
                                              worldMaxY)) {
    // WorldManager returns bounds in PIXELS; apply directly
    m_minX = worldMinX;
    m_minY = worldMinY;
    m_maxX = worldMaxX;
    m_maxY = worldMaxY;
  } else {
    // Fallback to reasonable world bounds if WorldManager not available yet
    m_minX = 0.0f;
    m_minY = 0.0f;
    m_maxX = 2048.0f; // Larger default world area
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
  m_width = 64;
  m_height = 32;     // Set height equal to the sprite sheet row height
  m_frameWidth = 32; // Default frame width (width/numFrames)

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
        m_frameHeight = frameHeight;

        // Sync new dimensions to collision body if already registered
        Vector2D newHalfSize(m_frameWidth * 0.5f, m_height * 0.5f);
        CollisionManager::Instance().updateCollisionBodySizeSOA(getID(), newHalfSize);
      } else {
        NPC_ERROR(std::format("Failed to query NPC texture dimensions: {}", SDL_GetError()));
      }
    }
  } else {
    NPC_ERROR(std::format("NPC texture '{}' not found in TextureManager", m_textureID));
  }
}

// State management removed - handled by AI Manager

void NPC::update(float deltaTime) {
  // The AI drives velocity directly; sync to collision body
  // Let collision system handle movement integration to prevent micro-bouncing

  // AIManager batch processing handles collision updates for AI-managed NPCs
  // Direct collision update removed to prevent race conditions with async AI threads
  // Collision updates are batched and submitted via submitPendingKinematicUpdates()
  m_acceleration = Vector2D(0, 0);

  // Update animation state machine
  m_stateManager.update(deltaTime);

  // Position sync is handled by setPosition() calls - no need for periodic
  // checks This prevents visual glitching from position corrections during
  // rendering

  // Area constraints handling removed; behaviors and managers coordinate
  // movement

  // --- Animation Frame Updates using deltaTime accumulator ---
  m_animationAccumulator += deltaTime;
  float frameTime = m_animSpeed / 1000.0f;  // ms to seconds

  // Advance animation frames based on accumulated time
  if (m_animationAccumulator >= frameTime) {
    if (m_animationLoops) {
      // Looping animation - cycle frames
      m_currentFrame = (m_currentFrame + 1) % m_numFrames;
    } else {
      // Non-looping animation - stop at last frame
      if (m_currentFrame < m_numFrames - 1) {
        m_currentFrame++;
      }
    }
    m_animationAccumulator -= frameTime;  // Preserve excess time for smooth timing
  }

  // --- Flip Direction based on Velocity ---
  // Smooth flip: require sufficient lateral speed and a minimum interval
  const float flipSpeedThreshold = 15.0f; // px/s
  Uint64 currentTime = SDL_GetTicks();  // Used only for flip timing
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

  // If the texture dimensions haven't been loaded yet, try loading them
  if (m_frameWidth == 0 &&
      TextureManager::Instance().isTextureInMap(m_textureID)) {
    loadDimensionsFromTexture();
  }
}

void NPC::render(SDL_Renderer* renderer, float cameraX, float cameraY, float interpolationAlpha) {
  // Get interpolated position for smooth rendering between fixed timestep updates
  Vector2D interpPos = getInterpolatedPosition(interpolationAlpha);

  // Convert world coords to screen coords using passed camera offset
  // Using floating-point for smooth sub-pixel rendering (no pixel-snapping)
  float renderX = interpPos.getX() - cameraX - (m_frameWidth / 2.0f);
  float renderY = interpPos.getY() - cameraY - (m_height / 2.0f);

  // Render the NPC with the current animation frame
  TextureManager::Instance().drawFrame(m_textureID,
                    renderX,
                    renderY,
                    m_frameWidth, m_height, m_currentRow, m_currentFrame,
                    renderer, m_flip);
}

void NPC::clean() {
  // Prevent double-cleanup (uses member flag instead of thread_local set that leaked memory)
  if (m_cleaned) {
    return;
  }
  m_cleaned = true;

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
  CollisionManager::Instance().removeCollisionBodySOA(getID());
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

  NPC_DEBUG(std::format("NPC inventory initialized with {} slots", m_inventory->getMaxSlots()));
}

void NPC::onResourceChanged(HammerEngine::ResourceHandle resourceHandle,
                            int oldQuantity, int newQuantity) {
  const std::string resourceId = resourceHandle.toString();
  // Use EventManager hub to trigger a ResourceChange (no registration needed)
  EventManager::Instance().triggerResourceChange(
      shared_this(), resourceHandle, oldQuantity, newQuantity, "npc_action",
      EventManager::DispatchMode::Deferred);
  NPC_DEBUG(std::format("Resource changed: {} from {} to {} - change triggered via EventManager",
                         resourceId, oldQuantity, newQuantity));
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
    NPC_ERROR(std::format("NPC::tradeWithPlayer - Invalid quantity: {}", quantity));
    return false;
  }

  // Simple trade: NPC gives item to player for free (could be enhanced with
  // currency exchange)
  if (m_inventory->transferTo(playerInventory, resourceHandle, quantity)) {
    NPC_DEBUG(std::format("NPC traded {} resources (handle: {}) to player",
                           quantity, resourceHandle.toString()));
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

  NPC_DEBUG(std::format("NPC loot drops initialized with {} possible drops", m_dropRates.size()));
}

void NPC::dropLoot() {
  if (!m_hasLootDrops) {
    return;
  }

  // Use member RNG (avoids static vars in threaded code per CLAUDE.md)
  NPC_DEBUG("NPC dropping loot...");

  // Check each potential drop
  for (const auto &[itemHandle, dropRate] : m_dropRates) {
    if (m_lootDist(m_lootRng) <= dropRate) {
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
  NPC_DEBUG(std::format("NPC dropped {} items (handle: {})", quantity, itemHandle.toString()));

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
        std::format("NPC::setLootDropRate - Drop rate must be between 0.0 and 1.0, got: {}", dropRate));
    return;
  }

  m_dropRates[itemHandle] = dropRate;
  m_hasLootDrops = !m_dropRates.empty();
  NPC_DEBUG(std::format("Set drop rate for item (handle: {}) to {}", itemHandle.toString(), dropRate));
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

  NPC_DEBUG(std::format("Registering collision body - ID: {}, Position: ({},{}), Size: {}x{}",
                         getID(), m_position.getX(), m_position.getY(), halfW * 2, halfH * 2));

  // Configure collision layers based on NPC type
  uint32_t layer, mask;
  if (m_npcType == NPCType::Pet) {
    // Pets use Layer_Pet and don't collide with Layer_Player or other Pets
    layer = HammerEngine::CollisionLayer::Layer_Pet;
    mask = 0xFFFFFFFFu & ~(HammerEngine::CollisionLayer::Layer_Player |
                           HammerEngine::CollisionLayer::Layer_Pet);
  } else {
    // Standard NPCs use Layer_Enemy and collide with everything except other NPCs and Pets
    layer = HammerEngine::CollisionLayer::Layer_Enemy;
    mask = 0xFFFFFFFFu & ~(HammerEngine::CollisionLayer::Layer_Enemy |
                           HammerEngine::CollisionLayer::Layer_Pet);
  }

  // Use new SOA-based collision system (deferred command queue)
  cm.addCollisionBodySOA(getID(), aabb.center, aabb.halfSize, HammerEngine::BodyType::KINEMATIC, layer, mask);
  // CRITICAL: Process command immediately so body exists before attaching entity
  // This is safe because we batch spawn NPCs (30 every 10 frames) instead of per-frame
  cm.processPendingCommands();
  // Attach entity reference to SOA storage
  cm.attachEntity(getID(), shared_this());

  std::string layerName = (m_npcType == NPCType::Pet) ? "Layer_Pet" : "Layer_Enemy";
  NPC_DEBUG(std::format("Collision body registered successfully - KINEMATIC type with {}", layerName));
}

void NPC::setFaction(Faction f) {
  m_faction = f;
  // Collision layers are now set atomically in ensurePhysicsBodyRegistered()
}

// Animation state management methods

void NPC::initializeAnimationMap() {
  // Default animation configuration mapping names to sprite sheet details
  // Can be overridden for NPCs with different sprite sheet layouts
  m_animationMap = {
      {"idle",       {0, 2, 150, true}},   // Row 0, 2 frames, 150ms, looping
      {"walking",    {1, 4, 100, true}},   // Row 1, 4 frames, 100ms, looping
      {"attacking",  {2, 3, 80, false}},   // Row 2, 3 frames, 80ms, play once
      {"recovering", {3, 2, 120, false}},  // Row 3, 2 frames, 120ms, play once
      {"hurt",       {4, 2, 100, false}},  // Row 4, 2 frames, 100ms, play once
      {"dying",      {5, 4, 120, false}}   // Row 5, 4 frames, 120ms, play once
  };
}

void NPC::setupAnimationStates() {
  // Create and add animation states to the state manager
  m_stateManager.addState("Idle", std::make_unique<NPCIdleState>(*this));
  m_stateManager.addState("Walking", std::make_unique<NPCWalkingState>(*this));
  m_stateManager.addState("Attacking", std::make_unique<NPCAttackingState>(*this));
  m_stateManager.addState("Recovering", std::make_unique<NPCRecoveringState>(*this));
  m_stateManager.addState("Hurt", std::make_unique<NPCHurtState>(*this));
  m_stateManager.addState("Dying", std::make_unique<NPCDyingState>(*this));
}

void NPC::setAnimationState(const std::string& stateName) {
  if (m_stateManager.hasState(stateName)) {
    m_stateManager.setState(stateName);
  } else {
    NPC_WARN(std::format("NPC animation state not found: {}", stateName));
  }
}

void NPC::playAnimation(const std::string& animName) {
  // Skip if already playing this animation - prevents jitter on state re-entry
  if (m_currentAnimationName == animName) {
    return;
  }

  auto it = m_animationMap.find(animName);
  if (it != m_animationMap.end()) {
    const auto& config = it->second;
    m_currentRow = config.row + 1;  // TextureManager uses 1-based rows
    m_numFrames = config.frameCount;
    m_animSpeed = config.speed;
    m_animationLoops = config.loop;
    m_currentFrame = 0;
    m_animationAccumulator = 0.0f;  // Reset deltaTime accumulator
    m_currentAnimationName = animName;  // Track current animation
  } else {
    NPC_WARN(std::format("NPC animation not found: {}", animName));
  }
}

std::string NPC::getCurrentAnimationState() const {
  return m_stateManager.getCurrentStateName();
}

std::string NPC::getName() const {
  // Format: "TextureType #ID" (e.g., "skeleton #42")
  // Uses last 4 digits of entity ID for readability
  return std::format("{} #{}", m_textureID, getID() % 10000);
}

// Combat system methods

void NPC::takeDamage(float damage, const Vector2D& knockback) {
  if (m_currentHealth <= 0.0f) {
    return;  // Already dead
  }

  m_currentHealth = std::max(0.0f, m_currentHealth - damage);

  // Apply knockback
  if (knockback.length() > 0.001f) {
    setPosition(getPosition() + knockback);
  }

  // Handle death or hurt
  if (m_currentHealth <= 0.0f) {
    die();
  } else {
    setAnimationState("Hurt");
  }

  NPC_DEBUG(std::format("NPC took {} damage, health: {}/{}", damage, m_currentHealth, m_maxHealth));
}

void NPC::heal(float amount) {
  if (m_currentHealth <= 0.0f) {
    return;  // Can't heal dead NPC
  }

  float oldHealth = m_currentHealth;
  m_currentHealth = std::min(m_maxHealth, m_currentHealth + amount);

  NPC_DEBUG(std::format("NPC healed {} HP: {}/{} -> {}/{}",
            amount, oldHealth, m_maxHealth, m_currentHealth, m_maxHealth));
}

void NPC::die() {
  NPC_INFO(std::format("NPC died at position ({}, {})", m_position.getX(), m_position.getY()));

  setAnimationState("Dying");
  dropLoot();

  // Note: Actual entity removal handled by AIManager when animation completes
  // or via other cleanup mechanisms
}

void NPC::setMaxHealth(float maxHealth) {
  m_maxHealth = maxHealth;
  m_currentHealth = std::min(m_currentHealth, m_maxHealth);
}

void NPC::setMaxStamina(float maxStamina) {
  m_maxStamina = maxStamina;
  m_currentStamina = std::min(m_currentStamina, m_maxStamina);
}
