/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef WANDER_BEHAVIOR_HPP
#define WANDER_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "utils/Vector2D.hpp"

#include <random>
#include <boost/container/flat_map.hpp>
#include <SDL3/SDL.h>

class WanderBehavior : public AIBehavior {
public:
    WanderBehavior(float speed = 1.5f, float changeDirectionInterval = 2000.0f, float areaRadius = 300.0f);

    // No state management - handled by AI Manager
    void init(Entity* entity) override;
    void update(Entity* entity) override;
    void clean(Entity* entity) override;
    void onMessage(Entity* entity, const std::string& message) override;
    std::string getName() const override;

    // Set a new center point for wandering
    void setCenterPoint(const Vector2D& centerPoint);

    // Set the area radius for wandering
    void setAreaRadius(float radius);

    // Set the speed of movement
    void setSpeed(float speed);

    // Set how often the direction changes
    void setChangeDirectionInterval(float interval);

    // Set screen dimensions directly (more accurate than estimating from center point)
    void setScreenDimensions(float width, float height);

    // Set the probability of wandering offscreen
    void setOffscreenProbability(float probability);

private:
    // Entity-specific state data
    struct EntityState {
        Vector2D currentDirection{0, 0};
        Uint64 lastDirectionChangeTime{0};
        bool currentlyWanderingOffscreen{false};
        bool resetScheduled{false};
        Uint64 lastDirectionFlip{0};
        Uint64 startDelay{0};           // Random delay before entity starts moving
        bool movementStarted{false};    // Flag to track if movement has started
    };

    // Map to store per-entity state
    boost::container::flat_map<Entity*, EntityState> m_entityStates;

    // Shared behavior parameters
    float m_speed{1.5f};
    float m_changeDirectionInterval{2000.0f}; // milliseconds
    float m_areaRadius{300.0f};
    Vector2D m_centerPoint{0, 0};

    // Screen dimensions - defaults that will be updated in setCenterPoint
    float m_screenWidth{1280.0f};
    float m_screenHeight{720.0f};

    // Offscreen wandering properties
    float m_offscreenProbability{0.15f}; // 15% chance to wander offscreen when changing direction

    // Flip stability properties
    Uint64 m_minimumFlipInterval{400}; // Minimum time between flips (milliseconds)

    // Random number generation
    std::mt19937 m_rng{std::random_device{}()};
    std::uniform_real_distribution<float> m_angleDistribution{0.0f, 2.0f * M_PI};
    std::uniform_real_distribution<float> m_wanderOffscreenChance{0.0f, 1.0f};

    // Check if entity is well off screen (completely out of view)
    bool isWellOffscreen(const Vector2D& position) const;

    // Reset entity to a new position on the opposite side of the screen
    void resetEntityPosition(Entity* entity);

    // Choose a new random direction for the entity
    void chooseNewDirection(Entity* entity, bool wanderOffscreen = false);
};

#endif // WANDER_BEHAVIOR_HPP
