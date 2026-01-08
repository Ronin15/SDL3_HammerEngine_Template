/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/DroppedItem.hpp"
#include "core/Logger.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/TextureManager.hpp"
#include "utils/Camera.hpp"
#include <cmath>
#include <format>

DroppedItem::DroppedItem(HammerEngine::ResourceHandle resourceHandle,
                         const Vector2D &position, int quantity)
    : Entity(), m_resourceHandle(resourceHandle), m_quantity(quantity),
      m_pickupTimer(0.0f), m_bobTimer(0.0f), m_canBePickedUp(false) {

  // Register with EntityDataManager first - it sets up the initial transform
  // EntityDataManager is the single source of truth for all entity data
  auto& edm = EntityDataManager::Instance();
  if (edm.isInitialized()) {
    EntityHandle handle = edm.registerDroppedItem(getID(), position, m_resourceHandle, m_quantity);
    setHandle(handle);  // Enable EntityDataManager-backed accessors
  }

  // Get the resource template to copy visual properties
  auto resourceTemplate = getResourceTemplate();
  if (resourceTemplate) {
    // Copy visual properties from the resource template using Entity methods
    setTextureID(resourceTemplate->getWorldTextureId());
    setNumFrames(resourceTemplate->getNumFrames());
    setAnimSpeed(resourceTemplate->getAnimSpeed());

    // Set default entity size (can be overridden)
    setWidth(32);
    setHeight(32);

    ENTITY_INFO(std::format(
        "Created DroppedItem for resource: {} (Quantity: {})",
        resourceTemplate->getName(), quantity));
  } else {
    ENTITY_ERROR("Failed to create DroppedItem: Invalid resource handle");
  }

  // Items become pickupable after a short delay to prevent immediate re-pickup
  m_pickupTimer = 0.5f; // 0.5 second delay
}

void DroppedItem::update(float deltaTime) {
  // Update pickup timer
  if (m_pickupTimer > 0.0f) {
    m_pickupTimer -= deltaTime;
    if (m_pickupTimer <= 0.0f) {
      m_canBePickedUp = true;
    }
  }

  // Update visual effects
  updateVisualEffects(deltaTime);

  // Update animation frame (manual implementation since Entity is pure virtual)
  if (getNumFrames() > 1 && getAnimSpeed() > 0) {
    m_animTimer += deltaTime * getAnimSpeed();
    if (m_animTimer >= 100.0f) { // Reset at 100ms intervals
      setCurrentFrame((getCurrentFrame() + 1) % getNumFrames());
      m_animTimer = 0.0f;
    }
  }
}

void DroppedItem::render(SDL_Renderer* renderer, float cameraX, float cameraY, float interpolationAlpha) {
  if (m_quantity <= 0) {
    return; // Don't render empty stacks
  }

  // Cache texture on first render (like WorldManager pattern - no hash lookup per frame)
  if (!m_cachedTexture) {
    m_cachedTexture = TextureManager::Instance().getTexturePtr(m_textureID);
    if (!m_cachedTexture) return;
  }

  // Get interpolated position for smooth rendering between physics updates
  Vector2D interpPos = getInterpolatedPosition(interpolationAlpha);

  // Apply bobbing effect to the interpolated Y position
  float bobOffset = std::sin(m_bobTimer) * 3.0f; // 3 pixel bobbing range

  // Convert world coords to screen coords using passed camera offset
  float renderX = interpPos.getX() - cameraX - (m_width / 2.0f);
  float renderY = interpPos.getY() - cameraY - (m_height / 2.0f) + bobOffset;

  // Direct SDL call with cached texture - no hash lookup!
  SDL_FRect srcRect = {
      static_cast<float>(m_width * m_currentFrame),
      static_cast<float>(m_height * (m_currentRow - 1)),
      static_cast<float>(m_width),
      static_cast<float>(m_height)
  };
  SDL_FRect destRect = {renderX, renderY,
                        static_cast<float>(m_width),
                        static_cast<float>(m_height)};
  SDL_FPoint center = {m_width / 2.0f, m_height / 2.0f};

  SDL_RenderTextureRotated(renderer, m_cachedTexture, &srcRect, &destRect, 0.0, &center, SDL_FLIP_NONE);
}

void DroppedItem::clean() {
  ENTITY_INFO("Cleaning up DroppedItem");

  // Clean up DroppedItem specific resources
  m_quantity = 0;
  m_pickupTimer = 0.0f;
  m_bobTimer = 0.0f;
  m_canBePickedUp = false;

  // Unregister from EntityDataManager (Phase 1 parallel storage)
  auto& edm = EntityDataManager::Instance();
  if (edm.isInitialized()) {
    edm.unregisterEntity(getID());
  }

  // Clean up Entity base properties using Entity methods
  setTextureID("");
  setNumFrames(1);
  setAnimSpeed(100);
  setCurrentFrame(0);
  setWidth(0);
  setHeight(0);
}

bool DroppedItem::addQuantity(int amount) {
  if (amount <= 0) {
    return false;
  }

  auto resourceTemplate = getResourceTemplate();
  if (!resourceTemplate) {
    return false;
  }

  // Check if we can add this quantity without exceeding stack limits
  int newQuantity = m_quantity + amount;
  if (newQuantity > resourceTemplate->getMaxStackSize()) {
    return false;
  }

  m_quantity = newQuantity;
  return true;
}

bool DroppedItem::removeQuantity(int amount) {
  if (amount <= 0 || amount > m_quantity) {
    return false;
  }

  m_quantity -= amount;
  return true;
}

std::shared_ptr<Resource> DroppedItem::getResourceTemplate() const {
  return ResourceTemplateManager::Instance().getResourceTemplate(
      m_resourceHandle);
}

void DroppedItem::updateVisualEffects(float deltaTime) {
  // Update bobbing timer
  m_bobTimer += deltaTime * 2.0f; // Bobbing speed

  // Keep the timer in a reasonable range to avoid precision issues
  if (m_bobTimer > 2.0f * M_PI) {
    m_bobTimer -= 2.0f * M_PI;
  }
}
