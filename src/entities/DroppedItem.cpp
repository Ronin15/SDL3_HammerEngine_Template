/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/DroppedItem.hpp"
#include "core/Logger.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "utils/Camera.hpp"
#include <cmath>

DroppedItem::DroppedItem(HammerEngine::ResourceHandle resourceHandle,
                         const Vector2D &position, int quantity)
    : Entity(), m_resourceHandle(resourceHandle), m_quantity(quantity),
      m_pickupTimer(0.0f), m_bobTimer(0.0f), m_canBePickedUp(false) {

  // Set position after construction
  m_position = position;

  // Get the resource template to copy visual properties
  auto resourceTemplate = getResourceTemplate();
  if (resourceTemplate) {
    // Copy visual properties from the resource template
    m_textureID = resourceTemplate->getWorldTextureId();
    m_numFrames = resourceTemplate->getNumFrames();
    m_animSpeed = resourceTemplate->getAnimSpeed();

    // Set default entity size (can be overridden)
    m_width = 32;
    m_height = 32;

    ENTITY_INFO(
        "Created DroppedItem for resource: " + resourceTemplate->getName() +
        " (Quantity: " + std::to_string(quantity) + ")");
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
  if (m_numFrames > 1 && m_animSpeed > 0) {
    static float animTimer = 0.0f;
    animTimer += deltaTime * m_animSpeed;
    if (animTimer >= 100.0f) { // Reset at 100ms intervals
      m_currentFrame = (m_currentFrame + 1) % m_numFrames;
      animTimer = 0.0f;
    }
  }
}

void DroppedItem::render(const HammerEngine::Camera *camera) {
  if (m_quantity <= 0) {
    return; // Don't render empty stacks
  }

  // Apply bobbing effect before rendering
  applyBobbingEffect();

  // TODO: Implement actual rendering logic here
  // This would typically involve:
  // 1. Getting the texture from TextureManager using m_textureID
  // 2. Calculating screen position from world position using camera
  // 3. Rendering the sprite with current animation frame
  // 4. Optionally rendering quantity text for stacks > 1

  // For now, just suppress unused parameter warning
  (void)camera;
}

void DroppedItem::clean() {
  ENTITY_INFO("Cleaning up DroppedItem");

  // Clean up DroppedItem specific resources
  m_quantity = 0;
  m_pickupTimer = 0.0f;
  m_bobTimer = 0.0f;
  m_canBePickedUp = false;

  // Clean up Entity base properties
  m_textureID.clear();
  m_numFrames = 1;
  m_animSpeed = 100;
  m_currentFrame = 0;
  m_width = 0;
  m_height = 0;
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

void DroppedItem::applyBobbingEffect() {
  // Create a gentle bobbing motion
  float bobOffset = std::sin(m_bobTimer) * 3.0f; // 3 pixel bobbing range

  // Temporarily modify the position for rendering using accessor methods
  float originalY = m_position.getY();
  m_position.setY(originalY + bobOffset);

  // Note: This modifies the actual position temporarily for rendering
  // In a more complex system you might use separate render coordinates
}
