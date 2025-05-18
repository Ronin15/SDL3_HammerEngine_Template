/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef WANDER_BEHAVIOR_HPP
#define WANDER_BEHAVIOR_HPP

#include "AIBehavior.hpp"
#include "Vector2D.hpp"
#include <cmath>
#include <random>
#include <SDL3/SDL.h>

class WanderBehavior : public AIBehavior {
public:
    WanderBehavior(float speed = 1.5f, float changeDirectionInterval = 2000.0f, float areaRadius = 300.0f)
        : m_speed(speed), m_changeDirectionInterval(changeDirectionInterval), m_areaRadius(areaRadius) {
        // Initialize random number generator
        std::random_device rd;
        m_rng = std::mt19937(rd());
        m_angleDistribution = std::uniform_real_distribution<float>(0.0f, 2.0f * M_PI);
    }

    void init(Entity* entity) override {
        if (!entity) return;

        // Store initial position as center point
        m_centerPoint = entity->getPosition();

        // Record start time for direction changes
        m_lastDirectionChangeTime = SDL_GetTicks();

        // Set initial random direction
        chooseNewDirection(entity);
    }

    void update(Entity* entity) override {
        if (!entity || !m_active) return;

        // Get current time
        Uint64 currentTime = SDL_GetTicks();

        // Check if it's time to change direction
        if (currentTime - m_lastDirectionChangeTime > m_changeDirectionInterval) {
            chooseNewDirection(entity);
            m_lastDirectionChangeTime = currentTime;
        }

        // Check if entity is moving out of the allowed area
        Vector2D position = entity->getPosition();
        Vector2D toCenter = m_centerPoint - position;
        float distanceFromCenter = toCenter.length();

        // If too far from center, adjust direction to return
        if (distanceFromCenter > m_areaRadius) {
            // Calculate direction back to center
            Vector2D returnDirection = toCenter;
            returnDirection.normalize();

            // Blend current direction with return direction
            float blendFactor = (distanceFromCenter - m_areaRadius) / 50.0f;
            blendFactor = std::min(blendFactor, 1.0f);

            m_currentDirection = (m_currentDirection * (1.0f - blendFactor)) +
                                (returnDirection * blendFactor);
            m_currentDirection.normalize();

            // Apply the new direction
            entity->setVelocity(m_currentDirection * m_speed);
        }

        // Apply current direction and handle sprite flipping
        if (m_currentDirection.getX() < 0) {
            entity->setFlip(SDL_FLIP_HORIZONTAL);
        } else if (m_currentDirection.getX() > 0) {
            entity->setFlip(SDL_FLIP_NONE);
        }
    }

    void clean(Entity* entity) override {
        if (!entity) return;

        // Stop the entity when behavior is cleaned up
        entity->setVelocity(Vector2D(0, 0));
    }

    void onMessage(Entity* entity, const std::string& message) override {
        if (!entity) return;

        if (message == "pause") {
            setActive(false);
            entity->setVelocity(Vector2D(0, 0));
        } else if (message == "resume") {
            setActive(true);
            chooseNewDirection(entity);
        } else if (message == "new_direction") {
            chooseNewDirection(entity);
        } else if (message == "increase_speed") {
            m_speed *= 1.5f;
            if (entity && m_active) {
                entity->setVelocity(m_currentDirection * m_speed);
            }
        } else if (message == "decrease_speed") {
            m_speed *= 0.75f;
            if (entity && m_active) {
                entity->setVelocity(m_currentDirection * m_speed);
            }
        }
    }

    std::string getName() const override {
        return "Wander";
    }

    // Set a new center point for wandering
    void setCenterPoint(const Vector2D& centerPoint) {
        m_centerPoint = centerPoint;
    }

    // Set the area radius for wandering
    void setAreaRadius(float radius) {
        m_areaRadius = radius;
    }

    // Set the speed of movement
    void setSpeed(float speed) {
        m_speed = speed;
    }

    // Set how often the direction changes
    void setChangeDirectionInterval(float interval) {
        m_changeDirectionInterval = interval;
    }

private:
    float m_speed{1.5f};
    float m_changeDirectionInterval{2000.0f}; // milliseconds
    float m_areaRadius{300.0f};
    Vector2D m_centerPoint{0, 0};
    Vector2D m_currentDirection{0, 0};
    Uint64 m_lastDirectionChangeTime{0};

    // Random number generation
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_angleDistribution;

    // Choose a new random direction for the entity
    void chooseNewDirection(Entity* entity) {
        if (!entity) return;

        // Generate a random angle
        float angle = m_angleDistribution(m_rng);

        // Convert angle to direction vector
        float x = std::cos(angle);
        float y = std::sin(angle);

        // Set the new direction
        m_currentDirection = Vector2D(x, y);

        // Apply the new direction to the entity
        entity->setVelocity(m_currentDirection * m_speed);

        // Update flip direction based on movement
        if (x < 0) {
            entity->setFlip(SDL_FLIP_HORIZONTAL);
        } else {
            entity->setFlip(SDL_FLIP_NONE);
        }
    }
};

#endif // WANDER_BEHAVIOR_HPP
