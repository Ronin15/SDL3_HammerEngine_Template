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

        // Register built-in effects
        registerBuiltInEffects();

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
        PARTICLE_LOG("Skipping render - globally paused or invisible");
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Debug: Log render attempt
    PARTICLE_LOG("Render called with " + std::to_string(m_storage.hotData.size()) + " particles in storage");

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
            auto particleIndex = &particle - &m_storage.hotData[0];
            if (particleIndex < m_storage.coldData.size()) {
                size = m_storage.coldData[particleIndex].size;
            }
        }

        // Debug log first few particles
        if (renderCount <= 2) {
            PARTICLE_LOG("Rendering particle " + std::to_string(renderCount) + 
                        " at (" + std::to_string(particle.position.getX()) + 
                        ", " + std::to_string(particle.position.getY()) + 
                        ") size=" + std::to_string(size) + 
                        " color=0x" + std::to_string(particle.color));
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

    PARTICLE_LOG("Rendered " + std::to_string(renderCount) + " particles total");

    auto endTime = std::chrono::high_resolution_clock::now();
    double timeMs = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count() / 1000.0;
    recordPerformance(true, timeMs, m_storage.size());
}

uint32_t ParticleManager::playEffect(const std::string& effectName, const Vector2D& position, float intensity) {
    PARTICLE_LOG("*** PLAY EFFECT CALLED: " + effectName + " at (" + std::to_string(position.getX()) + ", " + std::to_string(position.getY()) + ") intensity=" + std::to_string(intensity));
    
    auto it = m_effectDefinitions.find(effectName);
    if (it == m_effectDefinitions.end()) {
        PARTICLE_LOG("ERROR: Effect not found: " + effectName);
        PARTICLE_LOG("Available effects: " + std::to_string(m_effectDefinitions.size()));
        for (const auto& pair : m_effectDefinitions) {
            PARTICLE_LOG("  - " + pair.first);
        }
        return 0;
    }

    auto& definition = it->second;
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
    auto it = m_effectIdToIndex.find(effectId);
    if (it != m_effectIdToIndex.end()) {
        m_effectInstances[it->second].active = false;
        PARTICLE_LOG("Effect stopped: ID " + std::to_string(effectId));
    }
}

