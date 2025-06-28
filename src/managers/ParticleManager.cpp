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
        PARTICLE_INFO("ParticleManager already initialized");
        return true;
    }

    try {
        // Pre-allocate storage for better performance
        // FIXED: Reserve much larger capacity for high-emission weather effects
        constexpr size_t INITIAL_CAPACITY = DEFAULT_MAX_PARTICLES * 3; // 30,000 particles
        m_storage.reserve(INITIAL_CAPACITY);

        // Built-in effects will be registered by GameEngine after init
        
        m_initialized.store(true, std::memory_order_release);
        m_isShutdown = false;

        PARTICLE_INFO("ParticleManager initialized successfully");
        return true;

    } catch (const std::exception& e) {
        PARTICLE_ERROR("Failed to initialize ParticleManager: " + std::string(e.what()));
        return false;
    }
}

void ParticleManager::clean() {
    if (m_isShutdown) {
        return;
    }

    PARTICLE_INFO("ParticleManager shutting down...");

    // Mark as shutting down
    m_isShutdown = true;
    m_initialized.store(false, std::memory_order_release);

    // Clear all storage
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    m_storage.clear();
    m_effectDefinitions.clear();
    m_effectInstances.clear();
    m_effectIdToIndex.clear();

    PARTICLE_INFO("ParticleManager shutdown complete");
}

void ParticleManager::prepareForStateTransition() {
    PARTICLE_INFO("Preparing ParticleManager for state transition...");

    // COMPREHENSIVE CLEANUP: Ensure all effects and particles are properly cleaned up
    // This prevents effects from continuing when re-entering a state
    
    // Use try_lock to avoid deadlock issues during testing
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        // If we can't get the lock immediately, force a simpler cleanup
        PARTICLE_WARN("Could not acquire lock for state transition, using simpler cleanup");
        m_globallyPaused.store(true, std::memory_order_release);
        
        // Simple atomic cleanup without locking
        for (auto& effect : m_effectInstances) {
            if (effect.isWeatherEffect || effect.isIndependentEffect) {
                effect.active = false;
            }
        }
        
        m_globallyPaused.store(false, std::memory_order_release);
        PARTICLE_INFO("ParticleManager state transition complete (simple cleanup)");
        return;
    }
    
    // Pause system to prevent new emissions during cleanup
    m_globallyPaused.store(true, std::memory_order_release);
    
    // 1. Stop ALL weather effects
    int weatherEffectsStopped = 0;
    auto it = m_effectInstances.begin();
    while (it != m_effectInstances.end()) {
        if (it->isWeatherEffect) {
            PARTICLE_INFO("Removing weather effect: " + it->effectName + " (ID: " + std::to_string(it->id) + ")");
            m_effectIdToIndex.erase(it->id);
            it = m_effectInstances.erase(it);
            weatherEffectsStopped++;
        } else {
            ++it;
        }
    }
    
    // 2. Remove ALL remaining effects (independent and regular) for complete state cleanup
    int independentEffectsStopped = 0;
    int regularEffectsStopped = 0;
    
    // First pass: count and deactivate
    for (auto& effect : m_effectInstances) {
        if (effect.active) {
            if (effect.isIndependentEffect) {
                independentEffectsStopped++;
            } else {
                regularEffectsStopped++;
            }
            effect.active = false;
        }
    }
    
    // Second pass: remove ALL non-weather effects from the list
    auto newEnd = std::remove_if(m_effectInstances.begin(), m_effectInstances.end(),
        [](const EffectInstance& effect) {
            return !effect.isWeatherEffect; // Remove everything except weather (already removed)
        });
    m_effectInstances.erase(newEnd, m_effectInstances.end());
    
    // Third pass: rebuild the effect ID index for the remaining effects (should be empty)
    m_effectIdToIndex.clear();
    for (size_t i = 0; i < m_effectInstances.size(); ++i) {
        m_effectIdToIndex[m_effectInstances[i].id] = i;
    }
    
    // 3. Clear ALL particles (both weather and independent) - MORE AGGRESSIVE APPROACH
    int particlesCleared = 0;
    for (auto& particle : m_storage.particles) {
        if (particle.isActive()) {
            particle.setActive(false);
            particle.life = 0.0f;  // Ensure life is zero for double safety
            particlesCleared++;
        }
    }
    
    // 3.5. IMMEDIATE COMPLETE CLEANUP - Remove ALL particles to ensure zero count
    size_t totalParticlesBefore = m_storage.particles.size();
    m_storage.particles.clear();  // Complete clear to ensure zero particles
    
    PARTICLE_INFO("Complete particle cleanup: cleared " + std::to_string(particlesCleared) + 
                " active particles, removed total of " + std::to_string(totalParticlesBefore) + " particles from storage");
    
    // 4. Rebuild effect index mapping for any remaining effects
    m_effectIdToIndex.clear();
    for (size_t i = 0; i < m_effectInstances.size(); ++i) {
        m_effectIdToIndex[m_effectInstances[i].id] = i;
    }
    
    // 5. Reset performance stats (safe operation)
    resetPerformanceStats();
    
    // Release lock before resuming
    lock.unlock();
    
    // Resume system
    m_globallyPaused.store(false, std::memory_order_release);
    
    PARTICLE_INFO("ParticleManager state transition complete - stopped " + 
                std::to_string(weatherEffectsStopped) + " weather effects, " +
                std::to_string(independentEffectsStopped) + " independent effects, " +
                std::to_string(regularEffectsStopped) + " regular effects, cleared " +
                std::to_string(particlesCleared) + " particles");
}

