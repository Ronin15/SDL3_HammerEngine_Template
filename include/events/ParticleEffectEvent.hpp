/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef PARTICLE_EFFECT_EVENT_HPP
#define PARTICLE_EFFECT_EVENT_HPP

/**
 * @file ParticleEffectEvent.hpp
 * @brief Event for triggering position-based particle effects through the EventManager
 *
 * This event type allows the EventManager to control particle effects with coordinates
 * while maintaining clean architectural boundaries between the event and particle systems.
 * 
 * Features:
 * - Position-based effect triggering
 * - Intensity control
 * - Duration settings
 * - Group tagging for batch operations
 * - Sound effect integration
 * - Proper separation of concerns
 */

#include "events/Event.hpp"
#include "utils/Vector2D.hpp"
#include <string>

/**
 * @brief Event for triggering particle effects at specific coordinates
 * 
 * This event maintains clean separation between EventManager and ParticleManager
 * while allowing coordinate-based particle effect control through the event system.
 */
class ParticleEffectEvent : public Event {
public:
    /**
     * @brief Construct a new Particle Effect Event
     * @param name Event name/identifier
     * @param effectName Name of the particle effect to trigger
     * @param position World position to spawn the effect
     * @param intensity Effect intensity multiplier (0.0 to 2.0+)
     * @param duration Effect duration in seconds (-1 for infinite)
     * @param groupTag Optional group tag for batch operations
     * @param soundEffect Optional sound effect name
     */
    ParticleEffectEvent(const std::string& name, 
                       const std::string& effectName,
                       const Vector2D& position, 
                       float intensity = 1.0f,
                       float duration = -1.0f,
                       const std::string& groupTag = "",
                       const std::string& soundEffect = "");

    /**
     * @brief Construct with separate x,y coordinates
     * @param name Event name/identifier
     * @param effectName Name of the particle effect to trigger
     * @param x X coordinate
     * @param y Y coordinate
     * @param intensity Effect intensity multiplier (0.0 to 2.0+)
     * @param duration Effect duration in seconds (-1 for infinite)
     * @param groupTag Optional group tag for batch operations
     * @param soundEffect Optional sound effect name
     */
    ParticleEffectEvent(const std::string& name,
                       const std::string& effectName,
                       float x, float y,
                       float intensity = 1.0f,
                       float duration = -1.0f,
                       const std::string& groupTag = "",
                       const std::string& soundEffect = "");

    virtual ~ParticleEffectEvent() = default;

    // Core Event interface
    void update() override;
    void execute() override;
    void reset() override;
    void clean() override;

    // Event identification
    std::string getName() const override { return m_name; }
    std::string getType() const override { return "ParticleEffect"; }

    // Condition checking
    bool checkConditions() override;

    // Particle effect specific methods
    /**
     * @brief Set the effect position
     * @param position New position for the effect
     */
    void setPosition(const Vector2D& position) { m_position = position; }
    
    /**
     * @brief Set the effect position with separate coordinates
     * @param x X coordinate
     * @param y Y coordinate
     */
    void setPosition(float x, float y) { m_position = Vector2D(x, y); }
    
    /**
     * @brief Get the current effect position
     * @return Current position
     */
    Vector2D getPosition() const { return m_position; }
    
    /**
     * @brief Set the effect intensity
     * @param intensity New intensity (0.0 to 2.0+)
     */
    void setIntensity(float intensity) { m_intensity = intensity; }
    
    /**
     * @brief Get the current effect intensity
     * @return Current intensity
     */
    float getIntensity() const { return m_intensity; }
    
    /**
     * @brief Set the effect duration
     * @param duration Duration in seconds (-1 for infinite)
     */
    void setDuration(float duration) { m_duration = duration; }
    
    /**
     * @brief Get the effect duration
     * @return Duration in seconds
     */
    float getDuration() const { return m_duration; }
    
    /**
     * @brief Set the group tag for batch operations
     * @param groupTag Group tag string
     */
    void setGroupTag(const std::string& groupTag) { m_groupTag = groupTag; }
    
    /**
     * @brief Get the group tag
     * @return Group tag string
     */
    std::string getGroupTag() const { return m_groupTag; }
    
    /**
     * @brief Get the effect name
     * @return Effect name string
     */
    std::string getEffectName() const { return m_effectName; }
    
    /**
     * @brief Check if this effect is currently playing
     * @return true if effect is active, false otherwise
     */
    bool isEffectActive() const { return m_effectId != 0; }
    
    /**
     * @brief Stop the effect if it's currently playing
     */
    void stopEffect();

private:
    std::string m_name;           // Event name
    std::string m_effectName;     // Particle effect name
    Vector2D m_position;          // Effect position
    float m_intensity;            // Effect intensity
    float m_duration;             // Effect duration (-1 = infinite)
    std::string m_groupTag;       // Group tag for batch operations
    std::string m_soundEffect;    // Optional sound effect
    
    uint32_t m_effectId;          // ID of spawned effect (0 = not spawned)
    bool m_hasExecuted;           // Execution tracking
};

#endif // PARTICLE_EFFECT_EVENT_HPP
