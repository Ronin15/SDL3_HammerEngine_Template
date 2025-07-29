/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/NPC.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/TextureManager.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <random>
#include <set>

NPC::NPC(const std::string &textureID, const Vector2D &startPosition,
         int frameWidth, int frameHeight)
    : m_frameWidth(frameWidth), m_frameHeight(frameHeight) {
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

  // Set default wander area (can be changed later via setWanderArea)
  m_minX = 0.0f;
  m_minY = 0.0f;
  m_maxX = 800.0f;
  m_maxY = 600.0f;

  // Disable bounds checking by default
  m_boundsCheckEnabled = false;

  // Initialize inventory system - NOTE: Do NOT call setupInventory() here
  // because it can trigger shared_this() during construction.
  // Call initializeInventory() after construction completes.
  // std::cout << "Hammer Game Engine - NPC created at position: " <<
  // m_position.getX() << ", " << m_position.getY() << "\n";
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
  // Update position based on velocity and acceleration using deltaTime
  m_velocity += m_acceleration * deltaTime;
  m_position += m_velocity * deltaTime;

  // Apply frame-rate independent friction for smoother movement
  if (m_velocity.length() > 0.1f) {
    // Frame-rate independent friction using exponential decay
    const float frictionRate =
        0.05f; // Friction strength (higher = more friction)
    float frictionFactor = std::pow(frictionRate, deltaTime);
    m_velocity *= frictionFactor;

    // Update flip direction based on horizontal velocity
    // Only flip if the horizontal speed is significant enough
    if (std::abs(m_velocity.getX()) > 0.5f) {
      if (m_velocity.getX() < 0) {
        m_flip = SDL_FLIP_HORIZONTAL;
      } else {
        m_flip = SDL_FLIP_NONE;
      }
    }
  } else if (m_velocity.length() < 0.1f) {
    // If velocity is very small, stop completely to avoid tiny sliding
    m_velocity = Vector2D(0, 0);
  }

  // Reset acceleration
  m_acceleration = Vector2D(0, 0);

  // Only apply bounds checking if enabled
  if (m_boundsCheckEnabled) {
    // Handle bounds checking with bounce behavior if outside permitted area
    const float bounceBuffer = 20.0f; // Buffer zone before bouncing

    if (m_position.getX() < m_minX - bounceBuffer) {
      m_position.setX(m_minX);
      m_velocity.setX(std::abs(m_velocity.getX())); // Move right
      // Flip will be updated in the velocity section above
    } else if (m_position.getX() + m_width > m_maxX + bounceBuffer) {
      m_position.setX(m_maxX - m_width);
      m_velocity.setX(-std::abs(m_velocity.getX())); // Move left
      // Flip will be updated in the velocity section above
    }

    if (m_position.getY() < m_minY - bounceBuffer) {
      m_position.setY(m_minY);
      m_velocity.setY(std::abs(m_velocity.getY())); // Move down
    } else if (m_position.getY() + m_height > m_maxY + bounceBuffer) {
      m_position.setY(m_maxY - m_height);
      m_velocity.setY(-std::abs(m_velocity.getY())); // Move up
    }
  }

  // Update animation based on movement
  Uint64 currentTime = SDL_GetTicks();

  // Check if NPC is moving (velocity not close to zero)
  if (m_velocity.length() > 0.1f) {
    // Only update animation frame if enough time has passed
    if (currentTime > m_lastFrameTime + m_animSpeed) {
      m_currentFrame = (m_currentFrame + 1) % m_numFrames;
      m_lastFrameTime = currentTime;
    }
  } else {
    // When not moving, reset to first frame
    m_currentFrame = 0;
  }

  // If the texture dimensions haven't been loaded yet, try loading them
  if (m_frameWidth == 0 &&
      TextureManager::Instance().isTextureInMap(m_textureID)) {
    loadDimensionsFromTexture();
  }

  // Note: Flip direction is now determined by the NPC's velocity in this update
  // method AI behavior classes should no longer set flip directly
}

