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
#include <unordered_map>

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
 * @brief Animation configuration for sprite sheet handling
 * Unified struct used by NPC and Player for named animations
 */
struct AnimationConfig {
    int row;           // Sprite sheet row (0-based, converted to 1-based in playAnimation)
    int frameCount;    // Number of frames in animation
    int speed;         // Milliseconds per frame
    bool loop;         // Whether animation loops or plays once

    AnimationConfig() : row(0), frameCount(1), speed(100), loop(true) {}
    AnimationConfig(int r, int fc, int s, bool l)
        : row(r), frameCount(fc), speed(s), loop(l) {}
};

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
   * @brief Render the entity with interpolation support.
   *
   * This method is called once per frame for each entity. It should
   * use getInterpolatedPosition(interpolationAlpha) for smooth rendering
   * between fixed timestep updates.
   *
   * @param renderer SDL renderer from GameState render flow
   * @param cameraX Interpolated camera X offset (from GameState render)
   * @param cameraY Interpolated camera Y offset (from GameState render)
   * @param interpolationAlpha Blend factor between previous and current position (0.0-1.0)
   */
  virtual void render(SDL_Renderer* renderer, float cameraX, float cameraY, float interpolationAlpha = 1.0f) = 0;

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
  Vector2D getPreviousPosition() const { return m_previousPosition; }
  Vector2D getVelocity() const { return m_velocity; }
  Vector2D getAcceleration() const { return m_acceleration; }

  /**
   * @brief Get interpolated position for smooth rendering.
   *
   * Uses linear interpolation between previous and current position
   * based on the interpolation alpha from the game loop.
   *
   * Note: With the single-threaded main loop (update completes before render),
   * this is now a simple calculation without atomics.
   *
   * @param alpha Interpolation factor (0.0 = previous position, 1.0 = current position)
   * @return Interpolated position for rendering
   */
  Vector2D getInterpolatedPosition(float alpha) const {
    return Vector2D(
      m_previousPosition.getX() + (m_position.getX() - m_previousPosition.getX()) * alpha,
      m_previousPosition.getY() + (m_position.getY() - m_previousPosition.getY()) * alpha);
  }

  /**
   * @brief Store current position for interpolation before updating.
   *
   * Call this at the START of update() before modifying m_position.
   * This enables smooth rendering interpolation between fixed timestep updates.
   */
  void storePositionForInterpolation() { m_previousPosition = m_position; }

  /**
   * @brief Update position from movement (preserves interpolation state).
   *
   * Use this for smooth movement updates (physics integration, AI movement).
   * Unlike setPosition(), this does NOT reset m_previousPosition.
   * Call storePositionForInterpolation() before this each frame.
   */
  void updatePositionFromMovement(const Vector2D& position) { m_position = position; }

  int getWidth() const { return m_width; }
  int getHeight() const { return m_height; }
  const std::string& getTextureID() const { return m_textureID; }
  int getCurrentFrame() const { return m_currentFrame; }
  int getCurrentRow() const { return m_currentRow; }
  int getNumFrames() const { return m_numFrames; }
  int getAnimSpeed() const { return m_animSpeed; }
  float getAnimationAccumulator() const { return m_animationAccumulator; }
  const std::string& getCurrentAnimationName() const { return m_currentAnimationName; }

  // Setter methods

  /**
   * @brief Set entity position directly (teleport).
   *
   * This resets both current and previous position to prevent
   * interpolation artifacts when teleporting/spawning.
   */
  virtual void setPosition(const Vector2D& position) {
    m_position = position;
    m_previousPosition = position;  // Prevents interpolation sliding
  }
  virtual void setVelocity(const Vector2D& velocity) { m_velocity = velocity; }
  virtual void setAcceleration(const Vector2D& acceleration) { m_acceleration = acceleration; }
  virtual void setWidth(int width) { m_width = width; }
  virtual void setHeight(int height) { m_height = height; }
  virtual void setTextureID(const std::string& id) { m_textureID = id; }
  virtual void setCurrentFrame(int frame) { m_currentFrame = frame; }
  virtual void setCurrentRow(int row) { m_currentRow = row; }
  virtual void setNumFrames(int numFrames) { m_numFrames = numFrames; }
  virtual void setAnimSpeed(int speed) { m_animSpeed = speed; }
  virtual void setAnimationAccumulator(float acc) { m_animationAccumulator = acc; }

  // Used for rendering flipping - to be implemented by derived classes
  virtual void setFlip(SDL_FlipMode flip) { (void)flip; /* Unused in base class */ }
  virtual SDL_FlipMode getFlip() const { return SDL_FLIP_NONE; }

  /**
   * @brief Play a named animation from the animation map
   *
   * Looks up the animation config by name and sets the sprite sheet row,
   * frame count, animation speed, and loop flag. Does nothing if animation
   * name is not found in the map.
   *
   * @param animName The name of the animation (e.g., "idle", "walking", "attacking")
   */
  virtual void playAnimation(const std::string& animName);

  /**
   * @brief Initialize the animation map with named animations
   *
   * Override in derived classes to populate m_animationMap with
   * animation configurations specific to that entity type.
   */
  virtual void initializeAnimationMap() {}

 protected:
  const EntityID m_id;
  Vector2D m_acceleration{0, 0};
  Vector2D m_velocity{0, 0};
  Vector2D m_position{0, 0};
  Vector2D m_previousPosition{0, 0};  // For render interpolation
  int m_width{0};
  int m_height{0};
  std::string m_textureID{};
  int m_currentFrame{0};
  int m_currentRow{0};
  int m_numFrames{0};
  int m_animSpeed{0};

  // Animation abstraction - maps animation names to sprite sheet configurations
  std::unordered_map<std::string, AnimationConfig> m_animationMap;
  bool m_animationLoops{true};  // Whether current animation loops or plays once

  // Animation timing - uses deltaTime accumulation for synchronized timing with physics
  std::string m_currentAnimationName;      // Current animation name (for skip-if-same optimization)
  float m_animationAccumulator{0.0f};      // Accumulates deltaTime for frame advancement
};
#endif  // ENTITY_HPP
