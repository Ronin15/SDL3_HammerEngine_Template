/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef CHASE_BEHAVIOR_HPP
#define CHASE_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "utils/Vector2D.hpp"


class ChaseBehavior : public AIBehavior {
public:
    ChaseBehavior(EntityPtr target = nullptr, float chaseSpeed = 3.0f, float maxRange = 500.0f, float minRange = 50.0f);

    void init(EntityPtr entity) override;

    void update(EntityPtr entity) override;

    void clean(EntityPtr entity) override;

    void onMessage(EntityPtr entity, const std::string& message) override;

    std::string getName() const override;

    // Set a new target to chase
    void setTarget(EntityPtr target);

    // Get current target
    EntityPtr getTarget() const;

    // Set chase parameters
    void setChaseSpeed(float speed);
    void setMaxRange(float range);
    void setMinRange(float range);

    // Get state information
    bool isChasing() const;
    bool hasLineOfSight() const;

protected:
    // Called when target is reached (within minimum range)
    virtual void onTargetReached(EntityPtr entity);

    // Called when target is lost (out of max range)
    virtual void onTargetLost(EntityPtr entity);

private:
    // Weak pointer to the target entity
    // The target entity is owned elsewhere in the application
    EntityWeakPtr m_targetWeak{};
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
    bool checkLineOfSight(EntityPtr entity, EntityPtr target);

    // Handle behavior when line of sight is lost
    void handleNoLineOfSight(EntityPtr entity);
};

#endif // CHASE_BEHAVIOR_HPP
