/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/ParticleManager.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "managers/EventManager.hpp"
#include "events/ParticleEffectEvent.hpp"
#include "events/WeatherEvent.hpp"
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
// Remove static mutex - GameEngine handles synchronization

// ParticleData method implementations
bool ParticleData::isActive() const {
    return flags & FLAG_ACTIVE;
}

void ParticleData::setActive(bool active) {
    if (active)
        flags |= FLAG_ACTIVE;
    else
        flags &= ~FLAG_ACTIVE;
}

bool ParticleData::isVisible() const {
    return flags & FLAG_VISIBLE;
}

void ParticleData::setVisible(bool visible) {
    if (visible)
        flags |= FLAG_VISIBLE;
    else
        flags &= ~FLAG_VISIBLE;
}

bool ParticleData::isWeatherParticle() const {
    return flags & FLAG_WEATHER;
}

void ParticleData::setWeatherParticle(bool weather) {
    if (weather)
        flags |= FLAG_WEATHER;
    else
        flags &= ~FLAG_WEATHER;
}

bool ParticleData::isFadingOut() const {
    return flags & FLAG_FADE_OUT;
}

void ParticleData::setFadingOut(bool fading) {
    if (fading)
        flags |= FLAG_FADE_OUT;
    else
        flags &= ~FLAG_FADE_OUT;
}

float ParticleData::getLifeRatio() const {
    return maxLife > 0 ? life / maxLife : 0.0f;
}

// UnifiedParticle method implementations
bool UnifiedParticle::isActive() const {
    return flags & FLAG_ACTIVE;
}

void UnifiedParticle::setActive(bool active) {
    if (active)
        flags |= FLAG_ACTIVE;
    else
        flags &= ~FLAG_ACTIVE;
}

bool UnifiedParticle::isVisible() const {
    return flags & FLAG_VISIBLE;
}

void UnifiedParticle::setVisible(bool visible) {
    if (visible)
        flags |= FLAG_VISIBLE;
    else
        flags &= ~FLAG_VISIBLE;
}

bool UnifiedParticle::isWeatherParticle() const {
    return flags & FLAG_WEATHER;
}

void UnifiedParticle::setWeatherParticle(bool weather) {
    if (weather)
        flags |= FLAG_WEATHER;
    else
        flags &= ~FLAG_WEATHER;
}

bool UnifiedParticle::isFadingOut() const {
    return flags & FLAG_FADE_OUT;
}

void UnifiedParticle::setFadingOut(bool fading) {
    if (fading)
        flags |= FLAG_FADE_OUT;
    else
        flags &= ~FLAG_FADE_OUT;
}

float UnifiedParticle::getLifeRatio() const {
    return maxLife > 0 ? life / maxLife : 0.0f;
}

// ParticlePerformanceStats method implementations
void ParticlePerformanceStats::addUpdateSample(double timeMs, size_t particleCount) {
    totalUpdateTime += timeMs;
    updateCount++;
    activeParticles = particleCount;
    
    // Use simple instantaneous rate calculation instead of cumulative average
    // This prevents the ever-increasing rate issue seen in other managers
    if (timeMs > 0) {
        particlesPerSecond = (particleCount * 1000.0) / timeMs; // particles processed per second in this frame
    }
}

void ParticlePerformanceStats::addRenderSample(double timeMs) {
    totalRenderTime += timeMs;
    renderCount++;
}

void ParticlePerformanceStats::reset() {
    totalUpdateTime = 0.0;
    totalRenderTime = 0.0;
    updateCount = 0;
    renderCount = 0;
    activeParticles = 0;
    particlesPerSecond = 0.0;
}

// ParticleManager method implementations
bool ParticleManager::isInitialized() const {
    return m_initialized.load(std::memory_order_acquire);
}

bool ParticleManager::isShutdown() const {
    return m_isShutdown;
}

// ParticleSoA method implementations
void ParticleManager::LockFreeParticleStorage::ParticleSoA::resize(size_t newSize) {
    // Resize SIMD-friendly SoA float lanes
    posX.resize(newSize);
    posY.resize(newSize);
    velX.resize(newSize);
    velY.resize(newSize);
    accX.resize(newSize);
    accY.resize(newSize);
    lives.resize(newSize);
    maxLives.resize(newSize);
    sizes.resize(newSize);
    rotations.resize(newSize);
    angularVelocities.resize(newSize);
    colors.resize(newSize);
    flags.resize(newSize);
    generationIds.resize(newSize);
    effectTypes.resize(newSize);
    layers.resize(newSize);
}

void ParticleManager::LockFreeParticleStorage::ParticleSoA::reserve(size_t newCapacity) {
    // Reserve SIMD-friendly SoA float lanes
    posX.reserve(newCapacity);
    posY.reserve(newCapacity);
    velX.reserve(newCapacity);
    velY.reserve(newCapacity);
    accX.reserve(newCapacity);
    accY.reserve(newCapacity);
    lives.reserve(newCapacity);
    maxLives.reserve(newCapacity);
    sizes.reserve(newCapacity);
    rotations.reserve(newCapacity);
    angularVelocities.reserve(newCapacity);
    colors.reserve(newCapacity);
    flags.reserve(newCapacity);
    generationIds.reserve(newCapacity);
    effectTypes.reserve(newCapacity);
    layers.reserve(newCapacity);
}

void ParticleManager::LockFreeParticleStorage::ParticleSoA::push_back(const UnifiedParticle& p) {
    // Add to SIMD arrays (authoritative storage)
    posX.push_back(p.position.getX());
    posY.push_back(p.position.getY());
    velX.push_back(p.velocity.getX());
    velY.push_back(p.velocity.getY());
    accX.push_back(p.acceleration.getX());
    accY.push_back(p.acceleration.getY());
    lives.push_back(p.life);
    maxLives.push_back(p.maxLife);
    sizes.push_back(p.size);
    rotations.push_back(p.rotation);
    angularVelocities.push_back(p.angularVelocity);
    colors.push_back(p.color);
    flags.push_back(p.flags);
    generationIds.push_back(p.generationId);
    effectTypes.push_back(p.effectType);
    layers.push_back(p.layer);
}

void ParticleManager::LockFreeParticleStorage::ParticleSoA::clear() {
    // Clear all arrays
    posX.clear();
    posY.clear();
    velX.clear();
    velY.clear();
    accX.clear();
    accY.clear();
    lives.clear();
    maxLives.clear();
    sizes.clear();
    rotations.clear();
    angularVelocities.clear();
    colors.clear();
    flags.clear();
    generationIds.clear();
    effectTypes.clear();
    layers.clear();
}

size_t ParticleManager::LockFreeParticleStorage::ParticleSoA::size() const {
    // Authoritative size is flags.size(); ensure others are in sync
    const size_t baseSize = flags.size();
    if (baseSize == 0) return 0;
    if (posX.size() != baseSize || posY.size() != baseSize ||
        velX.size() != baseSize || velY.size() != baseSize ||
        accX.size() != baseSize || accY.size() != baseSize ||
        lives.size() != baseSize || maxLives.size() != baseSize ||
        sizes.size() != baseSize || rotations.size() != baseSize ||
        angularVelocities.size() != baseSize || colors.size() != baseSize ||
        generationIds.size() != baseSize ||
        effectTypes.size() != baseSize || layers.size() != baseSize) {
        return 0; // Return 0 if ANY array is inconsistent
    }
    
    return baseSize;
}

bool ParticleManager::LockFreeParticleStorage::ParticleSoA::empty() const {
    return flags.empty();
}

