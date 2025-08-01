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
    void render(double alpha) override { (void)alpha; /* Mock implementation */ }
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
    
    // Implement ISerializable interface
    bool serialize(std::ostream& stream) const override {
        // Serialize position using Entity's getter
        Vector2D pos = getPosition();
        if (!pos.serialize(stream)) {
            return false;
        }
        
        // Serialize velocity using Entity's getter
        Vector2D vel = getVelocity();
        if (!vel.serialize(stream)) {
            return false;
        }
        
        // Serialize textureID string using Entity's getter
        const std::string& textureID = getTextureID();
        uint32_t textureIDLength = static_cast<uint32_t>(textureID.length());
        stream.write(reinterpret_cast<const char*>(&textureIDLength), sizeof(uint32_t));
        if (textureIDLength > 0) {
            stream.write(textureID.c_str(), textureIDLength);
        }
        
        // Serialize currentStateName string
        uint32_t stateNameLength = static_cast<uint32_t>(m_currentStateName.length());
        stream.write(reinterpret_cast<const char*>(&stateNameLength), sizeof(uint32_t));
        if (stateNameLength > 0) {
            stream.write(m_currentStateName.c_str(), stateNameLength);
        }
        
        return stream.good();
    }
    
    bool deserialize(std::istream& stream) override {
        // Deserialize position
        Vector2D pos;
        if (!pos.deserialize(stream)) {
            return false;
        }
        setPosition(pos);
        
        // Deserialize velocity
        Vector2D vel;
        if (!vel.deserialize(stream)) {
            return false;
        }
        setVelocity(vel);
        
        // Deserialize textureID string
        uint32_t textureIDLength;
        stream.read(reinterpret_cast<char*>(&textureIDLength), sizeof(uint32_t));
        if (!stream.good()) return false;
        
        std::string textureID;
        if (textureIDLength == 0) {
            textureID.clear();
        } else {
            textureID.resize(textureIDLength);
            stream.read(&textureID[0], textureIDLength);
            if (stream.gcount() != static_cast<std::streamsize>(textureIDLength)) return false;
        }
        setTextureID(textureID);
        
        // Deserialize currentStateName string
        uint32_t stateNameLength;
        stream.read(reinterpret_cast<char*>(&stateNameLength), sizeof(uint32_t));
        if (!stream.good()) return false;
        
        if (stateNameLength == 0) {
            m_currentStateName.clear();
        } else {
            m_currentStateName.resize(stateNameLength);
            stream.read(&m_currentStateName[0], stateNameLength);
            if (stream.gcount() != static_cast<std::streamsize>(stateNameLength)) return false;
        }
        
        return stream.good();
    }
    
private:
    std::string m_currentStateName;
};

#endif // MOCK_PLAYER_HPP