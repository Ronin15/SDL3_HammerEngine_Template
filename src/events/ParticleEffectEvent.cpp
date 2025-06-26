/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "events/ParticleEffectEvent.hpp"
#include "managers/ParticleManager.hpp"
#include "managers/SoundManager.hpp"
#include <iostream>

ParticleEffectEvent::ParticleEffectEvent(const std::string& name, 
                                       const std::string& effectName,
                                       const Vector2D& position, 
                                       float intensity,
                                       float duration,
                                       const std::string& groupTag,
                                       const std::string& soundEffect)
    : m_name(name)
    , m_effectName(effectName)
    , m_position(position)
    , m_intensity(intensity)
    , m_duration(duration)
    , m_groupTag(groupTag)
    , m_soundEffect(soundEffect)
    , m_effectId(0)
    , m_hasExecuted(false)
{
    // Set default active state
    setActive(true);
}

ParticleEffectEvent::ParticleEffectEvent(const std::string& name,
                                       const std::string& effectName,
                                       float x, float y,
                                       float intensity,
                                       float duration,
                                       const std::string& groupTag,
                                       const std::string& soundEffect)
    : ParticleEffectEvent(name, effectName, Vector2D(x, y), intensity, duration, groupTag, soundEffect)
{
    // Delegating constructor
}

void ParticleEffectEvent::update() {
    // Update cooldown timer if applicable
    updateCooldown(0.016f); // Assume ~60 FPS for cooldown updates
    
    // Check if effect is still active (for finite duration effects)
    if (m_effectId != 0 && m_duration > 0.0f) {
        // Note: Duration tracking is handled by ParticleManager internally
        // We just need to check if our effect is still valid
        ParticleManager& particleMgr = ParticleManager::Instance();
        if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
            // Effect lifetime is managed by ParticleManager
            // No additional tracking needed here
        }
    }
}

void ParticleEffectEvent::execute() {
    // Check if we should execute (conditions, cooldown, one-time restrictions)
    if (!checkConditions()) {
        return;
    }
    
    if (isOnCooldown()) {
        return;
    }
    
    if (isOneTime() && hasTriggered()) {
        return;
    }
    
    // Get ParticleManager instance
    ParticleManager& particleMgr = ParticleManager::Instance();
    
    // Check if ParticleManager is available
    if (!particleMgr.isInitialized() || particleMgr.isShutdown()) {
        std::cerr << "ParticleEffectEvent::execute() - ParticleManager not available for effect: " 
                  << m_effectName << std::endl;
        return;
    }
    
    try {
        // Trigger the particle effect
        if (m_duration == -1.0f) {
            // Independent effect (infinite duration until manually stopped)
            m_effectId = particleMgr.playIndependentEffect(m_effectName, m_position, m_intensity, 
                                                          m_duration, m_groupTag, m_soundEffect);
        } else {
            // Regular effect with duration
            m_effectId = particleMgr.playEffect(m_effectName, m_position, m_intensity);
        }
        
        // Trigger sound effect if specified
        if (!m_soundEffect.empty()) {
            try {
                SoundManager& soundMgr = SoundManager::Instance();
                // Use SoundManager's playSFX method with proper volume range (0-128)
                soundMgr.playSFX(m_soundEffect, 0, 100); // loops=0, volume=100
            } catch (const std::exception& e) {
                std::cerr << "ParticleEffectEvent::execute() - Sound effect failed: " 
                          << e.what() << std::endl;
                // Continue execution even if sound fails
            }
        }
        
        if (m_effectId != 0) {
            std::cout << "ParticleEffectEvent '" << m_name << "' triggered effect '" 
                      << m_effectName << "' at (" << m_position.getX() << ", " 
                      << m_position.getY() << ") with intensity " << m_intensity 
                      << " -> Effect ID: " << m_effectId << std::endl;
            
            m_hasExecuted = true;
            
            // Start cooldown if configured
            if (getCooldown() > 0.0f) {
                startCooldown();
            }
        } else {
            std::cerr << "ParticleEffectEvent::execute() - Failed to trigger effect: " 
                      << m_effectName << " at position (" << m_position.getX() 
                      << ", " << m_position.getY() << ")" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "ParticleEffectEvent::execute() - Exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "ParticleEffectEvent::execute() - Unknown exception occurred" << std::endl;
    }
}

void ParticleEffectEvent::reset() {
    // Stop any active effect
    stopEffect();
    
    // Reset execution state
    m_hasExecuted = false;
    m_effectId = 0;
    
    // Reset cooldown
    resetCooldown();
    
    std::cout << "ParticleEffectEvent '" << m_name << "' reset" << std::endl;
}

void ParticleEffectEvent::clean() {
    // Stop any active effect
    stopEffect();
    
    // Clean up state
    m_hasExecuted = false;
    m_effectId = 0;
    
    std::cout << "ParticleEffectEvent '" << m_name << "' cleaned up" << std::endl;
}

bool ParticleEffectEvent::checkConditions() {
    // Basic condition: must be active
    if (!isActive()) {
        return false;
    }
    
    // Check if ParticleManager is available
    ParticleManager& particleMgr = ParticleManager::Instance();
    if (!particleMgr.isInitialized() || particleMgr.isShutdown()) {
        return false;
    }
    
    // Check if effect name is valid (non-empty)
    if (m_effectName.empty()) {
        return false;
    }
    
    // All conditions met
    return true;
}

void ParticleEffectEvent::stopEffect() {
    if (m_effectId != 0) {
        try {
            ParticleManager& particleMgr = ParticleManager::Instance();
            if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
                // Try stopping as independent effect first
                if (particleMgr.isIndependentEffect(m_effectId)) {
                    particleMgr.stopIndependentEffect(m_effectId);
                    std::cout << "Stopped independent particle effect ID: " << m_effectId << std::endl;
                } else {
                    // Stop as regular effect
                    particleMgr.stopEffect(m_effectId);
                    std::cout << "Stopped particle effect ID: " << m_effectId << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "ParticleEffectEvent::stopEffect() - Exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "ParticleEffectEvent::stopEffect() - Unknown exception" << std::endl;
        }
        
        m_effectId = 0;
    }
}
