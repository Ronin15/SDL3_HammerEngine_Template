/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef ENTITY_HPP
#define ENTITY_HPP

#include "utils/Vector2D.hpp"
#include <string>
#include <memory>
#include <SDL3/SDL_surface.h>

// Forward declaration for Entity shared_ptr typedef
class Entity;

// Define standard smart pointer types for Entity
using EntityPtr = std::shared_ptr<Entity>;
using EntityWeakPtr = std::weak_ptr<Entity>;

class Entity : public std::enable_shared_from_this<Entity> {
 public:
   virtual void update(float deltaTime) = 0;
   virtual void render(double alpha) = 0;
   
   /**
    * @brief Clean up the entity's resources before destruction
    * 
    * This method is called explicitly before an entity is destroyed.
    * It's safe to use shared_from_this() here.
    * 
    * IMPORTANT: All entity management operations (such as unassigning from AIManager)
    * should happen here, NOT in the destructor.
    */
   virtual void clean() = 0;
   
   /**
    * @brief Virtual destructor
    * 
    * IMPORTANT: Do NOT call shared_from_this() or any method that uses it
    * (like shared_this()) in the destructor. By the time the destructor runs,
    * all shared_ptrs to this object have been destroyed, and calling
    * shared_from_this() will throw std::bad_weak_ptr.
    */
   virtual ~Entity() = default;
   
   /**
    * @brief Helper to get a shared_ptr to this object
    * 
    * IMPORTANT: Never call this in constructors or destructors!
    * Only use this when the object is managed by a std::shared_ptr.
    * 
    * @return A shared_ptr to this object
    * @throws std::bad_weak_ptr if called from constructor/destructor or if the object
    *         is not managed by a std::shared_ptr
    */
   EntityPtr shared_this() {
     return shared_from_this();
   }
   
   /**
    * @brief Helper to get a weak_ptr to this object
    * 
    * IMPORTANT: Never call this in constructors or destructors!
    * Only use this when the object is managed by a std::shared_ptr.
    * 
    * @return A weak_ptr to this object
    * @throws std::bad_weak_ptr if called from constructor/destructor or if the object
    *         is not managed by a std::shared_ptr
    */
   EntityWeakPtr weak_this() {
     return shared_from_this();
   }

   // Accessor methods
   Vector2D getPosition() const { return m_position; }
   Vector2D getVelocity() const { return m_velocity; }
   Vector2D getAcceleration() const { return m_acceleration; }
   int getWidth() const { return m_width; }
   int getHeight() const { return m_height; }
   const std::string& getTextureID() const { return m_textureID; }
   int getCurrentFrame() const { return m_currentFrame; }
   int getCurrentRow() const { return m_currentRow; }
   int getNumFrames() const { return m_numFrames; }
   int getAnimSpeed() const { return m_animSpeed; }

   // Setter methods
   virtual void setPosition(const Vector2D& position) { m_position = position; }
   virtual void setVelocity(const Vector2D& velocity) { m_velocity = velocity; }
   virtual void setAcceleration(const Vector2D& acceleration) { m_acceleration = acceleration; }
   virtual void setWidth(int width) { m_width = width; }
   virtual void setHeight(int height) { m_height = height; }
   virtual void setTextureID(const std::string& id) { m_textureID = id; }
   virtual void setCurrentFrame(int frame) { m_currentFrame = frame; }
   virtual void setCurrentRow(int row) { m_currentRow = row; }
   virtual void setNumFrames(int numFrames) { m_numFrames = numFrames; }
   virtual void setAnimSpeed(int speed) { m_animSpeed = speed; }

   // Used for rendering flipping - to be implemented by derived classes
   virtual void setFlip(SDL_FlipMode flip) { (void)flip; /* Unused in base class */ }
   virtual SDL_FlipMode getFlip() const { return SDL_FLIP_NONE; }

   protected:
    Vector2D m_acceleration{0, 0};
    Vector2D m_velocity{0, 0};
    Vector2D m_position{0, 0};
    Vector2D m_previousPosition{0, 0};
    int m_width{0};
    int m_height{0};
    std::string m_textureID{};
    int m_currentFrame{0};
    int m_currentRow{0};
    int m_numFrames{0};
    int m_animSpeed{0};
};
#endif  // ENTITY_HPP