// NEW: Complete SOA validation for cross-platform safety
bool ParticleManager::LockFreeParticleStorage::ParticleSoA::isFullyConsistent() const {
    const size_t baseSize = flags.size();
    return (posX.size() == baseSize && posY.size() == baseSize &&
            velX.size() == baseSize && velY.size() == baseSize &&
            accX.size() == baseSize && accY.size() == baseSize &&
            lives.size() == baseSize && maxLives.size() == baseSize &&
            sizes.size() == baseSize && rotations.size() == baseSize &&
            angularVelocities.size() == baseSize && colors.size() == baseSize &&
            generationIds.size() == baseSize &&
            effectTypes.size() == baseSize && layers.size() == baseSize);
}

// NEW: Safe access count that works across all platforms
size_t ParticleManager::LockFreeParticleStorage::ParticleSoA::getSafeAccessCount() const {
    // Return minimum size of ALL arrays to prevent any out-of-bounds access
    return std::min({
        flags.size(), posX.size(), posY.size(), velX.size(), velY.size(), accX.size(), accY.size(),
        lives.size(), maxLives.size(), sizes.size(), rotations.size(),
        angularVelocities.size(), colors.size(), generationIds.size(), effectTypes.size(), layers.size()
    });
}

// NEW: Bounds checking for iterator safety
bool ParticleManager::LockFreeParticleStorage::ParticleSoA::isValidIndex(size_t index) const {
    return index < getSafeAccessCount();
}

// NEW: Safe erase single particle (swap-and-pop pattern)
void ParticleManager::LockFreeParticleStorage::ParticleSoA::eraseParticle(size_t index) {
    if (!isValidIndex(index)) return;
    
    const size_t lastIndex = flags.size() - 1;
    if (index != lastIndex) {
        // Swap with last element (all arrays must stay synchronized)
        swapParticles(index, lastIndex);
    }
    
    // Remove last element from ALL arrays atomically
    posX.pop_back();
    posY.pop_back();
    velX.pop_back();
    velY.pop_back();
    accX.pop_back();
    accY.pop_back();
    lives.pop_back();
    maxLives.pop_back();
    sizes.pop_back();
    rotations.pop_back();
    angularVelocities.pop_back();
    colors.pop_back();
    flags.pop_back();
    generationIds.pop_back();
    effectTypes.pop_back();
    layers.pop_back();
}

// NEW: Safe swap operation for all SOA arrays
void ParticleManager::LockFreeParticleStorage::ParticleSoA::swapParticles(size_t indexA, size_t indexB) {
    if (!isValidIndex(indexA) || !isValidIndex(indexB) || indexA == indexB) return;
    
    // Swap all particle data atomically
    std::swap(posX[indexA], posX[indexB]);
    std::swap(posY[indexA], posY[indexB]);
    std::swap(velX[indexA], velX[indexB]);
    std::swap(velY[indexA], velY[indexB]);
    std::swap(accX[indexA], accX[indexB]);
    std::swap(accY[indexA], accY[indexB]);
    std::swap(lives[indexA], lives[indexB]);
    std::swap(maxLives[indexA], maxLives[indexB]);
    std::swap(sizes[indexA], sizes[indexB]);
    std::swap(rotations[indexA], rotations[indexB]);
    std::swap(angularVelocities[indexA], angularVelocities[indexB]);
    std::swap(colors[indexA], colors[indexB]);
    std::swap(flags[indexA], flags[indexB]);
    std::swap(generationIds[indexA], generationIds[indexB]);
    std::swap(effectTypes[indexA], effectTypes[indexB]);
    std::swap(layers[indexA], layers[indexB]);
}


// LockFreeParticleStorage constructor implementation
ParticleManager::LockFreeParticleStorage::LockFreeParticleStorage() : creationRing{} {
    // Pre-allocate both buffers
    particles[0].reserve(DEFAULT_MAX_PARTICLES);
    particles[1].reserve(DEFAULT_MAX_PARTICLES);
    capacity.store(DEFAULT_MAX_PARTICLES, std::memory_order_relaxed);
}

// Lock-free particle creation implementation
bool ParticleManager::LockFreeParticleStorage::tryCreateParticle(
    const Vector2D &pos, const Vector2D &vel, const Vector2D &acc,
    uint32_t color, float life, float size, /*uint16_t texIndex,*/ uint8_t flags,
    uint8_t genId, ParticleEffectType effectType) {
    size_t head = creationHead.load(std::memory_order_acquire);
    size_t next = (head + 1) & (CREATION_RING_SIZE - 1);

    if (next == creationTail.load(std::memory_order_acquire)) {
        return false; // Ring buffer full
    }

    auto &req = creationRing[head];
    req.position = pos;
    req.velocity = vel;
    req.acceleration = acc;
    req.color = color;
    req.life = life;
    req.size = size;
    req.flags = flags;
    req.generationId = genId;
    req.effectType = effectType;
    req.ready.store(true, std::memory_order_release);

    creationHead.store(next, std::memory_order_release);
    return true;
}

// Process creation requests implementation
void ParticleManager::LockFreeParticleStorage::processCreationRequests() {
    size_t tail = creationTail.load(std::memory_order_acquire);
    size_t head = creationHead.load(std::memory_order_acquire);

    while (tail != head) {
        auto &req = creationRing[tail];
        if (req.ready.load(std::memory_order_acquire)) {
            // Add particle to active buffer
            size_t activeIdx = activeBuffer.load(std::memory_order_relaxed);
            auto &activeParticles = particles[activeIdx];

            const size_t currentCapacity = capacity.load(std::memory_order_relaxed);
            const size_t currentSize = activeParticles.flags.size();
            
            // CRITICAL FIX: Check buffer consistency before adding particles
            if (currentSize < currentCapacity &&
                activeParticles.posX.size() == currentSize &&
                activeParticles.flags.size() == currentSize) {
                UnifiedParticle particle;
                particle.position = req.position;
                particle.velocity = req.velocity;
                particle.acceleration = req.acceleration;
                particle.color = req.color;
                particle.life = req.life;
                particle.maxLife = req.life;
                particle.size = req.size;
                particle.flags = static_cast<uint8_t>(req.flags | UnifiedParticle::FLAG_ACTIVE | UnifiedParticle::FLAG_VISIBLE);
                particle.generationId = req.generationId;
                particle.effectType = req.effectType;

                // Prefer slot reuse: use a free index if available
                if (!freeIndices.empty()) {
                    const size_t idx = freeIndices.back();
                    freeIndices.pop_back();
                    if (idx < activeParticles.flags.size()) {
                        // Write into SIMD lanes and attribute arrays
                        activeParticles.posX[idx] = particle.position.getX();
                        activeParticles.posY[idx] = particle.position.getY();
                        activeParticles.velX[idx] = particle.velocity.getX();
                        activeParticles.velY[idx] = particle.velocity.getY();
                        activeParticles.accX[idx] = particle.acceleration.getX();
                        activeParticles.accY[idx] = particle.acceleration.getY();
                        activeParticles.lives[idx] = particle.life;
                        activeParticles.maxLives[idx] = particle.maxLife;
                        activeParticles.sizes[idx] = particle.size;
                        activeParticles.rotations[idx] = 0.0f;
                        activeParticles.angularVelocities[idx] = 0.0f;
                        activeParticles.colors[idx] = particle.color;
                        activeParticles.flags[idx] = particle.flags;
                        activeParticles.generationIds[idx] = particle.generationId;
                        activeParticles.effectTypes[idx] = particle.effectType;
                        activeParticles.layers[idx] = UnifiedParticle::RenderLayer::World;
                        // Track upper bound of active indices
                        if (idx > maxActiveIndex) maxActiveIndex = idx;
                        // Track active count
                        // Index reused from pool implies previously inactive
                        // Increment active count for newly activated slot
                        ParticleManager::Instance().m_activeCount.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        // Fallback: append if stale free index
                        activeParticles.push_back(particle);
                        size_t newIdx = activeParticles.flags.size() - 1;
                        if (newIdx > maxActiveIndex) maxActiveIndex = newIdx;
                        ParticleManager::Instance().m_activeCount.fetch_add(1, std::memory_order_relaxed);
                    }
                } else {
                    // Append new slot
                    activeParticles.push_back(particle);
                    size_t newIdx = activeParticles.flags.size() - 1;
                    if (newIdx > maxActiveIndex) maxActiveIndex = newIdx;
                    ParticleManager::Instance().m_activeCount.fetch_add(1, std::memory_order_relaxed);
                }
                particleCount.store(activeParticles.size(), std::memory_order_release);
            }

            // Mark as processed
            req.ready.store(false, std::memory_order_release);
        }
        tail = (tail + 1) & (CREATION_RING_SIZE - 1);
    }

    creationTail.store(tail, std::memory_order_release);
}

