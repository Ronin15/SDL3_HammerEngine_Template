/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef MOCK_PLAYER_HPP
#define MOCK_PLAYER_HPP

#include "Vector2D.hpp"
#include <string>
#include <SDL3/SDL.h>

// A completely independent mock player for testing SaveGameManager
class MockPlayer {
public:
    MockPlayer() : 
        m_position(100.0f, 200.0f),
        m_velocity(0.0f, 0.0f),
        m_textureID("mock_player"),
        m_currentStateName("idle") {}
    
    // Methods needed by SaveGameManager
    Vector2D getPosition() const { return m_position; }
    Vector2D getVelocity() const { return m_velocity; }
    std::string getTextureID() const { return m_textureID; }
    std::string getCurrentStateName() const { return m_currentStateName; }
    
    // Methods to set state
    void setPosition(const Vector2D& position) { m_position = position; }
    void setVelocity(const Vector2D& velocity) { m_velocity = velocity; }
    void changeState(const std::string& stateName) { m_currentStateName = stateName; }
    
    // Test helper methods
    void setTestPosition(float x, float y) { m_position = Vector2D(x, y); }
    void setTestTextureID(const std::string& id) { m_textureID = id; }
    void setTestState(const std::string& state) { m_currentStateName = state; }
    
private:
    Vector2D m_position;
    Vector2D m_velocity;
    std::string m_textureID;
    std::string m_currentStateName;
};

#endif // MOCK_PLAYER_HPP