void NPC::render() {
  // Cache manager references for better performance
  TextureManager &texMgr = TextureManager::Instance();
  SDL_Renderer *renderer = GameEngine::Instance().getRenderer();

  // Calculate centered position for rendering
  // This ensures the NPC is centered at its position coordinates
  int renderX = static_cast<int>(m_position.getX() - (m_frameWidth / 2.0f));
  int renderY = static_cast<int>(m_position.getY() - (m_height / 2.0f));

  // Render the NPC with the current animation frame
  texMgr.drawFrame(m_textureID, renderX, renderY, m_frameWidth, m_height,
                   m_currentRow, m_currentFrame, renderer, m_flip);
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
  // Get resource name for the event
  auto resourceTemplate =
      ResourceTemplateManager::Instance().getResourceTemplate(resourceHandle);
  std::string resourceId =
      resourceTemplate ? resourceTemplate->getName() : "Unknown Resource";

  // Create and fire resource change event
  auto resourceEvent = std::make_shared<ResourceChangeEvent>(
      shared_this(), resourceId, oldQuantity, newQuantity, "npc_action");

  NPC_DEBUG("NPC Resource changed: " + resourceId + " from " +
            std::to_string(oldQuantity) + " to " + std::to_string(newQuantity));
}

// Resource management methods - removed, use getInventory() directly with
// ResourceHandle

// Trading system - updated to use ResourceHandle
bool NPC::canTrade(const std::string &itemId, int quantity) const {
  if (!m_canTrade || !m_inventory) {
    return false;
  }

  // Convert string to handle via ResourceTemplateManager
  auto &templateManager = ResourceTemplateManager::Instance();
  auto resource = templateManager.getResourceByName(itemId);
  if (!resource) {
    return false;
  }

  return m_inventory->hasResource(resource->getHandle(), quantity);
}

bool NPC::tradeWithPlayer(const std::string &itemId, int quantity,
                          InventoryComponent &playerInventory) {
  if (!canTrade(itemId, quantity)) {
    NPC_WARN("NPC::tradeWithPlayer - Cannot trade item: " + itemId);
    return false;
  }

  // Simple trade: NPC gives item to player for free (could be enhanced with
  // currency exchange)
  auto &templateManager = ResourceTemplateManager::Instance();
  auto resource = templateManager.getResourceByName(itemId);
  if (!resource) {
    NPC_WARN("NPC::tradeWithPlayer - Unknown item: " + itemId);
    return false;
  }

  if (m_inventory->transferTo(playerInventory, resource->getHandle(),
                              quantity)) {
    NPC_DEBUG("NPC traded " + std::to_string(quantity) + " " + itemId +
              " to player");
    return true;
  }

  return false;
}

void NPC::initializeShopInventory() {
  m_canTrade = true;

  // Add some basic shop items using ResourceTemplateManager
  auto &templateManager = ResourceTemplateManager::Instance();

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

  // Set up drop rates (0.0 to 1.0)
  m_dropRates["gold"] = 0.8f;          // 80% chance to drop gold
  m_dropRates["health_potion"] = 0.3f; // 30% chance to drop health potion
  m_dropRates["iron_ore"] = 0.2f;      // 20% chance to drop iron ore
  m_dropRates["oak_wood"] = 0.15f;     // 15% chance to drop oak wood

  NPC_DEBUG("NPC loot drops initialized");
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
  for (const auto &[itemId, dropRate] : m_dropRates) {
    if (dis(gen) <= dropRate) {
      // Determine quantity (simple random 1-3 for most items, more for gold)
      int quantity = 1;
      if (itemId == "gold") {
        quantity = 5 + (rand() % 15); // 5-19 gold
      } else {
        quantity = 1 + (rand() % 3); // 1-3 of other items
      }

      dropSpecificItem(itemId, quantity);
    }
  }
}

void NPC::dropSpecificItem(const std::string &itemId, int quantity) {
  // In a real implementation, you would create a physical item drop in the
  // world For now, we'll just log the drop
  NPC_DEBUG("NPC dropped " + std::to_string(quantity) + " " + itemId);

  // You could add the items to a world container, create pickup entities, etc.
  // This would integrate with your game's item pickup system
}
// Animation handling removed - TextureManager handles this functionality

void NPC::setWanderArea(float minX, float minY, float maxX, float maxY) {
  m_minX = minX;
  m_minY = minY;
  m_maxX = maxX;
  m_maxY = maxY;
}