// Get read-only access to particles implementation
const ParticleManager::LockFreeParticleStorage::ParticleSoA &
ParticleManager::LockFreeParticleStorage::getParticlesForRead() const {
    size_t activeIdx = activeBuffer.load(std::memory_order_acquire);
    return particles[activeIdx];
}

// Get writable access to particles implementation
ParticleManager::LockFreeParticleStorage::ParticleSoA &
ParticleManager::LockFreeParticleStorage::getCurrentBuffer() {
    size_t activeIdx = activeBuffer.load(std::memory_order_acquire);
    return particles[activeIdx];
}

// Check if compaction is needed implementation

// Submit new particle implementation
bool ParticleManager::LockFreeParticleStorage::submitNewParticle(
    const NewParticleRequest &request) {
    return tryCreateParticle(
        request.position, request.velocity, request.acceleration,
        request.color, request.life, request.size, /*textureIndex*/
        static_cast<uint8_t>(UnifiedParticle::FLAG_ACTIVE |
                             UnifiedParticle::FLAG_VISIBLE),
        0, request.effectType);
}

// Swap buffers implementation
void ParticleManager::LockFreeParticleStorage::swapBuffers() {
    // Simplified: no deep copy, single-buffer model for stability
    // GameEngine updates and renders sequentially, so a copy is unnecessary.
    // Retain epoch advance for any potential readers relying on it.
    currentEpoch.fetch_add(1, std::memory_order_acq_rel);
}

