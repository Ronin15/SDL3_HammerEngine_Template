/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef MOCK_NPC_HPP
#define MOCK_NPC_HPP

#include "entities/Entity.hpp"
#include "utils/Vector2D.hpp"
#include <memory>
#include <string>

/**
 * @brief Mock NPC class for testing NPCSpawnEvent without full game dependencies
 */
class MockNPC : public Entity {
public:
    MockNPC(const std::string& textureID, const Vector2D& position, int width, int height)
        : m_wanderArea{0.0f, 0.0f, 0.0f, 0.0f}
        , m_boundsCheckEnabled(false) {
        setTextureID(textureID);
        setPosition(position);
        setWidth(width);
        setHeight(height);
    }
    
    virtual ~MockNPC() = default;

    // Mock methods that NPCSpawnEvent uses
    void setWanderArea(float x1, float y1, float x2, float y2) {
        m_wanderArea.x1 = x1;
        m_wanderArea.y1 = y1;
        m_wanderArea.x2 = x2;
        m_wanderArea.y2 = y2;
    }
    
    void setBoundsCheckEnabled(bool enabled) {
        m_boundsCheckEnabled = enabled;
    }
    
    bool getBoundsCheckEnabled() const {
        return m_boundsCheckEnabled;
    }
    
    struct WanderArea {
        float x1, y1, x2, y2;
    };
    
    const WanderArea& getWanderArea() const {
        return m_wanderArea;
    }

    // Static factory method like the real NPC class
    static std::shared_ptr<MockNPC> create(const std::string& textureID, const Vector2D& position, int width, int height) {
        return std::make_shared<MockNPC>(textureID, position, width, height);
    }

    // Override Entity methods for testing
    void update(float deltaTime) override {
        // Mock update - do nothing
        (void)deltaTime; // Suppress unused parameter warning
    }
    
    void render(double alpha) override {
        // Mock render - do nothing
        (void)alpha; // Suppress unused parameter warning
    }
    
    void clean() override {
        // Mock clean - do nothing
    }

private:
    WanderArea m_wanderArea;
    bool m_boundsCheckEnabled;
};

// Define NPC as an alias to MockNPC for testing
using NPC = MockNPC;

#endif // MOCK_NPC_HPP