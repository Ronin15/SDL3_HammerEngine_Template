/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef NPC_HPP
#define NPC_HPP

#include "entities/Entity.hpp"
#include "entities/resources/InventoryComponent.hpp"
#include "utils/ResourceHandle.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL_render.h>
#include <memory>
#include <string>
#include <unordered_map>

class NPC : public Entity {
public:
  NPC(const std::string &textureID, const Vector2D &startPosition,
      int frameWidth, int frameHeight);
  ~NPC() override;

  // Factory method to ensure proper shared_ptr creation
  // Factory method to ensure NPCs are always created with shared_ptr
  static std::shared_ptr<NPC> create(const std::string &textureID,
                                     const Vector2D &startPosition,
                                     int frameWidth = 0, int frameHeight = 0) {
    return std::make_shared<NPC>(textureID, startPosition, frameWidth,
                                 frameHeight);
  }

  void update(float deltaTime) override;
  void render() override;
  void clean() override;

  // No state management - handled by AI Manager

  // NPC-specific accessor methods
  SDL_FlipMode getFlip() const override { return m_flip; }

  // NPC-specific setter methods
  void setFlip(SDL_FlipMode flip) override { m_flip = flip; }

  // AI-specific methods
  void setWanderArea(float minX, float minY, float maxX, float maxY);

  // Enable or disable screen bounds checking
  void setBoundsCheckEnabled(bool enabled) { m_boundsCheckEnabled = enabled; }
  bool isBoundsCheckEnabled() const { return m_boundsCheckEnabled; }

  // Inventory management
  InventoryComponent *getInventory() { return m_inventory.get(); }
  const InventoryComponent *getInventory() const { return m_inventory.get(); }

  // Initialization - call after construction to setup inventory
  void initializeInventory();

  // NPC-specific resource operations (use ResourceHandle via getInventory())

  // Trading system (resource handle system compliance)
  bool canTrade(HammerEngine::ResourceHandle resourceHandle,
                int quantity = 1) const;
  bool tradeWithPlayer(HammerEngine::ResourceHandle resourceHandle,
                       int quantity, InventoryComponent &playerInventory);
  void initializeShopInventory();
  void initializeLootDrops();

  // Loot system
  void dropLoot();
  void dropSpecificItem(HammerEngine::ResourceHandle itemHandle,
                        int quantity = 1);

  // Helper method to set up loot drops during initialization
  void setLootDropRate(HammerEngine::ResourceHandle itemHandle, float dropRate);

private:
  void loadDimensionsFromTexture();
  void setupInventory();
  void onResourceChanged(HammerEngine::ResourceHandle resourceHandle,
                         int oldQuantity, int newQuantity);

  std::unique_ptr<InventoryComponent>
      m_inventory;                    // NPC inventory for trading/loot
  int m_frameWidth{0};                // Width of a single animation frame
  int m_frameHeight{0};               // Height of a single animation frame
  int m_spriteSheetRows{0};           // Number of rows in the sprite sheet
  Uint64 m_lastFrameTime{0};          // Time of last animation frame change
  SDL_FlipMode m_flip{SDL_FLIP_NONE}; // Default flip direction

  // Wander area bounds
  float m_minX{0.0f};
  float m_minY{0.0f};
  float m_maxX{800.0f};
  float m_maxY{600.0f};

  // Flag to control bounds checking behavior
  bool m_boundsCheckEnabled{false};

  // Trading and loot configuration
  bool m_canTrade{false};
  bool m_hasLootDrops{false};
  std::unordered_map<HammerEngine::ResourceHandle, float>
      m_dropRates; // itemHandle -> drop probability
};

#endif // NPC_HPP