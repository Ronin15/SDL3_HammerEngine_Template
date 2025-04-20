#ifndef ENTITY_HPP
#define ENTITY_HPP

#include <SDL3/SDL.h>
#include "Vector2D.hpp"
#include <string>

class Entity {
 public:
   virtual void update() = 0;
   virtual void render() = 0;
   virtual void clean() = 0;
   virtual ~Entity() = default;

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