void ParticleManager::update(float deltaTime) {
    if (!m_initialized.load(std::memory_order_acquire) ||
        m_globallyPaused.load(std::memory_order_acquire)) {
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    try {
        // Phase 1: Update effect instances (emission, timing) - MAIN THREAD ONLY
        updateEffectInstances(deltaTime);
        
        // Phase 2: Get snapshot of particle count for threading decision
        size_t totalParticleCount = m_storage.particles.size();
        if (totalParticleCount == 0) {
            return;
        }
        
        // Phase 3: Update particle physics with proper threading strategy
        // Use WorkerBudget system for optimal resource allocation
        bool useThreading = (totalParticleCount >= m_threadingThreshold &&
                           m_useThreading.load(std::memory_order_acquire) &&
                           Hammer::ThreadSystem::Exists());
        
        if (useThreading) {
            updateParticlesThreaded(deltaTime, totalParticleCount);
        } else {
            updateParticlesSingleThreaded(deltaTime, totalParticleCount);
        }
        
        // Phase 4: More frequent memory management to prevent leaks
        uint64_t currentFrame = m_frameCounter.fetch_add(1, std::memory_order_relaxed);
        if (currentFrame % 300 == 0) { // Every 5 seconds at 60fps instead of 20
            compactParticleStorageIfNeeded();
        }
        
        // Deep cleanup less frequently but more thoroughly
        if (currentFrame % 1800 == 0) { // Every 30 seconds
            compactParticleStorage();
        }
        
        // Phase 5: Performance tracking (less frequent to reduce overhead)
        auto endTime = std::chrono::high_resolution_clock::now();
        double timeMs = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count() / 1000.0;
        
        if (currentFrame % 600 == 0) {
            size_t activeCount = countActiveParticles();
            recordPerformance(false, timeMs, activeCount);
            
            if (activeCount > 0) {
                PARTICLE_DEBUG("Particle Summary - Count: " + std::to_string(activeCount) + 
                           ", Update: " + std::to_string(timeMs) + "ms" +
                           ", Effects: " + std::to_string(m_effectInstances.size()));
            }
        }
        
    } catch (const std::exception& e) {
        PARTICLE_ERROR("Exception in ParticleManager::update: " + std::string(e.what()));
    }
}

void ParticleManager::render(SDL_Renderer* renderer, float cameraX, float cameraY) {
    if (m_globallyPaused.load(std::memory_order_acquire) || !m_globallyVisible.load(std::memory_order_acquire)) {
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // FIXED: Unified storage - no more synchronization issues
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    int renderCount = 0;
    
    for (const auto& particle : m_storage.particles) {
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

        // FIXED: Size is directly accessible from unified particle - no synchronization issues
        float size = std::max(0.5f, std::min(particle.size, 50.0f));

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
        PARTICLE_DEBUG("Particle Summary - Total: " + std::to_string(m_storage.particles.size()) + 
                    ", Active: " + std::to_string(renderCount) + 
                    ", Effects: " + std::to_string(m_effectInstances.size()));
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double timeMs = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count() / 1000.0;
    recordPerformance(true, timeMs, m_storage.particles.size());
}

void ParticleManager::renderBackground(SDL_Renderer* renderer, float cameraX, float cameraY) {
    if (m_globallyPaused.load(std::memory_order_acquire) || !m_globallyVisible.load(std::memory_order_acquire)) {
        return;
    }

    // FIXED: Unified storage - no more synchronization issues
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    
    for (const auto& particle : m_storage.particles) {
        
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

        // FIXED: Direct size access from unified storage
        float size = std::max(0.5f, std::min(particle.size, 50.0f));

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

    // FIXED: Unified storage - no more synchronization issues
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    
    for (const auto& particle : m_storage.particles) {
        
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

        // FIXED: Direct size access from unified storage
        float size = std::max(0.5f, std::min(particle.size, 50.0f));

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
    PARTICLE_INFO("*** PLAY EFFECT CALLED: " + effectName + " at (" + std::to_string(position.getX()) + ", " + std::to_string(position.getY()) + ") intensity=" + std::to_string(intensity));
    
    // Thread safety: Use exclusive lock for modifying effect instances
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    auto it = m_effectDefinitions.find(effectName);
    if (it == m_effectDefinitions.end()) {
        PARTICLE_ERROR("ERROR: Effect not found: " + effectName);
        PARTICLE_INFO("Available effects: " + std::to_string(m_effectDefinitions.size()));
        for (const auto& pair : m_effectDefinitions) {
            PARTICLE_INFO("  - " + pair.first);
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

    PARTICLE_INFO("Effect successfully started: " + effectName + " (ID: " + std::to_string(instance.id) + ") - Total active effects: " + std::to_string(m_effectInstances.size()));
    return instance.id;
}

void ParticleManager::stopEffect(uint32_t effectId) {
    // Thread safety: Use exclusive lock for modifying effect instances
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    auto it = m_effectIdToIndex.find(effectId);
    if (it != m_effectIdToIndex.end()) {
        m_effectInstances[it->second].active = false;
        PARTICLE_INFO("Effect stopped: ID " + std::to_string(effectId));
    }
}

void ParticleManager::stopWeatherEffects(float transitionTime) {
    PARTICLE_INFO("*** STOPPING ALL WEATHER EFFECTS (transition: " + std::to_string(transitionTime) + "s)");
    
    // Use try_lock to avoid deadlocks during testing
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        PARTICLE_WARN("Could not acquire lock for stopping weather effects, skipping");
        return;
    }
    
    int stoppedCount = 0;
    
    // Clean approach: Remove ALL weather effects to prevent accumulation
    auto it = m_effectInstances.begin();
    while (it != m_effectInstances.end()) {
        if (it->isWeatherEffect) {
            PARTICLE_DEBUG("DEBUG: Removing weather effect: " + it->effectName + " (ID: " + std::to_string(it->id) + ")");
            
            // Remove from ID mapping
            m_effectIdToIndex.erase(it->id);
            
            // Remove the effect entirely
            it = m_effectInstances.erase(it);
            stoppedCount++;
        } else {
            ++it;
        }
    }
    
    // Rebuild index mapping for remaining effects
    m_effectIdToIndex.clear();
    for (size_t i = 0; i < m_effectInstances.size(); ++i) {
        m_effectIdToIndex[m_effectInstances[i].id] = i;
    }
    
    // Clear weather particles directly here to avoid re-locking
    if (transitionTime <= 0.0f) {
        // Clear immediately without calling clearWeatherGeneration to avoid re-locking
        int affectedCount = 0;
        for (auto& particle : m_storage.particles) {
            if (particle.isActive() && particle.isWeatherParticle()) {
                particle.setActive(false);
                affectedCount++;
            }
        }
        PARTICLE_INFO("Cleared " + std::to_string(affectedCount) + " weather particles immediately");
    } else {
        // Set fade-out for particles with transition time
        int affectedCount = 0;
        for (auto& particle : m_storage.particles) {
            if (particle.isActive() && particle.isWeatherParticle()) {
                particle.setFadingOut(true);
                particle.life = std::min(particle.life, transitionTime);
                affectedCount++;
            }
        }
        PARTICLE_INFO("Cleared " + std::to_string(affectedCount) + " weather particles with fade time: " + std::to_string(transitionTime) + "s");
    }
    
    PARTICLE_INFO("Stopped and removed " + std::to_string(stoppedCount) + " weather effects");
}

void ParticleManager::clearWeatherGeneration(uint8_t generationId, float fadeTime) {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    int affectedCount = 0;
    
    for (auto& particle : m_storage.particles) {
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
    
    PARTICLE_INFO("Cleared " + std::to_string(affectedCount) + " weather particles" + 
                (generationId > 0 ? " (generation " + std::to_string(generationId) + ")" : "") +
                " with fade time: " + std::to_string(fadeTime) + "s");
}

void ParticleManager::triggerWeatherEffect(const std::string& weatherType, float intensity, float transitionTime) {
    PARTICLE_INFO("*** WEATHER EFFECT TRIGGERED: " + weatherType + " intensity=" + std::to_string(intensity));
    
    // Use smooth transitions for better visual quality
    float actualTransitionTime = (transitionTime > 0.0f) ? transitionTime : 1.5f;
    
    // DEADLOCK FIX: Use single lock scope for entire operation
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    // Clear existing weather effects first (without calling stopWeatherEffects to avoid re-locking)
    int stoppedCount = 0;
    auto it = m_effectInstances.begin();
    while (it != m_effectInstances.end()) {
        if (it->isWeatherEffect) {
            PARTICLE_DEBUG("DEBUG: Removing weather effect: " + it->effectName + " (ID: " + std::to_string(it->id) + ")");
            m_effectIdToIndex.erase(it->id);
            it = m_effectInstances.erase(it);
            stoppedCount++;
        } else {
            ++it;
        }
    }
    
    // Rebuild index mapping for remaining effects
    m_effectIdToIndex.clear();
    for (size_t i = 0; i < m_effectInstances.size(); ++i) {
        m_effectIdToIndex[m_effectInstances[i].id] = i;
    }
    
    // Clear weather particles
    if (actualTransitionTime <= 0.0f) {
        // Clear immediately without calling clearWeatherGeneration to avoid re-locking
        int affectedCount = 0;
        for (auto& particle : m_storage.particles) {
            if (particle.isActive() && particle.isWeatherParticle()) {
                particle.setActive(false);
                affectedCount++;
            }
        }
        PARTICLE_INFO("Cleared " + std::to_string(affectedCount) + " weather particles immediately");
    }
    
    PARTICLE_INFO("Stopped " + std::to_string(stoppedCount) + " weather effects");
    
    // Handle Clear weather - just stop effects and return
    if (weatherType == "Clear") {
        PARTICLE_INFO("Clear weather triggered - only stopping weather effects");
        return;
    }
    
    // Map weather type to particle effect
    std::string effectName;
    if (weatherType == "Rainy") effectName = "Rain";
    else if (weatherType == "Snowy") effectName = "Snow";
    else if (weatherType == "Foggy") effectName = "Fog";
    else if (weatherType == "Cloudy") effectName = "Cloudy";
    else if (weatherType == "Stormy") effectName = "Rain"; // Stormy = heavy rain
    
    PARTICLE_INFO("Mapped weather type '" + weatherType + "' to effect '" + effectName + "'");

    if (!effectName.empty()) {
        // Check if effect definition exists
        auto defIt = m_effectDefinitions.find(effectName);
        if (defIt == m_effectDefinitions.end()) {
            PARTICLE_ERROR("ERROR: Effect not found: " + effectName);
            return;
        }
        
        // Calculate optimal weather position based on effect type
        Vector2D weatherPosition;
        if (effectName == "Rain" || effectName == "Snow") {
            weatherPosition = Vector2D(960, -100); // High spawn for falling particles
        } else if (effectName == "Fog") {
            weatherPosition = Vector2D(960, 300); // Mid-screen for fog spread
        } else if (effectName == "Cloudy") {
            weatherPosition = Vector2D(960, 50); // Visible clouds at top
        } else {
            weatherPosition = Vector2D(960, -50); // Default top spawn
        }
        
        // Create new weather effect directly (inline to avoid re-locking)
        EffectInstance instance;
        instance.id = generateEffectId();
        instance.effectName = effectName;
        instance.position = weatherPosition;
        instance.intensity = intensity;
        instance.currentIntensity = intensity;
        instance.targetIntensity = intensity;
        instance.active = true;
        instance.isWeatherEffect = true; // Mark as weather effect immediately
        
        // Register effect
        m_effectInstances.push_back(instance);
        m_effectIdToIndex[instance.id] = m_effectInstances.size() - 1;
        
        PARTICLE_INFO("Weather effect created: " + effectName + " (ID: " + std::to_string(instance.id) + ") at position (" + 
                    std::to_string(weatherPosition.getX()) + ", " + std::to_string(weatherPosition.getY()) + ")");
    } else {
        PARTICLE_ERROR("ERROR: No effect mapping found for weather type: " + weatherType);
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
    PARTICLE_INFO("Playing independent effect: " + effectName + " at (" + std::to_string(position.getX()) + ", " + std::to_string(position.getY()) + ")");
    
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    auto it = m_effectDefinitions.find(effectName);
    if (it == m_effectDefinitions.end()) {
        PARTICLE_ERROR("ERROR: Independent effect not found: " + effectName);
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
    
    PARTICLE_INFO("Independent effect started: " + effectName + " (ID: " + std::to_string(instance.id) + ")");
    return instance.id;
}

void ParticleManager::stopIndependentEffect(uint32_t effectId) {
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    auto it = m_effectIdToIndex.find(effectId);
    if (it != m_effectIdToIndex.end() && it->second < m_effectInstances.size()) {
        auto& effect = m_effectInstances[it->second];
        if (effect.isIndependentEffect) {
            effect.active = false;
            PARTICLE_INFO("Independent effect stopped: ID " + std::to_string(effectId));
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
    
    PARTICLE_INFO("Stopped " + std::to_string(stoppedCount) + " independent effects");
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
    
    PARTICLE_INFO("Stopped " + std::to_string(stoppedCount) + " independent effects in group: " + groupTag);
}

void ParticleManager::pauseIndependentEffect(uint32_t effectId, bool paused) {
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex);
    
    auto it = m_effectIdToIndex.find(effectId);
    if (it != m_effectIdToIndex.end() && it->second < m_effectInstances.size()) {
        auto& effect = m_effectInstances[it->second];
        if (effect.isIndependentEffect) {
            effect.paused = paused;
            PARTICLE_INFO("Independent effect " + std::to_string(effectId) + (paused ? " paused" : " unpaused"));
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
    
    PARTICLE_INFO("All independent effects " + std::string(paused ? "paused" : "unpaused") + " (" + std::to_string(affectedCount) + " effects)");
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
    
    PARTICLE_INFO("Independent effects in group " + groupTag + " " + std::string(paused ? "paused" : "unpaused") + " (" + std::to_string(affectedCount) + " effects)");
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
    PARTICLE_INFO("*** REGISTERING BUILT-IN EFFECTS");
    
    // Register preset weather effects
    m_effectDefinitions["Rain"] = createRainEffect();
    m_effectDefinitions["Snow"] = createSnowEffect();
    m_effectDefinitions["Fog"] = createFogEffect();
    m_effectDefinitions["Cloudy"] = createCloudyEffect();
    
    // Register independent particle effects
    m_effectDefinitions["Fire"] = createFireEffect();
    m_effectDefinitions["Smoke"] = createSmokeEffect();
    m_effectDefinitions["Sparks"] = createSparksEffect();
    
    PARTICLE_INFO("Built-in effects registered: " + std::to_string(m_effectDefinitions.size()));
    for (const auto& pair : m_effectDefinitions) {
        PARTICLE_INFO("  - Effect: " + pair.first);
    }
    
    // More effects can be added as needed
}

ParticleEffectDefinition ParticleManager::createRainEffect() {
    ParticleEffectDefinition rain("Rain", ParticleEffectType::Rain);
    rain.emitterConfig.spread = 1200.0f; // Wider spread to cover entire screen width plus margins
    rain.emitterConfig.emissionRate = 150.0f; // Increased emission rate for denser rain
    rain.emitterConfig.minSpeed = 180.0f; // Faster, more consistent rain speed
    rain.emitterConfig.maxSpeed = 220.0f; // Tighter speed range for uniform appearance
    rain.emitterConfig.minLife = 4.0f; // Longer life to ensure full screen traversal
    rain.emitterConfig.maxLife = 6.0f;
    rain.emitterConfig.minSize = 1.5f; // Slightly smaller for more realistic raindrops
    rain.emitterConfig.maxSize = 3.0f;
    rain.emitterConfig.gravity = Vector2D(15.0f, 280.0f); // More realistic wind drift + gravity
    rain.emitterConfig.windForce = Vector2D(8.0f, 0.0f); // Add wind force for natural movement
    rain.emitterConfig.textureID = "raindrop";
    rain.intensityMultiplier = 1.3f; // Higher multiplier for better rain intensity scaling
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
    cloudy.emitterConfig.spread = 1400.0f; // Even wider spread for better cloud coverage
    cloudy.emitterConfig.emissionRate = 0.8f; // Slightly higher emission rate for more clouds
    cloudy.emitterConfig.minSpeed = 15.0f; // Slightly faster movement
    cloudy.emitterConfig.maxSpeed = 25.0f;
    cloudy.emitterConfig.minLife = 25.0f; // Longer life for persistent cloud coverage
    cloudy.emitterConfig.maxLife = 40.0f;
    cloudy.emitterConfig.minSize = 120.0f; // Much larger cloud particles for better visibility
    cloudy.emitterConfig.maxSize = 200.0f; // Significantly larger maximum size
    cloudy.emitterConfig.gravity = Vector2D(12.0f, 2.0f); // Mostly horizontal drift like wind
    cloudy.emitterConfig.windForce = Vector2D(3.0f, -1.0f); // Additional wind force for natural movement
    cloudy.emitterConfig.textureID = "cloud";
    cloudy.intensityMultiplier = 0.7f; // Higher intensity for better visibility
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
    for (const auto& particle : m_storage.particles) {
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

    // CRITICAL FIX: Use try_lock to avoid deadlock with update threads
    // If we can't get the lock immediately, skip cleanup this frame
    // This prevents blocking particle creation and updates
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        // Skip cleanup this frame to avoid deadlock - updates have priority
        return;
    }
    
    // CRITICAL FIX: Remove aggressive cleanup that was causing rain/snow freezing
    // The 0.1s life threshold was prematurely removing active particles
    auto removeIt = std::remove_if(m_storage.particles.begin(), m_storage.particles.end(),
        [](const UnifiedParticle& particle) {
            // Only remove truly dead particles
            if (!particle.isActive()) return true;
            if (particle.life <= 0.0f) return true;
            
            // Remove particles with very low alpha (essentially invisible)
            uint8_t alpha = particle.color & 0xFF;
            if (alpha <= 5) return true;
            
            // REMOVED: Don't remove particles based on remaining life time
            // This was causing the freeze/resume cycle in rain and snow
            
            return false;
        });
    
    if (removeIt != m_storage.particles.end()) {
        size_t removedCount = std::distance(removeIt, m_storage.particles.end());
        m_storage.particles.erase(removeIt, m_storage.particles.end());
        
        PARTICLE_DEBUG("Cleaned up " + std::to_string(removedCount) + " inactive/faded particles");
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
        
        // ENHANCED: Keep weather effects active longer during transitions for smoother crossfade
        // Only deactivate when intensity is very low (not just zero) to maintain visual continuity
        if (effect.isWeatherEffect && effect.targetIntensity == 0.0f && effect.currentIntensity <= 0.05f) {
            effect.active = false;
            PARTICLE_INFO("Weather effect " + effect.effectName + " faded out and deactivated");
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
                PARTICLE_DEBUG("Weather Effect: " + effect.effectName + " emitting " + 
                            std::to_string(particlesToEmit) + " particles (Intensity: " + 
                            std::to_string(effect.currentIntensity) + ")");
            }
        }
    }
    
    // Note: Particles are now updated independently in the main update loop
}

void ParticleManager::createParticleForEffect(EffectInstance& effect, const ParticleEffectDefinition& effectDef) {
    // MEMORY LEAK FIX: Implement efficient particle reuse with slot management
    // First, try to find an inactive particle slot to reuse
    
    UnifiedParticle* particle = nullptr;
    
    // Try to reuse an inactive particle first (prevents memory growth)
    for (size_t i = 0; i < m_storage.particles.size(); ++i) {
        if (!m_storage.particles[i].isActive()) {
            particle = &m_storage.particles[i];
            break;
        }
    }
    
    // If no inactive particle found, add new one (but with better size management)
    if (!particle) {
        // Prevent excessive growth by limiting total particle count
        if (m_storage.particles.size() >= DEFAULT_MAX_PARTICLES * 2) {
            // Force cleanup of oldest particles when approaching memory limits
            size_t removedCount = 0;
            auto removeIt = std::remove_if(m_storage.particles.begin(), m_storage.particles.end(),
                [&removedCount](const UnifiedParticle& p) {
                    if (!p.isActive() || p.life <= 0.0f || (p.color & 0xFF) <= 10) {
                        removedCount++;
                        return true;
                    }
                    return false;
                });
            
            if (removeIt != m_storage.particles.end()) {
                m_storage.particles.erase(removeIt, m_storage.particles.end());
                PARTICLE_DEBUG("Memory management: cleaned up " + std::to_string(removedCount) + " particles");
            }
        }
        
        // Add new particle if still under reasonable limits
        if (m_storage.particles.size() < DEFAULT_MAX_PARTICLES * 2) {
            m_storage.particles.emplace_back();
            particle = &m_storage.particles.back();
        } else {
            // Skip creation if at hard limit to prevent memory explosion
            PARTICLE_WARN("Particle limit reached, skipping creation");
            return;
        }
    }
    
    // Generate random values within the effect's parameters
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    // Reset particle to clean state
    *particle = UnifiedParticle();
    
    // Position - start from effect position with some spread
    std::uniform_real_distribution<float> spreadDist(-effectDef.emitterConfig.spread, effectDef.emitterConfig.spread);
    particle->position = Vector2D(
        effect.position.getX() + spreadDist(gen),
        effect.position.getY() + spreadDist(gen)
    );

    // UNIFIED PHYSICS: All particles use the same reliable angular approach
    std::uniform_real_distribution<float> speedDist(effectDef.emitterConfig.minSpeed, effectDef.emitterConfig.maxSpeed);
    float speed = speedDist(gen);
    
    std::uniform_real_distribution<float> angleDist(-effectDef.emitterConfig.spread * 0.017453f, effectDef.emitterConfig.spread * 0.017453f);
    float angle = angleDist(gen);
    
    // For weather effects, bias the angle towards downward motion
    if (effectDef.type == ParticleEffectType::Rain || effectDef.type == ParticleEffectType::HeavyRain) {
        angle = (M_PI * 0.5f) + (angle * 0.1f); // Mostly downward
    } else if (effectDef.type == ParticleEffectType::Snow || effectDef.type == ParticleEffectType::HeavySnow) {
        angle = (M_PI * 0.5f) + (angle * 0.3f); // Gentle downward with drift
    }
    
    // Apply consistent velocity calculation
    particle->velocity = Vector2D(
        speed * sin(angle),
        speed * cos(angle)
    );
    
    // Apply acceleration/gravity from effect configuration
    particle->acceleration = effectDef.emitterConfig.gravity;

    // Size
    std::uniform_real_distribution<float> sizeDist(effectDef.emitterConfig.minSize, effectDef.emitterConfig.maxSize);
    particle->size = sizeDist(gen);

    // Life - Set life before making particle active
    std::uniform_real_distribution<float> lifeDist(effectDef.emitterConfig.minLife, effectDef.emitterConfig.maxLife);
    particle->maxLife = lifeDist(gen);
    particle->life = particle->maxLife;
    
    if (particle->life <= 0.0f) {
        particle->life = 1.0f;
        particle->maxLife = 1.0f;
    }

    // Color based on effect type
    if (effectDef.type == ParticleEffectType::Rain) {
        particle->color = 0x4080FFFF;
    } else if (effectDef.type == ParticleEffectType::Snow) {
        particle->color = 0xFFFFFFFF;
    } else if (effectDef.type == ParticleEffectType::Fog) {
        if (effectDef.name == "Cloudy") {
            particle->color = 0xF8F8F8C0;
        } else {
            particle->color = 0xCCCCCC88;
        }
    } else if (effectDef.type == ParticleEffectType::Fire) {
        std::uniform_int_distribution<int> colorChoice(0, 2);
        switch (colorChoice(gen)) {
            case 0: particle->color = 0xFF4500FF; break;
            case 1: particle->color = 0xFF6500FF; break;
            case 2: particle->color = 0xFFFF00FF; break;
        }
    } else if (effectDef.type == ParticleEffectType::Smoke) {
        std::uniform_int_distribution<int> colorChoice(0, 3);
        switch (colorChoice(gen)) {
            case 0: particle->color = 0x404040FF; break;
            case 1: particle->color = 0x606060FF; break;
            case 2: particle->color = 0x808080FF; break;
            case 3: particle->color = 0x202020FF; break;
        }
    } else if (effectDef.type == ParticleEffectType::Sparks) {
        std::uniform_int_distribution<int> colorChoice(0, 1);
        switch (colorChoice(gen)) {
            case 0: particle->color = 0xFFFF00FF; break;
            case 1: particle->color = 0xFF8C00FF; break;
        }
    } else {
        particle->color = 0xFFFFFFFF;
    }

    // Set active and visible LAST after ALL properties are initialized
    particle->setActive(true);
    particle->setVisible(true);
    
    // Mark weather particles for batch management
    if (effect.isWeatherEffect) {
        particle->setWeatherParticle(true);
        particle->generationId = effect.currentGenerationId;
    }
    
    // MEMORY LEAK FIX: More frequent cleanup to prevent accumulation
    static size_t cleanupCounter = 0;
    if (++cleanupCounter % 100 == 0) {  // Every 100 particles instead of 1000
        compactParticleStorageIfNeeded();
    }
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
    
    // FIXED: Remove dangerous pointer arithmetic - use basic physics only
    // Apply basic gravity if no cold data is available
    const float GRAVITY = 98.0f; // Default gravity
    particle.velocity.setY(particle.velocity.getY() + GRAVITY * deltaTime);
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
        PARTICLE_INFO("WorkerBudget threading enabled for ParticleManager");
    } else {
        PARTICLE_INFO("WorkerBudget threading disabled for ParticleManager");
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
                PARTICLE_WARN("Queue pressure detected (" + std::to_string(queueSize) + "/" + 
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
                PARTICLE_DEBUG("Particle Summary - Count: " + std::to_string(particleCount) + 
                           ", Update: " + std::to_string(duration) + "ms" +
                           ", Effects: " + std::to_string(m_effectInstances.size()));
            }
        }
        
    } catch (const std::exception& e) {
        PARTICLE_ERROR("Exception in ParticleManager::updateWithWorkerBudget: " + std::string(e.what()));
    }
}

// Implementation of missing methods for unified threading architecture

void ParticleManager::updateEffectInstances(float deltaTime) {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }
    
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    
    // Process all active particle effects
    for (auto& effect : m_effectInstances) {
        if (effect.active && !effect.paused) {
            updateEffectInstance(effect, deltaTime);
        }
    }
}

void ParticleManager::updateParticlesThreaded(float deltaTime, size_t totalParticleCount) {
    if (!Hammer::ThreadSystem::Exists()) {
        updateParticlesSingleThreaded(deltaTime, totalParticleCount);
        return;
    }
    
    // Apply frame-rate limiting BEFORE threading to ensure consistent timing
    float cappedDeltaTime = std::min(deltaTime, 0.033f); // Cap at ~30 FPS minimum for smooth motion
    
    auto& threadSystem = Hammer::ThreadSystem::Instance();
    size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
    
    // Check queue pressure before submitting tasks
    size_t queueSize = threadSystem.getQueueSize();
    size_t queueCapacity = threadSystem.getQueueCapacity();
    size_t pressureThreshold = (queueCapacity * 9) / 10; // 90% capacity threshold
    
    if (queueSize > pressureThreshold) {
        // Graceful degradation: fallback to single-threaded processing
        PARTICLE_WARN("Queue pressure detected (" + std::to_string(queueSize) + "/" + 
                    std::to_string(queueCapacity) + "), using single-threaded processing");
        updateParticlesSingleThreaded(cappedDeltaTime, totalParticleCount);
        return;
    }
    
    // Use WorkerBudget system for optimal resource allocation
    Hammer::WorkerBudget budget = Hammer::calculateWorkerBudget(availableWorkers);
    
    // Get optimal worker count with buffer allocation for particle workload
    size_t optimalWorkerCount = budget.getOptimalWorkerCount(budget.particleAllocated, totalParticleCount, 1000);
    
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
    
    size_t batchCount = std::min(optimalWorkerCount, totalParticleCount / minParticlesPerBatch);
    batchCount = std::max(size_t(1), std::min(batchCount, maxBatches));
    
    size_t particlesPerBatch = totalParticleCount / batchCount;
    size_t remainingParticles = totalParticleCount % batchCount;
    
    // Submit optimized particle update batches
    for (size_t i = 0; i < batchCount; ++i) {
        size_t start = i * particlesPerBatch;
        size_t end = start + particlesPerBatch;
        
        // Add remaining particles to last batch
        if (i == batchCount - 1) {
            end += remainingParticles;
        }
        
        threadSystem.enqueueTask([this, start, end, cappedDeltaTime]() {
            updateParticleBatch(start, end, cappedDeltaTime);
        }, Hammer::TaskPriority::High, "Particle_OptimalBatch");
    }
}

void ParticleManager::updateParticlesSingleThreaded(float deltaTime, size_t totalParticleCount) {
    // Apply frame-rate limiting to prevent stuttering - cap deltaTime to prevent large jumps
    float cappedDeltaTime = std::min(deltaTime, 0.033f); // Cap at ~30 FPS minimum for smooth motion
    
    // Update all particles directly using unified storage - lock-free for performance
    size_t particleCount = m_storage.particles.size();
    for (size_t i = 0; i < particleCount; ++i) {
        if (m_storage.particles[i].isActive()) {
            updateUnifiedParticle(m_storage.particles[i], cappedDeltaTime);
        }
    }
    
    // Suppress unused parameter warning
    (void)totalParticleCount;
}

void ParticleManager::updateParticleBatch(size_t start, size_t end, float deltaTime) {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }
    
    // CRITICAL FIX: Remove ALL locks to eliminate deadlock - use lock-free approach like AIManager
    // Particle vector is only modified during emission (main thread) and cleanup (try-lock)
    // Worker threads only read/update existing particles, which is safe without locks
    
    // Apply frame-rate limiting to prevent stuttering - cap deltaTime to prevent large jumps
    float cappedDeltaTime = std::min(deltaTime, 0.033f); // Cap at ~30 FPS minimum for smooth motion
    
    // Cache vector size to avoid repeated calls during lock-free access
    size_t particleCount = m_storage.particles.size();
    
    // Simple, correct sequential processing - update every particle in the batch range
    for (size_t i = start; i < end && i < particleCount; ++i) {
        if (m_storage.particles[i].isActive()) {
            updateUnifiedParticle(m_storage.particles[i], cappedDeltaTime);
        }
    }
}

void ParticleManager::updateUnifiedParticle(UnifiedParticle& particle, float deltaTime) {
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
    
    // Apply acceleration (gravity, wind, etc.) from unified particle data
    particle.velocity.setX(particle.velocity.getX() + particle.acceleration.getX() * deltaTime);
    particle.velocity.setY(particle.velocity.getY() + particle.acceleration.getY() * deltaTime);
    
    // Update rotation
    particle.rotation += particle.angularVelocity * deltaTime;
}

void ParticleManager::updateParticleWithColdData(ParticleData& particle, ParticleColdData& coldData, float deltaTime) {
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
    
    // Apply acceleration (gravity, wind, etc.) from cold data
    particle.velocity.setX(particle.velocity.getX() + coldData.acceleration.getX() * deltaTime);
    particle.velocity.setY(particle.velocity.getY() + coldData.acceleration.getY() * deltaTime);
}

// ARCHITECTURAL FIX: Add missing helper functions
size_t ParticleManager::countActiveParticles() const {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return 0;
    }
    
    std::shared_lock<std::shared_mutex> lock(m_particlesMutex);
    size_t count = 0;
    for (const auto& particle : m_storage.particles) {
        if (particle.isActive()) {
            count++;
        }
    }
    return count;
}


void ParticleManager::compactParticleStorage() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }
    
    // Use try_lock to avoid blocking normal operations
    std::unique_lock<std::shared_mutex> lock(m_particlesMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return; // Skip compaction if busy
    }
    
    // More aggressive cleanup: remove inactive particles AND faded particles
    auto removeIt = std::remove_if(m_storage.particles.begin(), m_storage.particles.end(),
        [](const UnifiedParticle& particle) {
            // Remove if inactive, dead, or essentially invisible
            return !particle.isActive() || 
                   particle.life <= 0.0f || 
                   (particle.color & 0xFF) <= 10;  // Very low alpha
        });
    
    if (removeIt != m_storage.particles.end()) {
        size_t removedCount = std::distance(removeIt, m_storage.particles.end());
        m_storage.particles.erase(removeIt, m_storage.particles.end());
        PARTICLE_DEBUG("Compacted storage: removed " + std::to_string(removedCount) + " inactive/faded particles");
    }
    
    // Shrink vector capacity if we have excessive unused space
    if (m_storage.particles.capacity() > m_storage.particles.size() * 2 && 
        m_storage.particles.capacity() > DEFAULT_MAX_PARTICLES) {
        m_storage.particles.shrink_to_fit();
        PARTICLE_DEBUG("Shrunk particle storage capacity to fit actual usage");
    }
}

// New helper method for more frequent, lightweight cleanup
void ParticleManager::compactParticleStorageIfNeeded() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }
    
    // Only compact if we have a significant number of inactive particles
    size_t totalParticles = m_storage.particles.size();
    if (totalParticles < 500) {
        return; // Not worth compacting small numbers
    }
    
    // Count inactive particles
    size_t inactiveCount = 0;
    for (const auto& particle : m_storage.particles) {
        if (!particle.isActive() || particle.life <= 0.0f || (particle.color & 0xFF) <= 10) {
            inactiveCount++;
        }
    }
    
    // Only compact if inactive particles are more than 30% of total
    if (inactiveCount > totalParticles * 0.3) {
        compactParticleStorage();
    }
}
