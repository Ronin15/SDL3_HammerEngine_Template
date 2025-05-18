/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef ENTITY_HPP
#define ENTITY_HPP

#include "Vector2D.hpp"
#include <string>
#include <SDL3/SDL.h>

class Entity {
 public:
   virtual void update() = 0;
   virtual void render() = 0;
   virtual void clean() = 0;
   virtual ~Entity() = default;

   // Accessor methods
   Vector2D getPosition() const { return m_position; }
   Vector2D getVelocity() const { return m_velocity; }
   Vector2D getAcceleration() const { return m_acceleration; }
   int getWidth() const { return m_width; }
   int getHeight() const { return m_height; }
   std::string getTextureID() const { return m_textureID; }
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

   protected:
    Vector2D m_acceleration{0, 0};
    Vector2D m_velocity{0, 0};
    Vector2D m_position{0, 0};
    int m_width{0};
    int m_height{0};
    std::string m_textureID{0};
    int m_currentFrame{0};
    int m_currentRow{0};
    int m_numFrames{0};
    int m_animSpeed{0};
};
#endif  // ENTITY_HPP
