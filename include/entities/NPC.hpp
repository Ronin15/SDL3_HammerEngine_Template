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
#include <random>
#include <string>
#include <unordered_map>

class NPC : public Entity {
public:
  enum class Faction { Friendly, Enemy, Neutral };
  enum class NPCType { Standard, Pet };

  NPC(const std::string &textureID, const Vector2D &startPosition,
      int frameWidth, int frameHeight, NPCType type = NPCType::Standard);
  ~NPC() override;

  // Factory method to ensure proper shared_ptr creation
  // Factory method to ensure NPCs are always created with shared_ptr
  static std::shared_ptr<NPC> create(const std::string &textureID,
                                     const Vector2D &startPosition,
                                     int frameWidth = 0, int frameHeight = 0,
                                     NPCType type = NPCType::Standard) {
    auto npc = std::make_shared<NPC>(textureID, startPosition, frameWidth,
                                     frameHeight, type);
    npc->ensurePhysicsBodyRegistered();
    // Collision layers are set atomically in ensurePhysicsBodyRegistered()
    // No need to call setFaction as m_faction defaults to Neutral
    return npc;
  }

  void update(float) override;
  void render(SDL_Renderer* renderer, float cameraX, float cameraY, float interpolationAlpha = 1.0f) override;
  void clean() override;
  // No state management - handled by AI Manager

  // NPC-specific accessor methods
  SDL_FlipMode getFlip() const override { return m_flip; }

  // NPC-specific setter methods
  void setFlip(SDL_FlipMode flip) override { m_flip = flip; }

  // AI-specific methods
  void setWanderArea(float minX, float minY, float maxX, float maxY);

  // Faction/layer control
  void setFaction(Faction f);
  Faction getFaction() const { return m_faction; }

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

protected:
  int m_frameWidth{0};                // Width of a single animation frame

private:
  void loadDimensionsFromTexture();
  virtual void ensurePhysicsBodyRegistered();
  void setupInventory();
  void onResourceChanged(HammerEngine::ResourceHandle resourceHandle,
                         int oldQuantity, int newQuantity);

  std::unique_ptr<InventoryComponent>
      m_inventory;                    // NPC inventory for trading/loot
  int m_frameHeight{0};               // Height of a single animation frame
  int m_spriteSheetRows{0};           // Number of rows in the sprite sheet
  Uint64 m_lastFrameTime{0};          // Time of last animation frame change
  SDL_FlipMode m_flip{SDL_FLIP_NONE}; // Default flip direction

  // Wander area bounds (still used for area-based behaviors if needed)
  float m_minX{0.0f};
  float m_minY{0.0f};
  float m_maxX{800.0f};
  float m_maxY{600.0f};

  // Trading and loot configuration
  bool m_canTrade{false};
  bool m_hasLootDrops{false};
  std::unordered_map<HammerEngine::ResourceHandle, float>
      m_dropRates; // itemHandle -> drop probability

  Faction m_faction{Faction::Neutral};
  NPCType m_npcType{NPCType::Standard};

  // Texture flip smoothing
  int m_lastFlipSign{1};
  Uint64 m_lastFlipTime{0};

  // Diagnostic throttling
  Uint64 m_lastStuckLogTime{0};

  // Loot drop RNG (member vars to avoid static in threaded code per CLAUDE.md)
  mutable std::mt19937 m_lootRng{std::random_device{}()};
  mutable std::uniform_real_distribution<float> m_lootDist{0.0f, 1.0f};

  // Double-cleanup prevention (replaces thread_local set that leaked memory)
  bool m_cleaned{false};
};

#endif // NPC_HPP