bool ParticleManager::init() {
  if (m_initialized.load(std::memory_order_acquire)) {
    PARTICLE_INFO("ParticleManager already initialized");
    return true;
  }

  try {
    // Pre-allocate storage for better performance
    // Modern engines can handle much more - reserve generous capacity
    // Note: LockFreeParticleStorage automatically pre-allocates in constructor

    // PERFORMANCE OPTIMIZATION: Initialize trigonometric lookup tables
    initTrigLookupTables();

    // Built-in effects will be registered by GameEngine after init

    m_initialized.store(true, std::memory_order_release);
    m_isShutdown = false;

    // Register EventManager handler for ParticleEffect events
    {
      auto &eventMgr = EventManager::Instance();
      eventMgr.registerHandler(EventTypeId::ParticleEffect, [](const EventData &data) {
        if (!data.isActive() || !data.event) return;
        auto pe = std::dynamic_pointer_cast<ParticleEffectEvent>(data.event);
        if (pe) {
          pe->execute();
        }
      });
      // Register handler for Weather events to drive weather effects
      eventMgr.registerHandler(EventTypeId::Weather, [](const EventData &data) {
        if (!data.isActive() || !data.event) return;
        auto we = std::dynamic_pointer_cast<WeatherEvent>(data.event);
        if (we) {
          const auto &wp = we->getWeatherParams();
          PARTICLE_INFO(std::string("Weather handler: ") + we->getWeatherTypeString() +
                       ", intensity=" + std::to_string(wp.intensity) +
                       ", transition=" + std::to_string(wp.transitionTime) + "s");
          we->execute();
        }
      });
    }

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

  // CRITICAL: Wait for any pending async batches to complete before cleanup
  {
    std::unique_lock<std::mutex> lock(m_batchCompletionMutex);
    if (m_pendingBatchGroups.load(std::memory_order_acquire) > 0) {
      PARTICLE_DEBUG("Waiting for " + std::to_string(m_pendingBatchGroups.load()) + " pending batch groups to complete...");
      m_batchCompletionCV.wait(lock, [this]() {
        return m_pendingBatchGroups.load(std::memory_order_acquire) == 0;
      });
      PARTICLE_DEBUG("All pending batches completed");
    }
  }

  // Clear all storage - no locks needed for lock-free storage
  m_storage.particles[0].clear();
  m_storage.particles[1].clear();
  m_storage.particleCount.store(0, std::memory_order_release);
  m_storage.writeHead.store(0, std::memory_order_release);
  m_activeCount.store(0, std::memory_order_release);

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
      m_storage.freeIndices.push_back(i);
      activeParticles.lives[i] = 0.0f; // Ensure life is zero for double safety
      particlesCleared++;
    }
  }

  // 3.5. IMMEDIATE COMPLETE CLEANUP - Remove ALL particles to ensure zero count
  activeParticles.clear(); // Complete clear to ensure zero particles
  m_storage.particles[1 - activeIdx].clear(); // Clear other buffer too
  m_storage.freeIndices.clear();
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
  // Reset active counter since storage is cleared
  m_activeCount.store(0, std::memory_order_release);

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

  // TRUST GAME ENGINE: GameEngine and GameLoop handle update/render synchronization
  // No blocking mutexes needed - the engine ensures thread safety at a higher level

  auto startTime = std::chrono::high_resolution_clock::now();

  try {
    // Phase 1: Process pending particle creation requests (lock-free)
    m_storage.processCreationRequests();

    // Phase 2: Update effect instances (emission, timing) - MAIN THREAD ONLY
    updateEffectInstances(deltaTime);

    // Phase 2.5: Process newly created particles from effect instances
    m_storage.processCreationRequests();

    // Phase 3: Get snapshot of buffer size and active count
    const auto &particles = m_storage.getParticlesForRead();
    const size_t bufferSize = particles.flags.size();
    if (bufferSize == 0) {
      return;
    }

    // Count active particles for accurate threading decision
    const size_t activeCount = getActiveParticleCount();
    if (activeCount == 0) {
      return;
    }

    // Advance and snapshot wind phase once per frame (thread-safe snapshot)
    m_windPhase += deltaTime * 0.5f;

    // Phase 4: Update particle physics with optimal threading strategy
    // Gate threading on ACTIVE particle count, not buffer size
    bool useThreading = (activeCount >= m_threadingThreshold &&
                         m_useThreading.load(std::memory_order_acquire) &&
                         HammerEngine::ThreadSystem::Exists());

    if (useThreading) {
      // Use WorkerBudget system if enabled, otherwise fall back to legacy
      // threading
      if (m_useWorkerBudget.load(std::memory_order_acquire)) {
        // Limit range to the highest potentially-active index + 1
        const size_t rangeEnd = std::min(bufferSize, m_storage.maxActiveIndex + 1);
        updateWithWorkerBudget(deltaTime, rangeEnd);
      } else {
        const size_t rangeEnd = std::min(bufferSize, m_storage.maxActiveIndex + 1);
        updateParticlesThreaded(deltaTime, rangeEnd);
      }
    } else {
      m_lastWasThreaded.store(false, std::memory_order_relaxed);
      m_lastThreadBatchCount.store(1, std::memory_order_relaxed);
      const size_t rangeEnd = std::min(bufferSize, m_storage.maxActiveIndex + 1);
      updateParticlesSingleThreaded(deltaTime, rangeEnd);
    }

  // Phase 5: Swap buffers for next frame (lock-free)
  m_storage.swapBuffers();

  // Phase 5.5: Collect recently deactivated particles into the free-index pool
  {
    size_t activeIdx = m_storage.activeBuffer.load(std::memory_order_acquire);
    auto &p = m_storage.particles[activeIdx];
    const size_t n = p.flags.size();
    size_t newMax = m_storage.maxActiveIndex;
    for (size_t i = 0; i < n; ++i) {
      if (!(p.flags[i] & UnifiedParticle::FLAG_ACTIVE) &&
          (p.flags[i] & UnifiedParticle::FLAG_RECENTLY_DEACTIVATED)) {
        m_storage.freeIndices.push_back(i);
        p.flags[i] &= ~UnifiedParticle::FLAG_RECENTLY_DEACTIVATED;
        if (i == newMax) {
          // We'll tighten upper bound after the loop
        }
      }
    }
    // Tighten upper bound by scanning down from previous max
    while (newMax > 0 && newMax < p.flags.size() && !(p.flags[newMax] & UnifiedParticle::FLAG_ACTIVE)) {
      --newMax;
    }
    m_storage.maxActiveIndex = newMax;
  }

    // Phase 6: Frame counter and performance tracking (compaction disabled)
    uint64_t currentFrame =
        m_frameCounter.fetch_add(1, std::memory_order_relaxed);

    // Phase 7: Performance tracking (reduced overhead)
    auto endTime = std::chrono::high_resolution_clock::now();
    double timeMs = std::chrono::duration_cast<std::chrono::microseconds>(
                        endTime - startTime)
                        .count() /
                    1000.0;

    // Only track performance every 1200 frames (20 seconds) to minimize
    // overhead
    if (currentFrame % 1200 == 0) {
      size_t currentActiveCount = countActiveParticles();
      recordPerformance(false, timeMs, currentActiveCount);

      if (currentActiveCount > 0) {
        bool wasThreaded = m_lastWasThreaded.load(std::memory_order_relaxed);
        if (wasThreaded) {
          [[maybe_unused]] size_t optimalWorkers = m_lastOptimalWorkerCount.load(std::memory_order_relaxed);
          [[maybe_unused]] size_t availableWorkers = m_lastAvailableWorkers.load(std::memory_order_relaxed);
          [[maybe_unused]] size_t particleBudget = m_lastParticleBudget.load(std::memory_order_relaxed);
          [[maybe_unused]] size_t batchCount = std::max<size_t>(
              1, m_lastThreadBatchCount.load(std::memory_order_relaxed));

          PARTICLE_DEBUG(
              "Particle Summary - Count: " + std::to_string(activeCount) +
              ", Update: " + std::to_string(timeMs) + "ms" +
              ", Effects: " + std::to_string(m_effectInstances.size()) +
              " [Threaded: " + std::to_string(optimalWorkers) + "/" +
              std::to_string(availableWorkers) + " workers, Budget: " +
              std::to_string(particleBudget) + ", Batches: " +
              std::to_string(batchCount) + "]");
        } else {
          PARTICLE_DEBUG(
              "Particle Summary - Count: " + std::to_string(activeCount) +
              ", Update: " + std::to_string(timeMs) + "ms" +
              ", Effects: " + std::to_string(m_effectInstances.size()) +
              " [Single-threaded]");
        }
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
  const size_t n = particles.getSafeAccessCount();
  if (n == 0) return;

  // Batch geometry rendering (indices-free: draw arrays for simplicity/safety)
  constexpr size_t MAX_RECTS_PER_BATCH = 2048;
  std::vector<float> xy; xy.reserve(MAX_RECTS_PER_BATCH * 6 * 2);     // 6 verts * 2 floats
  std::vector<SDL_FColor> cols; cols.reserve(MAX_RECTS_PER_BATCH * 6); // 6 verts
  size_t quadCount = 0;

  auto flush = [&]() {
    if (quadCount == 0) return;
    SDL_RenderGeometryRaw(renderer, nullptr,
                          xy.data(), sizeof(float) * 2,
                          cols.data(), sizeof(SDL_FColor),
                          nullptr, 0,
                          static_cast<int>(quadCount * 6),
                          nullptr, 0, 0);
    xy.clear(); cols.clear(); quadCount = 0;
  };

  for (size_t i = 0; i < n; ++i) {
    if (!(particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) ||
        !(particles.flags[i] & UnifiedParticle::FLAG_VISIBLE)) continue;
    const uint32_t c = particles.colors[i];
    const uint8_t a8 = c & 0xFF; if (a8 == 0) continue;
    const float r = ((c >> 24) & 0xFF) / 255.0f;
    const float g = ((c >> 16) & 0xFF) / 255.0f;
    const float b = ((c >> 8) & 0xFF) / 255.0f;
    const float a = a8 / 255.0f;

    const float size = particles.sizes[i];
    const float cx = particles.posX[i] - cameraX;
    const float cy = particles.posY[i] - cameraY;
    const float hx = size * 0.5f;
    const float hy = size * 0.5f;

    const float x0 = cx - hx, y0 = cy - hy;
    const float x1 = cx + hx, y1 = cy - hy;
    const float x2 = cx + hx, y2 = cy + hy;
    const float x3 = cx - hx, y3 = cy + hy;

    // Append vertices for two triangles (v0,v1,v2) and (v2,v3,v0)
    xy.insert(xy.end(), {x0, y0, x1, y1, x2, y2,  x2, y2, x3, y3, x0, y0});
    SDL_FColor col{r, g, b, a};
    cols.insert(cols.end(), {col, col, col, col, col, col});
    ++quadCount;
    if (quadCount == MAX_RECTS_PER_BATCH) flush();
  }
  flush();

  

  auto endTime = std::chrono::high_resolution_clock::now();
  double timeMs =
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
          .count() /
      1000.0;
  recordPerformance(true, timeMs, n);
}

void ParticleManager::renderBackground(SDL_Renderer *renderer, float cameraX,
                                       float cameraY) {
  if (m_globallyPaused.load(std::memory_order_acquire) ||
      !m_globallyVisible.load(std::memory_order_acquire)) {
    return;
  }

  // THREAD SAFETY: Get immutable snapshot of particle data
  const auto &particles = m_storage.getParticlesForRead();
  const size_t n = particles.getSafeAccessCount();
  if (n == 0) return;

  constexpr size_t MAX_RECTS_PER_BATCH = 2048;
  std::vector<float> xy; xy.reserve(MAX_RECTS_PER_BATCH * 6 * 2);
  std::vector<SDL_FColor> cols; cols.reserve(MAX_RECTS_PER_BATCH * 6);
  size_t quadCount = 0;
  auto flush = [&]() {
    if (quadCount == 0) return;
    SDL_RenderGeometryRaw(renderer, nullptr,
                          xy.data(), sizeof(float) * 2,
                          cols.data(), sizeof(SDL_FColor),
                          nullptr, 0,
                          static_cast<int>(quadCount * 6),
                          nullptr, 0, 0);
    xy.clear(); cols.clear(); quadCount = 0;
  };
  for (size_t i = 0; i < n; ++i) {
    if (!(particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) ||
        !(particles.flags[i] & UnifiedParticle::FLAG_VISIBLE) ||
        particles.layers[i] != UnifiedParticle::RenderLayer::Background) continue;
    const uint32_t c = particles.colors[i];
    const uint8_t a8 = c & 0xFF; if (a8 == 0) continue;
    const float r = ((c >> 24) & 0xFF) / 255.0f;
    const float g = ((c >> 16) & 0xFF) / 255.0f;
    const float b = ((c >> 8) & 0xFF) / 255.0f;
    const float a = a8 / 255.0f;
    const float size = particles.sizes[i];
    const float cx = particles.posX[i] - cameraX;
    const float cy = particles.posY[i] - cameraY;
    const float hx = size * 0.5f, hy = size * 0.5f;
    const float x0 = cx - hx, y0 = cy - hy;
    const float x1 = cx + hx, y1 = cy - hy;
    const float x2 = cx + hx, y2 = cy + hy;
    const float x3 = cx - hx, y3 = cy + hy;
    xy.insert(xy.end(), {x0, y0, x1, y1, x2, y2,  x2, y2, x3, y3, x0, y0});
    SDL_FColor col{r, g, b, a};
    cols.insert(cols.end(), {col, col, col, col, col, col});
    if (++quadCount == MAX_RECTS_PER_BATCH) flush();
  }
  flush();
}

void ParticleManager::renderForeground(SDL_Renderer *renderer, float cameraX,
                                       float cameraY) {
  if (m_globallyPaused.load(std::memory_order_acquire) ||
      !m_globallyVisible.load(std::memory_order_acquire)) {
    return;
  }

  // THREAD SAFETY: Get immutable snapshot of particle data
  const auto &particles = m_storage.getParticlesForRead();
  const size_t n = particles.getSafeAccessCount();
  if (n == 0) return;
  constexpr size_t MAX_RECTS_PER_BATCH = 2048;
  std::vector<float> xy; xy.reserve(MAX_RECTS_PER_BATCH * 6 * 2);
  std::vector<SDL_FColor> cols; cols.reserve(MAX_RECTS_PER_BATCH * 6);
  size_t quadCount = 0;
  auto flush = [&]() {
    if (quadCount == 0) return;
    SDL_RenderGeometryRaw(renderer, nullptr,
                          xy.data(), sizeof(float) * 2,
                          cols.data(), sizeof(SDL_FColor),
                          nullptr, 0,
                          static_cast<int>(quadCount * 6),
                          nullptr, 0, 0);
    xy.clear(); cols.clear(); quadCount = 0;
  };
  for (size_t i = 0; i < n; ++i) {
    if (!(particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) ||
        !(particles.flags[i] & UnifiedParticle::FLAG_VISIBLE) ||
        particles.layers[i] != UnifiedParticle::RenderLayer::Foreground) continue;
    const uint32_t c = particles.colors[i];
    const uint8_t a8 = c & 0xFF; if (a8 == 0) continue;
    const float r = ((c >> 24) & 0xFF) / 255.0f;
    const float g = ((c >> 16) & 0xFF) / 255.0f;
    const float b = ((c >> 8) & 0xFF) / 255.0f;
    const float a = a8 / 255.0f;
    const float size = particles.sizes[i];
    const float cx = particles.posX[i] - cameraX;
    const float cy = particles.posY[i] - cameraY;
    const float hx = size * 0.5f, hy = size * 0.5f;
    const float x0 = cx - hx, y0 = cy - hy;
    const float x1 = cx + hx, y1 = cy - hy;
    const float x2 = cx + hx, y2 = cy + hy;
    const float x3 = cx - hx, y3 = cy + hy;
    xy.insert(xy.end(), {x0, y0, x1, y1, x2, y2,  x2, y2, x3, y3, x0, y0});
    SDL_FColor col{r, g, b, a};
    cols.insert(cols.end(), {col, col, col, col, col, col});
    if (++quadCount == MAX_RECTS_PER_BATCH) flush();
  }
  flush();
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
        particles.flags[i] |= UnifiedParticle::FLAG_RECENTLY_DEACTIVATED;
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
          particles.flags[i] |= UnifiedParticle::FLAG_RECENTLY_DEACTIVATED;
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
        particles.flags[i] |= UnifiedParticle::FLAG_RECENTLY_DEACTIVATED;
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

  // Register effect (save ID before move)
  const uint32_t effectId = instance.id;
  m_effectInstances.emplace_back(std::move(instance));
  m_effectIdToIndex[effectId] = m_effectInstances.size() - 1;

  PARTICLE_INFO("Weather effect created: " + effectTypeToString(effectType) +
                " (ID: " + std::to_string(effectId) + ") at position (" +
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
  PARTICLE_INFO("Creating Rain effect: minSize=0.3f, maxSize=0.7f, minSpeed=220f, maxSpeed=380f");
  const auto &gameEngine = GameEngine::Instance();
  ParticleEffectDefinition rain("Rain", ParticleEffectType::Rain);
  rain.layer = UnifiedParticle::RenderLayer::Background;
  rain.emitterConfig.spread = static_cast<float>(gameEngine.getLogicalWidth());
  rain.emitterConfig.emissionRate =
      300.0f; // Reduced emission for better performance while maintaining
              // coverage
rain.emitterConfig.minSpeed = 400.0f; // Moderate speed for visibility
rain.emitterConfig.maxSpeed =
    600.0f; // Fast but not too fast to see
  rain.emitterConfig.minLife = 1.5f; // Shorter life for faster transition
  rain.emitterConfig.maxLife = 2.5f;
rain.emitterConfig.minSize = 3.0f; // Much smaller raindrops
rain.emitterConfig.maxSize = 3.5f; // Smaller max size
  rain.emitterConfig.minColor = 0x1E3A8AFF; // Dark blue
  rain.emitterConfig.maxColor = 0x3B82F6FF; // Medium blue
rain.emitterConfig.gravity =
    Vector2D(0.0f, 400.0f); // Moderate gravity for visibility
  rain.emitterConfig.windForce =
      Vector2D(5.0f, 0.0f); // Base wind - turbulence will add variation
  // textureID removed
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
      500.0f; // Reduced emission while maintaining storm intensity
  heavyRain.emitterConfig.minSpeed =
      120.0f; // Higher start speed for heavier drops
  heavyRain.emitterConfig.maxSpeed =
      220.0f; // Lower max - terminal velocity handles the rest                            
  heavyRain.emitterConfig.minLife = 3.5f; // Good life for screen coverage
  heavyRain.emitterConfig.maxLife = 6.0f;
  heavyRain.emitterConfig.minSize = 2.0f; // Larger drops for heavy rain
  heavyRain.emitterConfig.maxSize = 8.0f; // Much larger maximum size
  heavyRain.emitterConfig.minColor = 0x1E3A8AFF; // Dark blue
  heavyRain.emitterConfig.maxColor = 0x3B82F6FF; // Medium blue
  heavyRain.emitterConfig.gravity = Vector2D(
      0.0f, 350.0f); // Base gravity - enhanced physics handle acceleration
  heavyRain.emitterConfig.windForce =
      Vector2D(8.0f, 0.0f); // More base wind for stormy conditions
  // textureID removed
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
  // textureID removed
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
  // textureID removed
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
  // textureID removed
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
  // textureID removed
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
      100.0f; // Halved for performance
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
  // textureID removed
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
  // textureID removed
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
      100.0f; // Reduced from 300 for better performance
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
  // textureID removed
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
  if (!m_initialized.load(std::memory_order_acquire)) return 0;
  size_t cached = m_activeCount.load(std::memory_order_acquire);
  if (cached != 0) return cached;
  // Fallback: reconcile by scanning flags when cached is zero
  const auto &particles = m_storage.getParticlesForRead();
  const size_t n = particles.flags.size();
  size_t counted = 0;
  for (size_t i = 0; i < n; ++i) {
    if (particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) ++counted;
  }
  // Best-effort update
  const_cast<std::atomic<size_t>&>(m_activeCount).store(counted, std::memory_order_release);
  return counted;
}

// Compaction removed: object pool reuse only

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
  const size_t bufferSize = currentBuffer.flags.size();
  
  // BOUNDS SAFETY: Ensure all vectors have consistent sizes
  if (bufferSize == 0 ||
      currentBuffer.posX.size() != bufferSize ||
      currentBuffer.velX.size() != bufferSize ||
      currentBuffer.flags.size() != bufferSize ||
      currentBuffer.lives.size() != bufferSize) {
    // Buffer is inconsistent, fall back to single-threaded update
    m_lastWasThreaded.store(false, std::memory_order_relaxed);
    m_lastThreadBatchCount.store(1, std::memory_order_relaxed);
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

  // Store thread allocation info for debug output
  m_lastOptimalWorkerCount.store(optimalWorkerCount, std::memory_order_relaxed);
  m_lastAvailableWorkers.store(availableWorkers, std::memory_order_relaxed);
  m_lastParticleBudget.store(budget.particleAllocated, std::memory_order_relaxed);
  m_lastWasThreaded.store(true, std::memory_order_relaxed);

  // Use unified batch calculation for consistency across all managers
  const size_t threshold = std::max(m_threadingThreshold.load(std::memory_order_relaxed),
                                    static_cast<size_t>(1));
  double queuePressure = static_cast<double>(queueSize) / queueCapacity;

  auto [batchCount, batchSize] = HammerEngine::calculateBatchStrategy(
      HammerEngine::PARTICLE_BATCH_CONFIG,
      activeParticleCount,
      threshold,
      optimalWorkerCount,
      queuePressure
  );

  size_t particlesPerBatch = activeParticleCount / batchCount;
  size_t remainingParticles = activeParticleCount % batchCount;

  // PERFORMANCE OPTIMIZATION: Use batchEnqueueTasks() for O(1) lock acquisition
  // instead of O(N) individual enqueue calls. This reduces ThreadSystem mutex
  // contention significantly when submitting multiple batches.

  // Atomic counter for batch completion synchronization
  // Use shared_ptr so async lambdas can safely access this after update() returns
  auto remainingBatches = std::make_shared<std::atomic<size_t>>(batchCount);

  // Build task vector for batch submission
  std::vector<std::function<void()>> tasks;
  tasks.reserve(batchCount);

  for (size_t i = 0; i < batchCount; ++i) {
    size_t startIdx = i * particlesPerBatch;
    size_t endIdx = startIdx + particlesPerBatch +
                    (i == batchCount - 1 ? remainingParticles : 0);

    // CRITICAL FIX: Ensure endIdx doesn't exceed buffer size
    endIdx = std::min(endIdx, bufferSize);

    // Skip empty batches or invalid ranges
    if (startIdx >= bufferSize || startIdx >= endIdx) {
      // Decrement counter for skipped batch
      remainingBatches->fetch_sub(1, std::memory_order_relaxed);
      continue;
    }

    // Build task with captured values
    // currentBuffer captured by reference is safe - it points to member storage
    // remainingBatches captured by shared_ptr value - safe for async execution
    tasks.push_back([this, &currentBuffer, startIdx, endIdx, deltaTime,
                    windPhase = m_windPhase, remainingBatches]() {
      try {
        updateParticleRange(currentBuffer, startIdx, endIdx, deltaTime, windPhase);
      } catch (const std::exception &e) {
        PARTICLE_ERROR(std::string("Exception in particle batch: ") + e.what());
      } catch (...) {
        PARTICLE_ERROR("Unknown exception in particle batch");
      }

      // When last batch completes, signal batch group completion for safe shutdown
      if (remainingBatches->fetch_sub(1, std::memory_order_acq_rel) == 1) {
        if (this->m_pendingBatchGroups.fetch_sub(1, std::memory_order_acq_rel) == 1) {
          std::lock_guard<std::mutex> lock(this->m_batchCompletionMutex);
          this->m_batchCompletionCV.notify_all();
        }
      }
    });
  }

  // Track this batch group for safe shutdown
  if (!tasks.empty()) {
    m_pendingBatchGroups.fetch_add(1, std::memory_order_release);

    // Single mutex acquisition for entire batch submission (O(1) instead of O(N))
    threadSystem.batchEnqueueTasks(tasks, HammerEngine::TaskPriority::Normal, "Particle_Batch");

    // NO BLOCKING WAIT: Batches complete asynchronously
    // GameEngine::update() mutex ensures batches finish before next frame
  }

  m_lastThreadBatchCount.store(batchCount, std::memory_order_relaxed);
}

void ParticleManager::updateParticlesSingleThreaded(
    float deltaTime, size_t activeParticleCount) {
  auto &currentBuffer = m_storage.getCurrentBuffer();
  updateParticleRange(currentBuffer, 0, activeParticleCount, deltaTime, m_windPhase);
}

void ParticleManager::updateParticleRange(
    LockFreeParticleStorage::ParticleSoA &particles, size_t startIdx, size_t endIdx,
    float deltaTime, float windPhase) {

  // PRODUCTION OPTIMIZATION: Pre-compute expensive operations
  const float windPhase0_8 = windPhase * 0.8f;
  const float windPhase1_2 = windPhase * 1.2f;
  const float windPhase3_0 = windPhase * 3.0f;
  const float windPhase8_0 = windPhase * 8.0f;
  const float windPhase6_0 = windPhase * 6.0f;

  // CRITICAL FIX: Cache all buffer sizes and validate consistency before processing
  const size_t positionsSize = particles.posX.size();
  const size_t velocitiesSize = particles.velX.size();
  const size_t accelerationsSize = particles.accX.size();
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
    float windVariation = fastSin(windPhase + particleOffset) *
                          0.3f;    // Per-particle wind variation
    float atmosphericDrag = 0.98f; // Slight air resistance

    // OPTIMIZATION: Restructure effect type handling for better branch prediction
    const ParticleEffectType effectType = particles.effectTypes[i];
    
    // OPTIMIZATION: Use switch statement for better branch prediction than nested if-else
    if (effectType == ParticleEffectType::Cloudy) {
      // Apply horizontal movement for cloud drift
      particles.accX[i] = 15.0f;
      particles.accY[i] = 0.0f;

      // PRODUCTION OPTIMIZATION: Pre-computed trigonometric values
      const float drift = fastSin(windPhase0_8 + particleOffset15) * 3.0f;
      const float verticalFloat = fastCos(windPhase1_2 + particleOffset) * 1.5f;

      particles.velX[i] += drift * deltaTime;
      particles.velY[i] += verticalFloat * deltaTime;

      atmosphericDrag = 1.0f;
    }
    // Apply wind variation for weather particles
    else if (particles.flags[i] & UnifiedParticle::FLAG_WEATHER) {
      // Add natural wind turbulence
      particles.accX[i] += windVariation * 20.0f;

      // Different atmospheric effects for different particle types
      
      // OPTIMIZATION: Use switch for better branch prediction
      switch (effectType) {
        case ParticleEffectType::Snow:
        case ParticleEffectType::HeavySnow: {
          const float flutter = fastSin(windPhase3_0 + particleOffset2) * 8.0f;
          particles.velX[i] += flutter * deltaTime;
          atmosphericDrag = 0.96f; // More air resistance for snow
          break;
        }
        case ParticleEffectType::Rain:
        case ParticleEffectType::HeavyRain: {
          // Enhanced rain physics for natural movement
          const float particleSize = particles.sizes[i];
          const float sizeNormalized = (particleSize - 1.5f) / 4.5f; // Normalize to 0-1 range
          
          // Terminal velocity based on droplet size (larger drops fall faster)
          const float terminalVelocity = 200.0f + (sizeNormalized * 150.0f); // 200-350 range
          const float currentVerticalSpeed = std::fabs(particles.velY[i]);
          
          // Apply gravity only if below terminal velocity
          if (currentVerticalSpeed < terminalVelocity) {
            const float gravityScale = 1.0f - (currentVerticalSpeed / terminalVelocity);
            particles.accY[i] += gravityScale * 80.0f; // Enhanced gravity
          }
          
          // Safe division with zero check for life-based effects
          const float lifeRatio = (particles.maxLives[i] > 0.0f) ? 
                                 (particles.lives[i] / particles.maxLives[i]) : 0.0f;
          
          // Enhanced wind variation with gusts and turbulence
          const float gustPhase = windPhase * 2.0f + particleOffset;
          const float microGust = fastSin(gustPhase) * fastCos(gustPhase * 1.3f) * 15.0f;
          const float atmosphericTurbulence = fastSin(windPhase * 5.0f + particleOffset2) * 8.0f;
          
          // Apply horizontal wind forces (older drops are more susceptible to wind)
          const float windSusceptibility = 0.5f + (1.0f - lifeRatio) * 0.5f;
          particles.velX[i] += (microGust + atmosphericTurbulence) * windSusceptibility * deltaTime;
          
          // Size-dependent air resistance (larger drops are less affected)
          atmosphericDrag = 0.985f + (sizeNormalized * 0.01f); // 0.985-0.995 range
          
          // Add subtle vertical oscillation for realism (air currents)
          const float verticalOscillation = fastCos(windPhase * 3.0f + particleOffset25) * 2.0f;
          particles.velY[i] += verticalOscillation * deltaTime;
          break;
        }
        default: { // Regular fog behavior (not clouds)
          const float drift = fastSin(windPhase0_8 + particleOffset15) * 15.0f;
          const float verticalDrift = fastCos(windPhase1_2 + particleOffset) * 3.0f * deltaTime;
          particles.velX[i] += drift * deltaTime;
          particles.velY[i] += verticalDrift;
          atmosphericDrag = 0.999f;
          break;
        }
      }
    }
    // Special handling for fire and smoke particles for natural movement
    else {
      // Safe division with zero check
      const float lifeRatio = (particles.maxLives[i] > 0.0f) ? 
                             (particles.lives[i] / particles.maxLives[i]) : 0.0f;
      
      // OPTIMIZATION: Use switch for better branch prediction than nested if-else
      switch (effectType) {
        case ParticleEffectType::Fire: {
          float randomFactor = static_cast<float>(fast_rand()) / 32767.0f;
          
          // More random turbulence for fire
          const float heatTurbulence = fastSin(windPhase8_0 + particleOffset3) * 15.0f +
                                     (randomFactor - 0.5f) * 10.0f;
          const float heatRise = fastCos(windPhase6_0 + particleOffset25) * 10.0f;

          particles.velX[i] += heatTurbulence * deltaTime;
          particles.velY[i] += heatRise * deltaTime;

          // Fire particles get more chaotic as they age (burns out)
          const float chaos = (1.0f - lifeRatio) * 25.0f;
          const float chaosValue = (randomFactor - 0.5f) * chaos * deltaTime;
          particles.accX[i] += chaosValue;

          // Visuals: Fire particles use their initial random color and just fade with age
          particles.sizes[i] *= (lifeRatio * 0.99f);
          atmosphericDrag = 0.94f; // High drag for fire flicker
          break;
        }
        case ParticleEffectType::Smoke: {
          float randomFactor = static_cast<float>(fast_rand()) / 32767.0f;
          
          // Circular billowing motion
          float angle = (i % 360) * 3.14159f / 180.0f; // Unique angle per particle
          float speed = 15.0f + (randomFactor * 10.0f);
          float circleX = fastCos(angle + windPhase) * speed * (1.0f - lifeRatio);
          float circleY = fastSin(angle + windPhase) * speed * (1.0f - lifeRatio);

          particles.velX[i] += circleX * deltaTime;
          particles.velY[i] += circleY * deltaTime - (20.0f * deltaTime);

          // Visuals: Shrink slightly over life
          particles.sizes[i] *= 0.998f;
          atmosphericDrag = 0.96f;
          break;
        }
        default: { // Other particles (sparks, magic, etc.) - use standard turbulence
          const float generalTurbulence = windVariation * 10.0f;
          particles.velX[i] += generalTurbulence * deltaTime;
          atmosphericDrag = 0.97f;
          break;
        }
      }
    }

    // Apply atmospheric drag on velocity components
    particles.velX[i] *= atmosphericDrag;
    particles.velY[i] *= atmosphericDrag;

    // Update life
    particles.lives[i] -= deltaTime;
    if (particles.lives[i] <= 0.0f) {
      if (particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) {
        particles.flags[i] &= ~UnifiedParticle::FLAG_ACTIVE;
        particles.flags[i] |= UnifiedParticle::FLAG_RECENTLY_DEACTIVATED;
        ParticleManager::Instance().m_activeCount.fetch_sub(1, std::memory_order_relaxed);
      }
      continue;
    }

    // Note: Enhanced visual properties with natural fading are now handled
    // in batch color processing after the main update loop for better performance

    // Note: Size variation for natural appearance would be applied during
    // rendering
   }
   
   // PERFORMANCE OPTIMIZATION: Apply physics integration for all platforms.
   // The implementation uses SIMD when available, otherwise falls back to scalar.
   updateParticlePhysicsSIMD(particles, startIdx, endIdx, deltaTime);
   
   // PERFORMANCE OPTIMIZATION: Apply batch color processing for non-fire particles
   // This provides better cache utilization and eliminates redundant alpha calculations
   batchProcessParticleColors(particles, startIdx, endIdx);
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
  
  // Special handling for weather effects that use spread as screen coverage, not angle
  if (effectDef.type == ParticleEffectType::Rain || 
      effectDef.type == ParticleEffectType::HeavyRain ||
      effectDef.type == ParticleEffectType::Snow ||
      effectDef.type == ParticleEffectType::HeavySnow) {
    // For weather, use slight angular variation (±5 degrees) for natural movement
    float angleRange = 5.0f * 0.017453f; // 5 degrees in radians
    float angle = (naturalRand * 2.0f - 1.0f) * angleRange;
    request.velocity = Vector2D(speed * fastSin(angle), speed * fastCos(angle));
  } else {
    // For other effects, use spread as angular range
    float angleRange = config.spread * 0.017453f; // Convert degrees to radians
    float angle = (naturalRand * 2.0f - 1.0f) * angleRange;
    request.velocity = Vector2D(speed * fastSin(angle), speed * fastCos(angle));
  }
  request.acceleration = config.gravity;
  request.life = config.minLife + (config.maxLife - config.minLife) *
                                      static_cast<float>(fast_rand()) / 32767.0f;
  request.size = config.minSize + (config.maxSize - config.minSize) *
                                      static_cast<float>(fast_rand()) / 32767.0f;
  request.color = interpolateColor(config.minColor, config.maxColor, naturalRand);
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

// getTextureIndex removed: particles render as rects

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
    m_lastWasThreaded.store(false, std::memory_order_relaxed);
    m_lastThreadBatchCount.store(1, std::memory_order_relaxed);
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

size_t ParticleManager::getThreadingThreshold() const {
  return m_threadingThreshold;
}

// Helper methods for enum-based classification system
ParticleEffectType
ParticleManager::weatherStringToEnum(const std::string &weatherType,
                                     float intensity) const {
  if (weatherType == "Rainy") {
    ParticleEffectType result = (intensity > 0.9f) ? ParticleEffectType::HeavyRain
                              : ParticleEffectType::Rain;
    PARTICLE_INFO("Weather mapping: \"" + weatherType + "\" intensity=" + std::to_string(intensity) + 
                  " -> " + (result == ParticleEffectType::Rain ? "Rain" : "HeavyRain"));
    return result;
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

void ParticleManager::initTrigLookupTables() {
  // PERFORMANCE OPTIMIZATION: Pre-compute sin/cos lookup tables
  // This eliminates expensive real-time trigonometric calculations
  constexpr float step = 2.0f * 3.14159265f / TRIG_LUT_SIZE;
  
  for (size_t i = 0; i < TRIG_LUT_SIZE; ++i) {
    const float angle = i * step;
    m_sinLUT[i] = std::sin(angle);
    m_cosLUT[i] = std::cos(angle);
  }
}

void ParticleManager::updateParticlePhysicsSIMD(
    LockFreeParticleStorage::ParticleSoA &particles, size_t startIdx, size_t endIdx,
    float deltaTime) {
    
#ifdef PARTICLE_SIMD_SSE2
  // Prepare aligned SIMD constants
  const __m128 deltaTimeVec = _mm_set1_ps(deltaTime);
  const __m128 atmosphericDragVec = _mm_set1_ps(0.98f);

  // Quick bounds check - only validate once
  const size_t particleCount = particles.size();
  if (endIdx > particleCount || startIdx >= endIdx) return;
  endIdx = std::min(endIdx, particleCount);

  // SIMD arrays are primary storage; operate directly on them

  // Scalar pre-loop to align to 4-float boundary for aligned loads
  size_t i = startIdx;
  while (i < endIdx && (i & 0x3) != 0) {
    if (particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) {
      particles.velX[i] = (particles.velX[i] + particles.accX[i] * deltaTime) * 0.98f;
      particles.velY[i] = (particles.velY[i] + particles.accY[i] * deltaTime) * 0.98f;
      particles.posX[i] = particles.posX[i] + particles.velX[i] * deltaTime;
      particles.posY[i] = particles.posY[i] + particles.velY[i] * deltaTime;
    }
    ++i;
  }

  // SIMD main loop - use aligned loads for maximum performance
  const size_t simdEnd = ((endIdx - i) / 4) * 4 + i;
  for (; i < simdEnd; i += 4) {
    // SIMD flag check for 4 particles: skip if none active
    const __m128i flagsv = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&particles.flags[i]));
    const __m128i activeMask = _mm_set1_epi8(static_cast<char>(UnifiedParticle::FLAG_ACTIVE));
    const __m128i activev = _mm_and_si128(flagsv, activeMask);
    const __m128i gt0 = _mm_cmpgt_epi8(activev, _mm_setzero_si128());
    const int maskBits = _mm_movemask_epi8(gt0);
    if ((maskBits & 0xF) == 0) continue;

    // Use aligned loads - AlignedAllocator guarantees 16-byte alignment
    __m128 posXv = _mm_load_ps(&particles.posX[i]);
    __m128 posYv = _mm_load_ps(&particles.posY[i]);
    __m128 velXv = _mm_load_ps(&particles.velX[i]);
    __m128 velYv = _mm_load_ps(&particles.velY[i]);
    const __m128 accXv = _mm_load_ps(&particles.accX[i]);
    const __m128 accYv = _mm_load_ps(&particles.accY[i]);

    // SIMD physics update
    velXv = _mm_mul_ps(_mm_add_ps(velXv, _mm_mul_ps(accXv, deltaTimeVec)), atmosphericDragVec);
    velYv = _mm_mul_ps(_mm_add_ps(velYv, _mm_mul_ps(accYv, deltaTimeVec)), atmosphericDragVec);

    posXv = _mm_add_ps(posXv, _mm_mul_ps(velXv, deltaTimeVec));
    posYv = _mm_add_ps(posYv, _mm_mul_ps(velYv, deltaTimeVec));

    // Store results back to SIMD arrays
    _mm_store_ps(&particles.velX[i], velXv);
    _mm_store_ps(&particles.velY[i], velYv);
    _mm_store_ps(&particles.posX[i], posXv);
    _mm_store_ps(&particles.posY[i], posYv);
  }

  // Scalar tail
  for (; i < endIdx; ++i) {
    if (particles.flags[i] & UnifiedParticle::FLAG_ACTIVE) {
      particles.velX[i] = (particles.velX[i] + particles.accX[i] * deltaTime) * 0.98f;
      particles.velY[i] = (particles.velY[i] + particles.accY[i] * deltaTime) * 0.98f;
      particles.posX[i] = particles.posX[i] + particles.velX[i] * deltaTime;
      particles.posY[i] = particles.posY[i] + particles.velY[i] * deltaTime;
    }
  }

  // No Vector2D sync required

#else
  // Fallback to scalar implementation for platforms without SSE2
  for (size_t i = startIdx; i < endIdx; ++i) {
    if (!(particles.flags[i] & UnifiedParticle::FLAG_ACTIVE)) continue;
    particles.velX[i] = (particles.velX[i] + particles.accX[i] * deltaTime) * 0.98f;
    particles.velY[i] = (particles.velY[i] + particles.accY[i] * deltaTime) * 0.98f;
    particles.posX[i] += particles.velX[i] * deltaTime;
    particles.posY[i] += particles.velY[i] * deltaTime;
  }
#endif
}

void ParticleManager::batchProcessParticleColors(
    LockFreeParticleStorage::ParticleSoA &particles, size_t startIdx, size_t endIdx) {
    
  // PERFORMANCE OPTIMIZATION: Batch process color alpha blending
  // This eliminates repeated alpha calculation and bit manipulation per particle
  
  for (size_t i = startIdx; i < endIdx; ++i) {
    if (!(particles.flags[i] & UnifiedParticle::FLAG_ACTIVE)) continue;
    
    // Skip particles that have special color handling - but allow fire alpha fading
    if (particles.effectTypes[i] == ParticleEffectType::Fire) {
      // Fire particles: fade alpha over time but keep their color
      const float lifeRatio = (particles.maxLives[i] > 0.0f) ? 
                             (particles.lives[i] / particles.maxLives[i]) : 0.0f;
      const uint32_t color = particles.colors[i];
      const uint8_t alpha = static_cast<uint8_t>(255.0f * lifeRatio); // Fade out as life decreases
      particles.colors[i] = (color & 0xFFFFFF00) | alpha;
      continue;
    }
    
    // Cache color value to avoid repeated vector access
    const uint32_t color = particles.colors[i];
    
    // Safe division with zero check for life ratio calculation
    const float lifeRatio = (particles.maxLives[i] > 0.0f) ? 
                           (particles.lives[i] / particles.maxLives[i]) : 0.0f;
    
    // Fast alpha calculation based on particle type
    float alphaMultiplier = 1.0f;
    if (particles.flags[i] & UnifiedParticle::FLAG_WEATHER) {
      // Weather particles: fade in at start, fade out at end
      if (lifeRatio > 0.9f) {
        alphaMultiplier = (1.0f - lifeRatio) * 10.0f; // Fade in during first 10%
      } else if (lifeRatio < 0.2f) {
        alphaMultiplier = lifeRatio * 5.0f; // Fade out during last 20%
      }
    } else {
      // Standard fade for non-weather particles
      alphaMultiplier = lifeRatio;
    }
    
    // Fast bit manipulation for alpha blending
    const uint8_t alpha = static_cast<uint8_t>(255.0f * alphaMultiplier);
    particles.colors[i] = (color & 0xFFFFFF00) | alpha;
  }
}
