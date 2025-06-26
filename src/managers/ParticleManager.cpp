/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/ParticleManager.hpp"
#include "managers/TextureManager.hpp"
#include "managers/EventManager.hpp"
#include "core/Logger.hpp"
#include "core/GameTime.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include <chrono>
#include <algorithm>
#include <random>

bool ParticleManager::init() {
    if (m_initialized.load(std::memory_order_acquire)) {
        PARTICLE_LOG("ParticleManager already initialized");
        return true;
    }

    try {
        // Pre-allocate storage for better performance
        constexpr size_t INITIAL_CAPACITY = DEFAULT_MAX_PARTICLES;
        m_storage.reserve(INITIAL_CAPACITY);

        // Initialize double buffers
        m_storage.doubleBuffer[0].reserve(INITIAL_CAPACITY);
        m_storage.doubleBuffer[1].reserve(INITIAL_CAPACITY);

        // Built-in effects will be registered by GameEngine after init
        
        m_initialized.store(true, std::memory_order_release);
        m_isShutdown = false;

        PARTICLE_LOG("ParticleManager initialized successfully");
        return true;

    } catch (const std::exception& e) {
        PARTICLE_LOG("Failed to initialize ParticleManager: " + std::string(e.what()));
        return false;
    }
}

void ParticleManager::clean() {
    if (m_isShutdown) {
        return;
    }

    PARTICLE_LOG("ParticleManager shutting down...");

    // Mark as shutting down
    m_isShutdown = true;
    m_initialized.store(false, std::memory_order_release);

    // Clear all storage
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    m_storage.clear();
    m_effectDefinitions.clear();
    m_effectInstances.clear();
    m_effectIdToIndex.clear();

    PARTICLE_LOG("ParticleManager shutdown complete");
}

void ParticleManager::prepareForStateTransition() {
    PARTICLE_LOG("Preparing ParticleManager for state transition...");

    // Temporarily pause for safe cleanup
    m_globallyPaused.store(true, std::memory_order_release);

    // Stop all weather effects immediately (clean slate for new state)
    stopWeatherEffects(0.0f);

    // Clean up inactive particles and effects
    cleanupInactiveParticles();

    // Reset performance stats for fresh monitoring in new state
    resetPerformanceStats();

    // Resume particle system (ready for immediate reuse)
    m_globallyPaused.store(false, std::memory_order_release);

    PARTICLE_LOG("ParticleManager prepared and ready for state transition");
}

