/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef MOCK_PLAYER_HPP
#define MOCK_PLAYER_HPP

#include "utils/Vector2D.hpp"
#include "utils/BinarySerializer.hpp"
#include "entities/Entity.hpp"
#include <string>
#include <memory>
#include <SDL3/SDL.h>

// Forward declaration
namespace HammerEngine {
    class Camera;
}

// A mock player that extends Entity for testing SaveGameManager
class MockPlayer : public Entity, public ISerializable {
public:
    MockPlayer() : m_currentStateName("idle") {
        setPosition(Vector2D(100.0f, 200.0f));
        setVelocity(Vector2D(0.0f, 0.0f));
        setTextureID("mock_player");
    }
        
    // Required Entity interface implementations
    void update(float deltaTime) override { (void)deltaTime; /* Mock implementation */ }
    void render(const HammerEngine::Camera* camera) override { (void)camera; /* Mock implementation */ }
    void clean() override { /* Mock implementation */ }
    
    // Factory method for proper creation with shared_ptr
    static std::shared_ptr<MockPlayer> create() {
        return std::make_shared<MockPlayer>();
    }
    
    // Player-specific methods needed by SaveGameManager
    std::string getCurrentStateName() const { return m_currentStateName; }
    void changeState(const std::string& stateName) { m_currentStateName = stateName; }
    
    // Test helper methods
    void setTestPosition(float x, float y) { setPosition(Vector2D(x, y)); }
    void setTestTextureID(const std::string& id) { setTextureID(id); }
    void setTestState(const std::string& state) { m_currentStateName = state; }
    
    // Declare serializable interface using BinarySerializer macros
    DECLARE_SERIALIZABLE()
    
private:
    std::string m_currentStateName;
};

#endif // MOCK_PLAYER_HPP