void ParticleManager::stopWeatherEffects(float transitionTime) {
    PARTICLE_LOG("*** STOPPING ALL WEATHER EFFECTS (transition: " + std::to_string(transitionTime) + "s)");
    
    int stoppedCount = 0;
    
    // Stop emitter effects - particles will naturally fade out
    for (auto& effect : m_effectInstances) {
        if (effect.active && effect.isWeatherEffect) {
            if (transitionTime <= 0.0f) {
                // Immediate stop - deactivate effect
                effect.active = false;
            } else {
                // Gradual fade out by setting target intensity to 0
                effect.targetIntensity = 0.0f;
                effect.transitionSpeed = 1.0f / transitionTime;
            }
            stoppedCount++;
        }
    }
    
    // If immediate stop, clear all weather particles
    if (transitionTime <= 0.0f) {
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
    
    // Stop all existing weather effects first to prevent stacking
    stopWeatherEffects(0.5f); // Quick fade out
    
    // Map weather type to particle effect
    std::string effectName;
    if (weatherType == "Rainy") effectName = "Rain";
    else if (weatherType == "Snowy") effectName = "Snow";
    else if (weatherType == "Foggy") effectName = "Fog";
    else if (weatherType == "Stormy") effectName = "Rain"; // Stormy = heavy rain
    
    PARTICLE_LOG("Mapped weather type '" + weatherType + "' to effect '" + effectName + "'");

    if (!effectName.empty()) {
        // Position particles across the top of the screen for weather effects
        Vector2D weatherPosition(960, -50); // Center-top of screen with some margin
        uint32_t effectId = playEffect(effectName, weatherPosition, intensity);
        
        // Mark this effect as a weather effect for tracking
        auto it = m_effectIdToIndex.find(effectId);
        if (it != m_effectIdToIndex.end()) {
            m_effectInstances[it->second].isWeatherEffect = true;
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
    
    PARTICLE_LOG("Built-in effects registered: " + std::to_string(m_effectDefinitions.size()));
    for (const auto& pair : m_effectDefinitions) {
        PARTICLE_LOG("  - Effect: " + pair.first);
    }
    
    // More effects can be added as needed
}

ParticleEffectDefinition ParticleManager::createRainEffect() {
    ParticleEffectDefinition rain("Rain", ParticleEffectType::Rain);
    rain.emitterConfig.spread = 50.0f;
    rain.emitterConfig.emissionRate = 100.0f;
    rain.emitterConfig.minSpeed = 100.0f;
    rain.emitterConfig.maxSpeed = 200.0f;
    rain.emitterConfig.minLife = 2.0f;
    rain.emitterConfig.maxLife = 4.0f;
    rain.emitterConfig.minSize = 20.0f;
    rain.emitterConfig.maxSize = 30.0f;
    rain.emitterConfig.gravity = Vector2D(0, 200.0f);
    rain.emitterConfig.textureID = "raindrop";
    rain.intensityMultiplier = 1.0f;
    return rain;
}

ParticleEffectDefinition ParticleManager::createSnowEffect() {
    ParticleEffectDefinition snow("Snow", ParticleEffectType::Snow);
    snow.emitterConfig.spread = 30.0f;
    snow.emitterConfig.emissionRate = 50.0f;
    snow.emitterConfig.minSpeed = 10.0f;
    snow.emitterConfig.maxSpeed = 30.0f;
    snow.emitterConfig.minLife = 3.0f;
    snow.emitterConfig.maxLife = 6.0f;
    snow.emitterConfig.minSize = 15.0f;
    snow.emitterConfig.maxSize = 25.0f;
    snow.emitterConfig.gravity = Vector2D(0, 50.0f);
    snow.emitterConfig.textureID = "snowflake";
    snow.intensityMultiplier = 0.8f;
    return snow;
}

ParticleEffectDefinition ParticleManager::createFogEffect() {
    ParticleEffectDefinition fog("Fog", ParticleEffectType::Fog);
    fog.emitterConfig.spread = 100.0f;
    fog.emitterConfig.emissionRate = 30.0f;
    fog.emitterConfig.minSpeed = 5.0f;
    fog.emitterConfig.maxSpeed = 15.0f;
    fog.emitterConfig.minLife = 5.0f;
    fog.emitterConfig.maxLife = 10.0f;
    fog.emitterConfig.minSize = 15.0f;
    fog.emitterConfig.maxSize = 40.0f;
    fog.emitterConfig.gravity = Vector2D(0, -10.0f);
    fog.emitterConfig.textureID = "fog";
    fog.intensityMultiplier = 0.5f;
    return fog;
}

size_t ParticleManager::getActiveParticleCount() const {
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
        
        // Emit new particles based on emission rate
        float emissionInterval = 1.0f / effectDef.emitterConfig.emissionRate;
        if (effect.emissionTimer >= emissionInterval) {
            // Create actual particles
            int particlesToEmit = static_cast<int>(effect.currentIntensity * 5.0f); // Scale by intensity
            for (int i = 0; i < particlesToEmit; ++i) {
                createParticleForEffect(effect, effectDef);
            }
            effect.emissionTimer = 0.0f;
            
            PARTICLE_LOG("Emitted " + std::to_string(particlesToEmit) + " particles for effect: " + effect.effectName +
                        " (Intensity: " + std::to_string(effect.currentIntensity) + ")");
        }
    }
    
    // Note: Particles are now updated independently in the main update loop
}

void ParticleManager::createParticleForEffect(EffectInstance& effect, const ParticleEffectDefinition& effectDef) {
    PARTICLE_LOG("*** CREATING PARTICLE for effect: " + effect.effectName + " - Storage size before: " + std::to_string(m_storage.hotData.size()));
    
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
        particle.color = 0xCCCCCC88; // Semi-transparent gray for fog
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
    
    size_t particleIndex = m_storage.hotData.size() - 1;
    
    PARTICLE_LOG("*** PARTICLE CREATED: Index " + std::to_string(particleIndex) + 
                " at position (" + std::to_string(particle.position.getX()) + 
                ", " + std::to_string(particle.position.getY()) + ") - Storage size now: " + 
                std::to_string(m_storage.hotData.size()) + " active=" + (particle.isActive() ? "true" : "false"));
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