void ParticleManager::update(float deltaTime) {
    if (!m_initialized.load(std::memory_order_acquire) ||
        m_globallyPaused.load(std::memory_order_acquire)) {
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Process all active particle effects
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    size_t activeParticleCount = 0;
    for (auto& effect : m_effectInstances) {
        if (effect.active) {
            updateEffectInstance(effect, deltaTime);
        }
    }
    
    // Update all active particles independently
    for (auto& particle : m_storage.hotData) {
        if (particle.isActive()) {
            updateParticle(particle, deltaTime);
            activeParticleCount++;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double timeMs = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count() / 1000.0;
    recordPerformance(false, timeMs, activeParticleCount);
}

void ParticleManager::render(SDL_Renderer* renderer, float cameraX, float cameraY) {
    if (m_globallyPaused.load(std::memory_order_acquire) || !m_globallyVisible.load(std::memory_order_acquire)) {
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Render all active particles as simple colored rectangles
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    int renderCount = 0;
    for (const auto& particle : m_storage.hotData) {
        if (!particle.isActive() || !particle.isVisible()) {
            continue;
        }
        renderCount++;

        // Extract color components
        uint8_t r = (particle.color >> 24) & 0xFF;
        uint8_t g = (particle.color >> 16) & 0xFF;
        uint8_t b = (particle.color >> 8) & 0xFF;
        uint8_t a = particle.color & 0xFF;

        // Set particle color
        SDL_SetRenderDrawColor(renderer, r, g, b, a);

        // Get particle size from cold data or use default
        float size = 2.0f;
        if (m_storage.hotData.size() == m_storage.coldData.size()) {
            auto particleIndex = static_cast<size_t>(&particle - &m_storage.hotData[0]);
            if (particleIndex < m_storage.coldData.size()) {
                size = m_storage.coldData[particleIndex].size;
            }
        }

        // Render particle as a filled rectangle (accounting for camera offset)
        SDL_FRect rect = {
            particle.position.getX() - cameraX - size/2,
            particle.position.getY() - cameraY - size/2,
            size,
            size
        };
        SDL_RenderFillRect(renderer, &rect);
    }

    // Periodic summary logging (every 900 frames ~15 seconds)
    uint64_t currentFrame = m_frameCounter.fetch_add(1, std::memory_order_relaxed);
    if (currentFrame % 900 == 0 && renderCount > 0) {
        PARTICLE_LOG("Particle Summary - Total: " + std::to_string(m_storage.hotData.size()) + 
                    ", Active: " + std::to_string(renderCount) + 
                    ", Effects: " + std::to_string(m_effectInstances.size()));
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double timeMs = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count() / 1000.0;
    recordPerformance(true, timeMs, m_storage.size());
}

void ParticleManager::renderBackground(SDL_Renderer* renderer, float cameraX, float cameraY) {
    if (m_globallyPaused.load(std::memory_order_acquire) || !m_globallyVisible.load(std::memory_order_acquire)) {
        return;
    }

    // Render only background particles (rain, snow)
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    for (const auto& particle : m_storage.hotData) {
        if (!particle.isActive() || !particle.isVisible()) {
            continue;
        }
        
        // Check if this is a background particle (rain, snow, fire, smoke, sparks)
        uint32_t color = particle.color;
        bool isBackground = false;
        
        // Rain particles are blue-ish (0x4080FF00)
        if ((color & 0xFFFFFF00) == 0x4080FF00) {
            isBackground = true;
        }
        // Snow particles are white (0xFFFFFFFF with any alpha)
        else if ((color & 0xFFFFFF00) == 0xFFFFFF00) {
            isBackground = true;
        }
        // Fire particles - orange/red/yellow range
        else if ((color & 0xFF000000) == 0xFF000000 && 
                 ((color & 0x00FF0000) >= 0x00450000) && // Red component >= 0x45
                 ((color & 0x0000FF00) <= 0x0000FF00)) {   // Green component <= 0xFF
            isBackground = true;
        }
        // Smoke particles - grey range
        else if ((color & 0xFF000000) >= 0x20000000 && (color & 0xFF000000) <= 0x80000000 &&
                 (color & 0x00FFFFFF) >= 0x00202020 && (color & 0x00FFFFFF) <= 0x00808080) {
            isBackground = true;
        }
        // Sparks particles - bright yellow/orange
        else if ((color & 0xFFFF0000) == 0xFFFF0000 || // Yellow (FFFF__)
                 (color & 0xFF8C0000) == 0xFF8C0000) {     // Orange (FF8C__)
            isBackground = true;
        }
        
        if (!isBackground) {
            continue; // Skip foreground particles
        }

        // Extract color components
        uint8_t r = (color >> 24) & 0xFF;
        uint8_t g = (color >> 16) & 0xFF;
        uint8_t b = (color >> 8) & 0xFF;
        uint8_t a = color & 0xFF;

        // Set particle color
        SDL_SetRenderDrawColor(renderer, r, g, b, a);

        // Get particle size from cold data or use default
        float size = 2.0f;
        if (m_storage.hotData.size() == m_storage.coldData.size()) {
            auto particleIndex = static_cast<size_t>(&particle - &m_storage.hotData[0]);
            if (particleIndex < m_storage.coldData.size()) {
                size = m_storage.coldData[particleIndex].size;
            }
        }

        // Render particle as a filled rectangle (accounting for camera offset)
        SDL_FRect rect = {
            particle.position.getX() - cameraX - size/2,
            particle.position.getY() - cameraY - size/2,
            size,
            size
        };
        SDL_RenderFillRect(renderer, &rect);
    }
    
    // Background particle render complete
}

void ParticleManager::renderForeground(SDL_Renderer* renderer, float cameraX, float cameraY) {
    if (m_globallyPaused.load(std::memory_order_acquire) || !m_globallyVisible.load(std::memory_order_acquire)) {
        return;
    }

    // Render only foreground particles (fog)
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    for (const auto& particle : m_storage.hotData) {
        if (!particle.isActive() || !particle.isVisible()) {
            continue;
        }
        
        // Check if this is a foreground particle (fog = gray-ish, clouds = light white)
        uint32_t color = particle.color;
        bool isForeground = false;
        
        // Fog particles are gray-ish (0xCCCCCC88)
        if ((color & 0xFFFFFF00) == 0xCCCCCC00) {
            isForeground = true;
        }
        // Cloudy particles are light white (0xF8F8F8xx)
        else if ((color & 0xFFFFFF00) == 0xF8F8F800) {
            isForeground = true;
        }
        
        if (!isForeground) {
            continue; // Skip background particles
        }

        // Extract color components
        uint8_t r = (color >> 24) & 0xFF;
        uint8_t g = (color >> 16) & 0xFF;
        uint8_t b = (color >> 8) & 0xFF;
        uint8_t a = color & 0xFF;

        // Set particle color
        SDL_SetRenderDrawColor(renderer, r, g, b, a);

        // Get particle size from cold data or use default
        float size = 2.0f;
        if (m_storage.hotData.size() == m_storage.coldData.size()) {
            auto particleIndex = static_cast<size_t>(&particle - &m_storage.hotData[0]);
            if (particleIndex < m_storage.coldData.size()) {
                size = m_storage.coldData[particleIndex].size;
            }
        }

        // Render particle as a filled rectangle (accounting for camera offset)
        SDL_FRect rect = {
            particle.position.getX() - cameraX - size/2,
            particle.position.getY() - cameraY - size/2,
            size,
            size
        };
        SDL_RenderFillRect(renderer, &rect);
    }
    
    // Foreground particle render complete
}

uint32_t ParticleManager::playEffect(const std::string& effectName, const Vector2D& position, float intensity) {
    PARTICLE_LOG("*** PLAY EFFECT CALLED: " + effectName + " at (" + std::to_string(position.getX()) + ", " + std::to_string(position.getY()) + ") intensity=" + std::to_string(intensity));
    
    // Thread safety: Use exclusive lock for modifying effect instances
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    auto it = m_effectDefinitions.find(effectName);
    if (it == m_effectDefinitions.end()) {
        PARTICLE_LOG("ERROR: Effect not found: " + effectName);
        PARTICLE_LOG("Available effects: " + std::to_string(m_effectDefinitions.size()));
        for (const auto& pair : m_effectDefinitions) {
            PARTICLE_LOG("  - " + pair.first);
        }
        return 0;
    }

    // Note: definition would be used for more complex effect configuration
    // auto& definition = it->second;
    EffectInstance instance;
    instance.id = generateEffectId();
    instance.effectName = effectName;
    instance.position = position;
    instance.intensity = intensity;
    instance.currentIntensity = intensity;
    instance.targetIntensity = intensity;
    instance.active = true;

    // Register effect
    m_effectInstances.push_back(instance);
    m_effectIdToIndex[instance.id] = m_effectInstances.size() - 1;

    PARTICLE_LOG("Effect successfully started: " + effectName + " (ID: " + std::to_string(instance.id) + ") - Total active effects: " + std::to_string(m_effectInstances.size()));
    return instance.id;
}

void ParticleManager::stopEffect(uint32_t effectId) {
    // Thread safety: Use exclusive lock for modifying effect instances
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    auto it = m_effectIdToIndex.find(effectId);
    if (it != m_effectIdToIndex.end()) {
        m_effectInstances[it->second].active = false;
        PARTICLE_LOG("Effect stopped: ID " + std::to_string(effectId));
    }
}

void ParticleManager::stopWeatherEffects(float transitionTime) {
    PARTICLE_LOG("*** STOPPING ALL WEATHER EFFECTS (transition: " + std::to_string(transitionTime) + "s)");
    PARTICLE_LOG("DEBUG: Total effects in system: " + std::to_string(m_effectInstances.size()));
    
    int stoppedCount = 0;
    int activeCount = 0;
    int weatherCount = 0;
    
    // Stop emitter effects - particles will naturally fade out
    for (auto& effect : m_effectInstances) {
        PARTICLE_LOG("DEBUG: Effect '" + effect.effectName + "' - active: " + 
                    (effect.active ? "true" : "false") + ", isWeatherEffect: " + 
                    (effect.isWeatherEffect ? "true" : "false"));
                    
        if (effect.active) activeCount++;
        if (effect.isWeatherEffect) weatherCount++;
        
        if (effect.active && effect.isWeatherEffect) {
            PARTICLE_LOG("DEBUG: Stopping weather effect: " + effect.effectName + " (ID: " + std::to_string(effect.id) + ")");
            
            if (transitionTime <= 0.0f) {
                // Immediate stop - deactivate effect
                effect.active = false;
                PARTICLE_LOG("DEBUG: Effect " + effect.effectName + " immediately deactivated");
            } else {
                // Gradual fade out by setting target intensity to 0
                effect.targetIntensity = 0.0f;
                effect.transitionSpeed = 1.0f / transitionTime;
                PARTICLE_LOG("DEBUG: Effect " + effect.effectName + " set to fade out over " + std::to_string(transitionTime) + "s");
            }
            stoppedCount++;
        }
    }
    
    PARTICLE_LOG("DEBUG: Effects summary - Total: " + std::to_string(m_effectInstances.size()) + 
                ", Active: " + std::to_string(activeCount) + 
                ", Weather: " + std::to_string(weatherCount) + 
                ", Stopped: " + std::to_string(stoppedCount));
    
    // If immediate stop, clear all weather particles
    if (transitionTime <= 0.0f) {
        PARTICLE_LOG("DEBUG: Clearing weather particles immediately");
        clearWeatherGeneration(0, 0.0f); // Clear all weather particles immediately
    }
    
    PARTICLE_LOG("Stopped " + std::to_string(stoppedCount) + " weather effects");
}

void ParticleManager::clearWeatherGeneration(uint8_t generationId, float fadeTime) {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    int affectedCount = 0;
    
    for (auto& particle : m_storage.hotData) {
        if (particle.isActive() && particle.isWeatherParticle()) {
            // Clear specific generation or all weather particles if generationId is 0
            if (generationId == 0 || particle.generationId == generationId) {
                if (fadeTime <= 0.0f) {
                    // Immediate removal
                    particle.setActive(false);
                } else {
                    // Set fade-out and limit life to fade time
                    particle.setFadingOut(true);
                    particle.life = std::min(particle.life, fadeTime);
                }
                affectedCount++;
            }
        }
    }
    
    PARTICLE_LOG("Cleared " + std::to_string(affectedCount) + " weather particles" + 
                (generationId > 0 ? " (generation " + std::to_string(generationId) + ")" : "") +
                " with fade time: " + std::to_string(fadeTime) + "s");
}

void ParticleManager::triggerWeatherEffect(const std::string& weatherType, float intensity, float transitionTime) {
    PARTICLE_LOG("*** WEATHER EFFECT TRIGGERED: " + weatherType + " intensity=" + std::to_string(intensity));
    
    // Note: transitionTime parameter available for future fade-in implementation
    (void)transitionTime; // Suppress unused parameter warning
    
    // Stop all existing weather effects first to prevent stacking
    stopWeatherEffects(0.5f); // Quick fade out
    
    // Handle Clear weather - just stop effects and return
    if (weatherType == "Clear") {
        PARTICLE_LOG("Clear weather triggered - only stopping weather effects");
        // stopWeatherEffects() already called above, so we're done
        return;
    }
    
    // Map weather type to particle effect
    std::string effectName;
    if (weatherType == "Rainy") effectName = "Rain";
    else if (weatherType == "Snowy") effectName = "Snow";
    else if (weatherType == "Foggy") effectName = "Fog";
    else if (weatherType == "Cloudy") effectName = "Cloudy";
    else if (weatherType == "Stormy") effectName = "Rain"; // Stormy = heavy rain
    
    PARTICLE_LOG("Mapped weather type '" + weatherType + "' to effect '" + effectName + "'");

    if (!effectName.empty()) {
        // For realistic weather effects, create screen-wide coverage
        Vector2D weatherPosition;
        if (effectName == "Rain" || effectName == "Snow") {
            // Use center-top position but with much wider spread in effect definition
            weatherPosition = Vector2D(960, -100); // Center-top, higher up for more falling distance
        } else if (effectName == "Fog") {
            // For fog, use center-middle position to fill screen with drifting fog
            weatherPosition = Vector2D(960, 300); // Center-middle for better fog distribution
        } else if (effectName == "Cloudy") {
            // For clouds, spawn across top of screen to be visible
            weatherPosition = Vector2D(960, 50); // Center-top but visible on screen
        } else {
            // For other effects, use original positioning
            weatherPosition = Vector2D(960, -50); // Center-top of screen with some margin
        }
        
        uint32_t effectId = playEffect(effectName, weatherPosition, intensity);
        
        // CRITICAL: Mark this effect as a weather effect IMMEDIATELY after creation
        {
            std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
            auto it = m_effectIdToIndex.find(effectId);
            if (it != m_effectIdToIndex.end()) {
                m_effectInstances[it->second].isWeatherEffect = true;
                PARTICLE_LOG("MARKED effect ID " + std::to_string(effectId) + " as weather effect");
            } else {
                PARTICLE_LOG("ERROR: Could not find effect ID " + std::to_string(effectId) + " to mark as weather effect");
            }
            
            // Verify the effect was properly marked
            if (it != m_effectIdToIndex.end()) {
                std::string_view isWeatherStr = m_effectInstances[it->second].isWeatherEffect ? "true" : "false";
                PARTICLE_LOG("Weather effect verification - isWeatherEffect: " + std::string(isWeatherStr));
            }
        }
        
        PARTICLE_LOG("Weather effect triggered: " + weatherType + " -> " + effectName + 
                    " at position (" + std::to_string(weatherPosition.getX()) + 
                    ", " + std::to_string(weatherPosition.getY()) + ") -> Effect ID: " + std::to_string(effectId));
    } else {
        PARTICLE_LOG("ERROR: No effect mapping found for weather type: " + weatherType);
    }
}

void ParticleManager::recordPerformance(bool isRender, double timeMs, size_t particleCount) {
    if (isRender) {
        m_performanceStats.addRenderSample(timeMs);
    } else {
        m_performanceStats.addUpdateSample(timeMs, particleCount);
    }
}

void ParticleManager::toggleFireEffect() {
    std::unique_lock<std::shared_mutex> lock(m_effectsMutex);
    if (!m_fireActive) {
        m_fireEffectId = playIndependentEffect("Fire", Vector2D(400, 300));
        m_fireActive = true;
    } else {
        stopIndependentEffect(m_fireEffectId);
        m_fireActive = false;
    }
}

void ParticleManager::toggleSmokeEffect() {
    std::unique_lock<std::shared_mutex> lock(m_effectsMutex);
    if (!m_smokeActive) {
        m_smokeEffectId = playIndependentEffect("Smoke", Vector2D(400, 300));
        m_smokeActive = true;
    } else {
        stopIndependentEffect(m_smokeEffectId);
        m_smokeActive = false;
    }
}

void ParticleManager::toggleSparksEffect() {
    std::unique_lock<std::shared_mutex> lock(m_effectsMutex);
    if (!m_sparksActive) {
        m_sparksEffectId = playIndependentEffect("Sparks", Vector2D(400, 300));
        m_sparksActive = true;
    } else {
        stopIndependentEffect(m_sparksEffectId);
        m_sparksActive = false;
    }
}

// Independent Effect Management Implementation
uint32_t ParticleManager::playIndependentEffect(const std::string& effectName, const Vector2D& position, 
                                              float intensity, float duration, 
                                              const std::string& groupTag, const std::string& soundEffect) {
    PARTICLE_LOG("Playing independent effect: " + effectName + " at (" + std::to_string(position.getX()) + ", " + std::to_string(position.getY()) + ")");
    
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    auto it = m_effectDefinitions.find(effectName);
    if (it == m_effectDefinitions.end()) {
        PARTICLE_LOG("ERROR: Independent effect not found: " + effectName);
        return 0;
    }
    
    EffectInstance instance;
    instance.id = generateEffectId();
    instance.effectName = effectName;
    instance.position = position;
    instance.intensity = intensity;
    instance.currentIntensity = intensity;
    instance.targetIntensity = intensity;
    instance.active = true;
    instance.isIndependentEffect = true;
    instance.isWeatherEffect = false;
    instance.maxDuration = duration;
    instance.groupTag = groupTag;
    instance.soundEffect = soundEffect;
    
    // Register effect
    m_effectInstances.push_back(instance);
    m_effectIdToIndex[instance.id] = m_effectInstances.size() - 1;
    
    PARTICLE_LOG("Independent effect started: " + effectName + " (ID: " + std::to_string(instance.id) + ")");
    return instance.id;
}

void ParticleManager::stopIndependentEffect(uint32_t effectId) {
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    auto it = m_effectIdToIndex.find(effectId);
    if (it != m_effectIdToIndex.end() && it->second < m_effectInstances.size()) {
        auto& effect = m_effectInstances[it->second];
        if (effect.isIndependentEffect) {
            effect.active = false;
            PARTICLE_LOG("Independent effect stopped: ID " + std::to_string(effectId));
        }
    }
}

void ParticleManager::stopAllIndependentEffects() {
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    int stoppedCount = 0;
    for (auto& effect : m_effectInstances) {
        if (effect.active && effect.isIndependentEffect) {
            effect.active = false;
            stoppedCount++;
        }
    }
    
    PARTICLE_LOG("Stopped " + std::to_string(stoppedCount) + " independent effects");
}

void ParticleManager::stopIndependentEffectsByGroup(const std::string& groupTag) {
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    int stoppedCount = 0;
    for (auto& effect : m_effectInstances) {
        if (effect.active && effect.isIndependentEffect && effect.groupTag == groupTag) {
            effect.active = false;
            stoppedCount++;
        }
    }
    
    PARTICLE_LOG("Stopped " + std::to_string(stoppedCount) + " independent effects in group: " + groupTag);
}

void ParticleManager::pauseIndependentEffect(uint32_t effectId, bool paused) {
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    auto it = m_effectIdToIndex.find(effectId);
    if (it != m_effectIdToIndex.end() && it->second < m_effectInstances.size()) {
        auto& effect = m_effectInstances[it->second];
        if (effect.isIndependentEffect) {
            effect.paused = paused;
            PARTICLE_LOG("Independent effect " + std::to_string(effectId) + (paused ? " paused" : " unpaused"));
        }
    }
}

void ParticleManager::pauseAllIndependentEffects(bool paused) {
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    int affectedCount = 0;
    for (auto& effect : m_effectInstances) {
        if (effect.active && effect.isIndependentEffect) {
            effect.paused = paused;
            affectedCount++;
        }
    }
    
    PARTICLE_LOG("All independent effects " + std::string(paused ? "paused" : "unpaused") + " (" + std::to_string(affectedCount) + " effects)");
}

void ParticleManager::pauseIndependentEffectsByGroup(const std::string& groupTag, bool paused) {
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    int affectedCount = 0;
    for (auto& effect : m_effectInstances) {
        if (effect.active && effect.isIndependentEffect && effect.groupTag == groupTag) {
            effect.paused = paused;
            affectedCount++;
        }
    }
    
    PARTICLE_LOG("Independent effects in group " + groupTag + " " + std::string(paused ? "paused" : "unpaused") + " (" + std::to_string(affectedCount) + " effects)");
}

bool ParticleManager::isIndependentEffect(uint32_t effectId) const {
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    
    auto it = m_effectIdToIndex.find(effectId);
    if (it != m_effectIdToIndex.end() && it->second < m_effectInstances.size()) {
        return m_effectInstances[it->second].isIndependentEffect;
    }
    return false;
}

std::vector<uint32_t> ParticleManager::getActiveIndependentEffects() const {
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    
    std::vector<uint32_t> activeEffects;
    for (const auto& effect : m_effectInstances) {
        if (effect.active && effect.isIndependentEffect) {
            activeEffects.push_back(effect.id);
        }
    }
    
    return activeEffects;
}

std::vector<uint32_t> ParticleManager::getActiveIndependentEffectsByGroup(const std::string& groupTag) const {
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    
    std::vector<uint32_t> activeEffects;
    for (const auto& effect : m_effectInstances) {
        if (effect.active && effect.isIndependentEffect && effect.groupTag == groupTag) {
            activeEffects.push_back(effect.id);
        }
    }
    
    return activeEffects;
}

uint32_t ParticleManager::generateEffectId() {
    return m_nextEffectId.fetch_add(1, std::memory_order_relaxed);
}

void ParticleManager::registerBuiltInEffects() {
    PARTICLE_LOG("*** REGISTERING BUILT-IN EFFECTS");
    
    // Register preset weather effects
    m_effectDefinitions["Rain"] = createRainEffect();
    m_effectDefinitions["Snow"] = createSnowEffect();
    m_effectDefinitions["Fog"] = createFogEffect();
    m_effectDefinitions["Cloudy"] = createCloudyEffect();
    
    // Register independent particle effects
    m_effectDefinitions["Fire"] = createFireEffect();
    m_effectDefinitions["Smoke"] = createSmokeEffect();
    m_effectDefinitions["Sparks"] = createSparksEffect();
    
    PARTICLE_LOG("Built-in effects registered: " + std::to_string(m_effectDefinitions.size()));
    for (const auto& pair : m_effectDefinitions) {
        PARTICLE_LOG("  - Effect: " + pair.first);
    }
    
    // More effects can be added as needed
}

ParticleEffectDefinition ParticleManager::createRainEffect() {
    ParticleEffectDefinition rain("Rain", ParticleEffectType::Rain);
    rain.emitterConfig.spread = 1000.0f; // Wide spread to cover entire screen width (1920px)
    rain.emitterConfig.emissionRate = 120.0f; // Base emission rate - will be scaled by intensity
    rain.emitterConfig.minSpeed = 100.0f;
    rain.emitterConfig.maxSpeed = 250.0f; // Faster for more realistic rain
    rain.emitterConfig.minLife = 3.0f; // Longer life to fall across screen
    rain.emitterConfig.maxLife = 5.0f;
    rain.emitterConfig.minSize = 2.0f; // Smaller, more realistic raindrops
    rain.emitterConfig.maxSize = 4.0f;
    rain.emitterConfig.gravity = Vector2D(10.0f, 300.0f); // Slight horizontal drift + strong downward
    rain.emitterConfig.textureID = "raindrop";
    rain.intensityMultiplier = 1.2f; // Slightly higher multiplier for rain intensity
    return rain;
}

ParticleEffectDefinition ParticleManager::createSnowEffect() {
    ParticleEffectDefinition snow("Snow", ParticleEffectType::Snow);
    snow.emitterConfig.spread = 1000.0f; // Wide spread to cover entire screen width
    snow.emitterConfig.emissionRate = 60.0f; // Base emission rate - will be scaled by intensity
    snow.emitterConfig.minSpeed = 5.0f; // Slower falling speed for snow
    snow.emitterConfig.maxSpeed = 25.0f;
    snow.emitterConfig.minLife = 4.0f; // Longer life for gentle falling
    snow.emitterConfig.maxLife = 8.0f;
    snow.emitterConfig.minSize = 3.0f; // Varied snowflake sizes
    snow.emitterConfig.maxSize = 8.0f;
    snow.emitterConfig.gravity = Vector2D(-5.0f, 40.0f); // Slight wind drift + gentle fall
    snow.emitterConfig.textureID = "snowflake";
    snow.intensityMultiplier = 1.0f; // Normal multiplier for snow intensity
    return snow;
}

ParticleEffectDefinition ParticleManager::createFogEffect() {
    ParticleEffectDefinition fog("Fog", ParticleEffectType::Fog);
    fog.emitterConfig.spread = 1000.0f; // Wide spread to cover entire screen width
    fog.emitterConfig.emissionRate = 300.0f; // Higher emission rate for more particles
    fog.emitterConfig.minSpeed = 1.0f; // Slower movement for realistic fog drift
    fog.emitterConfig.maxSpeed = 10.0f;
    fog.emitterConfig.minLife = 8.0f; // Longer life for persistent fog coverage
    fog.emitterConfig.maxLife = 15.0f;
    fog.emitterConfig.minSize = 20.0f; // Smaller particles to avoid square appearance
    fog.emitterConfig.maxSize = 35.0f;
    fog.emitterConfig.gravity = Vector2D(5.0f, -5.0f); // Slight horizontal drift + very light upward float
    fog.emitterConfig.textureID = "fog";
    fog.intensityMultiplier = 0.8f; // Better intensity scaling for fog
    return fog;
}

ParticleEffectDefinition ParticleManager::createCloudyEffect() {
    ParticleEffectDefinition cloudy("Cloudy", ParticleEffectType::Fog);
    cloudy.emitterConfig.position = Vector2D(400, -50); // Start above screen
    cloudy.emitterConfig.direction = Vector2D(1.0f, 0.1f); // Mostly horizontal
    cloudy.emitterConfig.spread = 1200.0f; // Wide spread for cloud wisps
    cloudy.emitterConfig.emissionRate = 0.5f; // Lower emission rate for fewer clouds
    cloudy.emitterConfig.minSpeed = 12.0f; // Faster horizontal movement like clouds
    cloudy.emitterConfig.maxSpeed = 20.0f;
    cloudy.emitterConfig.minLife = 20.0f; // Long life - clouds move across sky
    cloudy.emitterConfig.maxLife = 35.0f;
    cloudy.emitterConfig.minSize = 60.0f; // Large cloud-like particles
    cloudy.emitterConfig.maxSize = 100.0f;
    cloudy.emitterConfig.gravity = Vector2D(12.0f, 2.0f); // Mostly horizontal drift like wind
    cloudy.emitterConfig.textureID = "cloud";
    cloudy.intensityMultiplier = 0.5f; // Moderate intensity for visible wisps
    return cloudy;
}

ParticleEffectDefinition ParticleManager::createFireEffect() {
    ParticleEffectDefinition fire("Fire", ParticleEffectType::Fire);
    fire.emitterConfig.position = Vector2D(0, 0); // Will be set when played
    fire.emitterConfig.direction = Vector2D(0, -1); // Upward flames
    fire.emitterConfig.spread = 30.0f; // Moderate spread for realistic flame shape
    fire.emitterConfig.emissionRate = 80.0f; // High emission for dense flames
    fire.emitterConfig.minSpeed = 20.0f; // Slow initial upward movement
    fire.emitterConfig.maxSpeed = 60.0f;
    fire.emitterConfig.minLife = 0.8f; // Short-lived for flickering effect
    fire.emitterConfig.maxLife = 2.0f;
    fire.emitterConfig.minSize = 2.0f; // Small flame particles
    fire.emitterConfig.maxSize = 8.0f;
    fire.emitterConfig.minColor = 0xFF4500FF; // Orange-red
    fire.emitterConfig.maxColor = 0xFFFF00FF; // Yellow
    fire.emitterConfig.gravity = Vector2D(0, -30.0f); // Negative gravity for upward movement
    fire.emitterConfig.windForce = Vector2D(10.0f, 0); // Slight horizontal flicker
    fire.emitterConfig.textureID = "fire_particle";
    fire.emitterConfig.blendMode = ParticleBlendMode::Additive; // Additive for glowing effect
    fire.emitterConfig.duration = -1.0f; // Infinite by default
    fire.intensityMultiplier = 1.0f;
    return fire;
}

ParticleEffectDefinition ParticleManager::createSmokeEffect() {
    ParticleEffectDefinition smoke("Smoke", ParticleEffectType::Smoke);
    smoke.emitterConfig.position = Vector2D(0, 0); // Will be set when played
    smoke.emitterConfig.direction = Vector2D(0, -1); // Upward smoke
    smoke.emitterConfig.spread = 45.0f; // Wide spread as smoke disperses
    smoke.emitterConfig.emissionRate = 25.0f; // Moderate emission for wispy smoke
    smoke.emitterConfig.minSpeed = 15.0f; // Slow upward drift
    smoke.emitterConfig.maxSpeed = 40.0f;
    smoke.emitterConfig.minLife = 3.0f; // Long-lived for realistic smoke trails
    smoke.emitterConfig.maxLife = 6.0f;
    smoke.emitterConfig.minSize = 8.0f; // Large smoke particles
    smoke.emitterConfig.maxSize = 20.0f;
    smoke.emitterConfig.minColor = 0x202020AA; // Very dark grey (almost black)
    smoke.emitterConfig.maxColor = 0x606060AA; // Medium grey
    smoke.emitterConfig.gravity = Vector2D(0, -20.0f); // Light upward movement
    smoke.emitterConfig.windForce = Vector2D(15.0f, 0); // Wind affects smoke
    smoke.emitterConfig.textureID = "smoke_particle";
    smoke.emitterConfig.blendMode = ParticleBlendMode::Alpha; // Standard alpha blending
    smoke.emitterConfig.duration = -1.0f; // Infinite by default
    smoke.intensityMultiplier = 1.0f;
    return smoke;
}

ParticleEffectDefinition ParticleManager::createSparksEffect() {
    ParticleEffectDefinition sparks("Sparks", ParticleEffectType::Sparks);
    sparks.emitterConfig.position = Vector2D(0, 0); // Will be set when played
    sparks.emitterConfig.direction = Vector2D(0, -1); // Initial upward burst
    sparks.emitterConfig.spread = 180.0f; // Wide spread for explosive spark pattern
    sparks.emitterConfig.emissionRate = 150.0f; // High burst rate
    sparks.emitterConfig.minSpeed = 80.0f; // Fast initial velocity
    sparks.emitterConfig.maxSpeed = 200.0f;
    sparks.emitterConfig.minLife = 0.3f; // Very short-lived for realistic sparks
    sparks.emitterConfig.maxLife = 1.2f;
    sparks.emitterConfig.minSize = 1.0f; // Small spark particles
    sparks.emitterConfig.maxSize = 3.0f;
    sparks.emitterConfig.minColor = 0xFFFF00FF; // Bright yellow
    sparks.emitterConfig.maxColor = 0xFF8C00FF; // Orange
    sparks.emitterConfig.gravity = Vector2D(0, 120.0f); // Strong downward gravity
    sparks.emitterConfig.windForce = Vector2D(5.0f, 0); // Slight wind resistance
    sparks.emitterConfig.textureID = "spark_particle";
    sparks.emitterConfig.blendMode = ParticleBlendMode::Additive; // Bright additive blending
    sparks.emitterConfig.duration = 2.0f; // Short burst effect
    sparks.emitterConfig.burstCount = 50; // Burst of sparks
    sparks.emitterConfig.enableCollision = true; // Sparks bounce off surfaces
    sparks.emitterConfig.bounceDamping = 0.6f; // Medium bounce damping
    sparks.intensityMultiplier = 1.0f;
    return sparks;
}

size_t ParticleManager::getActiveParticleCount() const {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return 0;
    }
    
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    size_t count = 0;
    for (const auto& particle : m_storage.hotData) {
        if (particle.isActive()) {
            count++;
        }
    }
    return count;
}

void ParticleManager::cleanupInactiveParticles() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    // More aggressive cleanup - remove particles with very low life or invisible alpha
    auto removeIt = std::remove_if(m_storage.hotData.begin(), m_storage.hotData.end(),
        [](const ParticleData& particle) {
            if (!particle.isActive()) return true;
            if (particle.life <= 0.0f) return true;
            
            // Also remove particles with very low alpha (essentially invisible)
            uint8_t alpha = particle.color & 0xFF;
            if (alpha <= 5) return true; // Remove if alpha is very low
            
            // Remove particles with very little life remaining (< 0.1 seconds)
            if (particle.life < 0.1f) return true;
            
            return false;
        });
    
    if (removeIt != m_storage.hotData.end()) {
        size_t removedCount = std::distance(removeIt, m_storage.hotData.end());
        m_storage.hotData.erase(removeIt, m_storage.hotData.end());
        
        // Also cleanup cold data to maintain consistency
        if (m_storage.coldData.size() > m_storage.hotData.size()) {
            m_storage.coldData.resize(m_storage.hotData.size());
        }
        
        PARTICLE_LOG("Cleaned up " + std::to_string(removedCount) + " inactive/faded particles");
    }
    
    // No need to clean up particle indices since effects don't own particles anymore
    
    // Remove inactive effect instances
    auto effectRemoveIt = std::remove_if(m_effectInstances.begin(), m_effectInstances.end(),
        [](const EffectInstance& effect) {
            return !effect.active;
        });
    
    if (effectRemoveIt != m_effectInstances.end()) {
        m_effectInstances.erase(effectRemoveIt, m_effectInstances.end());
        
        // Rebuild effect ID to index mapping
        m_effectIdToIndex.clear();
        for (size_t i = 0; i < m_effectInstances.size(); ++i) {
            m_effectIdToIndex[m_effectInstances[i].id] = i;
        }
    }
}

void ParticleManager::updateEffectInstance(EffectInstance& effect, float deltaTime) {
    if (!effect.active) {
        return;
    }
    
    // Update intensity transitions
    if (effect.currentIntensity != effect.targetIntensity) {
        float intensityDelta = effect.transitionSpeed * deltaTime;
        if (effect.currentIntensity < effect.targetIntensity) {
            effect.currentIntensity = std::min(effect.targetIntensity, 
                                             effect.currentIntensity + intensityDelta);
        } else {
            effect.currentIntensity = std::max(effect.targetIntensity, 
                                             effect.currentIntensity - intensityDelta);
        }
        
        // If we're fading out a weather effect and reached 0 intensity, deactivate it
        if (effect.isWeatherEffect && effect.targetIntensity == 0.0f && effect.currentIntensity <= 0.0f) {
            effect.active = false;
            PARTICLE_LOG("Weather effect " + effect.effectName + " faded out and deactivated");
            return;
        }
    }
    
    // Update emission timer
    effect.emissionTimer += deltaTime;
    
    // Update duration timer if this effect has a limited duration
    auto effectDefIt = m_effectDefinitions.find(effect.effectName);
    if (effectDefIt != m_effectDefinitions.end()) {
        const auto& effectDef = effectDefIt->second;
        
        if (effectDef.emitterConfig.duration > 0.0f) {
            effect.durationTimer += deltaTime;
            if (effect.durationTimer >= effectDef.emitterConfig.duration) {
                effect.active = false;
                return;
            }
        }
        
        // Emit new particles based on emission rate and intensity
        float baseEmissionRate = effectDef.emitterConfig.emissionRate;
        float intensityScaledRate = baseEmissionRate * effect.currentIntensity;
        float emissionInterval = 1.0f / std::max(1.0f, intensityScaledRate); // Prevent division by zero
        
        if (effect.emissionTimer >= emissionInterval) {
            // For weather effects, scale particle count by intensity for better visual impact
            int baseParticleCount = (effect.isWeatherEffect) ? 8 : 5; // More particles for weather
            int particlesToEmit = static_cast<int>(effect.currentIntensity * baseParticleCount);
            
            // Ensure minimum particles for visual continuity, but respect zero intensity
            if (effect.currentIntensity > 0.1f && particlesToEmit < 1) {
                particlesToEmit = 1;
            }
            
            for (int i = 0; i < particlesToEmit; ++i) {
                createParticleForEffect(effect, effectDef);
            }
            effect.emissionTimer = 0.0f;
            
            // Reduced logging: only log weather effects periodically (every 10 seconds)
            static uint64_t emissionLogCounter = 0;
            if (effect.isWeatherEffect && (++emissionLogCounter % 600 == 0)) {
                PARTICLE_LOG("Weather Effect: " + effect.effectName + " emitting " + 
                            std::to_string(particlesToEmit) + " particles (Intensity: " + 
                            std::to_string(effect.currentIntensity) + ")");
            }
        }
    }
    
    // Note: Particles are now updated independently in the main update loop
}

void ParticleManager::createParticleForEffect(EffectInstance& effect, const ParticleEffectDefinition& effectDef) {
    // PARTICLE_LOG("*** CREATING PARTICLE for effect: " + effect.effectName + " - Storage size before: " + std::to_string(m_storage.hotData.size()));
    
    // Create a new particle
    ParticleData particle;
    ParticleColdData coldData;

    // Generate random values within the effect's parameters
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    // Position - start from effect position with some spread
    std::uniform_real_distribution<float> spreadDist(-effectDef.emitterConfig.spread, effectDef.emitterConfig.spread);
    particle.position = Vector2D(
        effect.position.getX() + spreadDist(gen),
        effect.position.getY() + spreadDist(gen)
    );

    // Velocity
    std::uniform_real_distribution<float> speedDist(effectDef.emitterConfig.minSpeed, effectDef.emitterConfig.maxSpeed);
    std::uniform_real_distribution<float> angleDist(-effectDef.emitterConfig.spread * 0.017453f, effectDef.emitterConfig.spread * 0.017453f); // Convert to radians
    
    float speed = speedDist(gen);
    float angle = angleDist(gen);
    particle.velocity = Vector2D(
        speed * sin(angle),
        speed * cos(angle)
    );

    // Apply gravity
    coldData.acceleration = effectDef.emitterConfig.gravity;

    // Size
    std::uniform_real_distribution<float> sizeDist(effectDef.emitterConfig.minSize, effectDef.emitterConfig.maxSize);
    coldData.size = sizeDist(gen);

    // Life
    std::uniform_real_distribution<float> lifeDist(effectDef.emitterConfig.minLife, effectDef.emitterConfig.maxLife);
    particle.maxLife = lifeDist(gen);
    particle.life = particle.maxLife;

    // Color based on effect type
    if (effectDef.type == ParticleEffectType::Rain) {
        particle.color = 0x4080FFFF; // Blue-ish for rain
    } else if (effectDef.type == ParticleEffectType::Snow) {
        particle.color = 0xFFFFFFFF; // White for snow
    } else if (effectDef.type == ParticleEffectType::Fog) {
        if (effectDef.name == "Cloudy") {
            particle.color = 0xF8F8F8C0; // Light white with higher alpha for more visible clouds
        } else {
            particle.color = 0xCCCCCC88; // Semi-transparent gray for fog
        }
    } else if (effectDef.type == ParticleEffectType::Fire) {
        // Fire particles with realistic colors - random between orange-red and yellow
        std::uniform_int_distribution<int> colorChoice(0, 2);
        switch (colorChoice(gen)) {
            case 0: particle.color = 0xFF4500FF; break; // Orange-red
            case 1: particle.color = 0xFF6500FF; break; // Red-orange  
            case 2: particle.color = 0xFFFF00FF; break; // Yellow
        }
    } else if (effectDef.type == ParticleEffectType::Smoke) {
        // Smoke particles with realistic black/grey variations
        std::uniform_int_distribution<int> colorChoice(0, 3);
        switch (colorChoice(gen)) {
            case 0: particle.color = 0x404040FF; break; // Dark grey
            case 1: particle.color = 0x606060FF; break; // Medium grey
            case 2: particle.color = 0x808080FF; break; // Light grey
            case 3: particle.color = 0x202020FF; break; // Very dark grey (almost black)
        }
    } else if (effectDef.type == ParticleEffectType::Sparks) {
        // Sparks with bright yellow/orange colors
        std::uniform_int_distribution<int> colorChoice(0, 1);
        switch (colorChoice(gen)) {
            case 0: particle.color = 0xFFFF00FF; break; // Bright yellow
            case 1: particle.color = 0xFF8C00FF; break; // Orange
        }
    } else {
        particle.color = 0xFFFFFFFF; // Default white
    }

    // Set active and visible
    particle.setActive(true);
    particle.setVisible(true);
    
    // Mark weather particles for batch management
    if (effect.isWeatherEffect) {
        particle.setWeatherParticle(true);
        particle.generationId = effect.currentGenerationId;
    }

    // Add to storage (Note: Caller must hold appropriate lock)
    // Add to hot and cold data
    m_storage.hotData.push_back(particle);
    m_storage.coldData.push_back(coldData);
    
    // PARTICLE_LOG("*** PARTICLE CREATED: Index " + std::to_string(particleIndex) + 
    //             " at position (" + std::to_string(particle.position.getX()) + 
    //             ", " + std::to_string(particle.position.getY()) + ") - Storage size now: " + 
    //             std::to_string(m_storage.hotData.size()) + " active=" + (particle.isActive() ? "true" : "false"));
}

void ParticleManager::updateParticle(ParticleData& particle, float deltaTime) {
    if (!particle.isActive()) {
        return;
    }
    
    // Update particle life
    particle.life -= deltaTime;
    if (particle.life <= 0.0f) {
        particle.setActive(false);
        return;
    }
    
    // Apply alpha fade as particle ages (fade out over last 25% of life)
    float lifeRatio = particle.getLifeRatio();
    if (lifeRatio < 0.25f || particle.isFadingOut()) {
        // Calculate fade ratio - particles that are marked for fade-out fade faster
        float fadeMultiplier = particle.isFadingOut() ? 4.0f : 1.0f;
        float fadeRatio = std::max(0.0f, lifeRatio / 0.25f) / fadeMultiplier;
        
        // Extract current alpha and apply fade
        uint8_t currentAlpha = particle.color & 0xFF;
        uint8_t newAlpha = static_cast<uint8_t>(currentAlpha * fadeRatio);
        
        // Update alpha component while preserving RGB
        particle.color = (particle.color & 0xFFFFFF00) | newAlpha;
        
        // Mark for removal if alpha is very low
        if (newAlpha <= 5) {
            particle.setActive(false);
            return;
        }
    }
    
    // Update particle position with velocity
    particle.position.setX(particle.position.getX() + particle.velocity.getX() * deltaTime);
    particle.position.setY(particle.position.getY() + particle.velocity.getY() * deltaTime);
    
    // Apply acceleration (gravity, wind, etc.) if available
    // Find the corresponding cold data for this particle
    auto particleIndex = &particle - &m_storage.hotData[0];
    if (particleIndex < static_cast<ptrdiff_t>(m_storage.coldData.size())) {
        auto& coldData = m_storage.coldData[particleIndex];
        particle.velocity.setX(particle.velocity.getX() + coldData.acceleration.getX() * deltaTime);
        particle.velocity.setY(particle.velocity.getY() + coldData.acceleration.getY() * deltaTime);
    }
}

// Global control methods
void ParticleManager::setGlobalPause(bool paused) {
    m_globallyPaused.store(paused, std::memory_order_release);
}

bool ParticleManager::isGloballyPaused() const {
    return m_globallyPaused.load(std::memory_order_acquire);
}

void ParticleManager::setGlobalVisibility(bool visible) {
    m_globallyVisible.store(visible, std::memory_order_release);
}

bool ParticleManager::isGloballyVisible() const {
    return m_globallyVisible.load(std::memory_order_acquire);
}

// Effect management methods
bool ParticleManager::isEffectPlaying(uint32_t effectId) const {
    auto it = m_effectIdToIndex.find(effectId);
    if (it != m_effectIdToIndex.end() && it->second < m_effectInstances.size()) {
        return m_effectInstances[it->second].active;
    }
    return false;
}

// Performance and capacity methods
ParticlePerformanceStats ParticleManager::getPerformanceStats() const {
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    std::lock_guard<std::mutex> statsLock(m_statsMutex);
    return m_performanceStats;
}

void ParticleManager::resetPerformanceStats() {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_performanceStats.reset();
}

size_t ParticleManager::getMaxParticleCapacity() const {
    return m_storage.capacity();
}

void ParticleManager::setMaxParticles(size_t maxParticles) {
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    m_storage.reserve(maxParticles);
}

// WorkerBudget threading implementation
void ParticleManager::enableWorkerBudgetThreading(bool enable) {
    m_useWorkerBudgetThreading.store(enable, std::memory_order_release);
    if (enable) {
        PARTICLE_LOG("WorkerBudget threading enabled for ParticleManager");
    } else {
        PARTICLE_LOG("WorkerBudget threading disabled for ParticleManager");
    }
}

void ParticleManager::updateWithWorkerBudget(float deltaTime, size_t particleCount) {
    if (!m_initialized.load(std::memory_order_acquire) ||
        m_globallyPaused.load(std::memory_order_acquire)) {
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    try {
        // Check if we should use WorkerBudget threading
        bool useThreading = (particleCount >= m_threadingThreshold &&
                           m_useThreading.load(std::memory_order_acquire) &&
                           m_useWorkerBudgetThreading.load(std::memory_order_acquire) &&
                           Hammer::ThreadSystem::Exists());

        if (useThreading) {
            auto& threadSystem = Hammer::ThreadSystem::Instance();
            size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
            
            // Check queue pressure before submitting tasks
            size_t queueSize = threadSystem.getQueueSize();
            size_t queueCapacity = threadSystem.getQueueCapacity();
            size_t pressureThreshold = (queueCapacity * 9) / 10; // 90% capacity threshold
            
            if (queueSize > pressureThreshold) {
                // Graceful degradation: fallback to single-threaded processing
                PARTICLE_LOG("Queue pressure detected (" + std::to_string(queueSize) + "/" + 
                           std::to_string(queueCapacity) + "), using single-threaded processing");
                update(deltaTime); // Use regular single-threaded update
                return;
            }
            
            // Use WorkerBudget system for optimal resource allocation
            Hammer::WorkerBudget budget = Hammer::calculateWorkerBudget(availableWorkers);
            
            // Get optimal worker count with buffer allocation for particle workload
            size_t optimalWorkerCount = budget.getOptimalWorkerCount(budget.particleAllocated, particleCount, 1000);
            
            // Dynamic batch sizing based on queue pressure for optimal performance
            size_t minParticlesPerBatch = 1000;
            size_t maxBatches = 4;
            
            // Adjust batch strategy based on queue pressure
            double queuePressure = static_cast<double>(queueSize) / queueCapacity;
            if (queuePressure > 0.5) {
                // High pressure: use fewer, larger batches to reduce queue overhead
                minParticlesPerBatch = 1500;
                maxBatches = 2;
            } else if (queuePressure < 0.25) {
                // Low pressure: can use more batches for better parallelization
                minParticlesPerBatch = 800;
                maxBatches = 4;
            }
            
            size_t batchCount = std::min(optimalWorkerCount, particleCount / minParticlesPerBatch);
            batchCount = std::max(size_t(1), std::min(batchCount, maxBatches));
            
            size_t particlesPerBatch = particleCount / batchCount;
            size_t remainingParticles = particleCount % batchCount;
            
            // Submit optimized particle update batches
            for (size_t i = 0; i < batchCount; ++i) {
                size_t start = i * particlesPerBatch;
                size_t end = start + particlesPerBatch;
                
                // Add remaining particles to last batch
                if (i == batchCount - 1) {
                    end += remainingParticles;
                }
                
                threadSystem.enqueueTask([this, start, end, deltaTime]() {
                    updateParticleBatch(start, end, deltaTime);
                }, Hammer::TaskPriority::High, "Particle_OptimalBatch");
            }
            
        } else {
            // Single-threaded processing
            update(deltaTime);
        }

        // Performance tracking
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        
        // Update frame counter
        uint64_t currentFrame = m_frameCounter.fetch_add(1, std::memory_order_relaxed);
        
        // Periodic performance summary (every 300 frames ~5 seconds)
        if (currentFrame % 300 == 0) {
            std::lock_guard<std::mutex> statsLock(m_statsMutex);
            recordPerformance(false, duration, particleCount);
            
            if (particleCount > 0) {
                PARTICLE_LOG("Particle Summary - Count: " + std::to_string(particleCount) + 
                           ", Update: " + std::to_string(duration) + "ms" +
                           ", Effects: " + std::to_string(m_effectInstances.size()));
            }
        }
        
    } catch (const std::exception& e) {
        PARTICLE_LOG("Exception in ParticleManager::updateWithWorkerBudget: " + std::string(e.what()));
    }
}

void ParticleManager::updateParticleBatch(size_t start, size_t end, float deltaTime) {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    // Process particle batch with shared lock for read access
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    
    // Update particles in the specified range
    for (size_t i = start; i < end && i < m_storage.hotData.size(); ++i) {
        auto& particle = m_storage.hotData[i];
        if (particle.isActive()) {
            updateParticle(particle, deltaTime);
        }
    }
    
    // Also update effect instances in this batch (shared across batches, but thread-safe)
    size_t effectBatchSize = m_effectInstances.size() / 4; // Rough division
    size_t effectStart = (start * m_effectInstances.size()) / (end - start + 1);
    size_t effectEnd = std::min(effectStart + effectBatchSize, m_effectInstances.size());
    
    for (size_t i = effectStart; i < effectEnd; ++i) {
        auto& effect = m_effectInstances[i];
        if (effect.active) {
            updateEffectInstance(effect, deltaTime);
        }
    }
}
