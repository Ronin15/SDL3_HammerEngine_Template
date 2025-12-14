/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/DroppedItem.hpp"
#include "core/Logger.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "utils/Camera.hpp"
#include <cmath>
#include <format>

DroppedItem::DroppedItem(HammerEngine::ResourceHandle resourceHandle,
                         const Vector2D &position, int quantity)
    : Entity(), m_resourceHandle(resourceHandle), m_quantity(quantity),
      m_pickupTimer(0.0f), m_bobTimer(0.0f), m_canBePickedUp(false) {

  // Set position using Entity's method
  setPosition(position);

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
    static thread_local float animTimer = 0.0f;
    animTimer += deltaTime * getAnimSpeed();
    if (animTimer >= 100.0f) { // Reset at 100ms intervals
      setCurrentFrame((getCurrentFrame() + 1) % getNumFrames());
      animTimer = 0.0f;
    }
  }
}

void DroppedItem::render(SDL_Renderer* renderer, float cameraX, float cameraY, float interpolationAlpha) {
  if (m_quantity <= 0) {
    return; // Don't render empty stacks
  }

  // Get interpolated position for smooth rendering between physics updates
  Vector2D interpPos = getInterpolatedPosition(interpolationAlpha);

  // Apply bobbing effect to the interpolated Y position
  float bobOffset = std::sin(m_bobTimer) * 3.0f; // 3 pixel bobbing range

  // Convert world coords to screen coords using passed camera offset
  // Same formula as WorldManager: screenX = worldX - cameraX
  float renderX = interpPos.getX() - cameraX;
  float renderY = interpPos.getY() - cameraY + bobOffset;

  // TODO: Implement actual rendering logic here using renderer and renderX/renderY
  // For now, suppress unused parameter warning until full rendering is implemented
  (void)renderer;
  (void)renderX;
  (void)renderY;
}

void DroppedItem::clean() {
  ENTITY_INFO("Cleaning up DroppedItem");

  // Clean up DroppedItem specific resources
  m_quantity = 0;
  m_pickupTimer = 0.0f;
  m_bobTimer = 0.0f;
  m_canBePickedUp = false;

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
