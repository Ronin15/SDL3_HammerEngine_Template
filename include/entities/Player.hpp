/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "entities/Entity.hpp"
#include "entities/resources/InventoryComponent.hpp"
#include "managers/EntityStateManager.hpp"
#include "utils/ResourceHandle.hpp"
#include <SDL3/SDL_render.h>
#include <memory>
#include <unordered_map>

class Player : public Entity {
public:
  Player();
  ~Player() override;

  void update(float deltaTime) override;
  void render(const HammerEngine::Camera *camera) override;
  void clean() override;

  // Sync movement with CollisionManager (player moves itself)
  void setVelocity(const Vector2D& velocity) override;
  void setPosition(const Vector2D& position) override;

  // State management
  void changeState(const std::string &stateName);
  std::string getCurrentStateName() const;
  // void setVelocity(const Vector2D& m_velocity); for later in save manager
  // void setFlip(SDL_FlipMode m_flip);

  // Player-specific accessor methods
  SDL_FlipMode getFlip() const override { return m_flip; }

  // Player-specific setter methods
  void setFlip(SDL_FlipMode flip) override { m_flip = flip; }

  // Animation timing methods (needed for state machine)
  Uint64 getLastFrameTime() const { return m_lastFrameTime; }
  void setLastFrameTime(Uint64 time) { m_lastFrameTime = time; }

  // Movement speed accessor
  float getMovementSpeed() const { return m_movementSpeed; }

  // Inventory management
  InventoryComponent *getInventory() { return m_inventory.get(); }
  const InventoryComponent *getInventory() const { return m_inventory.get(); }

  // Player-specific resource operations (use ResourceHandle via getInventory())

  // Equipment management
  bool equipItem(HammerEngine::ResourceHandle itemHandle);
  bool unequipItem(const std::string &slotName);
  HammerEngine::ResourceHandle
  getEquippedItem(const std::string &slotName) const;

  // Crafting and consumption
  bool canCraft(const std::string &recipeId) const;
  bool craftItem(const std::string &recipeId);
  bool consumeItem(HammerEngine::ResourceHandle itemHandle);

  // Initialization - call after construction to setup inventory
  void initializeInventory();

  // Post-construction registration with CollisionManager
  void registerCollisionBody() { ensurePhysicsBodyRegistered(); }

private:
  void handleMovementInput(float deltaTime);
  void handleStateTransitions();
  void loadDimensionsFromTexture();
  void setupStates();
  void setupInventory();
  void ensurePhysicsBodyRegistered();
  void onResourceChanged(HammerEngine::ResourceHandle resourceHandle,
                         int oldQuantity, int newQuantity);
  EntityStateManager m_stateManager;
  std::unique_ptr<InventoryComponent> m_inventory; // Player inventory
  int m_frameWidth{0};                // Width of a single animation frame
  int m_spriteSheetRows{0};           // Number of rows in the sprite sheet
  Uint64 m_lastFrameTime{0};          // Time of last animation frame change
  SDL_FlipMode m_flip{SDL_FLIP_NONE}; // Default flip direction
  float m_movementSpeed{112.5f};      // Movement speed in pixels per second

  // Equipment slots - store handles instead of item IDs
  std::unordered_map<std::string, HammerEngine::ResourceHandle>
      m_equippedItems; // slot -> itemHandle
};
#endif // PLAYER_HPP
