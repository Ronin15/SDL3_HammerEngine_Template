/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef CHASE_BEHAVIOR_HPP
#define CHASE_BEHAVIOR_HPP

#include "AIBehavior.hpp"
#include "Vector2D.hpp"


class ChaseBehavior : public AIBehavior {
public:
    ChaseBehavior(Entity* target = nullptr, float chaseSpeed = 3.0f, float maxRange = 500.0f, float minRange = 50.0f)
        : m_target(target), m_chaseSpeed(chaseSpeed), m_maxRange(maxRange), m_minRange(minRange),
          m_isChasing(false), m_hasLineOfSight(false), m_lastKnownTargetPos(0, 0), m_timeWithoutSight(0),
          m_currentDirection(0, 0) {
    }

    void init(Entity* entity) override {
        if (!entity) return;

        // Initialize the entity's state for chasing
        m_isChasing = false;
        m_hasLineOfSight = false;

        // If we have a target, check if it's in range
        if (m_target) {
            Vector2D entityPos = entity->getPosition();
            Vector2D targetPos = m_target->getPosition();
            float distance = (targetPos - entityPos).length();

            m_isChasing = (distance <= m_maxRange);
            m_hasLineOfSight = checkLineOfSight(entity, m_target);
        }
    }

    void update(Entity* entity) override {
        if (!entity || !m_active || !m_target) {
            return;
        }

        // Get positions
        Vector2D entityPos = entity->getPosition();
        Vector2D targetPos = m_target->getPosition();

        // Calculate distance to target and ensure we have a valid target position
        Vector2D toTarget = targetPos - entityPos;
        float distance = toTarget.length();

        // Store current target position for debugging
        m_lastKnownTargetPos = targetPos;

        // Check if target is in chase range
        if (distance <= m_maxRange) {
            // Check line of sight (simplified)
            m_hasLineOfSight = checkLineOfSight(entity, m_target);

            if (m_hasLineOfSight) {
                m_isChasing = true;

                // If not too close, move toward target
                if (distance > m_minRange) {
                    // Normalize the direction
                    Vector2D direction = toTarget * (1.0f / distance);

                    // Set velocity toward target with consistent speed
                    Vector2D newVelocity = direction * m_chaseSpeed;
                    entity->setVelocity(newVelocity);


                    // Handle sprite flipping for visual direction
                    if (direction.getX() < 0) {
                        entity->setFlip(SDL_FLIP_HORIZONTAL);
                    } else if (direction.getX() > 0) {
                        entity->setFlip(SDL_FLIP_NONE);
                    }
                } else {
                    // Target is within minimum range, stop moving
                    entity->setVelocity(Vector2D(0, 0));

                    onTargetReached(entity);
                }
            } else {
                // Lost line of sight
                if (m_isChasing) {
                    // Continue to last known position for a while
                    m_lastKnownTargetPos = targetPos;
                    m_timeWithoutSight = 0;
                }

                handleNoLineOfSight(entity);
            }
        } else {
            // Target out of range, stop chasing
            if (m_isChasing) {
                m_isChasing = false;
                entity->setVelocity(Vector2D(0, 0));

                onTargetLost(entity);
            }
        }
    }

    void clean(Entity* entity) override {
        if (!entity) return;

        // Stop the entity's movement when cleaning up
        entity->setVelocity(Vector2D(0, 0));
        m_isChasing = false;
    }

    void onMessage(Entity* entity, const std::string& message) override {
        if (message == "pause") {
            setActive(false);
            if (entity) {
                entity->setVelocity(Vector2D(0, 0));
            }
        } else if (message == "resume") {
            setActive(true);
            // Reinitialize chase state when resuming
            if (entity && m_target) {
                init(entity);
            }
        } else if (message == "lose_target") {
            m_isChasing = false;
            m_hasLineOfSight = false;
            if (entity) {
                entity->setVelocity(Vector2D(0, 0));
            }
        }
    }

    std::string getName() const override {
        return "Chase";
    }

    // Set a new target to chase
    void setTarget(Entity* target) {
        m_target = target;
    }

    // Get current target
    Entity* getTarget() const {
        return m_target;
    }

    // Set chase parameters
    void setChaseSpeed(float speed) { m_chaseSpeed = speed; }
    void setMaxRange(float range) { m_maxRange = range; }
    void setMinRange(float range) { m_minRange = range; }

    // Get state information
    bool isChasing() const { return m_isChasing; }
    bool hasLineOfSight() const { return m_hasLineOfSight; }

protected:
    // Called when target is reached (within minimum range)
    virtual void onTargetReached(Entity* entity) {
        // Base implementation does nothing
        // Override in derived behaviors for specific actions
        (void)entity; // Mark parameter as intentionally unused
    }

    // Called when target is lost (out of max range)
    virtual void onTargetLost(Entity* entity) {
        // Base implementation does nothing
        // Override in derived behaviors for specific actions
        (void)entity; // Mark parameter as intentionally unused
    }

private:
    Entity* m_target{nullptr};
    float m_chaseSpeed{10.0f};  // Increased to 10.0 for very visible movement
    float m_maxRange{1000.0f};  // Maximum distance to chase target - increased to 1000
    float m_minRange{50.0f};   // Minimum distance to maintain from target

    bool m_isChasing{false};
    bool m_hasLineOfSight{false};
    Vector2D m_lastKnownTargetPos{0, 0};
    int m_timeWithoutSight{0};
    const int m_maxTimeWithoutSight{60}; // Frames to chase last known position
    Vector2D m_currentDirection{0, 0};

    // Check if entity has line of sight to target (simplified)
    bool checkLineOfSight(Entity* entity, Entity* target) {
        // For a more complex implementation, you would do raycasting here
        // This simplified version just checks distance
        if (!entity || !target) return false;

        Vector2D entityPos = entity->getPosition();
        Vector2D targetPos = target->getPosition();
        float distance = (targetPos - entityPos).length();

        // Always report in range for testing purposes
        // Simple line of sight check - would be replaced with raycasting in a real game
        return distance <= m_maxRange;
    }

    // Handle behavior when line of sight is lost
    void handleNoLineOfSight(Entity* entity) {
        if (m_timeWithoutSight < m_maxTimeWithoutSight) {
            // Move toward last known position for a while
            Vector2D entityPos = entity->getPosition();
            Vector2D toLastKnown = m_lastKnownTargetPos - entityPos;
            float distance = toLastKnown.length();

            if (distance > 10.0f) {
                // Still moving to last known position
                Vector2D direction = toLastKnown * (1.0f / distance);

                // Use a slightly slower speed when tracking last known position
                Vector2D newVelocity = direction * (m_chaseSpeed * 0.8f);
                entity->setVelocity(newVelocity);

                // Handle sprite flipping
                if (direction.getX() < 0) {
                    entity->setFlip(SDL_FLIP_HORIZONTAL);
                } else if (direction.getX() > 0) {
                    entity->setFlip(SDL_FLIP_NONE);
                }
            } else {
                // Reached last known position, stop
                entity->setVelocity(Vector2D(0, 0));

            }

            m_timeWithoutSight++;
        } else {
            // Give up chase after timeout
            m_isChasing = false;
            entity->setVelocity(Vector2D(0, 0));

            onTargetLost(entity);
        }
    }
};

#endif // CHASE_BEHAVIOR_HPP
