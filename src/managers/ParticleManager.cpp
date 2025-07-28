/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/ParticleManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include <algorithm>
#include <algorithm> // for std::clamp
#include <chrono>
#include <cmath>
#include <random>

// Static mutex for update serialization
std::mutex ParticleManager::updateMutex;

bool ParticleManager::init() {
  if (m_initialized.load(std::memory_order_acquire)) {
    PARTICLE_INFO("ParticleManager already initialized");
    return true;
  }

  try {
    // Pre-allocate storage for better performance
    // Modern engines can handle much more - reserve generous capacity
    // Note: LockFreeParticleStorage automatically pre-allocates in constructor

    // Built-in effects will be registered by GameEngine after init

    m_initialized.store(true, std::memory_order_release);
    m_isShutdown = false;

    PARTICLE_INFO("ParticleManager initialized successfully");
    return true;

  } catch (const std::exception &e) {
    PARTICLE_ERROR("Failed to initialize ParticleManager: " +
                   std::string(e.what()));
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

  // Clear all storage - no locks needed for lock-free storage
  m_storage.particles[0].clear();
  m_storage.particles[1].clear();
  m_storage.particleCount.store(0, std::memory_order_release);
  m_storage.writeHead.store(0, std::memory_order_release);

  m_effectDefinitions.clear();
  m_effectInstances.clear();
  m_effectIdToIndex.clear();

  PARTICLE_INFO("ParticleManager shutdown complete");
}

void ParticleManager::prepareForStateTransition() {
  PARTICLE_INFO("Preparing ParticleManager for state transition...");

  // COMPREHENSIVE CLEANUP: Ensure all effects and particles are properly
  // cleaned up This prevents effects from continuing when re-entering a state

  // Lock-free cleanup - no mutex needed for particle storage

  // Pause system to prevent new emissions during cleanup
  m_globallyPaused.store(true, std::memory_order_release);

  // 1. Stop ALL weather effects
  int weatherEffectsStopped = 0;
  int independentEffectsStopped = 0;
  int regularEffectsStopped = 0;

  auto it = m_effectInstances.begin();
  while (it != m_effectInstances.end()) {
    if (it->isWeatherEffect) {
      PARTICLE_INFO("Removing weather effect: " + it->effectName +
                    " (ID: " + std::to_string(it->id) + ")");
      m_effectIdToIndex.erase(it->id);
      it = m_effectInstances.erase(it);
      weatherEffectsStopped++;
    } else {
      ++it;
    }
  }

  // First pass: count and deactivate
  for (auto &effect : m_effectInstances) {
    if (effect.active) {
      if (effect.isIndependentEffect) {
        independentEffectsStopped++;
      } else {
        regularEffectsStopped++;
      }
      effect.active = false;
    }
  }

  // 2. Remove ALL remaining effects (independent and regular) for complete
  // state cleanup

  // Second pass: remove ALL non-weather effects from the list
  auto newEnd = std::remove_if(
      m_effectInstances.begin(), m_effectInstances.end(),
      [](const EffectInstance &effect) {
        return !effect.isWeatherEffect; // Remove everything except weather
                                        // (already removed)
      });
  m_effectInstances.erase(newEnd, m_effectInstances.end());

  // Third pass: rebuild the effect ID index for the remaining effects (should
  // be empty)
  m_effectIdToIndex.clear();
  for (size_t i = 0; i < m_effectInstances.size(); ++i) {
    m_effectIdToIndex[m_effectInstances[i].id] = i;
  }

  // 3. Clear ALL particles (both weather and independent) - LOCK-FREE APPROACH
  int particlesCleared = 0;
  size_t activeIdx = m_storage.activeBuffer.load(std::memory_order_acquire);
  auto &activeParticles = m_storage.particles[activeIdx];

  for (auto &particle : activeParticles) {
    if (particle.isActive()) {
      particle.setActive(false);
      particle.life = 0.0f; // Ensure life is zero for double safety
      particlesCleared++;
    }
  }

  // 3.5. IMMEDIATE COMPLETE CLEANUP - Remove ALL particles to ensure zero count
  size_t totalParticlesBefore = activeParticles.size();
  activeParticles.clear(); // Complete clear to ensure zero particles
  m_storage.particles[1 - activeIdx].clear(); // Clear other buffer too
  m_storage.particleCount.store(0, std::memory_order_release);

  PARTICLE_INFO(
      "Complete particle cleanup: cleared " + std::to_string(particlesCleared) +
      " active particles, removed total of " +
      std::to_string(totalParticlesBefore) + " particles from storage");

  // 4. Rebuild effect index mapping for any remaining effects
  m_effectIdToIndex.clear();
  for (size_t i = 0; i < m_effectInstances.size(); ++i) {
    m_effectIdToIndex[m_effectInstances[i].id] = i;
  }

  // 5. Reset performance stats (safe operation)
  resetPerformanceStats();

  // Resume system (no lock to release with lock-free design)
  m_globallyPaused.store(false, std::memory_order_release);

  PARTICLE_INFO(
      "ParticleManager state transition complete - stopped " +
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

  // PERFORMANCE-OPTIMIZED: Single update serialization only
  std::lock_guard<std::mutex> updateLock(updateMutex);

  auto startTime = std::chrono::high_resolution_clock::now();

  try {
    // Phase 1: Process pending particle creation requests (lock-free)
    m_storage.processCreationRequests();

    // Phase 2: Update effect instances (emission, timing) - MAIN THREAD ONLY
    updateEffectInstances(deltaTime);

    // Phase 2.5: Process newly created particles from effect instances
    m_storage.processCreationRequests();

    // Phase 3: Get snapshot of particle count for threading decision
    const auto &particles = m_storage.getParticlesForRead();
    size_t totalParticleCount = particles.size();
    if (totalParticleCount == 0) {
      return;
    }

    // Phase 4: Update particle physics with optimal threading strategy
    bool useThreading = (totalParticleCount >= m_threadingThreshold &&
                         m_useThreading.load(std::memory_order_acquire) &&
                         HammerEngine::ThreadSystem::Exists());

    if (useThreading) {
      // Use WorkerBudget system if enabled, otherwise fall back to legacy
      // threading
      if (m_useWorkerBudget.load(std::memory_order_acquire)) {
        updateWithWorkerBudget(deltaTime, totalParticleCount);
      } else {
        updateParticlesThreaded(deltaTime, totalParticleCount);
      }
    } else {
      updateParticlesSingleThreaded(deltaTime, totalParticleCount);
    }

    // Phase 5: Swap buffers for next frame (lock-free)
    m_storage.swapBuffers();

    // Phase 6: Optimized memory management
    uint64_t currentFrame =
        m_frameCounter.fetch_add(1, std::memory_order_relaxed);
    if (currentFrame % 300 == 0) { // Every 5 seconds at 60fps
      compactParticleStorageIfNeeded();
    }

    if (currentFrame % 1800 == 0) { // Every 30 seconds - deep cleanup
      compactParticleStorage();
    }

    // Phase 7: Performance tracking (reduced overhead)
    auto endTime = std::chrono::high_resolution_clock::now();
    double timeMs = std::chrono::duration_cast<std::chrono::microseconds>(
                        endTime - startTime)
                        .count() /
                    1000.0;

    // Only track performance every 1200 frames (20 seconds) to minimize
    // overhead
    if (currentFrame % 1200 == 0) {
      size_t activeCount = countActiveParticles();
      recordPerformance(false, timeMs, activeCount);

      if (activeCount > 0) {
        PARTICLE_DEBUG(
            "Particle Summary - Count: " + std::to_string(activeCount) +
            ", Update: " + std::to_string(timeMs) + "ms" +
            ", Effects: " + std::to_string(m_effectInstances.size()));
      }
    }

  } catch (const std::exception &e) {
    PARTICLE_ERROR("Exception in ParticleManager::update: " +
                   std::string(e.what()));
  }
}

void ParticleManager::render(SDL_Renderer *renderer, float cameraX,
                             float cameraY) {
  if (m_globallyPaused.load(std::memory_order_acquire) ||
      !m_globallyVisible.load(std::memory_order_acquire)) {
    return;
  }

  auto startTime = std::chrono::high_resolution_clock::now();

  // PERFORMANCE: Lock-free rendering using read-only snapshot
  const auto &particles = m_storage.getParticlesForRead();
  int renderCount = 0;

  for (const auto &particle : particles) {
    if (!particle.isActive() || !particle.isVisible()) {
      continue;
    }

    // Skip particles that are completely transparent
    uint8_t alpha = particle.color & 0xFF;
    if (alpha == 0) {
      continue;
    }

    renderCount++;

    // Extract color components
    uint8_t r = (particle.color >> 24) & 0xFF;
    uint8_t g = (particle.color >> 16) & 0xFF;
    uint8_t b = (particle.color >> 8) & 0xFF;

    // Set particle color
    SDL_SetRenderDrawColor(renderer, r, g, b, alpha);

    // Use particle size directly without any size limits
    float size = particle.size;

    // Render particle as a filled rectangle (accounting for camera offset)
    SDL_FRect rect = {particle.position.getX() - cameraX - size / 2,
                      particle.position.getY() - cameraY - size / 2, size,
                      size};
    SDL_RenderFillRect(renderer, &rect);
  }

  // Periodic summary logging (every 900 frames ~15 seconds)
  uint64_t currentFrame =
      m_frameCounter.fetch_add(1, std::memory_order_relaxed);
  if (currentFrame % 900 == 0 && renderCount > 0) {
    PARTICLE_DEBUG(
        "Particle Summary - Total: " + std::to_string(particles.size()) +
        ", Active: " + std::to_string(renderCount) +
        ", Effects: " + std::to_string(m_effectInstances.size()));
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  double timeMs =
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
          .count() /
      1000.0;
  recordPerformance(true, timeMs, particles.size());
}

void ParticleManager::renderBackground(SDL_Renderer *renderer, float cameraX,
                                       float cameraY) {
  if (m_globallyPaused.load(std::memory_order_acquire) ||
      !m_globallyVisible.load(std::memory_order_acquire)) {
    return;
  }

  // PERFORMANCE: Lock-free rendering using read-only snapshot
  const auto &particles = m_storage.getParticlesForRead();

  for (const auto &particle : particles) {
    if (!particle.isActive() || !particle.isVisible()) {
      continue;
    }

    // Check if this is a background particle (rain, snow, fire, smoke, sparks)
    uint32_t color = particle.color;
    bool isBackground = false;

    // Rain particles (including HeavyRain) - blue-dominant colors
    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;

    // Rain/HeavyRain: blue is dominant (blue > red AND blue > green)
    if (b > r && b > g && b >= 150) {
      isBackground = true;
    }
    // Snow particles (including HeavySnow) - white (all RGB components high and
    // similar)
    else if (r >= 200 && g >= 200 && b >= 200 &&
             abs(static_cast<int>(r) - static_cast<int>(g)) <= 30 &&
             abs(static_cast<int>(r) - static_cast<int>(b)) <= 30 &&
             abs(static_cast<int>(g) - static_cast<int>(b)) <= 30) {
      isBackground = true;
    }
    // Fire particles - orange/red/yellow range (red component >= 0x45)
    else if ((color & 0xFF000000) == 0xFF000000 &&
             ((color & 0x00FF0000) >= 0x00450000)) {
      isBackground = true;
    }
    // Smoke particles - grey range
    else if ((color & 0xFF000000) >= 0x20000000 &&
             (color & 0xFF000000) <= 0x80000000 &&
             (color & 0x00FFFFFF) >= 0x00202020 &&
             (color & 0x00FFFFFF) <= 0x00808080) {
      isBackground = true;
    }
    // Sparks particles - bright yellow/orange
    else if ((color & 0xFFFF0000) == 0xFFFF0000 || // Yellow (FFFF__)
             (color & 0xFF8C0000) == 0xFF8C0000) { // Orange (FF8C__)
      isBackground = true;
    }

    if (!isBackground) {
      continue; // Skip foreground particles
    }

    // Use already extracted color components for rendering
    // uint8_t a = color & 0xFF; // Already declared above

    // Set particle color
    SDL_SetRenderDrawColor(renderer, r, g, b, (color & 0xFF));

    // Use particle size directly without any size limits
    float size = particle.size;

    // Render particle as a filled rectangle (accounting for camera offset)
    SDL_FRect rect = {particle.position.getX() - cameraX - size / 2,
                      particle.position.getY() - cameraY - size / 2, size,
                      size};
    SDL_RenderFillRect(renderer, &rect);
  }

  // Background particle render complete
}

void ParticleManager::renderForeground(SDL_Renderer *renderer, float cameraX,
                                       float cameraY) {
  if (m_globallyPaused.load(std::memory_order_acquire) ||
      !m_globallyVisible.load(std::memory_order_acquire)) {
    return;
  }

  // PERFORMANCE: Lock-free rendering using read-only snapshot
  const auto &particles = m_storage.getParticlesForRead();

  for (const auto &particle : particles) {

    if (!particle.isActive() || !particle.isVisible()) {
      continue;
    }

    // Check if this is a foreground particle (fog = gray-ish, clouds = light
    // white)
    uint32_t color = particle.color;
    bool isForeground = false;

    // Extract RGB components
    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;

    // Fog particles: gray range (190-230 RGB as created in
    // createParticleForEffect) Check if all RGB components are similar (gray)
    // and in the fog range
    if (r >= 180 && r <= 240 && g >= 180 && g <= 240 && b >= 180 && b <= 240 &&
        abs(static_cast<int>(r) - static_cast<int>(g)) <= 25 &&
        abs(static_cast<int>(r) - static_cast<int>(b)) <= 25 &&
        abs(static_cast<int>(g) - static_cast<int>(b)) <= 25) {
      isForeground = true;
    }
    // Cloudy particles: light white range (240-255 RGB as created in
    // createParticleForEffect)
    else if (r >= 235 && g >= 235 && b >= 235) {
      isForeground = true;
    }

    if (!isForeground) {
      continue; // Skip background particles
    }

    // Use already extracted color components
    uint8_t a = color & 0xFF;

    // Set particle color
    SDL_SetRenderDrawColor(renderer, r, g, b, a);

    // Use particle size directly without any size limits
    float size = particle.size;

    // Render particle as a filled rectangle (accounting for camera offset)
    SDL_FRect rect = {particle.position.getX() - cameraX - size / 2,
                      particle.position.getY() - cameraY - size / 2, size,
                      size};
    SDL_RenderFillRect(renderer, &rect);
  }

  // Foreground particle render complete
}

uint32_t ParticleManager::playEffect(const std::string &effectName,
                                     const Vector2D &position,
                                     float intensity) {
  PARTICLE_INFO("*** PLAY EFFECT CALLED: " + effectName + " at (" +
                std::to_string(position.getX()) + ", " +
                std::to_string(position.getY()) +
                ") intensity=" + std::to_string(intensity));

  // PERFORMANCE: Exclusive lock needed for adding effect instances
  std::unique_lock<std::shared_mutex> lock(m_effectsMutex);

  auto it = m_effectDefinitions.find(effectName);
  if (it == m_effectDefinitions.end()) {
    PARTICLE_ERROR("ERROR: Effect not found: " + effectName);
    PARTICLE_INFO("Available effects: " +
                  std::to_string(m_effectDefinitions.size()));
    for (const auto &pair : m_effectDefinitions) {
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

  PARTICLE_INFO("Effect successfully started: " + effectName + " (ID: " +
                std::to_string(instance.id) + ") - Total active effects: " +
                std::to_string(m_effectInstances.size()));
  return instance.id;
}

void ParticleManager::stopEffect(uint32_t effectId) {
  // Thread safety: Use exclusive lock for modifying effect instances
  // PERFORMANCE: No locks needed for lock-free particle system

  auto it = m_effectIdToIndex.find(effectId);
  if (it != m_effectIdToIndex.end() && it->second < m_effectInstances.size()) {
    m_effectInstances[it->second].active = false;
    PARTICLE_INFO("Effect stopped: ID " + std::to_string(effectId));
  }
}
void ParticleManager::stopWeatherEffects(float transitionTime) {
  PARTICLE_INFO("*** STOPPING ALL WEATHER EFFECTS (transition: " +
                std::to_string(transitionTime) + "s)");

  // PERFORMANCE: Lock-free weather effect stopping
  // No synchronization needed for effect state changes

  int stoppedCount = 0;

  // Clean approach: Remove ALL weather effects to prevent accumulation
  auto it = m_effectInstances.begin();
  while (it != m_effectInstances.end()) {
    if (it->isWeatherEffect) {
      PARTICLE_DEBUG("DEBUG: Removing weather effect: " + it->effectName +
                     " (ID: " + std::to_string(it->id) + ")");

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

  // Clear weather particles directly here
  if (transitionTime <= 0.0f) {
    // Clear immediately without transition
    int affectedCount = 0;
    size_t activeIdx = m_storage.activeBuffer.load(std::memory_order_acquire);
    auto &particles = m_storage.particles[activeIdx];

    for (auto &particle : particles) {
      if (particle.isActive() && particle.isWeatherParticle()) {
        particle.setActive(false);
        affectedCount++;
      }
    }

    PARTICLE_INFO("Cleared " + std::to_string(affectedCount) +
                  " weather particles immediately");
  } else {
    // Set fade-out for particles with transition time
    int affectedCount = 0;
    size_t activeIdx = m_storage.activeBuffer.load(std::memory_order_acquire);
    auto &particles = m_storage.particles[activeIdx];

    for (auto &particle : particles) {
      if (particle.isActive() && particle.isWeatherParticle()) {
        particle.setFadingOut(true);
        particle.life = std::min(particle.life, transitionTime);
        affectedCount++;
      }
    }
    PARTICLE_INFO("Cleared " + std::to_string(affectedCount) +
                  " weather particles with fade time: " +
                  std::to_string(transitionTime) + "s");
  }

  PARTICLE_INFO("Stopped and removed " + std::to_string(stoppedCount) +
                " weather effects");
}

void ParticleManager::clearWeatherGeneration(uint8_t generationId,
                                             float fadeTime) {
  if (!m_initialized.load(std::memory_order_acquire)) {
    return;
  }

  // PERFORMANCE: No locks needed for lock-free particle system

  int affectedCount = 0;

  // PERFORMANCE: Lock-free weather particle clearing
  size_t activeIdx = m_storage.activeBuffer.load(std::memory_order_acquire);
  auto &particles = m_storage.particles[activeIdx];

  for (auto &particle : particles) {
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

  PARTICLE_INFO(
      "Cleared " + std::to_string(affectedCount) + " weather particles" +
      (generationId > 0 ? " (generation " + std::to_string(generationId) + ")"
                        : "") +
      " with fade time: " + std::to_string(fadeTime) + "s");
}

void ParticleManager::triggerWeatherEffect(const std::string &weatherType,
                                           float intensity,
                                           float transitionTime) {
  PARTICLE_INFO("*** WEATHER EFFECT TRIGGERED: " + weatherType +
                " intensity=" + std::to_string(intensity));

  // Use smooth transitions for better visual quality
  float actualTransitionTime = (transitionTime > 0.0f) ? transitionTime : 1.5f;

  // THREADING FIX: Effect management requires synchronization
  std::unique_lock<std::shared_mutex> lock(m_effectsMutex);

  // Clear existing weather effects first (without calling
  // stopWeatherEffects to avoid re-locking)
  int stoppedCount = 0;
  auto it = m_effectInstances.begin();
  while (it != m_effectInstances.end()) {
    if (it->isWeatherEffect) {
      PARTICLE_DEBUG("DEBUG: Removing weather effect: " + it->effectName +
                     " (ID: " + std::to_string(it->id) + ")");
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
    // Clear immediately using lock-free access
    int affectedCount = 0;
    size_t activeIdx = m_storage.activeBuffer.load(std::memory_order_acquire);
    auto &particles = m_storage.particles[activeIdx];

    for (auto &particle : particles) {
      if (particle.isActive() && particle.isWeatherParticle()) {
        particle.setActive(false);
        affectedCount++;
      }
    }
    PARTICLE_INFO("Cleared " + std::to_string(affectedCount) +
                  " weather particles immediately");
  }

  PARTICLE_INFO("Stopped " + std::to_string(stoppedCount) + " weather effects");

  // Handle Clear weather - just stop effects and return
  if (weatherType == "Clear") {
    PARTICLE_INFO("Clear weather triggered - only stopping weather effects");
    return;
  }

  // Map weather type to particle effect with intensity-based selection
  std::string effectName;
  if (weatherType == "Rainy") {
    effectName = (intensity > 0.7f) ? "HeavyRain"
                                    : "Rain"; // Heavy rain for high intensity
  } else if (weatherType == "Snowy") {
    effectName = (intensity > 0.7f) ? "HeavySnow"
                                    : "Snow"; // Heavy snow for high intensity
  } else if (weatherType == "Foggy") {
    effectName = "Fog";
  } else if (weatherType == "Cloudy") {
    effectName = "Cloudy";
  } else if (weatherType == "Stormy") {
    effectName = "HeavyRain"; // Stormy always uses heavy rain
  } else if (weatherType == "HeavyRain") {
    effectName = "HeavyRain"; // Direct heavy rain
  } else if (weatherType == "HeavySnow") {
    effectName = "HeavySnow"; // Direct heavy snow
  }

  PARTICLE_INFO("Mapped weather type '" + weatherType + "' to effect '" +
                effectName + "'");

  if (!effectName.empty()) {
    // Check if effect definition exists
    auto defIt = m_effectDefinitions.find(effectName);
    if (defIt == m_effectDefinitions.end()) {
      PARTICLE_ERROR("ERROR: Effect not found: " + effectName);
      return;
    }

    // Calculate optimal weather position based on effect type
    Vector2D weatherPosition;
    if (effectName == "Rain" || effectName == "HeavyRain" ||
        effectName == "Snow" || effectName == "HeavySnow") {
      weatherPosition = Vector2D(960, -100); // High spawn for falling particles
    } else if (effectName == "Fog") {
      weatherPosition = Vector2D(960, 300); // Mid-screen for fog spread
    } else if (effectName == "Cloudy") {
      weatherPosition = Vector2D(960, -100); // Higher spawn point for clouds
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
    m_effectInstances.emplace_back(std::move(instance));
    m_effectIdToIndex[instance.id] = m_effectInstances.size() - 1;

    PARTICLE_INFO("Weather effect created: " + effectName +
                  " (ID: " + std::to_string(instance.id) + ") at position (" +
                  std::to_string(weatherPosition.getX()) + ", " +
                  std::to_string(weatherPosition.getY()) + ")");
  } else {
    PARTICLE_ERROR("ERROR: No effect mapping found for weather type: " +
                   weatherType);
  }
}

void ParticleManager::recordPerformance(bool isRender, double timeMs,
                                        size_t particleCount) {
  if (isRender) {
    m_performanceStats.addRenderSample(timeMs);
  } else {
    m_performanceStats.addUpdateSample(timeMs, particleCount);
  }
}

void ParticleManager::toggleFireEffect() {
  std::unique_lock<std::shared_mutex> lock(m_effectsMutex);
  if (!m_fireActive) {
    // Create multiple fire sources for a more natural distributed campfire
    // effect
    Vector2D basePosition(400, 300);

    // Main central fire
    m_fireEffectId =
        playIndependentEffect("Fire", basePosition, 1.2f, -1.0f, "campfire");

    // Additional distributed fire sources for realism
    playIndependentEffect("Fire", basePosition + Vector2D(-15, 5), 0.8f, -1.0f,
                          "campfire");
    playIndependentEffect("Fire", basePosition + Vector2D(20, 8), 0.9f, -1.0f,
                          "campfire");
    playIndependentEffect("Fire", basePosition + Vector2D(-8, 12), 0.7f, -1.0f,
                          "campfire");
    playIndependentEffect("Fire", basePosition + Vector2D(12, -3), 0.6f, -1.0f,
                          "campfire");

    m_fireActive = true;
  } else {
    // Stop all fire effects in the campfire group
    stopIndependentEffectsByGroup("campfire");
    m_fireActive = false;
  }
}

void ParticleManager::toggleSmokeEffect() {
  std::unique_lock<std::shared_mutex> lock(m_effectsMutex);
  if (!m_smokeActive) {
    // Create distributed smoke sources that rise from different points
    Vector2D basePosition(400, 280); // Slightly above fire

    // Main smoke column
    m_smokeEffectId = playIndependentEffect("Smoke", basePosition, 1.0f, -1.0f,
                                            "campfire_smoke");

    // Additional smoke sources for realistic dispersion
    playIndependentEffect("Smoke", basePosition + Vector2D(-25, 10), 0.7f,
                          -1.0f, "campfire_smoke");
    playIndependentEffect("Smoke", basePosition + Vector2D(30, 15), 0.8f, -1.0f,
                          "campfire_smoke");
    playIndependentEffect("Smoke", basePosition + Vector2D(-10, 20), 0.6f,
                          -1.0f, "campfire_smoke");

    m_smokeActive = true;
  } else {
    // Stop all smoke effects in the campfire_smoke group
    stopIndependentEffectsByGroup("campfire_smoke");
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
uint32_t ParticleManager::playIndependentEffect(
    const std::string &effectName, const Vector2D &position, float intensity,
    float duration, const std::string &groupTag,
    const std::string &soundEffect) {
  PARTICLE_INFO("Playing independent effect: " + effectName + " at (" +
                std::to_string(position.getX()) + ", " +
                std::to_string(position.getY()) + ")");

  // PERFORMANCE: No locks needed for lock-free particle system

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

  PARTICLE_INFO("Independent effect started: " + effectName +
                " (ID: " + std::to_string(instance.id) + ")");
  return instance.id;
}

void ParticleManager::stopIndependentEffect(uint32_t effectId) {
  // PERFORMANCE: No locks needed for lock-free particle system

  auto it = m_effectIdToIndex.find(effectId);
  if (it != m_effectIdToIndex.end() && it->second < m_effectInstances.size()) {
    auto &effect = m_effectInstances[it->second];
    if (effect.isIndependentEffect) {
      effect.active = false;
      PARTICLE_INFO("Independent effect stopped: ID " +
                    std::to_string(effectId));
    }
  }
}

void ParticleManager::stopAllIndependentEffects() {
  // PERFORMANCE: No locks needed for lock-free particle system

  int stoppedCount = 0;
  for (auto &effect : m_effectInstances) {
    if (effect.active && effect.isIndependentEffect) {
      effect.active = false;
      stoppedCount++;
    }
  }

  PARTICLE_INFO("Stopped " + std::to_string(stoppedCount) +
                " independent effects");
}

void ParticleManager::stopIndependentEffectsByGroup(
    const std::string &groupTag) {
  // PERFORMANCE: No locks needed for lock-free particle system

  int stoppedCount = 0;
  for (auto &effect : m_effectInstances) {
    if (effect.active && effect.isIndependentEffect &&
        effect.groupTag == groupTag) {
      effect.active = false;
      stoppedCount++;
    }
  }

  PARTICLE_INFO("Stopped " + std::to_string(stoppedCount) +
                " independent effects in group: " + groupTag);
}

void ParticleManager::pauseIndependentEffect(uint32_t effectId, bool paused) {
  // PERFORMANCE: No locks needed for lock-free particle system

  auto it = m_effectIdToIndex.find(effectId);
  if (it != m_effectIdToIndex.end() && it->second < m_effectInstances.size()) {
    auto &effect = m_effectInstances[it->second];
    if (effect.isIndependentEffect) {
      effect.paused = paused;
      PARTICLE_INFO("Independent effect " + std::to_string(effectId) +
                    (paused ? " paused" : " unpaused"));
    }
  }
}

void ParticleManager::pauseAllIndependentEffects(bool paused) {
  // PERFORMANCE: No locks needed for lock-free particle system

  int affectedCount = 0;
  for (auto &effect : m_effectInstances) {
    if (effect.active && effect.isIndependentEffect) {
      effect.paused = paused;
      affectedCount++;
    }
  }

  PARTICLE_INFO("All independent effects " +
                std::string(paused ? "paused" : "unpaused") + " (" +
                std::to_string(affectedCount) + " effects)");
}

void ParticleManager::pauseIndependentEffectsByGroup(
    const std::string &groupTag, bool paused) {
  // PERFORMANCE: No locks needed for lock-free particle system

  int affectedCount = 0;
  for (auto &effect : m_effectInstances) {
    if (effect.active && effect.isIndependentEffect &&
        effect.groupTag == groupTag) {
      effect.paused = paused;
      affectedCount++;
    }
  }

  PARTICLE_INFO("Independent effects in group " + groupTag + " " +
                std::string(paused ? "paused" : "unpaused") + " (" +
                std::to_string(affectedCount) + " effects)");
}

bool ParticleManager::isIndependentEffect(uint32_t effectId) const {
  // PERFORMANCE: No locks needed for lock-free particle system

  auto it = m_effectIdToIndex.find(effectId);
  if (it != m_effectIdToIndex.end() && it->second < m_effectInstances.size()) {
    return m_effectInstances[it->second].isIndependentEffect;
  }
  return false;
}

std::vector<uint32_t> ParticleManager::getActiveIndependentEffects() const {
  // PERFORMANCE: No locks needed for lock-free particle system

  std::vector<uint32_t> activeEffects;
  for (auto &effect : m_effectInstances) {
    if (effect.active && effect.isIndependentEffect) {
      activeEffects.push_back(effect.id);
    }
  }

  return activeEffects;
}

std::vector<uint32_t> ParticleManager::getActiveIndependentEffectsByGroup(
    const std::string &groupTag) const {
  // PERFORMANCE: No locks needed for lock-free particle system

  std::vector<uint32_t> activeEffects;
  for (auto &effect : m_effectInstances) {
    if (effect.active && effect.isIndependentEffect &&
        effect.groupTag == groupTag) {
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
  m_effectDefinitions["HeavyRain"] = createHeavyRainEffect();
  m_effectDefinitions["Snow"] = createSnowEffect();
  m_effectDefinitions["HeavySnow"] = createHeavySnowEffect();
  m_effectDefinitions["Fog"] = createFogEffect();
  m_effectDefinitions["Cloudy"] = createCloudyEffect();

  // Register independent particle effects
  m_effectDefinitions["Fire"] = createFireEffect();
  m_effectDefinitions["Smoke"] = createSmokeEffect();
  m_effectDefinitions["Sparks"] = createSparksEffect();

  PARTICLE_INFO("Built-in effects registered: " +
                std::to_string(m_effectDefinitions.size()));
  for (const auto &pair : m_effectDefinitions) {
    PARTICLE_INFO("  - Effect: " + pair.first);
  }

  // More effects can be added as needed
}

ParticleEffectDefinition ParticleManager::createRainEffect() {
  ParticleEffectDefinition rain("Rain", ParticleEffectType::Rain);
  rain.emitterConfig.spread =
      600.0f; // Narrower spread for more vertical rain fall
  rain.emitterConfig.emissionRate =
      800.0f; // Reduced emission for better performance while maintaining
              // coverage
  rain.emitterConfig.minSpeed = 250.0f; // Faster minimum speed for quicker rain
  rain.emitterConfig.maxSpeed =
      350.0f;                        // Much faster for more dynamic rain fall
  rain.emitterConfig.minLife = 4.0f; // Longer to ensure screen traversal
  rain.emitterConfig.maxLife = 7.0f;
  rain.emitterConfig.minSize = 2.0f; // Much smaller for realistic raindrops
  rain.emitterConfig.maxSize = 6.0f;
  rain.emitterConfig.gravity =
      Vector2D(2.0f, 450.0f); // More vertical fall for 2D isometric view
  rain.emitterConfig.windForce =
      Vector2D(3.0f, 1.0f); // Minimal wind for straighter downward fall
  rain.emitterConfig.textureID = "raindrop";
  rain.emitterConfig.blendMode = ParticleBlendMode::Alpha;
  rain.intensityMultiplier =
      1.4f; // Higher multiplier for better intensity scaling
  return rain;
}

ParticleEffectDefinition ParticleManager::createHeavyRainEffect() {
  ParticleEffectDefinition heavyRain("HeavyRain",
                                     ParticleEffectType::HeavyRain);
  heavyRain.emitterConfig.spread =
      800.0f; // Narrower spread for more intense, vertical heavy rain
  heavyRain.emitterConfig.emissionRate =
      1200.0f; // Reduced emission while maintaining storm intensity
  heavyRain.emitterConfig.minSpeed =
      320.0f; // Much faster falling in heavy storm
  heavyRain.emitterConfig.maxSpeed =
      450.0f;                             // Very high speed for intense impact
  heavyRain.emitterConfig.minLife = 3.5f; // Good life for screen coverage
  heavyRain.emitterConfig.maxLife = 6.0f;
  heavyRain.emitterConfig.minSize = 1.5f; // Smaller but more numerous
  heavyRain.emitterConfig.maxSize = 5.0f;
  heavyRain.emitterConfig.gravity = Vector2D(
      5.0f, 500.0f); // Strong vertical fall for intense rain in 2D isometric
  heavyRain.emitterConfig.windForce =
      Vector2D(5.0f, 2.0f); // Minimal wind for mostly vertical heavy rain
  heavyRain.emitterConfig.textureID = "raindrop";
  heavyRain.emitterConfig.blendMode = ParticleBlendMode::Alpha;
  heavyRain.intensityMultiplier = 1.8f; // High intensity for storms
  return heavyRain;
}

ParticleEffectDefinition ParticleManager::createSnowEffect() {
  ParticleEffectDefinition snow("Snow", ParticleEffectType::Snow);
  snow.emitterConfig.spread = 1200.0f; // Moderate spread for gentle snow drift
  snow.emitterConfig.emissionRate =
      350.0f; // Further reduced emission for optimal density
  snow.emitterConfig.minSpeed = 15.0f; // Faster minimum for quicker snow fall
  snow.emitterConfig.maxSpeed = 50.0f; // Much faster max for more dynamic drift
  snow.emitterConfig.minLife = 8.0f;   // Much longer life for coverage
  snow.emitterConfig.maxLife = 15.0f;  // Extended for slow drift
  snow.emitterConfig.minSize = 8.0f;   // Larger for better visibility
  snow.emitterConfig.maxSize = 16.0f;  // Good visible size range
  snow.emitterConfig.gravity = Vector2D(
      -2.0f,
      60.0f); // More vertical fall with minimal wind drift for 2D isometric
  snow.emitterConfig.windForce =
      Vector2D(3.0f, 0.5f); // Very gentle wind for mostly downward snow
  snow.emitterConfig.textureID = "snowflake";
  snow.emitterConfig.blendMode = ParticleBlendMode::Alpha;
  snow.intensityMultiplier = 1.1f; // Slightly enhanced for visibility
  return snow;
}

ParticleEffectDefinition ParticleManager::createHeavySnowEffect() {
  ParticleEffectDefinition heavySnow("HeavySnow",
                                     ParticleEffectType::HeavySnow);
  heavySnow.emitterConfig.spread =
      1800.0f; // Wider spread for blizzard but not extreme
  heavySnow.emitterConfig.emissionRate =
      600.0f; // Further reduced emission for realistic blizzard
  heavySnow.emitterConfig.minSpeed = 25.0f; // Much faster in heavy blizzard
  heavySnow.emitterConfig.maxSpeed = 80.0f; // High wind speeds for blizzard
  heavySnow.emitterConfig.minLife = 5.0f;   // Good life for coverage
  heavySnow.emitterConfig.maxLife = 10.0f;
  heavySnow.emitterConfig.minSize = 6.0f; // Visible but numerous flakes
  heavySnow.emitterConfig.maxSize = 14.0f;
  heavySnow.emitterConfig.gravity =
      Vector2D(-5.0f, 80.0f); // Stronger vertical fall with some wind for
                              // blizzard in 2D isometric
  heavySnow.emitterConfig.windForce = Vector2D(
      8.0f,
      2.0f); // Moderate wind for blizzard effect but still mostly downward
  heavySnow.emitterConfig.textureID = "snowflake";
  heavySnow.emitterConfig.blendMode = ParticleBlendMode::Alpha;
  heavySnow.intensityMultiplier = 1.6f; // High intensity for blizzard
  return heavySnow;
}

ParticleEffectDefinition ParticleManager::createFogEffect() {
  ParticleEffectDefinition fog("Fog", ParticleEffectType::Fog);
  fog.emitterConfig.spread = 3000.0f; // Very wide spread to cover entire screen
  fog.emitterConfig.emissionRate = 30.0f; // Lower emission rate for gradual fog
  fog.emitterConfig.minSpeed = 2.0f;  // Slower movement for realistic fog drift
  fog.emitterConfig.maxSpeed = 15.0f; // Varied speeds for natural movement
  fog.emitterConfig.minLife = 8.0f;   // Reduced life for faster turnover
  fog.emitterConfig.maxLife =
      18.0f; // Shorter max life to prevent hanging around
  fog.emitterConfig.minSize = 25.0f; // Smaller fog particles
  fog.emitterConfig.maxSize = 50.0f; // Reduced maximum for subtler fog
  fog.emitterConfig.gravity =
      Vector2D(3.0f, -2.0f); // Gentle horizontal drift + slight upward float
  fog.emitterConfig.windForce = Vector2D(8.0f, 1.0f); // Variable wind effect
  fog.emitterConfig.textureID = "fog";
  fog.emitterConfig.blendMode = ParticleBlendMode::Alpha;
  fog.intensityMultiplier = 0.9f; // Balanced intensity for fog
  return fog;
}

ParticleEffectDefinition ParticleManager::createCloudyEffect() {
  ParticleEffectDefinition cloudy("Cloudy", ParticleEffectType::Fog);
  // No initial position - will be set by triggerWeatherEffect
  cloudy.emitterConfig.direction = Vector2D(
      1.0f, 0.0f); // Horizontal movement for clouds sweeping across sky
  cloudy.emitterConfig.spread =
      2000.0f; // Wider spread to cover entire screen width
  cloudy.emitterConfig.emissionRate =
      1.5f; // Further reduced for less dense cloud effect
  cloudy.emitterConfig.minSpeed =
      25.0f; // Much faster horizontal movement for visible sweeping motion
  cloudy.emitterConfig.maxSpeed =
      60.0f;                            // Very fast for dramatic cloud movement
  cloudy.emitterConfig.minLife = 20.0f; // Shorter life for faster turnover
  cloudy.emitterConfig.maxLife =
      35.0f; // Reduced max life to prevent screen crowding
  cloudy.emitterConfig.minSize =
      100.0f; // Much larger clouds for better coverage
  cloudy.emitterConfig.maxSize = 200.0f; // Very large maximum size
  cloudy.emitterConfig.gravity =
      Vector2D(0.0f, 0.0f); // No gravity - clouds float and drift with wind
  cloudy.emitterConfig.windForce =
      Vector2D(25.0f, 0.0f); // Strong horizontal wind for sweeping motion
  cloudy.emitterConfig.textureID = "cloud";
  cloudy.emitterConfig.blendMode =
      ParticleBlendMode::Alpha;      // Standard alpha blending
  cloudy.intensityMultiplier = 1.2f; // Slightly enhanced intensity
  return cloudy;
}

ParticleEffectDefinition ParticleManager::createFireEffect() {
  ParticleEffectDefinition fire("Fire", ParticleEffectType::Fire);
  fire.emitterConfig.position = Vector2D(0, 0);   // Will be set when played
  fire.emitterConfig.direction = Vector2D(0, -1); // Upward flames
  fire.emitterConfig.spread =
      90.0f; // Very wide spread for natural flame distribution
  fire.emitterConfig.emissionRate =
      640.0f; // Reduced by 20% from 800 for more natural emission
  fire.emitterConfig.minSpeed =
      10.0f; // Slower minimum for realistic base flames
  fire.emitterConfig.maxSpeed = 100.0f; // Higher max for dramatic flame tips
  fire.emitterConfig.minLife = 0.3f;    // Very short min life for intense core
  fire.emitterConfig.maxLife = 3.0f;  // Longer max life for outer flame trails
  fire.emitterConfig.minSize = 1.0f;  // Very small min size for fine detail
  fire.emitterConfig.maxSize = 15.0f; // Larger max size for dramatic effect
  fire.emitterConfig.minColor = 0xFF1100FF; // Deep red for flame core
  fire.emitterConfig.maxColor = 0xFFEE00FF; // Bright yellow for flame tips
  fire.emitterConfig.gravity =
      Vector2D(0, -20.0f); // Moderate negative gravity for natural rise
  fire.emitterConfig.windForce =
      Vector2D(35.0f, 0); // Strong wind for dynamic movement
  fire.emitterConfig.textureID = "fire_particle";
  fire.emitterConfig.blendMode =
      ParticleBlendMode::Additive;     // Additive for glowing effect
  fire.emitterConfig.duration = -1.0f; // Infinite by default
  // Burst configuration for more natural fire
  fire.emitterConfig.burstCount = 15;      // Particles per burst
  fire.emitterConfig.burstInterval = 0.1f; // Frequent small bursts
  fire.intensityMultiplier = 1.3f;         // More intense
  return fire;
}

ParticleEffectDefinition ParticleManager::createSmokeEffect() {
  ParticleEffectDefinition smoke("Smoke", ParticleEffectType::Smoke);
  smoke.emitterConfig.position = Vector2D(0, 0);   // Will be set when played
  smoke.emitterConfig.direction = Vector2D(0, -1); // Upward smoke
  smoke.emitterConfig.spread =
      120.0f; // Very wide spread for natural smoke dispersion
  smoke.emitterConfig.emissionRate =
      120.0f; // Reduced by 20% from 150 for more natural smoke emission
  smoke.emitterConfig.minSpeed = 5.0f;  // Very slow minimum for realistic drift
  smoke.emitterConfig.maxSpeed = 60.0f; // Higher max for initial smoke burst
  smoke.emitterConfig.minLife = 3.0f;   // Long-lived for realistic smoke trails
  smoke.emitterConfig.maxLife = 10.0f; // Very long max life for lingering smoke
  smoke.emitterConfig.minSize = 4.0f;  // Smaller minimum for initial puffs
  smoke.emitterConfig.maxSize = 45.0f; // Much larger for expanded smoke clouds
  smoke.emitterConfig.minColor = 0x101010AA;         // Very dark smoke
  smoke.emitterConfig.maxColor = 0x808080DD;         // Lighter grey for variety
  smoke.emitterConfig.gravity = Vector2D(0, -12.0f); // Light upward force
  smoke.emitterConfig.windForce =
      Vector2D(40.0f, 0); // Very strong wind influence
  smoke.emitterConfig.textureID = "smoke_particle";
  smoke.emitterConfig.blendMode =
      ParticleBlendMode::Alpha;         // Standard alpha blending
  smoke.emitterConfig.duration = -1.0f; // Infinite by default
  // Burst configuration for more natural smoke puffs
  smoke.emitterConfig.burstCount = 8;       // Smaller bursts for smoke puffs
  smoke.emitterConfig.burstInterval = 0.2f; // Less frequent bursts
  smoke.intensityMultiplier = 1.4f;         // More intense smoke
  return smoke;
}

ParticleEffectDefinition ParticleManager::createSparksEffect() {
  ParticleEffectDefinition sparks("Sparks", ParticleEffectType::Sparks);
  sparks.emitterConfig.position = Vector2D(0, 0);   // Will be set when played
  sparks.emitterConfig.direction = Vector2D(0, -1); // Initial upward burst
  sparks.emitterConfig.spread =
      180.0f; // Wide spread for explosive spark pattern
  sparks.emitterConfig.emissionRate = 300.0f; // RESTORED: Original very high
                                              // burst rate for explosive sparks
  sparks.emitterConfig.minSpeed = 80.0f;      // Fast initial velocity
  sparks.emitterConfig.maxSpeed = 200.0f;
  sparks.emitterConfig.minLife = 0.3f; // Very short-lived for realistic sparks
  sparks.emitterConfig.maxLife = 1.2f;
  sparks.emitterConfig.minSize = 1.0f; // Small spark particles
  sparks.emitterConfig.maxSize = 3.0f;
  sparks.emitterConfig.minColor = 0xFFFF00FF;         // Bright yellow
  sparks.emitterConfig.maxColor = 0xFF8C00FF;         // Orange
  sparks.emitterConfig.gravity = Vector2D(0, 120.0f); // Strong downward gravity
  sparks.emitterConfig.windForce = Vector2D(5.0f, 0); // Slight wind resistance
  sparks.emitterConfig.textureID = "spark_particle";
  sparks.emitterConfig.blendMode =
      ParticleBlendMode::Additive;             // Bright additive blending
  sparks.emitterConfig.duration = 2.0f;        // Short burst effect
  sparks.emitterConfig.burstCount = 50;        // Burst of sparks
  sparks.emitterConfig.enableCollision = true; // Sparks bounce off surfaces
  sparks.emitterConfig.bounceDamping = 0.6f;   // Medium bounce damping
  sparks.intensityMultiplier = 1.0f;
  return sparks;
}

size_t ParticleManager::getActiveParticleCount() const {
  if (!m_initialized.load(std::memory_order_acquire)) {
    return 0;
  }

  // PERFORMANCE: Lock-free particle counting
  const auto &particles = m_storage.getParticlesForRead();
  return std::count_if(
      particles.begin(), particles.end(),
      [](const auto &particle) { return particle.isActive(); });
}

void ParticleManager::compactParticleStorage() {
  if (!m_initialized.load(std::memory_order_acquire)) {
    return;
  }

  // PERFORMANCE: Lock-free compaction during buffer swap
  // This happens during the update phase when we have exclusive access

  size_t activeIdx = m_storage.activeBuffer.load(std::memory_order_relaxed);
  auto &particles = m_storage.particles[activeIdx];

  // More aggressive cleanup: remove inactive particles AND faded particles
  auto removeIt = std::remove_if(
      particles.begin(), particles.end(), [](const UnifiedParticle &particle) {
        // Remove if inactive, dead, or
        // essentially invisible
        return !particle.isActive() || particle.life <= 0.0f ||
               (particle.color & 0xFF) <= 10; // Very low alpha
      });

  if (removeIt != particles.end()) {
    size_t removedCount = std::distance(removeIt, particles.end());
    particles.erase(removeIt, particles.end());
    m_storage.particleCount.store(particles.size(), std::memory_order_release);
    PARTICLE_DEBUG("Compacted storage: removed " +
                   std::to_string(removedCount) + " inactive/faded particles");
  }

  // Shrink vector capacity if we have excessive unused space
  if (particles.capacity() > particles.size() * 2 &&
      particles.capacity() > DEFAULT_MAX_PARTICLES) {
    particles.shrink_to_fit();
    PARTICLE_DEBUG("Shrunk particle storage capacity to fit actual usage");
  }
}

// New helper method for more frequent, lightweight cleanup
void ParticleManager::compactParticleStorageIfNeeded() {
  if (m_storage.needsCompaction()) {
    compactParticleStorage();
  }
}

void ParticleManager::resetPerformanceStats() {
  std::lock_guard<std::mutex> lock(m_statsMutex);
  m_performanceStats = {};
}

ParticlePerformanceStats ParticleManager::getPerformanceStats() const {
  std::lock_guard<std::mutex> lock(m_statsMutex);
  return m_performanceStats;
}

bool ParticleManager::isEffectPlaying(uint32_t effectId) const {
  std::shared_lock<std::shared_mutex> lock(m_effectsMutex);
  auto it = m_effectIdToIndex.find(effectId);
  if (it == m_effectIdToIndex.end())
    return false;

  size_t index = it->second;
  return index < m_effectInstances.size() && m_effectInstances[index].active;
}

size_t ParticleManager::countActiveParticles() const {
  return getActiveParticleCount();
}

void ParticleManager::updateEffectInstances(float deltaTime) {
  std::unique_lock<std::shared_mutex> lock(m_effectsMutex);

  auto it = m_effectInstances.begin();
  while (it != m_effectInstances.end()) {
    auto &instance = *it;

    if (!instance.active) {
      ++it;
      continue;
    }

    // Update effect instance lifetime
    instance.durationTimer += deltaTime;

    // Check if effect should expire
    if (instance.maxDuration > 0.0f &&
        instance.durationTimer >= instance.maxDuration) {
      instance.active = false;
      ++it;
      continue;
    }

    // Update emission timing
    instance.emissionTimer += deltaTime;

    // Find effect definition to get emission rate
    auto defIt = m_effectDefinitions.find(instance.effectName);
    if (defIt != m_effectDefinitions.end()) {
      const auto &config = defIt->second.emitterConfig;

      if (config.emissionRate > 0.0f) {
        float emissionInterval = 1.0f / config.emissionRate;
        while (instance.emissionTimer >= emissionInterval) {
          // Create particle via lock-free system
          createParticleForEffect(defIt->second, instance.position);
          instance.emissionTimer -= emissionInterval;
        }
      }
    }

    ++it;
  }
}
void ParticleManager::updateParticlesThreaded(float deltaTime,
                                              size_t activeParticleCount) {
  // Use lock-free double buffering for threaded updates
  auto &currentBuffer = m_storage.getCurrentBuffer();

  // WorkerBudget-aware threading following engine architecture
  // This implementation follows the same patterns as AIManager for consistent
  // resource allocation across the engine's subsystems
  auto &threadSystem = HammerEngine::ThreadSystem::Instance();
  size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
  size_t queueSize = threadSystem.getQueueSize();
  size_t queueCapacity = threadSystem.getQueueCapacity();

  // Calculate WorkerBudget allocation for particle system
  // WorkerBudget ensures fair distribution of threads between AI, particles,
  // events, etc.
  HammerEngine::WorkerBudget budget =
      HammerEngine::calculateWorkerBudget(availableWorkers);

  // Use WorkerBudget system with threshold-based buffer allocation
  // This allows particle system to use additional threads when workload is high
  size_t optimalWorkerCount = budget.getOptimalWorkerCount(
      budget.particleAllocated, activeParticleCount, m_threadingThreshold);

  // Dynamic batch sizing based on queue pressure for optimal performance
  // This prevents overwhelming the ThreadSystem when other subsystems are busy
  size_t minParticlesPerBatch = 500;
  size_t maxBatches = 8;

  // Adjust batch strategy based on queue pressure
  double queuePressure = static_cast<double>(queueSize) / queueCapacity;
  if (queuePressure > 0.5) {
    // High pressure: use fewer, larger batches to reduce queue overhead
    minParticlesPerBatch = 1000;
    maxBatches = 4;
  } else if (queuePressure < 0.25) {
    // Low pressure: can use more batches for better parallelization
    minParticlesPerBatch = 300;
    maxBatches = 8;
  }

  size_t batchCount =
      std::min(optimalWorkerCount, activeParticleCount / minParticlesPerBatch);
  batchCount = std::max(size_t(1), std::min(batchCount, maxBatches));

  size_t particlesPerBatch = activeParticleCount / batchCount;
  size_t remainingParticles = activeParticleCount % batchCount;

  // Submit optimized batches to ThreadSystem with Normal priority
  // Particle updates are typically non-critical compared to AI or input
  // processing
  std::vector<std::future<void>> futures;
  futures.reserve(batchCount);

  for (size_t i = 0; i < batchCount; ++i) {
    size_t startIdx = i * particlesPerBatch;
    size_t endIdx = startIdx + particlesPerBatch +
                    (i == batchCount - 1 ? remainingParticles : 0);

    futures.push_back(threadSystem.enqueueTaskWithResult(
        [this, &currentBuffer, startIdx, endIdx, deltaTime]() {
          updateParticleRange(currentBuffer, startIdx, endIdx, deltaTime);
        },
        HammerEngine::TaskPriority::Normal, "Particle_UpdateBatch"));
  }

  // Wait for all particle update batches to complete
  for (auto &future : futures) {
    future.get();
  }
}
void ParticleManager::updateParticlesSingleThreaded(
    float deltaTime, size_t activeParticleCount) {
  auto &currentBuffer = m_storage.getCurrentBuffer();
  updateParticleRange(currentBuffer, 0, activeParticleCount, deltaTime);
}

void ParticleManager::updateParticleRange(
    std::vector<UnifiedParticle> &particles, size_t startIdx, size_t endIdx,
    float deltaTime) {
  static float windPhase = 0.0f; // Static wind phase for natural variation
  windPhase += deltaTime * 0.5f; // Slow wind variation

  for (size_t i = startIdx; i < endIdx; ++i) {
    if (i >= particles.size() || !particles[i].isActive())
      continue;

    auto &particle = particles[i];

    // Enhanced physics with natural atmospheric effects
    float windVariation =
        std::sin(windPhase + i * 0.1f) * 0.3f; // Per-particle wind variation
    float atmosphericDrag = 0.98f;             // Slight air resistance

    // Cloud detection and movement for light-colored particles
    uint8_t r = (particle.color >> 24) & 0xFF;
    uint8_t g = (particle.color >> 16) & 0xFF;
    uint8_t b = (particle.color >> 8) & 0xFF;

    // Cloud particles: light white/gray range (240-255 RGB)
    bool isCloud = (r >= 240 && g >= 240 && b >= 240);

    if (isCloud) {
      // Apply horizontal movement for cloud drift
      particle.acceleration.setX(15.0f);
      particle.acceleration.setY(0.0f);

      // Add natural wind variation
      float drift = std::sin(windPhase * 0.8f + i * 0.15f) * 3.0f;
      float verticalFloat = std::cos(windPhase * 1.2f + i * 0.1f) * 1.5f;

      particle.velocity.setX(particle.velocity.getX() + drift * deltaTime);
      particle.velocity.setY(particle.velocity.getY() +
                             verticalFloat * deltaTime);

      atmosphericDrag = 1.0f;
    }
    // Apply wind variation for weather particles
    else if (particle.isWeatherParticle()) {
      // Add natural wind turbulence
      particle.acceleration.setX(particle.acceleration.getX() +
                                 windVariation * 20.0f);

      // Different atmospheric effects for different particle types
      float lifeRatio = particle.getLifeRatio();

      // Snow particles drift more with wind and have flutter
      if (particle.generationId % 3 == 0) { // Assume snow-like behavior
        float flutter = std::sin(windPhase * 3.0f + i * 0.2f) * 8.0f;
        particle.velocity.setX(particle.velocity.getX() + flutter * deltaTime);
        atmosphericDrag = 0.96f; // More air resistance for snow
      }

      // Rain particles are more affected by gravity as they age
      else if (particle.generationId % 3 == 1) { // Assume rain-like behavior
        particle.acceleration.setY(particle.acceleration.getY() +
                                   (1.0f - lifeRatio) *
                                       50.0f); // Accelerate with age
        atmosphericDrag = 0.99f;               // Less air resistance for rain
      }

      // Fog/cloud particles drift and have gentle movement
      else { // Regular fog behavior (not clouds)
        float drift = std::sin(windPhase * 0.8f + i * 0.15f) * 15.0f;
        particle.velocity.setX(particle.velocity.getX() + drift * deltaTime);
        particle.velocity.setY(particle.velocity.getY() +
                               std::cos(windPhase * 1.2f + i * 0.1f) * 3.0f *
                                   deltaTime);
        atmosphericDrag = 0.999f;
      }
    }
    // Special handling for fire and smoke particles for natural movement
    else {
      float lifeRatio = particle.getLifeRatio();

      // Fire particles: flickering, turbulent movement with heat distortion
      if (particle.textureIndex == getTextureIndex("fire_particle") ||
          (particle.color & 0xFF000000) ==
              0xFF000000) { // Detect fire by color/texture

        // Heat-based turbulence - more chaotic movement
        float heatTurbulence = std::sin(windPhase * 8.0f + i * 0.3f) * 15.0f;
        float heatRise = std::cos(windPhase * 6.0f + i * 0.25f) * 10.0f;

        particle.velocity.setX(particle.velocity.getX() +
                               heatTurbulence * deltaTime);
        particle.velocity.setY(particle.velocity.getY() + heatRise * deltaTime);

        // Fire gets more chaotic as it ages (burns out)
        float chaos = (1.0f - lifeRatio) * 25.0f;
        particle.acceleration.setX(
            particle.acceleration.getX() +
            (std::sin(windPhase * 12.0f + i * 0.4f) * chaos * deltaTime));

        atmosphericDrag = 0.94f; // High drag for fire flicker
      }

      // Smoke particles: billowing, wind-affected movement
      else if (particle.textureIndex == getTextureIndex("smoke_particle") ||
               ((particle.color & 0xFF000000) >> 24) <
                   200) { // Detect smoke by transparency

        // Billowing smoke movement with wind influence
        float smokeWind = windVariation * 40.0f;
        float smokeBillow = std::sin(windPhase * 2.0f + i * 0.12f) * 20.0f;
        float smokeRise = std::cos(windPhase * 1.5f + i * 0.08f) * 8.0f;

        particle.velocity.setX(particle.velocity.getX() +
                               (smokeWind + smokeBillow) * deltaTime);
        particle.velocity.setY(particle.velocity.getY() +
                               smokeRise * deltaTime);

        // Smoke disperses and slows down as it ages
        float dispersion = lifeRatio * 0.5f; // Older smoke is more dispersed
        particle.velocity.setX(particle.velocity.getX() *
                               (1.0f - dispersion * deltaTime));

        // Wind affects smoke more as it gets older and lighter
        float windSensitivity = (1.0f - lifeRatio) * 30.0f;
        particle.acceleration.setX(particle.acceleration.getX() +
                                   windSensitivity * windVariation * deltaTime);

        atmosphericDrag = 0.92f; // High drag for realistic smoke movement
      }

      // Other particles (sparks, magic, etc.) - use standard turbulence
      else {
        float generalTurbulence = windVariation * 10.0f;
        particle.velocity.setX(particle.velocity.getX() +
                               generalTurbulence * deltaTime);
        atmosphericDrag = 0.97f;
      }
    }

    // Apply atmospheric drag
    particle.velocity.setX(particle.velocity.getX() * atmosphericDrag);
    particle.velocity.setY(particle.velocity.getY() * atmosphericDrag);

    // Update physics
    particle.velocity.setX(particle.velocity.getX() +
                           particle.acceleration.getX() * deltaTime);
    particle.velocity.setY(particle.velocity.getY() +
                           particle.acceleration.getY() * deltaTime);

    particle.position.setX(particle.position.getX() +
                           particle.velocity.getX() * deltaTime);
    particle.position.setY(particle.position.getY() +
                           particle.velocity.getY() * deltaTime);

    // Update life
    particle.life -= deltaTime;
    if (particle.life <= 0.0f) {
      particle.setActive(false);
      continue;
    }

    // Enhanced visual properties with natural fading
    float lifeRatio = particle.getLifeRatio();

    // Natural fade-in and fade-out for weather particles
    float alphaMultiplier = 1.0f;
    if (particle.isWeatherParticle()) {
      if (lifeRatio > 0.9f) {
        // Fade in during first 10% of life
        alphaMultiplier = (1.0f - lifeRatio) * 10.0f;
      } else if (lifeRatio < 0.2f) {
        // Fade out during last 20% of life
        alphaMultiplier = lifeRatio * 5.0f;
      }
    } else {
      // Standard fade for non-weather particles
      alphaMultiplier = lifeRatio;
    }

    uint8_t alpha = static_cast<uint8_t>(255 * alphaMultiplier);
    particle.color = (particle.color & 0xFFFFFF00) | alpha;

    // Note: Size variation for natural appearance would be applied during
    // rendering
  }
}

void ParticleManager::createParticleForEffect(
    const ParticleEffectDefinition &effectDef, const Vector2D &position) {
  // Create a new particle request for the lock-free system
  const auto &config = effectDef.emitterConfig;
  NewParticleRequest request;

  // WEATHER COVERAGE FIX: Spread weather particles across entire screen
  Vector2D spawnPosition = position;
  if (effectDef.type == ParticleEffectType::Rain ||
      effectDef.type == ParticleEffectType::HeavyRain ||
      effectDef.type == ParticleEffectType::Snow ||
      effectDef.type == ParticleEffectType::HeavySnow ||
      effectDef.type == ParticleEffectType::Fog ||
      (effectDef.type == ParticleEffectType::Fog &&
       effectDef.name == "Cloudy")) {

    // Spread particles across full screen width (much wider for rain/snow)
    float screenWidth = 3200.0f; // Much wider to ensure full coverage
    float randomX = (static_cast<float>(rand()) / RAND_MAX) * screenWidth -
                    600.0f; // -600 to 2600 for complete coverage
    spawnPosition.setX(randomX);

    // Different Y positioning for different effect types
    if (effectDef.type == ParticleEffectType::Fog) {
      if (effectDef.name == "Cloudy") {
        // Clouds spread across full screen height for layered effect
        float screenHeight = 1080.0f; // Full screen height
        float randomY = (static_cast<float>(rand()) / RAND_MAX) * screenHeight;
        spawnPosition.setY(randomY);
      } else {
        // Fog spreads across full screen height for complete coverage
        float screenHeight = 1080.0f; // Full screen height
        float randomY = (static_cast<float>(rand()) / RAND_MAX) * screenHeight;
        spawnPosition.setY(randomY);
      }
    } else {
      // Rain/snow need FULL SCREEN COVERAGE immediately
      // Spawn particles across the entire screen height for instant coverage
      float screenHeight = 1080.0f;
      float randomY =
          (static_cast<float>(rand()) / RAND_MAX) * (screenHeight + 200.0f) -
          100.0f; // -100 to 1180 for coverage + some above/below
      spawnPosition.setY(randomY);
    }
  }
  // FIRE AND SMOKE DISPERSION: Add random scatter around base position
  else if (effectDef.type == ParticleEffectType::Fire) {
    // Fire particles need random dispersion in a circular area
    float disperseRadius = 25.0f; // Random spread radius
    float randomAngle = (static_cast<float>(rand()) / RAND_MAX) * 2.0f * M_PI;
    float randomRadius =
        (static_cast<float>(rand()) / RAND_MAX) * disperseRadius;

    spawnPosition.setX(position.getX() + randomRadius * cos(randomAngle));
    spawnPosition.setY(position.getY() + randomRadius * sin(randomAngle));

    // Add small vertical offset for natural fire base variation
    float verticalOffset =
        (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f;
    spawnPosition.setY(spawnPosition.getY() + verticalOffset);
  } else if (effectDef.type == ParticleEffectType::Smoke) {
    // Smoke particles need wider random dispersion
    float disperseRadius = 40.0f; // Wider spread for smoke
    float randomAngle = (static_cast<float>(rand()) / RAND_MAX) * 2.0f * M_PI;
    float randomRadius =
        (static_cast<float>(rand()) / RAND_MAX) * disperseRadius;

    spawnPosition.setX(position.getX() + randomRadius * cos(randomAngle));
    spawnPosition.setY(position.getY() + randomRadius * sin(randomAngle));

    // Add more vertical variation for smoke sources
    float verticalOffset =
        (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 20.0f;
    spawnPosition.setY(spawnPosition.getY() + verticalOffset);
  }

  request.position = spawnPosition;

  // FIXED: Use the old system's angular velocity calculation for correct
  // behavior
  float naturalRand = static_cast<float>(rand()) / RAND_MAX;
  float speed =
      config.minSpeed + (config.maxSpeed - config.minSpeed) * naturalRand;

  // Convert spread to radians and apply random angle within spread
  float angleRange = config.spread * 0.017453f; // Convert degrees to radians
  float angle = (naturalRand * 2.0f - 1.0f) * angleRange;

  // Apply effect-specific angle biasing for realistic movement patterns
  if (effectDef.type == ParticleEffectType::Rain) {
    angle = (M_PI * 0.5f) + (angle * 0.05f); // Very vertical with minimal drift
  } else if (effectDef.type == ParticleEffectType::HeavyRain) {
    angle = (M_PI * 0.5f) + (angle * 0.08f); // Slightly more drift in storms
  } else if (effectDef.type == ParticleEffectType::Snow) {
    angle = (M_PI * 0.5f) + (angle * 0.4f); // Gentle downward with more flutter
  } else if (effectDef.type == ParticleEffectType::HeavySnow) {
    angle = (M_PI * 0.5f) + (angle * 0.5f); // More chaotic movement in blizzard
  } else if (effectDef.type == ParticleEffectType::Fire) {
    // Fire goes upward with random spread and velocity variation
    angle = (M_PI * 1.5f) + angle; // Upward direction with spread

    // Add random velocity variation for natural fire movement
    float velocityVariation =
        (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.4f;
    speed *= (1.0f + velocityVariation); // ±20% speed variation

    // Add random angular jitter for flickering effect
    float angularJitter = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.3f;
    angle += angularJitter;

  } else if (effectDef.type == ParticleEffectType::Smoke) {
    // Smoke goes upward with wider spread and more random movement
    angle = (M_PI * 1.5f) + angle; // Upward direction with spread

    // Add significant velocity variation for billowing smoke
    float velocityVariation =
        (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.6f;
    speed *= (1.0f + velocityVariation); // ±30% speed variation

    // Add random angular variation for natural smoke dispersion
    float angularVariation =
        (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 0.5f;
    angle += angularVariation;

  } else if (effectDef.type == ParticleEffectType::Fog) {
    // Fog has gentle horizontal drift
    if (effectDef.name == "Cloudy") {
      // Clouds move horizontally - use 0 degrees for rightward movement
      angle =
          0.0f +
          (angle *
           0.05f); // Horizontal (0 degrees = rightward) with minimal variation
      speed =
          std::max(speed, 25.0f); // Ensure minimum horizontal speed for clouds
    } else {
      // Regular fog has minimal movement
      angle = angle * 0.5f; // Very small movement in any direction
    }
  }

  // Calculate velocity using trigonometric approach like the old system
  request.velocity = Vector2D(speed * sin(angle), speed * cos(angle));
  request.acceleration = config.gravity;
  request.life = config.minLife + (config.maxLife - config.minLife) *
                                      static_cast<float>(rand()) / RAND_MAX;
  request.size = config.minSize + (config.maxSize - config.minSize) *
                                      static_cast<float>(rand()) / RAND_MAX;

  // CRITICAL FIX: Implement effect-based color assignment like the old system

  if (effectDef.type == ParticleEffectType::Rain ||
      effectDef.type == ParticleEffectType::HeavyRain) {
    // Blue rain with slight variation
    uint8_t blue = static_cast<uint8_t>(200 + naturalRand * 55);  // 200-255
    uint8_t green = static_cast<uint8_t>(100 + naturalRand * 50); // 100-150
    uint8_t red = static_cast<uint8_t>(50 + naturalRand * 30);    // 50-80
    request.color = (red << 24) | (green << 16) | (blue << 8) | 0xFF;
  } else if (effectDef.type == ParticleEffectType::Snow ||
             effectDef.type == ParticleEffectType::HeavySnow) {
    // Bright white snow with high opacity for visibility
    uint8_t alphaVariation =
        static_cast<uint8_t>(240 + naturalRand * 15); // 240-255 (very opaque)
    request.color = 0xFFFFFF00 | alphaVariation;
  } else if (effectDef.type == ParticleEffectType::Fog) {
    if (effectDef.name == "Cloudy") {
      // Natural cloud color variation (like old system)
      uint8_t grayLevel =
          static_cast<uint8_t>(240 + naturalRand * 15);             // 240-255
      uint8_t alpha = static_cast<uint8_t>(180 + naturalRand * 75); // 180-255
      request.color =
          (grayLevel << 24) | (grayLevel << 16) | (grayLevel << 8) | alpha;
    } else {
      // Natural fog variation with much more transparency
      uint8_t grayLevel =
          static_cast<uint8_t>(200 + naturalRand * 30); // 200-230 (lighter)
      uint8_t alpha = static_cast<uint8_t>(
          40 + naturalRand * 40); // 40-80 (much more transparent)
      request.color =
          (grayLevel << 24) | (grayLevel << 16) | (grayLevel << 8) | alpha;
    }
  } else if (effectDef.type == ParticleEffectType::Fire) {
    // Natural fire color generation with realistic color mixing
    if (naturalRand < 0.3f) {
      // Deep red-orange (base of flame)
      uint8_t red = static_cast<uint8_t>(200 + naturalRand * 55);  // 200-255
      uint8_t green = static_cast<uint8_t>(60 + naturalRand * 40); // 60-100
      request.color = (red << 24) | (green << 16) | 0xFF;
    } else if (naturalRand < 0.6f) {
      // Orange (middle flame)
      uint8_t red = 255;
      uint8_t green = static_cast<uint8_t>(120 + naturalRand * 80); // 120-200
      uint8_t blue = static_cast<uint8_t>(naturalRand * 30);        // 0-30
      request.color = (red << 24) | (green << 16) | (blue << 8) | 0xFF;
    } else {
      // Yellow-white (tip of flame)
      uint8_t red = 255;
      uint8_t green = static_cast<uint8_t>(200 + naturalRand * 55); // 200-255
      uint8_t blue = static_cast<uint8_t>(naturalRand * 60);        // 0-60
      request.color = (red << 24) | (green << 16) | (blue << 8) | 0xFF;
    }
  } else if (effectDef.type == ParticleEffectType::Smoke) {
    // Use original smoke colors variation
    static const std::array<uint32_t, 8> smokeColors{
        {0x404040FF, 0x606060FF, 0x808080FF, 0x202020FF, 0x4A4A4AFF, 0x505050FF,
         0x707070FF, 0x303030FF}};
    size_t colorIndex = static_cast<size_t>(naturalRand * smokeColors.size());
    colorIndex = std::min(colorIndex, smokeColors.size() - 1);
    request.color = smokeColors[colorIndex];
  } else if (effectDef.type == ParticleEffectType::Sparks) {
    // Natural spark colors with more variation
    if (naturalRand < 0.7f) {
      // Bright yellow sparks
      uint8_t red = static_cast<uint8_t>(240 + naturalRand * 15);   // 240-255
      uint8_t green = static_cast<uint8_t>(220 + naturalRand * 35); // 220-255
      uint8_t blue = static_cast<uint8_t>(naturalRand * 40);        // 0-40
      request.color = (red << 24) | (green << 16) | (blue << 8) | 0xFF;
    } else {
      // Orange-white sparks
      uint8_t red = 255;
      uint8_t green = static_cast<uint8_t>(140 + naturalRand * 60); // 140-200
      uint8_t blue = static_cast<uint8_t>(naturalRand * 20);        // 0-20
      request.color = (red << 24) | (green << 16) | (blue << 8) | 0xFF;
    }
  } else {
    request.color = config.minColor; // Fallback to config color
  }

  request.textureIndex = getTextureIndex(config.textureID);
  request.blendMode = config.blendMode;

  // Submit to lock-free ring buffer
  m_storage.submitNewParticle(request);
}

uint16_t ParticleManager::getTextureIndex(const std::string &textureID) {
  auto it = m_textureIndices.find(textureID);
  if (it != m_textureIndices.end()) {
    return it->second;
  }

  // Add new texture ID
  uint16_t index = static_cast<uint16_t>(m_textureIDs.size());
  m_textureIDs.push_back(textureID);
  m_textureIndices[textureID] = index;
  return index;
}

bool ParticleManager::isGloballyPaused() const {
  return m_globallyPaused.load(std::memory_order_acquire);
}

void ParticleManager::setGlobalPause(bool paused) {
  m_globallyPaused.store(paused, std::memory_order_release);
}

size_t ParticleManager::getMaxParticleCapacity() const {
  return m_storage.capacity.load(std::memory_order_acquire);
}

bool ParticleManager::isGloballyVisible() const {
  return m_globallyVisible.load(std::memory_order_acquire);
}

void ParticleManager::setGlobalVisibility(bool visible) {
  m_globallyVisible.store(visible, std::memory_order_release);
}

void ParticleManager::setMaxParticles(size_t maxParticles) {
  m_storage.capacity.store(maxParticles, std::memory_order_release);
}

void ParticleManager::enableWorkerBudgetThreading(bool enable) {
  /**
   * Enable or disable WorkerBudget-aware threading for ParticleManager.
   *
   * WorkerBudget integration provides several benefits:
   * - Fair resource allocation with other engine subsystems (AI, Events, etc.)
   * - Dynamic thread allocation based on workload and system pressure
   * - Automatic scaling from single-threaded to multi-threaded operation
   * - Queue pressure monitoring to prevent ThreadSystem overload
   *
   * When enabled, ParticleManager will:
   * 1. Calculate its allocated thread budget using
   * HammerEngine::calculateWorkerBudget()
   * 2. Use budget.getOptimalWorkerCount() to determine threads needed for
   * current workload
   * 3. Submit particle update batches via ThreadSystem::enqueueTaskWithResult()
   * 4. Adjust batch sizes based on ThreadSystem queue pressure
   *
   * @param enable True to enable WorkerBudget threading, false for legacy
   * threading
   */
  m_useWorkerBudget.store(enable, std::memory_order_release);

  // When enabled, ensure main threading is also enabled
  if (enable) {
    m_useThreading.store(true, std::memory_order_release);
  }

  PARTICLE_INFO("WorkerBudget threading " +
                std::string(enable ? "enabled" : "disabled"));
}

void ParticleManager::updateWithWorkerBudget(float deltaTime,
                                             size_t particleCount) {
  /**
   * WorkerBudget-optimized particle update path.
   *
   * This method serves as the entry point for WorkerBudget-aware particle
   * updates. It performs validation and fallback logic before delegating to the
   * threaded update implementation.
   *
   * @param deltaTime Time elapsed since last update
   * @param particleCount Current number of active particles
   */
  if (!m_useWorkerBudget.load(std::memory_order_acquire) ||
      particleCount < m_threadingThreshold ||
      !HammerEngine::ThreadSystem::Exists()) {
    // Fall back to regular single-threaded update
    updateParticlesSingleThreaded(deltaTime, particleCount);
    return;
  }

  // Use WorkerBudget-aware threaded update
  updateParticlesThreaded(deltaTime, particleCount);
}

void ParticleManager::configureThreading(bool useThreading,
                                         unsigned int maxThreads) {
  m_useThreading.store(useThreading, std::memory_order_release);
  m_maxThreads = maxThreads;

  PARTICLE_INFO("Threading configured: " +
                std::string(useThreading ? "enabled" : "disabled") +
                (maxThreads > 0 ? " (max: " + std::to_string(maxThreads) + ")"
                                : " (auto)"));
}

void ParticleManager::setThreadingThreshold(size_t threshold) {
  m_threadingThreshold = threshold;
  PARTICLE_INFO("Threading threshold set to " + std::to_string(threshold) +
                " particles");
}
