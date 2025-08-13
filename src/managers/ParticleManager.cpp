/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/ParticleManager.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

// A simple and fast pseudo-random number generator
inline int fast_rand() {
    static thread_local unsigned int g_seed = []() {
        // Initialize with a combination of time and thread ID for better distribution
        auto now = std::chrono::high_resolution_clock::now();
        auto time_seed = static_cast<unsigned int>(now.time_since_epoch().count());
        auto thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
        return time_seed ^ static_cast<unsigned int>(thread_id);
    }();
    g_seed = (214013 * g_seed + 2531011);
    return (g_seed >> 16) & 0x7FFF;
}

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
  if (!m_initialized.load(std::memory_order_acquire) || m_isShutdown) {
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
      PARTICLE_INFO(
          "Removing weather effect: " + effectTypeToString(it->effectType) +
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
  const size_t particleCount = activeParticles.size();

  for (size_t i = 0; i < particleCount; ++i) {
    if (activeParticles.flags[i] & UnifiedParticle::FLAG_ACTIVE) {
      activeParticles.flags[i] &= ~UnifiedParticle::FLAG_ACTIVE;
      activeParticles.lives[i] = 0.0f; // Ensure life is zero for double safety
      particlesCleared++;
    }
  }

  // 3.5. IMMEDIATE COMPLETE CLEANUP - Remove ALL particles to ensure zero count
  activeParticles.clear(); // Complete clear to ensure zero particles
  m_storage.particles[1 - activeIdx].clear(); // Clear other buffer too
  m_storage.particleCount.store(0, std::memory_order_release);

  PARTICLE_INFO("Complete particle cleanup: cleared " +
                std::to_string(particlesCleared) +
                " active particles from storage");

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
    size_t totalParticleCount = particles.positions.size();
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

    // Phase 6: Optimized memory management - less aggressive
    uint64_t currentFrame =
        m_frameCounter.fetch_add(1, std::memory_order_relaxed);
    if (currentFrame % 600 == 0) { // Every 10 seconds at 60fps (was 5)
      compactParticleStorageIfNeeded();
    }

    if (currentFrame % 3600 == 0) { // Every 60 seconds - deep cleanup (was 30)
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

  // THREAD SAFETY: Get immutable snapshot of particle data for rendering
  const auto &particles = m_storage.getParticlesForRead();
  int renderCount = 0;
  
  // IMPROVED FIX: Use safest available particle count for rendering
  // This allows partial rendering when buffers are mostly consistent
  const size_t safeParticleCount = std::min({
    particles.positions.size(),
    particles.flags.size(), 
    particles.colors.size(),
    particles.sizes.size()
  });
  
  // Only skip rendering if no particles are available
  if (safeParticleCount == 0) {
    return;
  }

  for (size_t i = 0; i < safeParticleCount; ++i) {
    // BOUNDS CHECK: Defensive programming for edge cases
    if (i >= particles.flags.size() || i >= particles.colors.size() || 
        i >= particles.sizes.size() || i >= particles.positions.size()) {
      break; // Exit safely if we somehow exceed bounds
    }
    
    if (!(particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) || !(particles.flags[i] & UnifiedParticle::FLAG_VISIBLE)) {
      continue;
    }

    // Skip particles that are completely transparent
    uint8_t alpha = particles.colors[i] & 0xFF;
    if (alpha == 0) {
      continue;
    }

    renderCount++;

    // Extract color components
    uint8_t r = (particles.colors[i] >> 24) & 0xFF;
    uint8_t g = (particles.colors[i] >> 16) & 0xFF;
    uint8_t b = (particles.colors[i] >> 8) & 0xFF;
    SDL_SetRenderDrawColor(renderer, r, g, b, alpha);

    // Get particle size
    float size = particles.sizes[i];

    // Render particle as a filled rectangle (accounting for camera offset)
    SDL_FRect rect = {particles.positions[i].getX() - cameraX - size / 2,
                      particles.positions[i].getY() - cameraY - size / 2, size,
                      size};
    SDL_RenderFillRect(renderer, &rect);
  }

  // Periodic summary logging (every 900 frames ~15 seconds)
  uint64_t currentFrame =
      m_frameCounter.fetch_add(1, std::memory_order_relaxed);
  if (currentFrame % 900 == 0 && renderCount > 0) {
    PARTICLE_DEBUG(
        "Particle Summary - Total: " + std::to_string(safeParticleCount) +
        ", Active: " + std::to_string(renderCount) +
        ", Effects: " + std::to_string(m_effectInstances.size()));
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  double timeMs =
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
          .count() /
      1000.0;
  recordPerformance(true, timeMs, safeParticleCount);
}

void ParticleManager::renderBackground(SDL_Renderer *renderer, float cameraX,
                                       float cameraY) {
  if (m_globallyPaused.load(std::memory_order_acquire) ||
      !m_globallyVisible.load(std::memory_order_acquire)) {
    return;
  }

  // THREAD SAFETY: Get immutable snapshot of particle data
  const auto &particles = m_storage.getParticlesForRead();
  
  // IMPROVED FIX: Use safest available particle count for background rendering
  // This allows partial rendering when buffers are mostly consistent
  const size_t safeParticleCount = std::min({
    particles.positions.size(),
    particles.flags.size(),
    particles.colors.size(),
    particles.sizes.size(),
    particles.layers.size()
  });
  
  // Only skip rendering if no particles are available
  if (safeParticleCount == 0) {
    return;
  }

  for (size_t i = 0; i < safeParticleCount; ++i) {
    // BOUNDS CHECK: Defensive programming for edge cases
    if (i >= particles.flags.size() || i >= particles.colors.size() || 
        i >= particles.sizes.size() || i >= particles.positions.size() ||
        i >= particles.layers.size()) {
      break; // Exit safely if we somehow exceed bounds
    }
    
    if (!(particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) || !(particles.flags[i] & UnifiedParticle::FLAG_VISIBLE) ||
        particles.layers[i] != UnifiedParticle::RenderLayer::Background) {
      continue;
    }

    // Extract color components
    uint8_t r = (particles.colors[i] >> 24) & 0xFF;
    uint8_t g = (particles.colors[i] >> 16) & 0xFF;
    uint8_t b = (particles.colors[i] >> 8) & 0xFF;
    uint8_t a = particles.colors[i] & 0xFF;

    // Set particle color
    SDL_SetRenderDrawColor(renderer, r, g, b, a);

    // Use particle size directly without any size limits
    float size = particles.sizes[i];

    // Render particle as a filled rectangle (accounting for camera offset)
    SDL_FRect rect = {particles.positions[i].getX() - cameraX - size / 2,
                      particles.positions[i].getY() - cameraY - size / 2, size,
                      size};
    SDL_RenderFillRect(renderer, &rect);
  }
}

void ParticleManager::renderForeground(SDL_Renderer *renderer, float cameraX,
                                       float cameraY) {
  if (m_globallyPaused.load(std::memory_order_acquire) ||
      !m_globallyVisible.load(std::memory_order_acquire)) {
    return;
  }

  // THREAD SAFETY: Get immutable snapshot of particle data
  const auto &particles = m_storage.getParticlesForRead();
  
  // IMPROVED FIX: Use safest available particle count for foreground rendering
  // This allows partial rendering when buffers are mostly consistent
  const size_t safeParticleCount = std::min({
    particles.positions.size(),
    particles.flags.size(),
    particles.colors.size(),
    particles.sizes.size(),
    particles.layers.size()
  });
  
  // Only skip rendering if no particles are available
  if (safeParticleCount == 0) {
    return;
  }

  for (size_t i = 0; i < safeParticleCount; ++i) {
    // BOUNDS CHECK: Defensive programming for edge cases
    if (i >= particles.flags.size() || i >= particles.colors.size() || 
        i >= particles.sizes.size() || i >= particles.positions.size() ||
        i >= particles.layers.size()) {
      break; // Exit safely if we somehow exceed bounds
    }

    if (!(particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) || !(particles.flags[i] & UnifiedParticle::FLAG_VISIBLE) ||
        particles.layers[i] != UnifiedParticle::RenderLayer::Foreground) {
      continue;
    }

    // Extract RGB components
    uint8_t r = (particles.colors[i] >> 24) & 0xFF;
    uint8_t g = (particles.colors[i] >> 16) & 0xFF;
    uint8_t b = (particles.colors[i] >> 8) & 0xFF;
    uint8_t a = particles.colors[i] & 0xFF;

    // Set particle color
    SDL_SetRenderDrawColor(renderer, r, g, b, a);

    // Use particle size directly without any size limits
    float size = particles.sizes[i];

    // Render particle as a filled rectangle (accounting for camera offset)
    SDL_FRect rect = {particles.positions[i].getX() - cameraX - size / 2,
                      particles.positions[i].getY() - cameraY - size / 2, size,
                      size};
    SDL_RenderFillRect(renderer, &rect);
  }
}

uint32_t ParticleManager::playEffect(ParticleEffectType effectType,
                                     const Vector2D &position,
                                     float intensity) {
  PARTICLE_INFO("*** PLAY EFFECT CALLED: " + effectTypeToString(effectType) +
                " at (" + std::to_string(position.getX()) + ", " +
                std::to_string(position.getY()) +
                ") intensity=" + std::to_string(intensity));

  // PERFORMANCE: Exclusive lock needed for adding effect instances
  std::unique_lock<std::shared_mutex> lock(m_effectsMutex);

  // Check if effect definition exists
  auto it = m_effectDefinitions.find(effectType);
  if (it == m_effectDefinitions.end()) {
    PARTICLE_ERROR("ERROR: Effect not registered: " +
                   effectTypeToString(effectType));
    PARTICLE_INFO("Available effects: " +
                  std::to_string(m_effectDefinitions.size()));
    for (const auto &pair : m_effectDefinitions) {
      PARTICLE_INFO("  - " + effectTypeToString(pair.first));
    }
    return 0;
  }

  EffectInstance instance;
  instance.id = generateEffectId();
  instance.effectType = effectType;
  instance.position = position;
  instance.intensity = intensity;
  instance.currentIntensity = intensity;
  instance.targetIntensity = intensity;
  instance.active = true;

  // Register effect
  m_effectInstances.push_back(instance);
  m_effectIdToIndex[instance.id] = m_effectInstances.size() - 1;

  PARTICLE_INFO(
      "Effect successfully started: " + effectTypeToString(effectType) +
      " (ID: " + std::to_string(instance.id) +
      ") - Total active effects: " + std::to_string(m_effectInstances.size()));

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
  std::unique_lock<std::mutex> lock(m_weatherMutex);

  int stoppedCount = 0;

  // Clean approach: Remove ALL weather effects to prevent accumulation
  auto it = m_effectInstances.begin();
  while (it != m_effectInstances.end()) {
    if (it->isWeatherEffect) {
      PARTICLE_DEBUG("DEBUG: Removing weather effect: " +
                     effectTypeToString(it->effectType) +
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
    const size_t particleCount = particles.size();

    for (size_t i = 0; i < particleCount; ++i) {
      if ((particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) && (particles.flags[i] & UnifiedParticle::FLAG_WEATHER)) {
        particles.flags[i] &= ~UnifiedParticle::FLAG_ACTIVE;
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
    const size_t particleCount = particles.size();

    for (size_t i = 0; i < particleCount; ++i) {
      if ((particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) && (particles.flags[i] & UnifiedParticle::FLAG_WEATHER)) {
        particles.flags[i] |= UnifiedParticle::FLAG_FADE_OUT;
        particles.lives[i] = std::min(particles.lives[i], transitionTime);
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
  const size_t particleCount = particles.size();

  for (size_t i = 0; i < particleCount; ++i) {
    if ((particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) && (particles.flags[i] & UnifiedParticle::FLAG_WEATHER)) {
      // Clear specific generation or all weather particles if generationId is 0
      if (generationId == 0 || particles.generationIds[i] == generationId) {
        if (fadeTime <= 0.0f) {
          // Immediate removal
          particles.flags[i] &= ~UnifiedParticle::FLAG_ACTIVE;
        } else {
          // Set fade-out and limit life to fade time
          particles.flags[i] |= UnifiedParticle::FLAG_FADE_OUT;
          particles.lives[i] = std::min(particles.lives[i], fadeTime);
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
  // Convert string weather type to enum and delegate to enum-based method
  ParticleEffectType effectType = weatherStringToEnum(weatherType, intensity);

  // Handle Clear weather - just stop effects and return
  if (weatherType == "Clear") {
    stopWeatherEffects(transitionTime);
    return;
  }

  // Use enum-based method (this will handle the logging)
  triggerWeatherEffect(effectType, intensity, transitionTime);
}

void ParticleManager::triggerWeatherEffect(ParticleEffectType effectType,
                                           float intensity,
                                           float transitionTime) {
  PARTICLE_INFO(
      "*** WEATHER EFFECT TRIGGERED: " + effectTypeToString(effectType) +
      " intensity=" + std::to_string(intensity));

  // Use smooth transitions for better visual quality
  float actualTransitionTime = (transitionTime > 0.0f) ? transitionTime : 1.5f;

  // THREADING FIX: Effect management requires synchronization
  std::unique_lock<std::mutex> lock(m_weatherMutex);
  std::unique_lock<std::shared_mutex> effectsLock(m_effectsMutex);

  // Clear existing weather effects first
  int stoppedCount = 0;
  auto it = m_effectInstances.begin();
  while (it != m_effectInstances.end()) {
    if (it->isWeatherEffect) {
      PARTICLE_DEBUG("DEBUG: Removing weather effect: " +
                     effectTypeToString(it->effectType) +
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
    const size_t particleCount = particles.size();

    for (size_t i = 0; i < particleCount; ++i) {
      if ((particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) && (particles.flags[i] & UnifiedParticle::FLAG_WEATHER)) {
        particles.flags[i] &= ~UnifiedParticle::FLAG_ACTIVE;
        affectedCount++;
      }
    }
    PARTICLE_INFO("Cleared " + std::to_string(affectedCount) +
                  " weather particles immediately");
  }

  PARTICLE_INFO("Stopped " + std::to_string(stoppedCount) + " weather effects");

  // Validate effect type
  if (effectType >= ParticleEffectType::COUNT) {
    PARTICLE_ERROR("ERROR: Invalid effect type: " +
                   std::to_string(static_cast<int>(effectType)));
    return;
  }

  // Check if effect definition exists
  auto defIt = m_effectDefinitions.find(effectType);
  if (defIt == m_effectDefinitions.end()) {
    PARTICLE_ERROR("ERROR: Effect not registered: " +
                   effectTypeToString(effectType));
    return;
  }

  // Check if effect definition exists
  const auto &definition = m_effectDefinitions[effectType];
  if (definition.name.empty()) {
    PARTICLE_ERROR("ERROR: Effect not registered: " +
                   effectTypeToString(effectType));
    return;
  }

  // Calculate optimal weather position based on effect type
  Vector2D weatherPosition;
  if (effectType == ParticleEffectType::Rain ||
      effectType == ParticleEffectType::HeavyRain ||
      effectType == ParticleEffectType::Snow ||
      effectType == ParticleEffectType::HeavySnow) {
    weatherPosition = Vector2D(960, -100); // High spawn for falling particles
  } else if (effectType == ParticleEffectType::Fog) {
    weatherPosition = Vector2D(960, 300); // Mid-screen for fog spread
  } else {
    weatherPosition = Vector2D(960, -50); // Default top spawn
  }

  // Create new weather effect
  EffectInstance instance;
  instance.id = generateEffectId();
  instance.effectType = effectType;
  instance.position = weatherPosition;
  instance.intensity = intensity;
  instance.currentIntensity = intensity;
  instance.targetIntensity = intensity;
  instance.active = true;
  instance.isWeatherEffect = true; // Mark as weather effect immediately

  // Register effect
  m_effectInstances.emplace_back(std::move(instance));
  m_effectIdToIndex[instance.id] = m_effectInstances.size() - 1;

  PARTICLE_INFO("Weather effect created: " + effectTypeToString(effectType) +
                " (ID: " + std::to_string(instance.id) + ") at position (" +
                std::to_string(weatherPosition.getX()) + ", " +
                std::to_string(weatherPosition.getY()) + ")");
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
    // Create a single central fire source for better performance
    Vector2D basePosition(400, 300);

    // Single main fire effect
    m_fireEffectId = playIndependentEffect(
        ParticleEffectType::Fire, basePosition, 1.2f, -1.0f, "campfire");

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
    // Create single smoke source for better performance
    Vector2D basePosition(400, 280); // Slightly above fire

    // Single main smoke column
    m_smokeEffectId = playIndependentEffect(
        ParticleEffectType::Smoke, basePosition, 1.0f, -1.0f, "campfire_smoke");

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
    m_sparksEffectId =
        playIndependentEffect(ParticleEffectType::Sparks, Vector2D(400, 300));
    m_sparksActive = true;
  } else {
    stopIndependentEffect(m_sparksEffectId);
    m_sparksActive = false;
  }
}

// Independent Effect Management Implementation
uint32_t ParticleManager::playIndependentEffect(
    ParticleEffectType effectType, const Vector2D &position, float intensity,
    float duration, const std::string &groupTag,
    const std::string &soundEffect) {
  PARTICLE_INFO(
      "Playing independent effect: " + effectTypeToString(effectType) +
      " at (" + std::to_string(position.getX()) + ", " +
      std::to_string(position.getY()) + ")");

  // PERFORMANCE: No locks needed for lock-free particle system

  // Check if effect definition exists
  auto it = m_effectDefinitions.find(effectType);
  if (it == m_effectDefinitions.end()) {
    PARTICLE_ERROR("ERROR: Independent effect not registered: " +
                   effectTypeToString(effectType));
    return 0;
  }

  EffectInstance instance;
  instance.id = generateEffectId();
  instance.effectType = effectType;
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

  PARTICLE_INFO(
      "Independent effect started: " + effectTypeToString(effectType) +
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
  for (const auto &effect : m_effectInstances) {
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
  for (const auto &effect : m_effectInstances) {
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
  m_effectDefinitions[ParticleEffectType::Rain] = createRainEffect();
  m_effectDefinitions[ParticleEffectType::HeavyRain] = createHeavyRainEffect();
  m_effectDefinitions[ParticleEffectType::Snow] = createSnowEffect();
  m_effectDefinitions[ParticleEffectType::HeavySnow] = createHeavySnowEffect();
  m_effectDefinitions[ParticleEffectType::Fog] = createFogEffect();
  m_effectDefinitions[ParticleEffectType::Cloudy] = createCloudyEffect();

  // Register independent particle effects
  m_effectDefinitions[ParticleEffectType::Fire] = createFireEffect();
  m_effectDefinitions[ParticleEffectType::Smoke] = createSmokeEffect();
  m_effectDefinitions[ParticleEffectType::Sparks] = createSparksEffect();

  PARTICLE_INFO("Built-in effects registered: " +
                std::to_string(m_effectDefinitions.size()));

  for (const auto &pair : m_effectDefinitions) {
    PARTICLE_INFO("  - Effect: " + effectTypeToString(pair.first));
  }

  // More effects can be added as needed
}

ParticleEffectDefinition ParticleManager::createRainEffect() {
  const auto &gameEngine = GameEngine::Instance();
  ParticleEffectDefinition rain("Rain", ParticleEffectType::Rain);
  rain.layer = UnifiedParticle::RenderLayer::Background;
  rain.emitterConfig.spread = static_cast<float>(gameEngine.getLogicalWidth());
  rain.emitterConfig.emissionRate =
      500.0f; // Reduced emission for better performance while maintaining
              // coverage
  rain.emitterConfig.minSpeed = 100.0f; // Slower rain
  rain.emitterConfig.maxSpeed =
      280.0f;                        
  rain.emitterConfig.minLife = 4.0f; // Longer to ensure screen traversal
  rain.emitterConfig.maxLife = 7.0f;
  rain.emitterConfig.minSize = 2.0f; // Much smaller for realistic raindrops
  rain.emitterConfig.maxSize = 6.0f;
  rain.emitterConfig.minColor = 0xADD8E6FF; // Light blue
  rain.emitterConfig.maxColor = 0x87CEFAFF; // Lighter blue
  rain.emitterConfig.gravity =
      Vector2D(2.0f, 450.0f); // More vertical fall for 2D isometric view
  rain.emitterConfig.windForce =
      Vector2D(3.0f, 1.0f); // Minimal wind for straighter downward fall
  rain.emitterConfig.textureID = "raindrop";
  rain.emitterConfig.blendMode = ParticleBlendMode::Alpha;
  rain.emitterConfig.useWorldSpace = false;
  rain.emitterConfig.position.setY(0);
  rain.intensityMultiplier = 1.4f; // Higher multiplier for better intensity scaling
  return rain;
}

ParticleEffectDefinition ParticleManager::createHeavyRainEffect() {
  const auto &gameEngine = GameEngine::Instance();
  ParticleEffectDefinition heavyRain("HeavyRain",
                                     ParticleEffectType::HeavyRain);
  heavyRain.layer = UnifiedParticle::RenderLayer::Background;
  heavyRain.emitterConfig.spread =
      static_cast<float>(gameEngine.getLogicalWidth());
  heavyRain.emitterConfig.emissionRate =
      800.0f; // Reduced emission while maintaining storm intensity
  heavyRain.emitterConfig.minSpeed =
      200.0f; // Slower heavy rain
  heavyRain.emitterConfig.maxSpeed =
      300.0f;                             
  heavyRain.emitterConfig.minLife = 3.5f; // Good life for screen coverage
  heavyRain.emitterConfig.maxLife = 6.0f;
  heavyRain.emitterConfig.minSize = 1.5f; // Smaller but more numerous
  heavyRain.emitterConfig.maxSize = 5.0f;
  heavyRain.emitterConfig.minColor = 0xADD8E6FF; // Light blue
  heavyRain.emitterConfig.maxColor = 0x87CEFAFF; // Lighter blue
  heavyRain.emitterConfig.gravity = Vector2D(
      5.0f, 500.0f); // Strong vertical fall for intense rain in 2D isometric
  heavyRain.emitterConfig.windForce =
      Vector2D(5.0f, 2.0f); // Minimal wind for mostly vertical heavy rain
  heavyRain.emitterConfig.textureID = "raindrop";
  heavyRain.emitterConfig.blendMode = ParticleBlendMode::Alpha;
  heavyRain.emitterConfig.useWorldSpace = false;
  heavyRain.emitterConfig.position.setY(0);
  heavyRain.intensityMultiplier = 1.8f; // High intensity for storms
  return heavyRain;
}

ParticleEffectDefinition ParticleManager::createSnowEffect() {
  const auto &gameEngine = GameEngine::Instance();
  ParticleEffectDefinition snow("Snow", ParticleEffectType::Snow);
  snow.layer = UnifiedParticle::RenderLayer::Background;
  snow.emitterConfig.spread =
      static_cast<float>(gameEngine.getLogicalWidth()); // Moderate spread for
                                                        // gentle snow drift
  snow.emitterConfig.emissionRate =
      180.0f; // Further reduced emission for optimal density
  snow.emitterConfig.minSpeed = 15.0f; // Faster minimum for quicker snow fall
  snow.emitterConfig.maxSpeed = 50.0f; // Much faster max for more dynamic drift
  snow.emitterConfig.minLife = 8.0f;   // Much longer life for coverage
  snow.emitterConfig.maxLife = 15.0f;  // Extended for slow drift
  snow.emitterConfig.minSize = 8.0f;   // Larger for better visibility
  snow.emitterConfig.maxSize = 16.0f;  // Good visible size range
  snow.emitterConfig.minColor = 0xFFFAFAFF; // White
  snow.emitterConfig.maxColor = 0xE6E6EAFF; // Light grey
  snow.emitterConfig.gravity = Vector2D(
      -2.0f,
      60.0f); // More vertical fall with minimal wind drift for 2D isometric
  snow.emitterConfig.windForce =
      Vector2D(3.0f, 0.5f); // Very gentle wind for mostly downward snow
  snow.emitterConfig.textureID = "snowflake";
  snow.emitterConfig.blendMode = ParticleBlendMode::Alpha;
  snow.emitterConfig.useWorldSpace = false;
  snow.emitterConfig.position.setY(0);
  snow.intensityMultiplier = 1.1f; // Slightly enhanced for visibility
  return snow;
}

ParticleEffectDefinition ParticleManager::createHeavySnowEffect() {
  const auto &gameEngine = GameEngine::Instance();
  ParticleEffectDefinition heavySnow("HeavySnow",
                                     ParticleEffectType::HeavySnow);
  heavySnow.layer = UnifiedParticle::RenderLayer::Background;
  heavySnow.emitterConfig.spread =
      static_cast<float>(gameEngine.getLogicalWidth());
  heavySnow.emitterConfig.emissionRate =
      350.0f; // Further reduced emission for realistic blizzard
  heavySnow.emitterConfig.minSpeed = 25.0f; // Much faster in heavy blizzard
  heavySnow.emitterConfig.maxSpeed = 80.0f; // High wind speeds for blizzard
  heavySnow.emitterConfig.minLife = 5.0f;   // Good life for coverage
  heavySnow.emitterConfig.maxLife = 10.0f;
  heavySnow.emitterConfig.minSize = 6.0f; // Visible but numerous flakes
  heavySnow.emitterConfig.maxSize = 14.0f;
  heavySnow.emitterConfig.minColor = 0xFFFAFAFF; // White
  heavySnow.emitterConfig.maxColor = 0xE6E6EAFF; // Light grey
  heavySnow.emitterConfig.gravity =
      Vector2D(-5.0f, 80.0f); // Stronger vertical fall with some wind for
                              // blizzard in 2D isometric
  heavySnow.emitterConfig.windForce = Vector2D(
      8.0f,
      2.0f); // Moderate wind for blizzard effect but still mostly downward
  heavySnow.emitterConfig.textureID = "snowflake";
  heavySnow.emitterConfig.blendMode = ParticleBlendMode::Alpha;
  heavySnow.emitterConfig.useWorldSpace = false;
  heavySnow.emitterConfig.position.setY(0);
  heavySnow.intensityMultiplier = 1.6f; // High intensity for blizzard
  return heavySnow;
}

ParticleEffectDefinition ParticleManager::createFogEffect() {
  const auto &gameEngine = GameEngine::Instance();
  ParticleEffectDefinition fog("Fog", ParticleEffectType::Fog);
  fog.layer = UnifiedParticle::RenderLayer::Foreground;
  fog.emitterConfig.spread =
      static_cast<float>(gameEngine.getLogicalWidth()); // Very wide spread to
                                                        // cover entire screen
  fog.emitterConfig.emissionRate =
      38.0f;                          // Increased emission rate for denser fog
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
  fog.emitterConfig.useWorldSpace = false;
  fog.intensityMultiplier = 0.9f; // Balanced intensity for fog
  return fog;
}

ParticleEffectDefinition ParticleManager::createCloudyEffect() {
  const auto &gameEngine = GameEngine::Instance();
  ParticleEffectDefinition cloudy("Cloudy", ParticleEffectType::Cloudy);
  cloudy.layer = UnifiedParticle::RenderLayer::Foreground;
  // No initial position - will be set by triggerWeatherEffect
  cloudy.emitterConfig.direction = Vector2D(
      1.0f, 0.0f); // Horizontal movement for clouds sweeping across sky
  cloudy.emitterConfig.spread =
      static_cast<float>(gameEngine.getLogicalWidth());
  cloudy.emitterConfig.emissionRate =
      1.2f; // Further reduced for less dense cloud effect
  cloudy.emitterConfig.minSpeed =
      25.0f; // Much faster horizontal movement for visible sweeping motion
  cloudy.emitterConfig.maxSpeed =
      35.0f; // Reduced speed for more gentle cloud movement
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
  cloudy.emitterConfig.useWorldSpace = false;
  cloudy.intensityMultiplier = 1.2f; // Slightly enhanced intensity
  return cloudy;
}

ParticleEffectDefinition ParticleManager::createFireEffect() {
  ParticleEffectDefinition fire("Fire", ParticleEffectType::Fire);
  fire.layer = UnifiedParticle::RenderLayer::World;
  fire.emitterConfig.position = Vector2D(0, 0);   // Will be set when played
  fire.emitterConfig.direction = Vector2D(0, -1); // Upward flames
  fire.emitterConfig.spread =
      60.0f; // Tighter spread for a more controlled flame
  fire.emitterConfig.emissionRate =
      175.0f; // Halved for performance
  fire.emitterConfig.minSpeed = 20.0f; // Faster base speed for more energy
  fire.emitterConfig.maxSpeed = 110.0f; // Higher max for more dynamic flicker
  fire.emitterConfig.minLife = 0.2f;    // Shorter life for faster flicker
  fire.emitterConfig.maxLife = 1.8f;    // Reduced max life
  fire.emitterConfig.minSize = 4.0f;    // Larger base size
  fire.emitterConfig.maxSize = 14.0f;   // Smaller max size for finer detail
  fire.emitterConfig.minColor = 0xFFD700FF; // Bright Gold/Yellow core
  fire.emitterConfig.maxColor = 0xFF450088; // Orange-Red, semi-transparent
  fire.emitterConfig.gravity =
      Vector2D(0, -45.0f); // Stronger negative gravity for faster rise
  fire.emitterConfig.windForce =
      Vector2D(25.0f, 0); // Reduced wind for less horizontal sway
  fire.emitterConfig.textureID = "fire_particle";
  fire.emitterConfig.blendMode =
      ParticleBlendMode::Additive;     // Additive for glowing effect
  fire.emitterConfig.duration = -1.0f; // Infinite by default
  // Burst configuration for more natural fire
  fire.emitterConfig.burstCount = 15;      // More particles per burst
  fire.emitterConfig.burstInterval = 0.08f; // More frequent bursts
  fire.intensityMultiplier = 1.2f;         // Slightly reduced intensity
  return fire;
}

ParticleEffectDefinition ParticleManager::createSmokeEffect() {
  ParticleEffectDefinition smoke("Smoke", ParticleEffectType::Smoke);
  smoke.layer = UnifiedParticle::RenderLayer::World;
  smoke.emitterConfig.position = Vector2D(0, 0);   // Will be set when played
  smoke.emitterConfig.direction = Vector2D(0, -1); // Upward smoke
  smoke.emitterConfig.spread =
      75.0f; // Tighter spread for a more focused plume
  smoke.emitterConfig.emissionRate =
      75.0f; // Halved for performance
  smoke.emitterConfig.minSpeed = 15.0f;   // Faster initial speed
  smoke.emitterConfig.maxSpeed = 60.0f;  // Faster max speed
  smoke.emitterConfig.minLife =
      2.0f; // Shorter life for a more energetic effect
  smoke.emitterConfig.maxLife = 6.0f; 
  smoke.emitterConfig.minSize = 5.0f;  // Smaller particles
  smoke.emitterConfig.maxSize = 20.0f; 
  smoke.emitterConfig.minColor = 0x333333DD; // Dark, dense smoke core
  smoke.emitterConfig.maxColor = 0x80808044; // Light grey, very transparent
  smoke.emitterConfig.gravity = Vector2D(0, -30.0f); // Faster rise
  smoke.emitterConfig.windForce =
      Vector2D(30.0f, 0); // Moderate wind influence
  smoke.emitterConfig.textureID = "smoke_particle";
  smoke.emitterConfig.blendMode =
      ParticleBlendMode::Alpha;         // Standard alpha blending
  smoke.emitterConfig.duration = -1.0f; // Infinite by default
  // Burst configuration for more natural smoke puffs
  smoke.emitterConfig.burstCount = 5;       // Fewer particles per burst
  smoke.emitterConfig.burstInterval = 0.25f; // Less frequent bursts
  smoke.intensityMultiplier = 1.2f;         // Reduced intensity
  return smoke;
}

ParticleEffectDefinition ParticleManager::createSparksEffect() {
  ParticleEffectDefinition sparks("Sparks", ParticleEffectType::Sparks);
  sparks.layer = UnifiedParticle::RenderLayer::World;
  sparks.emitterConfig.position = Vector2D(0, 0);   // Will be set when played
  sparks.emitterConfig.direction = Vector2D(0, -1); // Initial upward burst
  sparks.emitterConfig.spread =
      180.0f; // Wide spread for explosive spark pattern
  sparks.emitterConfig.emissionRate =
      150.0f; // Reduced from 300 for better performance
              // while maintaining explosive sparks
  sparks.emitterConfig.minSpeed = 80.0f; // Fast initial velocity
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
  sparks.emitterConfig.burstCount = 38;        // Burst of sparks
  sparks.emitterConfig.enableCollision = true; // Sparks bounce off surfaces
  sparks.emitterConfig.bounceDamping = 0.6f;   // Medium bounce damping
  sparks.intensityMultiplier = 1.0f;
  return sparks;
}

size_t ParticleManager::getActiveParticleCount() const {
  if (!m_initialized.load(std::memory_order_acquire)) {
    return 0;
  }

  // THREAD SAFETY: Get immutable snapshot of particle data
  const auto &particles = m_storage.getParticlesForRead();
  
  // CRITICAL FIX: Cache flag vector size to avoid race conditions
  const size_t flagCount = particles.flags.size();
  
  size_t activeCount = 0;
  for (size_t i = 0; i < flagCount; ++i) {
    // BOUNDS CHECK: Additional safety for Windows gcc strictness
    if (i >= particles.flags.size()) {
      break; // Exit safely if buffer changed during iteration
    }
    
    if (particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) {
      activeCount++;
    }
  }
  return activeCount;
}

void ParticleManager::compactParticleStorage() {
  if (!m_initialized.load(std::memory_order_acquire)) {
    return;
  }

  // Increment epoch to signal start of compaction
  uint64_t currentEpoch = m_storage.currentEpoch.fetch_add(1, std::memory_order_acq_rel);
  
  size_t activeIdx = m_storage.activeBuffer.load(std::memory_order_acquire);
  size_t inactiveIdx = 1 - activeIdx;
  
  // Read from active buffer, write to inactive buffer
  const auto &sourceParticles = m_storage.particles[activeIdx];
  auto &targetParticles = m_storage.particles[inactiveIdx];

  // Clear target buffer and prepare for compaction
  targetParticles.clear();
  
  // CRITICAL FIX: Cache source buffer size to avoid race conditions
  const size_t sourceSize = sourceParticles.positions.size();
  
  // BOUNDS SAFETY: Validate source buffer consistency
  if (sourceSize == 0 ||
      sourceParticles.flags.size() != sourceSize ||
      sourceParticles.velocities.size() != sourceSize ||
      sourceParticles.lives.size() != sourceSize) {
    // Source buffer is inconsistent, abort compaction
    m_storage.safeEpoch.store(currentEpoch, std::memory_order_release);
    return;
  }
  
  size_t writeIndex = 0;
  for (size_t readIndex = 0; readIndex < sourceSize; ++readIndex) {
    // BOUNDS CHECK: Ensure we don't access invalid indices
    if (readIndex >= sourceParticles.flags.size()) {
      break; // Exit safely if buffer changed during iteration
    }
    
    if (sourceParticles.flags[readIndex] & UnifiedParticle::FLAG_ACTIVE) {
      // BOUNDS CHECK: Verify all array accesses are safe
      if (readIndex >= sourceParticles.positions.size() ||
          readIndex >= sourceParticles.velocities.size() ||
          readIndex >= sourceParticles.accelerations.size() ||
          readIndex >= sourceParticles.lives.size() ||
          readIndex >= sourceParticles.maxLives.size() ||
          readIndex >= sourceParticles.sizes.size()) {
        break; // Exit safely if any buffer is inconsistent
      }
      
      // Expand target buffer if needed
      if (writeIndex >= targetParticles.positions.size()) {
        targetParticles.positions.push_back(sourceParticles.positions[readIndex]);
        targetParticles.velocities.push_back(sourceParticles.velocities[readIndex]);
        targetParticles.accelerations.push_back(sourceParticles.accelerations[readIndex]);
        targetParticles.lives.push_back(sourceParticles.lives[readIndex]);
        targetParticles.maxLives.push_back(sourceParticles.maxLives[readIndex]);
        targetParticles.sizes.push_back(sourceParticles.sizes[readIndex]);
        targetParticles.rotations.push_back(sourceParticles.rotations[readIndex]);
        targetParticles.angularVelocities.push_back(sourceParticles.angularVelocities[readIndex]);
        targetParticles.colors.push_back(sourceParticles.colors[readIndex]);
        targetParticles.textureIndices.push_back(sourceParticles.textureIndices[readIndex]);
        targetParticles.flags.push_back(sourceParticles.flags[readIndex]);
        targetParticles.generationIds.push_back(sourceParticles.generationIds[readIndex]);
        targetParticles.effectTypes.push_back(sourceParticles.effectTypes[readIndex]);
        targetParticles.layers.push_back(sourceParticles.layers[readIndex]);
      } else {
        // Overwrite existing entries
        targetParticles.positions[writeIndex] = sourceParticles.positions[readIndex];
        targetParticles.velocities[writeIndex] = sourceParticles.velocities[readIndex];
        targetParticles.accelerations[writeIndex] = sourceParticles.accelerations[readIndex];
        targetParticles.lives[writeIndex] = sourceParticles.lives[readIndex];
        targetParticles.maxLives[writeIndex] = sourceParticles.maxLives[readIndex];
        targetParticles.sizes[writeIndex] = sourceParticles.sizes[readIndex];
        targetParticles.rotations[writeIndex] = sourceParticles.rotations[readIndex];
        targetParticles.angularVelocities[writeIndex] = sourceParticles.angularVelocities[readIndex];
        targetParticles.colors[writeIndex] = sourceParticles.colors[readIndex];
        targetParticles.textureIndices[writeIndex] = sourceParticles.textureIndices[readIndex];
        targetParticles.flags[writeIndex] = sourceParticles.flags[readIndex];
        targetParticles.generationIds[writeIndex] = sourceParticles.generationIds[readIndex];
        targetParticles.effectTypes[writeIndex] = sourceParticles.effectTypes[readIndex];
        targetParticles.layers[writeIndex] = sourceParticles.layers[readIndex];
      }
      writeIndex++;
    }
  }

  size_t originalCount = sourceSize;
  size_t compactedCount = writeIndex;
  
  if (compactedCount < originalCount) {
    // Atomically switch to the compacted buffer
    m_storage.activeBuffer.store(inactiveIdx, std::memory_order_release);
    m_storage.particleCount.store(compactedCount, std::memory_order_release);
    
    // Update safe epoch - only modify old buffer after this point
    // Use seq_cst only for the critical buffer safety signal
    m_storage.safeEpoch.store(currentEpoch, std::memory_order_seq_cst);
    
    size_t removedCount = originalCount - compactedCount;
    PARTICLE_DEBUG("Compacted storage: removed " +
                   std::to_string(removedCount) + " inactive/faded particles");
  } else {
    // No compaction needed, update safe epoch with release ordering
    m_storage.safeEpoch.store(currentEpoch, std::memory_order_release);
    PARTICLE_DEBUG("No compaction needed - all particles are active");
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
    auto defIt = m_effectDefinitions.find(instance.effectType);
    if (defIt != m_effectDefinitions.end()) {
      const auto &config = defIt->second.emitterConfig;

      if (config.emissionRate > 0.0f) {
        float emissionInterval = 1.0f / config.emissionRate;
        while (instance.emissionTimer >= emissionInterval) {
          // Create particle via lock-free system
          createParticleForEffect(defIt->second, instance.position,
                                  instance.isWeatherEffect);
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

  // CRITICAL FIX: Validate buffer consistency before threading
  const size_t bufferSize = currentBuffer.positions.size();
  
  // BOUNDS SAFETY: Ensure all vectors have consistent sizes
  if (bufferSize == 0 ||
      currentBuffer.velocities.size() != bufferSize ||
      currentBuffer.flags.size() != bufferSize ||
      currentBuffer.lives.size() != bufferSize) {
    // Buffer is inconsistent, fall back to single-threaded update
    updateParticlesSingleThreaded(deltaTime, activeParticleCount);
    return;
  }

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
    
    // CRITICAL FIX: Ensure endIdx doesn't exceed buffer size
    endIdx = std::min(endIdx, bufferSize);
    
    // Skip empty batches or invalid ranges
    if (startIdx >= bufferSize || startIdx >= endIdx) {
      continue;
    }
    
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
    LockFreeParticleStorage::ParticleSoA &particles, size_t startIdx, size_t endIdx,
    float deltaTime) {
  static float windPhase = 0.0f; // Static wind phase for natural variation
  windPhase += deltaTime * 0.5f; // Slow wind variation

  // PRODUCTION OPTIMIZATION: Pre-compute expensive operations
  const float windPhase0_8 = windPhase * 0.8f;
  const float windPhase1_2 = windPhase * 1.2f;
  const float windPhase3_0 = windPhase * 3.0f;
  const float windPhase8_0 = windPhase * 8.0f;
  const float windPhase6_0 = windPhase * 6.0f;

  // CRITICAL FIX: Cache all buffer sizes and validate consistency before processing
  const size_t positionsSize = particles.positions.size();
  const size_t velocitiesSize = particles.velocities.size();
  const size_t accelerationsSize = particles.accelerations.size();
  const size_t livesSize = particles.lives.size();
  const size_t maxLivesSize = particles.maxLives.size();
  const size_t sizesSize = particles.sizes.size();
  const size_t flagsSize = particles.flags.size();
  const size_t colorsSize = particles.colors.size();
  const size_t effectTypesSize = particles.effectTypes.size();
  
  // BOUNDS SAFETY: Find minimum consistent size across all vectors
  const size_t safeSize = std::min({positionsSize, velocitiesSize, accelerationsSize,
                                   livesSize, maxLivesSize, sizesSize, flagsSize,
                                   colorsSize, effectTypesSize});
  
  // Ensure we don't exceed the actual safe particle buffer size
  endIdx = std::min(endIdx, safeSize);
  startIdx = std::min(startIdx, safeSize);
  
  if (startIdx >= endIdx || safeSize == 0) {
    return; // Nothing to process
  }
  
  for (size_t i = startIdx; i < endIdx; ++i) {
    // BOUNDS CHECK: Double-check index is still valid (Windows gcc strictness)
    if (i >= flagsSize || !(particles.flags[i] & UnifiedParticle::FLAG_ACTIVE)) {
      continue;
    }
    
    // BOUNDS CHECK: Verify all array accesses are safe before proceeding
    if (i >= positionsSize || i >= velocitiesSize || i >= accelerationsSize ||
        i >= livesSize || i >= maxLivesSize || i >= sizesSize ||
        i >= colorsSize || i >= effectTypesSize) {
      break; // Exit safely if any buffer is inconsistent
    }
    
    // PRODUCTION OPTIMIZATION: Pre-compute per-particle values
    const float particleOffset = i * 0.1f;
    
    const float particleOffset15 = i * 0.15f;
    const float particleOffset2 = i * 0.2f;
    const float particleOffset25 = i * 0.25f;
    const float particleOffset3 = i * 0.3f;
    
    

    // Enhanced physics with natural atmospheric effects
    float windVariation = std::sin(windPhase + particleOffset) *
                          0.3f;    // Per-particle wind variation
    float atmosphericDrag = 0.98f; // Slight air resistance

    // PRODUCTION OPTIMIZATION: Extract color components once and cache
    // comparison results
    const uint32_t color = particles.colors[i];

    if (particles.effectTypes[i] == ParticleEffectType::Cloudy) {
      // Apply horizontal movement for cloud drift
      particles.accelerations[i].setX(15.0f);
      particles.accelerations[i].setY(0.0f);

      // PRODUCTION OPTIMIZATION: Pre-computed trigonometric values
      const float drift = std::sin(windPhase0_8 + particleOffset15) * 3.0f;
      const float verticalFloat =
          std::cos(windPhase1_2 + particleOffset) * 1.5f;

      particles.velocities[i].setX(particles.velocities[i].getX() + drift * deltaTime);
      particles.velocities[i].setY(particles.velocities[i].getY() +
                             verticalFloat * deltaTime);

      atmosphericDrag = 1.0f;
    }
    // Apply wind variation for weather particles
    else if (particles.flags[i] & UnifiedParticle::FLAG_WEATHER) {
      // Add natural wind turbulence
      particles.accelerations[i].setX(particles.accelerations[i].getX() +
                                 windVariation * 20.0f);

      // Different atmospheric effects for different particle types
      // Safe division with zero check
      const float lifeRatio = (particles.maxLives[i] > 0.0f) ? 
                             (particles.lives[i] / particles.maxLives[i]) : 0.0f;

      // Snow particles drift more with wind and have flutter
      if (particles.effectTypes[i] == ParticleEffectType::Snow ||
          particles.effectTypes[i] == ParticleEffectType::HeavySnow) {
        const float flutter = std::sin(windPhase3_0 + particleOffset2) * 8.0f;
        particles.velocities[i].setX(particles.velocities[i].getX() + flutter * deltaTime);
        atmosphericDrag = 0.96f; // More air resistance for snow
      }

      // Rain particles are more affected by gravity as they age
      else if (particles.effectTypes[i] == ParticleEffectType::Rain ||
               particles.effectTypes[i] == ParticleEffectType::HeavyRain) {
        particles.accelerations[i].setY(particles.accelerations[i].getY() +
                                   (1.0f - lifeRatio) *
                                       50.0f); // Accelerate with age
        atmosphericDrag = 0.99f;               // Less air resistance for rain
      }

      // Fog/cloud particles drift and have gentle movement
      else { // Regular fog behavior (not clouds)
        const float drift = std::sin(windPhase0_8 + particleOffset15) * 15.0f;
        const float verticalDrift =
            std::cos(windPhase1_2 + particleOffset) * 3.0f * deltaTime;
        particles.velocities[i].setX(particles.velocities[i].getX() + drift * deltaTime);
        particles.velocities[i].setY(particles.velocities[i].getY() + verticalDrift);
        atmosphericDrag = 0.999f;
      }
    }
    // Special handling for fire and smoke particles for natural movement
    else {
      // Safe division with zero check
      const float lifeRatio = (particles.maxLives[i] > 0.0f) ? 
                             (particles.lives[i] / particles.maxLives[i]) : 0.0f;
      float randomFactor = static_cast<float>(fast_rand()) / 32767.0f;

      // Fire particles: flickering, turbulent movement with heat distortion
      if (particles.effectTypes[i] == ParticleEffectType::Fire) {

        // More random turbulence for fire
        const float heatTurbulence =
            std::sin(windPhase8_0 + particleOffset3) * 15.0f +
            (randomFactor - 0.5f) * 10.f;
        const float heatRise =
            std::cos(windPhase6_0 + particleOffset25) * 10.0f;

        particles.velocities[i].setX(particles.velocities[i].getX() +
                               heatTurbulence * deltaTime);
        particles.velocities[i].setY(particles.velocities[i].getY() + heatRise * deltaTime);

        // Fire gets more chaotic as it ages (burns out)
        const float chaos = (1.0f - lifeRatio) * 25.0f;
        const float chaosValue =
            (randomFactor - 0.5f) * chaos * deltaTime;
        particles.accelerations[i].setX(particles.accelerations[i].getX() + chaosValue);

        // Visuals: Change color and size over life
        particles.colors[i] = interpolateColor(0xFFD700FF, 0xFF450088, 1.0f - lifeRatio);
        particles.sizes[i] *= (lifeRatio * 0.99f);


        atmosphericDrag = 0.94f; // High drag for fire flicker
      }

      // Smoke particles: billowing, wind-affected movement
      else if (particles.effectTypes[i] == ParticleEffectType::Smoke) {

        // Circular billowing motion
        float angle = (i % 360) * 3.14159f / 180.0f; // Unique angle per particle
        float speed = 15.0f + (randomFactor * 10.0f);
        float circleX = std::cos(angle + windPhase) * speed * (1.0f - lifeRatio);
        float circleY = std::sin(angle + windPhase) * speed * (1.0f - lifeRatio);

        particles.velocities[i].setX(particles.velocities[i].getX() + circleX * deltaTime);
        particles.velocities[i].setY(particles.velocities[i].getY() + circleY * deltaTime - (20.0f * deltaTime)); // Upward motion

        // Visuals: Shrink slightly over life
        particles.sizes[i] *= 0.998f;


        atmosphericDrag = 0.96f; 
      }

      // Other particles (sparks, magic, etc.) - use standard turbulence
      else {
        const float generalTurbulence = windVariation * 10.0f;
        particles.velocities[i].setX(particles.velocities[i].getX() +
                               generalTurbulence * deltaTime);
        atmosphericDrag = 0.97f;
      }
    }

    // Apply atmospheric drag
    particles.velocities[i].setX(particles.velocities[i].getX() * atmosphericDrag);
    particles.velocities[i].setY(particles.velocities[i].getY() * atmosphericDrag);

    // Update physics
    particles.velocities[i].setX(particles.velocities[i].getX() +
                           particles.accelerations[i].getX() * deltaTime);
    particles.velocities[i].setY(particles.velocities[i].getY() +
                           particles.accelerations[i].getY() * deltaTime);

    particles.positions[i].setX(particles.positions[i].getX() +
                           particles.velocities[i].getX() * deltaTime);
    particles.positions[i].setY(particles.positions[i].getY() +
                           particles.velocities[i].getY() * deltaTime);

    // Update life
    particles.lives[i] -= deltaTime;
    if (particles.lives[i] <= 0.0f) {
      particles.flags[i] &= ~UnifiedParticle::FLAG_ACTIVE;
      continue;
    }

    // Enhanced visual properties with natural fading
    // Safe division with zero check
    const float lifeRatio = (particles.maxLives[i] > 0.0f) ? 
                           (particles.lives[i] / particles.maxLives[i]) : 0.0f;

    // Natural fade-in and fade-out for weather particles
    float alphaMultiplier = 1.0f;
    if (particles.flags[i] & UnifiedParticle::FLAG_WEATHER) {
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

    const uint8_t alpha = static_cast<uint8_t>(255 * alphaMultiplier);
    particles.colors[i] = (color & 0xFFFFFF00) | alpha;

    // Note: Size variation for natural appearance would be applied during
    // rendering
  }
}

void ParticleManager::createParticleForEffect(
    const ParticleEffectDefinition &effectDef, const Vector2D &position,
    bool isWeatherEffect) {
  // Create a new particle request for the lock-free system
  const auto &config = effectDef.emitterConfig;
  NewParticleRequest request;

  if (!config.useWorldSpace) {
    // Screen-space effect (like weather)
    const auto &gameEngine = GameEngine::Instance();
    float spawnX =
        static_cast<float>(fast_rand() % gameEngine.getLogicalWidth());
    float spawnY = config.position.getY();
    if (effectDef.type == ParticleEffectType::Fog ||
        effectDef.type == ParticleEffectType::Cloudy ||
        effectDef.type == ParticleEffectType::Rain ||
        effectDef.type == ParticleEffectType::HeavyRain ||
        effectDef.type == ParticleEffectType::Snow ||
        effectDef.type == ParticleEffectType::HeavySnow) {
      spawnY = static_cast<float>(fast_rand() % gameEngine.getLogicalHeight());
    }
    request.position = Vector2D(spawnX, spawnY);
  } else {
    // World-space effect (like an explosion at a point)
    request.position = position;
    if (effectDef.type == ParticleEffectType::Smoke) {
        float offsetX = (static_cast<float>(fast_rand()) / 32767.0f - 0.5f) * 20.0f; // Random offset in a 20px range
        float offsetY = (static_cast<float>(fast_rand()) / 32767.0f - 0.5f) * 10.0f; // Smaller vertical offset
        request.position.setX(request.position.getX() + offsetX);
        request.position.setY(request.position.getY() + offsetY);
    }
  }

  // Simplified physics and color calculation
  float naturalRand = static_cast<float>(fast_rand()) / 32767.0f;
  float speed =
      config.minSpeed + (config.maxSpeed - config.minSpeed) * naturalRand;
  float angleRange = config.spread * 0.017453f; // Convert degrees to radians
  float angle = (naturalRand * 2.0f - 1.0f) * angleRange;

  request.velocity = Vector2D(speed * sin(angle), speed * cos(angle));
  request.acceleration = config.gravity;
  request.life = config.minLife + (config.maxLife - config.minLife) *
                                      static_cast<float>(fast_rand()) / 32767.0f;
  request.size = config.minSize + (config.maxSize - config.minSize) *
                                      static_cast<float>(fast_rand()) / 32767.0f;
  request.color =
      interpolateColor(config.minColor, config.maxColor, naturalRand);
  request.textureIndex = getTextureIndex(config.textureID);
  request.blendMode = config.blendMode;
  request.effectType = effectDef.type;

  // Set weather flag
  if (isWeatherEffect) {
    request.flags = UnifiedParticle::FLAG_WEATHER;
  } else {
    request.flags = 0;
  }

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

// Helper methods for enum-based classification system
ParticleEffectType
ParticleManager::weatherStringToEnum(const std::string &weatherType,
                                     float intensity) const {
  if (weatherType == "Rainy") {
    return (intensity > 0.7f) ? ParticleEffectType::HeavyRain
                              : ParticleEffectType::Rain;
  } else if (weatherType == "Snowy") {
    return (intensity > 0.7f) ? ParticleEffectType::HeavySnow
                              : ParticleEffectType::Snow;
  } else if (weatherType == "Foggy") {
    return ParticleEffectType::Fog;
  } else if (weatherType == "Cloudy") {
    return ParticleEffectType::Cloudy;
  } else if (weatherType == "Stormy") {
    return ParticleEffectType::HeavyRain; // Stormy always uses heavy rain
  } else if (weatherType == "HeavyRain") {
    return ParticleEffectType::HeavyRain;
  } else if (weatherType == "HeavySnow") {
    return ParticleEffectType::HeavySnow;
  }

  // Default/unknown weather type
  return ParticleEffectType::Custom;
}

std::string ParticleManager::effectTypeToString(ParticleEffectType type) const {
  switch (type) {
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
  default:
    return "Custom";
  }
}

uint32_t ParticleManager::interpolateColor(uint32_t color1, uint32_t color2,
                                           float factor) {
  uint8_t r1 = (color1 >> 24) & 0xFF;
  uint8_t g1 = (color1 >> 16) & 0xFF;
  uint8_t b1 = (color1 >> 8) & 0xFF;
  uint8_t a1 = color1 & 0xFF;

  uint8_t r2 = (color2 >> 24) & 0xFF;
  uint8_t g2 = (color2 >> 16) & 0xFF;
  uint8_t b2 = (color2 >> 8) & 0xFF;
  uint8_t a2 = color2 & 0xFF;

  uint8_t r = static_cast<uint8_t>(r1 + (r2 - r1) * factor);
  uint8_t g = static_cast<uint8_t>(g1 + (g2 - g1) * factor);
  uint8_t b = static_cast<uint8_t>(b1 + (b2 - b1) * factor);
  uint8_t a = static_cast<uint8_t>(a1 + (a2 - a1) * factor);

  return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}
