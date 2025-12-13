/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "events/ParticleEffectEvent.hpp"
#include "core/Logger.hpp"
#include "managers/ParticleManager.hpp"
#include "managers/SoundManager.hpp"
#include <format>

ParticleEffectEvent::ParticleEffectEvent(const std::string &name,
                                         ParticleEffectType effectType,
                                         const Vector2D &position,
                                         float intensity, float duration,
                                         const std::string &groupTag,
                                         const std::string &soundEffect)
    : m_name(name), m_effectType(effectType), m_position(position),
      m_intensity(intensity), m_duration(duration), m_groupTag(groupTag),
      m_soundEffect(soundEffect), m_effectId(0), m_hasExecuted(false) {
  // Set default active state
  setActive(true);
}

ParticleEffectEvent::ParticleEffectEvent(const std::string &name,
                                         ParticleEffectType effectType, float x,
                                         float y, float intensity,
                                         float duration,
                                         const std::string &groupTag,
                                         const std::string &soundEffect)
    : ParticleEffectEvent(name, effectType, Vector2D(x, y), intensity, duration,
                          groupTag, soundEffect) {
  // Delegating constructor
}

void ParticleEffectEvent::update() {
  // Update cooldown timer if applicable
  updateCooldown(0.016f); // Assume ~60 FPS for cooldown updates

  // Check if effect is still active (for finite duration effects)
  if (m_effectId != 0 && m_duration > 0.0f) {
    // Note: Duration tracking is handled by ParticleManager internally
    // We just need to check if our effect is still valid
    const ParticleManager &particleMgr = ParticleManager::Instance();
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
  ParticleManager &particleMgr = ParticleManager::Instance();

  // Check if ParticleManager is available
  if (!particleMgr.isInitialized() || particleMgr.isShutdown()) {
    EVENT_ERROR(std::format("ParticleEffectEvent::execute() - ParticleManager not "
                "available for effect: {}", getEffectName()));
    return;
  }

  try {
    // Trigger the particle effect
    if (m_duration == -1.0f) {
      // Independent effect (infinite duration until manually stopped)
      m_effectId = particleMgr.playIndependentEffect(m_effectType, m_position,
                                                     m_intensity, m_duration,
                                                     m_groupTag, m_soundEffect);
    } else {
      // Regular effect with duration
      m_effectId =
          particleMgr.playEffect(m_effectType, m_position, m_intensity);
    }

    // Trigger sound effect if specified
    if (!m_soundEffect.empty()) {
      try {
        SoundManager &soundMgr = SoundManager::Instance();
        // Use SoundManager's playSFX method with proper volume range (0-128)
        soundMgr.playSFX(m_soundEffect, 0, 100); // loops=0, volume=100
      } catch (const std::exception &e) {
        EVENT_ERROR(std::format("ParticleEffectEvent::execute() - Sound effect failed: {}",
                    e.what()));
        // Continue execution even if sound fails
      }
    }

    if (m_effectId != 0) {
      EVENT_INFO(std::format("ParticleEffectEvent '{}' triggered effect '{}' at ({}, {}) with intensity {} -> Effect ID: {}",
                 m_name, getEffectName(), m_position.getX(), m_position.getY(), m_intensity, m_effectId));

      m_hasExecuted = true;

      // Start cooldown if configured
      if (getCooldown() > 0.0f) {
        startCooldown();
      }
    } else {
      EVENT_ERROR(std::format(
          "ParticleEffectEvent::execute() - Failed to trigger effect: {} at position ({}, {})",
          getEffectName(), m_position.getX(), m_position.getY()));
    }

  } catch (const std::exception &e) {
    EVENT_ERROR(std::format("ParticleEffectEvent::execute() - Exception: {}",
                e.what()));
  } catch (...) {
    EVENT_ERROR("ParticleEffectEvent::execute() - Unknown exception occurred");
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

  EVENT_INFO(std::format("ParticleEffectEvent '{}' reset", m_name));
}

void ParticleEffectEvent::clean() {
  // Stop any active effect
  stopEffect();

  // Clean up state
  m_hasExecuted = false;
  m_effectId = 0;

  EVENT_INFO(std::format("ParticleEffectEvent '{}' cleaned up", m_name));
}

bool ParticleEffectEvent::checkConditions() {
  // Basic condition: must be active
  if (!isActive()) {
    return false;
  }

  // Check if ParticleManager is available
  const ParticleManager &particleMgr = ParticleManager::Instance();
  if (!particleMgr.isInitialized() || particleMgr.isShutdown()) {
    return false;
  }

  // Check if effect type is valid
  if (static_cast<uint8_t>(m_effectType) >=
      static_cast<uint8_t>(ParticleEffectType::COUNT)) {
    return false;
  }

  // All conditions met
  return true;
}

void ParticleEffectEvent::stopEffect() {
  if (m_effectId != 0) {
    try {
      ParticleManager &particleMgr = ParticleManager::Instance();
      if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
        // Try stopping as independent effect first
        if (particleMgr.isIndependentEffect(m_effectId)) {
          particleMgr.stopIndependentEffect(m_effectId);
          EVENT_INFO(std::format("Stopped independent particle effect ID: {}",
                     m_effectId));
        } else {
          // Stop as regular effect
          particleMgr.stopEffect(m_effectId);
          EVENT_INFO(std::format("Stopped particle effect ID: {}",
                     m_effectId));
        }
      }
    } catch (const std::exception &e) {
      EVENT_ERROR(std::format("ParticleEffectEvent::stopEffect() - Exception: {}",
                  e.what()));
    } catch (...) {
      EVENT_ERROR("ParticleEffectEvent::stopEffect() - Unknown exception");
    }

    m_effectId = 0;
  }
}

void ParticleEffectEvent::setEffectType(ParticleEffectType effectType) {
  m_effectType = effectType;
}

ParticleEffectType ParticleEffectEvent::getEffectType() const {
  return m_effectType;
}

std::string ParticleEffectEvent::getEffectName() const {
  // Create a map for effect type to string conversion since the method is
  // private
  switch (m_effectType) {
  case ParticleEffectType::Rain:
    return "Rain";
  case ParticleEffectType::HeavyRain:
    return "HeavyRain";
  case ParticleEffectType::Snow:
    return "Snow";
  case ParticleEffectType::HeavySnow:
    return "HeavySnow";
  case ParticleEffectType::Fog:
    return "Fog";
  case ParticleEffectType::Cloudy:
    return "Cloudy";
  case ParticleEffectType::Fire:
    return "Fire";
  case ParticleEffectType::Smoke:
    return "Smoke";
  case ParticleEffectType::Sparks:
    return "Sparks";
  case ParticleEffectType::Magic:
    return "Magic";
  case ParticleEffectType::Custom:
    return "Custom";
  default:
    return "Unknown";
  }
}

ParticleEffectType
ParticleEffectEvent::stringToEffectType(const std::string &effectName) {
  if (effectName == "Rain")
    return ParticleEffectType::Rain;
  if (effectName == "HeavyRain")
    return ParticleEffectType::HeavyRain;
  if (effectName == "Snow")
    return ParticleEffectType::Snow;
  if (effectName == "HeavySnow")
    return ParticleEffectType::HeavySnow;
  if (effectName == "Fog")
    return ParticleEffectType::Fog;
  if (effectName == "Cloudy")
    return ParticleEffectType::Cloudy;
  if (effectName == "Fire")
    return ParticleEffectType::Fire;
  if (effectName == "Smoke")
    return ParticleEffectType::Smoke;
  if (effectName == "Sparks")
    return ParticleEffectType::Sparks;
  if (effectName == "Magic")
    return ParticleEffectType::Magic;
  if (effectName == "Custom")
    return ParticleEffectType::Custom;

  // Default fallback
  return ParticleEffectType::Fire;
}