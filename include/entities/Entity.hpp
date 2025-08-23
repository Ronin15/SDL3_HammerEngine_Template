/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef ENTITY_HPP
#define ENTITY_HPP

#include "utils/UniqueID.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <string>

// Forward declarations
class Entity; // Forward declare for smart pointers
class InputHandler;
namespace HammerEngine {
    class Camera;
}

// Smart pointer type aliases
using EntityPtr = std::shared_ptr<Entity>;
using EntityWeakPtr = std::weak_ptr<Entity>;

// Type alias for entity ID
using EntityID = HammerEngine::UniqueID::IDType;

/**
 * @brief Pure virtual base class for all game objects.
 *
 * This class defines the common interface for all entities in the game,
 * including players, NPCs, items, and other interactive objects. It uses
 * the Component-Entity-System (CES) architecture, where entities are
 * composed of multiple components that define their behavior and
 * appearance.
 */
class Entity : public std::enable_shared_from_this<Entity> {
 public:
  /**
   * @brief Construct a new Entity object and assign it a unique ID.
   */
  Entity() : m_id(HammerEngine::UniqueID::generate()) {}

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
   * @brief Update the entity's state.
   *
   * This method is called once per frame for each entity. It should
   * update the entity's position, handle input, and perform any other
   * necessary calculations.
   *
   * @param deltaTime The time elapsed since the last frame, in seconds.
   */
  virtual void update(float deltaTime) = 0;

  /**
   * @brief Render the entity.
   *
   * This method is called once per frame for each entity. It should
   * draw the entity on the screen using the given camera.
   *
   * @param camera A pointer to the camera used for rendering.
   */
  virtual void render(const HammerEngine::Camera* camera) = 0;  // Camera-aware rendering

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
   * @brief Helper to get a shared_ptr to this object
   *
   * IMPORTANT: Never call this in constructors or destructors!
   * Only use this when the object is managed by a std::shared_ptr.
   *
   * @return A shared_ptr to this object
   * @throws std::bad_weak_ptr if called from constructor/destructor or if the object
   *         is not managed by a std::shared_ptr
   */
  EntityPtr shared_this() { return shared_from_this(); }

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
  EntityWeakPtr weak_this() { return shared_from_this(); }

  // Accessor methods
  EntityID getID() const { return m_id; }
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
  const EntityID m_id;
  Vector2D m_acceleration{0, 0};
  Vector2D m_velocity{0, 0};
  Vector2D m_position{0, 0};
  int m_width{0};
  int m_height{0};
  std::string m_textureID{};
  int m_currentFrame{0};
  int m_currentRow{0};
  int m_numFrames{0};
  int m_animSpeed{0};
};
#endif  // ENTITY_HPP
