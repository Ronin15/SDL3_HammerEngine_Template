/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef AI_BEHAVIOR_HPP
#define AI_BEHAVIOR_HPP

#include "Entity.hpp"
#include <string>

class AIBehavior {
public:
    virtual ~AIBehavior() = default;
    
    // Core behavior methods
    virtual void update(Entity* entity) = 0;
    virtual void init(Entity* entity) = 0;
    virtual void clean(Entity* entity) = 0;
    
    // Behavior identification
    virtual std::string getName() const = 0;
    
    // Optional message handling for behavior communication
    virtual void onMessage(Entity* entity, const std::string& message) { (void)entity; (void)message; }
    
    // Behavior state access
    virtual bool isActive() const { return m_active; }
    virtual void setActive(bool active) { m_active = active; }
    
    // Priority handling for behavior selection
    virtual int getPriority() const { return m_priority; }
    virtual void setPriority(int priority) { m_priority = priority; }

protected:
    bool m_active{true};
    int m_priority{0};  // Higher values = higher priority
};

#endif // AI_BEHAVIOR_HPP