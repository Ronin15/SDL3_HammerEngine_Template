/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/ParticleManager.hpp"
#include "managers/TextureManager.hpp"
#include "managers/EventManager.hpp"
#include "core/Logger.hpp"
#include "core/GameTime.hpp"
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

    // Pause particle updates
    m_globallyPaused.store(true, std::memory_order_release);

    // Clean up particles
    cleanupInactiveParticles();

    PARTICLE_LOG("ParticleManager prepared for state transition");
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
        
        // Check if this is a background particle (rain = blue-ish, snow = white)
        uint32_t color = particle.color;
        bool isBackground = false;
        
        // Rain particles are blue-ish (0x4080FFFF)
        if ((color & 0xFFFFFF00) == 0x4080FF00) {
            isBackground = true;
        }
        // Snow particles are white (0xFFFFFFFF with any alpha)
        else if ((color & 0xFFFFFF00) == 0xFFFFFF00) {
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
    ParticleEffectDefinition cloudy("Cloudy", ParticleEffectType::Fog); // Reuse Fog type for similar behavior
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
