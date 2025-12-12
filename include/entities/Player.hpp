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
  void render(SDL_Renderer* renderer, float cameraX, float cameraY, float interpolationAlpha = 1.0f) override;

  /**
   * @brief Render player at a pre-computed interpolated position
   *
   * Use this for unified interpolation where the calling code computes the
   * interpolated position once and uses it for both camera offset and player
   * rendering, eliminating any potential divergence.
   *
   * @param renderer SDL renderer
   * @param interpPos Pre-computed interpolated position
   * @param cameraX Camera X offset
   * @param cameraY Camera Y offset
   */
  void renderAtPosition(SDL_Renderer* renderer, const Vector2D& interpPos,
                        float cameraX, float cameraY);

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

  // Physics body registration - call after construction
  void ensurePhysicsBodyRegistered();

private:
  void handleMovementInput(float deltaTime);
  void handleStateTransitions();
  void loadDimensionsFromTexture();
  void setupStates();
  void setupInventory();
  void onResourceChanged(HammerEngine::ResourceHandle resourceHandle,
                         int oldQuantity, int newQuantity);
  EntityStateManager m_stateManager;
  std::unique_ptr<InventoryComponent> m_inventory; // Player inventory
  int m_frameWidth{0};                // Width of a single animation frame
  int m_spriteSheetRows{0};           // Number of rows in the sprite sheet
  Uint64 m_lastFrameTime{0};          // Time of last animation frame change
  SDL_FlipMode m_flip{SDL_FLIP_NONE}; // Default flip direction
  float m_movementSpeed{120.0f};      // Movement speed in pixels per second (2 px/frame at 60 FPS)

  // Equipment slots - store handles instead of item IDs
  std::unordered_map<std::string, HammerEngine::ResourceHandle>
      m_equippedItems; // slot -> itemHandle
};
#endif // PLAYER_HPP
