/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef DROPPED_ITEM_HPP
#define DROPPED_ITEM_HPP

#include "entities/Entity.hpp"
#include "entities/Resource.hpp"
#include "utils/ResourceHandle.hpp"
#include "utils/Vector2D.hpp"
#include <memory>

/**
 * @brief Entity representing a resource dropped in the game world
 * 
 * DroppedItem is a game entity that represents a physical instance
 * of a resource in the world. It inherits from Entity to have position,
 * rendering, and physics behavior while referencing a Resource template
 * for its properties.
 */
class DroppedItem : public Entity {
public:
    /**
     * @brief Create a dropped item from a resource template
     * @param resourceHandle Handle to the resource template
     * @param position World position where the item is dropped
     * @param quantity Number of items in this stack
     */
    DroppedItem(HammerEngine::ResourceHandle resourceHandle, 
                const Vector2D& position, 
                int quantity = 1);
    
    virtual ~DroppedItem() override = default;

    // Entity interface implementation
    void update(float deltaTime) override;
    void render(const HammerEngine::Camera* camera) override;
    void clean() override;

    // DroppedItem specific methods
    HammerEngine::ResourceHandle getResourceHandle() const { return m_resourceHandle; }
    int getQuantity() const { return m_quantity; }
    void setQuantity(int quantity) { m_quantity = quantity; }
    
    // Add/remove items from this stack
    bool addQuantity(int amount);
    bool removeQuantity(int amount);
    
    // Check if this item can be picked up
    bool canPickup() const { return m_quantity > 0 && m_canBePickedUp; }
    
    // Get the resource template (for properties like name, value, etc.)
    std::shared_ptr<Resource> getResourceTemplate() const;
    
    // Entity property getters (implementing Entity interface)
    const Vector2D& getPosition() const { return m_position; }
    void setPosition(const Vector2D& position) { m_position = position; }

protected:
    HammerEngine::ResourceHandle m_resourceHandle;
    int m_quantity;
    float m_pickupTimer;           // Timer for pickup availability
    float m_bobTimer;              // Timer for visual bobbing effect
    bool m_canBePickedUp;          // Whether this item can be picked up
    
    // Entity base properties (since we inherit from Entity)
    Vector2D m_position;           // World position
    std::string m_textureID;       // Texture identifier
    int m_numFrames;               // Animation frames
    int m_animSpeed;               // Animation speed
    int m_currentFrame;            // Current animation frame
    int m_width;                   // Entity width
    int m_height;                  // Entity height
    
    // Visual effects
    void updateVisualEffects(float deltaTime);
    void applyBobbingEffect();
};

#endif // DROPPED_ITEM_HPP
