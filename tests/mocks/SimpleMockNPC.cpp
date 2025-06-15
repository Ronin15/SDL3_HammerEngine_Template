/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

// Simple mock NPC class to provide typeinfo for dynamic_cast operations
// This avoids header conflicts by defining NPC directly in the source file

#include "entities/Entity.hpp"
#include "utils/Vector2D.hpp"
#include <string>

// Forward declare to avoid conflicts
class NPC : public Entity {
public:
    NPC() : m_boundsCheckEnabled(false) {
        setPosition(Vector2D(0, 0));
        setTextureID("");
        setWidth(64);
        setHeight(64);
    }
    
    virtual ~NPC() = default;

    // Essential Entity methods
    void update(float deltaTime) override { 
        // Simple physics update for tests
        Vector2D pos = getPosition();
        Vector2D vel = getVelocity();
        setPosition(pos + (vel * deltaTime));
    }
    
    void render() override { /* No rendering in tests */ }
    void clean() override { /* No cleanup needed */ }

    // NPC-specific methods that PatrolBehavior uses
    void setBoundsCheckEnabled(bool enabled) { m_boundsCheckEnabled = enabled; }
    bool getBoundsCheckEnabled() const { return m_boundsCheckEnabled; }
    
    void setWanderArea(float x1, float y1, float x2, float y2) {
        // Mock implementation - just silence warnings
        (void)x1; (void)y1; (void)x2; (void)y2;
    }

private:
    bool m_boundsCheckEnabled;
};

// Create a global instance to ensure typeinfo is available
static NPC g_mockNPCInstance;