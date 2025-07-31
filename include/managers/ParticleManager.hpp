/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef PARTICLE_MANAGER_HPP
#define PARTICLE_MANAGER_HPP

/**
 * @file ParticleManager.hpp
 * @brief High-performance particle system manager optimized for speed and
 * efficiency
 *
 * Ultra-high-performance ParticleManager with EventManager integration:
 * - Cache-friendly Structure of Arrays (SoA) layout for optimal performance
 * - Type-indexed particle storage for fast effect dispatch
 * - Lock-free double buffering for concurrent updates
 * - Object pooling and batch processing for memory efficiency
 * - SIMD optimizations for physics calculations
 * - Automatic integration with EventManager weather effects
 * - Scales to 10k+ particles while maintaining 60+ FPS
 * - Thread-safe design with minimal lock contention
 */

#include "core/WorkerBudget.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class TextureManager;
class EventManager;

// Use the proper logging system for thread-safe logging
// Note: This header only contains the LOG macro declaration, the actual include
// is in the .cpp file

/**
 * @brief Particle effect type enumeration for fast dispatch
 */
enum class ParticleEffectType : uint8_t {
  Rain = 0,
  HeavyRain = 1,
  Snow = 2,
  HeavySnow = 3,
  Fog = 4,
  Cloudy = 5,
  Fire = 6,
  Smoke = 7,
  Sparks = 8,
  Magic = 9,
  Custom = 10,
  COUNT = 11
};

/**
 * @brief Particle blend modes for rendering
 */
enum class ParticleBlendMode : uint8_t {
  Alpha = 0,    // Standard alpha blending
  Additive = 1, // Additive blending for lights/fire
  Multiply = 2, // Multiply blending for shadows/fog
  Screen = 3    // Screen blending for bright effects
};

/**
 * @brief Cache-efficient particle data using Structure of Arrays (SoA)
 * Hot data (frequently accessed) is separated from cold data for better cache
 * performance Optimized for 32-byte cache line alignment
 */
struct alignas(32) ParticleData {
  // Hot data - accessed every frame (32 bytes)
  Vector2D position;     // Current position (8 bytes)
  Vector2D velocity;     // Velocity vector (8 bytes)
  float life;            // Current life (4 bytes)
  float maxLife;         // Maximum life (4 bytes)
  uint32_t color;        // RGBA color packed (4 bytes)
  uint16_t textureIndex; // Texture index (2 bytes)
  uint8_t flags;         // Active, visible, etc. (1 byte)
  uint8_t generationId;  // Generation/wave ID for batch clearing (1 byte)

  // Flags bit definitions
  static constexpr uint8_t FLAG_ACTIVE = 1 << 0;
  static constexpr uint8_t FLAG_VISIBLE = 1 << 1;
  static constexpr uint8_t FLAG_GRAVITY = 1 << 2;
  static constexpr uint8_t FLAG_COLLISION = 1 << 3;
  static constexpr uint8_t FLAG_WEATHER = 1 << 4; // Mark as weather particle
  static constexpr uint8_t FLAG_FADE_OUT =
      1 << 5; // Particle is in fade-out phase

  ParticleData()
      : position(0, 0), velocity(0, 0), life(0.0f), maxLife(1.0f),
        color(0xFFFFFFFF), textureIndex(0), flags(0), generationId(0) {}

  bool isActive() const { return flags & FLAG_ACTIVE; }
  void setActive(bool active) {
    if (active)
      flags |= FLAG_ACTIVE;
    else
      flags &= ~FLAG_ACTIVE;
  }

  bool isVisible() const { return flags & FLAG_VISIBLE; }
  void setVisible(bool visible) {
    if (visible)
      flags |= FLAG_VISIBLE;
    else
      flags &= ~FLAG_VISIBLE;
  }

  bool isWeatherParticle() const { return flags & FLAG_WEATHER; }
  void setWeatherParticle(bool weather) {
    if (weather)
      flags |= FLAG_WEATHER;
    else
      flags &= ~FLAG_WEATHER;
  }

  bool isFadingOut() const { return flags & FLAG_FADE_OUT; }
  void setFadingOut(bool fading) {
    if (fading)
      flags |= FLAG_FADE_OUT;
    else
      flags &= ~FLAG_FADE_OUT;
  }

  float getLifeRatio() const { return maxLife > 0 ? life / maxLife : 0.0f; }
};

/**
 * @brief Cold particle data - accessed less frequently
 */
struct ParticleColdData {
  Vector2D acceleration; // Acceleration vector
  float size;            // Particle size
  float rotation;        // Current rotation
  float angularVelocity; // Angular velocity
  float fadeInTime;      // Fade in duration
  float fadeOutTime;     // Fade out duration

  ParticleColdData()
      : acceleration(0, 0), size(1.0f), rotation(0.0f), angularVelocity(0.0f),
        fadeInTime(0.1f), fadeOutTime(0.3f) {}
};

/**
 * @brief Particle emitter configuration
 */
struct ParticleEmitterConfig {
  Vector2D position{0, 0};       // Emitter position
  Vector2D direction{0, -1};     // Primary emission direction
  float spread{45.0f};           // Spread angle in degrees
  float emissionRate{100.0f};    // Particles per second
  float minSpeed{50.0f};         // Minimum particle speed
  float maxSpeed{150.0f};        // Maximum particle speed
  float minLife{1.0f};           // Minimum particle life
  float maxLife{3.0f};           // Maximum particle life
  float minSize{1.0f};           // Minimum particle size
  float maxSize{4.0f};           // Maximum particle size
  uint32_t minColor{0xFFFFFFFF}; // Minimum color (RGBA)
  uint32_t maxColor{0xFFFFFFFF}; // Maximum color (RGBA)
  Vector2D gravity{0, 98.0f};    // Gravity acceleration
  Vector2D windForce{0, 0};      // Wind force
  bool loops{true};              // Whether emitter loops
  float duration{-1.0f};         // Emitter duration (-1 for infinite)
  std::string textureID{""};     // Texture identifier
  ParticleBlendMode blendMode{ParticleBlendMode::Alpha}; // Blend mode

  // Advanced properties
  bool useWorldSpace{true};    // World space vs local space
  float burstCount{0};         // Particles per burst
  float burstInterval{1.0f};   // Time between bursts
  bool enableCollision{false}; // Enable collision detection
  float bounceDamping{0.8f};   // Collision bounce damping
};

struct ParticleEffectDefinition;

// Helper methods for enum-based classification system
ParticleEffectType weatherStringToEnum(const std::string &weatherType,
                                       float intensity);
std::string effectTypeToString(ParticleEffectType type);

// Built-in effect creation helpers
ParticleEffectDefinition createRainEffect();
ParticleEffectDefinition createHeavyRainEffect();
ParticleEffectDefinition createSnowEffect();
ParticleEffectDefinition createHeavySnowEffect();
ParticleEffectDefinition createFogEffect();
ParticleEffectDefinition createCloudyEffect();
ParticleEffectDefinition createFireEffect();
ParticleEffectDefinition createSmokeEffect();
ParticleEffectDefinition createSparksEffect();
ParticleEffectDefinition createMagicEffect();

struct UnifiedParticle {
  // All particle data in one structure - no synchronization issues
  Vector2D position;
  Vector2D velocity;
  Vector2D acceleration;
  float life;
  float maxLife;
  float size;
  float rotation;
  float angularVelocity;
  uint32_t color;
  uint16_t textureIndex;
  uint8_t flags;
  uint8_t generationId;
  ParticleEffectType effectType; // Effect type for this particle

  enum class RenderLayer : uint8_t { Background, World, Foreground } layer;

  // Flags bit definitions
  static constexpr uint8_t FLAG_ACTIVE = 1 << 0;
  static constexpr uint8_t FLAG_VISIBLE = 1 << 1;
  static constexpr uint8_t FLAG_GRAVITY = 1 << 2;
  static constexpr uint8_t FLAG_COLLISION = 1 << 3;
  static constexpr uint8_t FLAG_WEATHER = 1 << 4;
  static constexpr uint8_t FLAG_FADE_OUT = 1 << 5;

  UnifiedParticle()
      : position(0, 0), velocity(0, 0), acceleration(0, 0), life(0.0f),
        maxLife(1.0f), size(2.0f), rotation(0.0f), angularVelocity(0.0f),
        color(0xFFFFFFFF), textureIndex(0), flags(0), generationId(0),
        effectType(ParticleEffectType::Custom), layer(RenderLayer::World) {}

  bool isActive() const { return flags & FLAG_ACTIVE; }
  void setActive(bool active) {
    if (active)
      flags |= FLAG_ACTIVE;
    else
      flags &= ~FLAG_ACTIVE;
  }

  bool isVisible() const { return flags & FLAG_VISIBLE; }
  void setVisible(bool visible) {
    if (visible)
      flags |= FLAG_VISIBLE;
    else
      flags &= ~FLAG_VISIBLE;
  }

  bool isWeatherParticle() const { return flags & FLAG_WEATHER; }
  void setWeatherParticle(bool weather) {
    if (weather)
      flags |= FLAG_WEATHER;
    else
      flags &= ~FLAG_WEATHER;
  }

  bool isFadingOut() const { return flags & FLAG_FADE_OUT; }
  void setFadingOut(bool fading) {
    if (fading)
      flags |= FLAG_FADE_OUT;
    else
      flags &= ~FLAG_FADE_OUT;
  }

  float getLifeRatio() const { return maxLife > 0 ? life / maxLife : 0.0f; }
};

/**
 * @brief Particle effect definition combining emitter and behavior
 */
struct ParticleEffectDefinition {
  std::string name;
  ParticleEffectType type;
  ParticleEmitterConfig emitterConfig;
  std::vector<std::string> textureIDs; // Multiple textures for variety
  float intensityMultiplier{1.0f};     // Effect intensity scaling
  bool autoTriggerOnWeather{false};    // Auto-trigger on weather events
  UnifiedParticle::RenderLayer layer{
      UnifiedParticle::RenderLayer::World}; // Default render layer

  ParticleEffectDefinition() : type(ParticleEffectType::Custom) {}
  ParticleEffectDefinition(const std::string &n, ParticleEffectType t)
      : name(n), type(t) {}
};

/**
 * @brief Performance statistics for monitoring
 */
struct ParticlePerformanceStats {
  double totalUpdateTime{0.0};
  double totalRenderTime{0.0};
  uint64_t updateCount{0};
  uint64_t renderCount{0};
  size_t activeParticles{0};
  size_t maxParticles{0};
  double particlesPerSecond{0.0};

  void addUpdateSample(double timeMs, size_t particleCount) {
    totalUpdateTime += timeMs;
    updateCount++;
    activeParticles = particleCount;
    if (totalUpdateTime > 0) {
      particlesPerSecond =
          (activeParticles * updateCount * 1000.0) / totalUpdateTime;
    }
  }

  void addRenderSample(double timeMs) {
    totalRenderTime += timeMs;
    renderCount++;
  }

  void reset() {
    totalUpdateTime = 0.0;
    totalRenderTime = 0.0;
    updateCount = 0;
    renderCount = 0;
    activeParticles = 0;
    particlesPerSecond = 0.0;
  }
};

/**
 * @brief Ultra-high-performance ParticleManager
 */
class ParticleManager {
public:
  static ParticleManager &Instance() {
    static ParticleManager instance;
    return instance;
  }

  /**
   * @brief Initializes the ParticleManager and its internal systems
   * @return true if initialization successful, false otherwise
   */
  bool init();

  /**
   * @brief Checks if the Particle Manager has been initialized
   * @return true if initialized, false otherwise
   */
  bool isInitialized() const {
    return m_initialized.load(std::memory_order_acquire);
  }

  /**
   * @brief Cleans up all particle resources and marks manager as shut down
   */
  void clean();

  /**
   * @brief Prepares for state transition by safely cleaning up and resetting
   * the particle system
   * @details Stops all weather effects, cleans up inactive particles, resets
   * performance stats, and leaves the system ready for immediate reuse in the
   * new state. Call this during exit() in game states for a clean transition.
   */
  void prepareForStateTransition();

  /**
   * @brief Updates all active particles using high-performance batch processing
   *
   * PERFORMANCE FEATURES:
   * - Lock-free double buffering for zero contention
   * - Cache-efficient SoA layout for 3-4x better performance
   * - SIMD optimizations for physics calculations
   * - Frustum culling for off-screen particles
   * - Batch processing with threading support
   *
   * @param deltaTime Time elapsed since last update in seconds
   */
  void update(float deltaTime);

  /**
   * @brief Renders all visible particles using optimized batch rendering
   * @param renderer SDL renderer for drawing
   * @param cameraX Camera X offset for world-space rendering
   * @param cameraY Camera Y offset for world-space rendering
   */
  void render(SDL_Renderer *renderer, float cameraX = 0.0f,
              float cameraY = 0.0f);

  /**
   * @brief Renders only background particles (rain, snow) - call before
   * player/NPCs
   * @param renderer SDL renderer for drawing
   * @param cameraX Camera X offset for world-space rendering
   * @param cameraY Camera Y offset for world-space rendering
   */
  void renderBackground(SDL_Renderer *renderer, float cameraX = 0.0f,
                        float cameraY = 0.0f);

  /**
   * @brief Renders only foreground particles (fog) - call after player/NPCs
   * @param renderer SDL renderer for drawing
   * @param cameraX Camera X offset for world-space rendering
   * @param cameraY Camera Y offset for world-space rendering
   */
  void renderForeground(SDL_Renderer *renderer, float cameraX = 0.0f,
                        float cameraY = 0.0f);

  /**
   * @brief Checks if ParticleManager has been shut down
   * @return true if manager is shut down, false otherwise
   */
  bool isShutdown() const { return m_isShutdown; }

  // Effect Management
  /**
   * @brief Registers a particle effect definition for use
   * @param effectDef Effect definition to register
   * @return true if registration successful, false otherwise
   */
  bool registerEffect(const ParticleEffectDefinition &effectDef);

  /**
   * @brief Creates and plays a particle effect at specified position
   * @param effectType Type of the effect to play
   * @param position World position to play effect
   * @param intensity Effect intensity multiplier (0.0 to 2.0)
   * @return Effect ID for controlling the effect, or 0 if failed
   */
  uint32_t playEffect(ParticleEffectType effectType, const Vector2D &position,
                      float intensity = 1.0f);

  /**
   * @brief Stops a currently playing effect
   * @param effectId Effect ID returned from playEffect
   */
  void stopEffect(uint32_t effectId);

  /**
   * @brief Sets the intensity of a playing effect
   * @param effectId Effect ID returned from playEffect
   * @param intensity New intensity value (0.0 to 2.0)
   */
  void setEffectIntensity(uint32_t effectId, float intensity);

  /**
   * @brief Checks if an effect is currently playing
   * @param effectId Effect ID to check
   * @return true if effect is playing, false otherwise
   */
  bool isEffectPlaying(uint32_t effectId) const;

  // Independent Effect Management
  /**
   * @brief Creates and plays an independent particle effect that persists until
   * manually stopped
   * @param effectType Type of the effect to play
   * @param position World position to play effect
   * @param intensity Effect intensity multiplier (0.0 to 2.0)
   * @param duration Effect duration in seconds (-1 for infinite)
   * @param groupTag Optional group tag for bulk operations
   * @param soundEffect Optional sound effect name for SoundManager
   * @return Effect ID for controlling the effect, or 0 if failed
   */
  uint32_t playIndependentEffect(ParticleEffectType effectType,
                                 const Vector2D &position,
                                 float intensity = 1.0f, float duration = -1.0f,
                                 const std::string &groupTag = "",
                                 const std::string &soundEffect = "");

  /**
   * @brief Stops an independent effect
   * @param effectId Effect ID returned from playIndependentEffect
   */
  void stopIndependentEffect(uint32_t effectId);

  /**
   * @brief Stops all independent effects
   */
  void stopAllIndependentEffects();

  /**
   * @brief Stops all independent effects with a specific group tag
   * @param groupTag Group tag to stop
   */
  void stopIndependentEffectsByGroup(const std::string &groupTag);

  /**
   * @brief Pauses/unpauses an independent effect
   * @param effectId Effect ID to pause/unpause
   * @param paused Whether to pause the effect
   */
  void pauseIndependentEffect(uint32_t effectId, bool paused);

  /**
   * @brief Pauses/unpauses all independent effects
   * @param paused Whether to pause all independent effects
   */
  void pauseAllIndependentEffects(bool paused);

  /**
   * @brief Pauses/unpauses all independent effects with a specific group tag
   * @param groupTag Group tag to pause/unpause
   * @param paused Whether to pause the effects
   */
  void pauseIndependentEffectsByGroup(const std::string &groupTag, bool paused);

  /**
   * @brief Checks if an effect is an independent effect
   * @param effectId Effect ID to check
   * @return true if effect is independent, false otherwise
   */
  bool isIndependentEffect(uint32_t effectId) const;

  /**
   * @brief Gets all active independent effect IDs
   * @return Vector of active independent effect IDs
   */
  std::vector<uint32_t> getActiveIndependentEffects() const;

  /**
   * @brief Gets all active independent effect IDs with a specific group tag
   * @param groupTag Group tag to filter by
   * @return Vector of active independent effect IDs
   */
  std::vector<uint32_t>
  getActiveIndependentEffectsByGroup(const std::string &groupTag) const;

  // Effect Toggles for EventManager
  /**
   * @brief Toggles the Fire effect on/off for EventManager
   */
  void toggleFireEffect();

  /**
   * @brief Toggles the Smoke effect on/off for EventManager
   */
  void toggleSmokeEffect();

  /**
   * @brief Toggles the Sparks effect on/off for EventManager
   */
  void toggleSparksEffect();

  // Weather Integration (EventManager callbacks)
  /**
   * @brief Triggers weather particle effects (called by EventManager)
   * @param weatherType Weather type string ("Rainy", "Snowy", etc.)
   * @param intensity Weather intensity (0.0 to 1.0)
   * @param transitionTime Time to transition to new intensity
   */
  void triggerWeatherEffect(const std::string &weatherType, float intensity,
                            float transitionTime = 2.0f);

  /**
   * @brief Triggers weather particle effects using enum type
   * @param effectType Weather effect type
   * @param intensity Weather intensity (0.0 to 1.0)
   * @param transitionTime Time to transition to new intensity
   */
  void triggerWeatherEffect(ParticleEffectType effectType, float intensity,
                            float transitionTime = 2.0f);

  /**
   * @brief Stops all weather effects
   * @param transitionTime Time to fade out effects
   */
  void stopWeatherEffects(float transitionTime = 2.0f);

  /**
   * @brief Clears all weather particles of a specific generation
   * @param generationId Generation ID to clear (0 = all weather particles)
   * @param fadeTime Time to fade out particles before removal
   */
  void clearWeatherGeneration(uint8_t generationId = 0, float fadeTime = 0.5f);

  // Global Controls
  /**
   * @brief Sets global pause state for all particles
   * @param paused Whether to pause particle updates
   */
  void setGlobalPause(bool paused);

  /**
   * @brief Gets global pause state
   * @return true if globally paused, false otherwise
   */
  bool isGloballyPaused() const;

  /**
   * @brief Sets global particle visibility
   * @param visible Whether particles should be rendered
   */
  void setGlobalVisibility(bool visible);

  /**
   * @brief Gets global visibility state
   * @return true if particles are visible, false otherwise
   */
  bool isGloballyVisible() const;

  /**
   * @brief Sets the camera viewport for frustum culling
   * @param x Camera X position
   * @param y Camera Y position
   * @param width Viewport width
   * @param height Viewport height
   */
  void setCameraViewport(float x, float y, float width, float height);

  // Threading and Performance
  /**
   * @brief Configures threading behavior
   * @param useThreading Whether to use multi-threading
   * @param maxThreads Maximum threads to use (0 = auto-detect)
   */
  void configureThreading(bool useThreading, unsigned int maxThreads = 0);

  /**
   * @brief Sets the threading threshold (minimum particles to use threading)
   * @param threshold Particle count threshold
   */
  void setThreadingThreshold(size_t threshold);

  /**
   * @brief Enables WorkerBudget-aware threading with intelligent resource
   * allocation
   * @param enable Whether to enable WorkerBudget integration
   *
   * WorkerBudget integration provides coordinated thread allocation across all
   * engine subsystems (AI, Particles, Events, etc.) following the engine's
   * architectural patterns. When enabled:
   *
   * - Uses HammerEngine::calculateWorkerBudget() for fair thread distribution
   * - Dynamically adjusts worker count based on workload and system pressure
   * - Submits tasks via ThreadSystem::enqueueTaskWithResult() for proper
   * scheduling
   * - Monitors ThreadSystem queue pressure to prevent resource contention
   * - Automatically falls back to single-threaded mode when appropriate
   *
   * This is the recommended threading mode for production use as it ensures
   * the particle system cooperates well with other engine subsystems.
   */
  void enableWorkerBudgetThreading(bool enable);

  /**
   * @brief Updates particles using WorkerBudget-optimized batch processing
   * @param deltaTime Time elapsed since last update
   * @param particleCount Current number of active particles
   *
   * This method provides the WorkerBudget-aware update path, which:
   * - Calculates optimal thread allocation using WorkerBudget system
   * - Adjusts batch sizes based on ThreadSystem queue pressure
   * - Falls back to single-threaded mode when WorkerBudget is disabled
   * - Respects the threading threshold for efficient resource usage
   *
   * Called automatically by update() when WorkerBudget threading is enabled.
   */
  void updateWithWorkerBudget(float deltaTime, size_t particleCount);

  /**
   * @brief Gets current performance statistics
   * @return Performance statistics structure
   */
  ParticlePerformanceStats getPerformanceStats() const;

  /**
   * @brief Resets performance statistics
   */
  void resetPerformanceStats();

  /**
   * @brief Gets the current number of active particles
   * @return Number of active particles
   */
  size_t getActiveParticleCount() const;

  /**
   * @brief Counts the actual number of active particles in storage
   * @return Number of active particles
   */
  size_t countActiveParticles() const;

  /**
   * @brief Gets the maximum particle capacity
   * @return Maximum number of particles
   */
  size_t getMaxParticleCapacity() const;

  // Memory Management
  /**
   * @brief Compacts particle storage to optimize memory usage
   */
  void compactParticleStorage();

  /**
   * @brief Performs lightweight storage compaction if needed (based on
   * thresholds)
   */
  void compactParticleStorageIfNeeded();

  /**
   * @brief Sets the maximum number of particles
   * @param maxParticles Maximum particle count
   */
  void setMaxParticles(size_t maxParticles);

  // Built-in Effect Presets
  /**
   * @brief Registers all built-in weather effect presets
   */
  void registerBuiltInEffects();

private:
  ParticleManager() = default;
  ~ParticleManager() {
    if (!m_isShutdown) {
      clean();
    }
  }
  ParticleManager(const ParticleManager &) = delete;
  ParticleManager &operator=(const ParticleManager &) = delete;

  // Effect instance tracking - effects only emit particles, don't own them
  struct EffectInstance {
    uint32_t id;
    ParticleEffectType effectType;
    Vector2D position;
    float intensity;
    float currentIntensity; // For transitions
    float targetIntensity;  // Target during transitions
    float transitionSpeed;  // Transition rate
    float emissionTimer;
    float durationTimer;
    float maxDuration; // Maximum duration (-1 for infinite)
    bool active;
    bool paused; // Independent pause state
    bool isWeatherEffect;
    bool isIndependentEffect;    // Independent effects (not weather)
    std::string groupTag;        // For bulk operations
    std::string soundEffect;     // Associated sound effect
    uint8_t currentGenerationId; // Current generation for new particles

    EffectInstance()
        : id(0), effectType(ParticleEffectType::Custom), position(0, 0),
          intensity(1.0f), currentIntensity(0.0f), targetIntensity(1.0f),
          transitionSpeed(1.0f), emissionTimer(0.0f), durationTimer(0.0f),
          maxDuration(-1.0f), active(false), paused(false),
          isWeatherEffect(false), isIndependentEffect(false), groupTag(""),
          soundEffect(""), currentGenerationId(0) {}
  };

  // New particle request structure for lock-free creation
  struct NewParticleRequest {
    Vector2D position;
    Vector2D velocity;
    Vector2D acceleration;
    float life;
    float size;
    uint32_t color;
    uint16_t textureIndex;
    ParticleBlendMode blendMode;
    ParticleEffectType effectType;
    uint8_t flags;
  };

  // Lock-free high-performance storage with double buffering
  struct alignas(64) LockFreeParticleStorage {
    // SoA data layout for cache-friendly updates
    struct ParticleSoA {
        std::vector<Vector2D> positions;
        std::vector<Vector2D> velocities;
        std::vector<Vector2D> accelerations;
        std::vector<float> lives;
        std::vector<float> maxLives;
        std::vector<float> sizes;
        std::vector<float> rotations;
        std::vector<float> angularVelocities;
        std::vector<uint32_t> colors;
        std::vector<uint16_t> textureIndices;
        std::vector<uint8_t> flags;
        std::vector<uint8_t> generationIds;
        std::vector<ParticleEffectType> effectTypes;
        std::vector<UnifiedParticle::RenderLayer> layers;

        void resize(size_t newSize) {
            positions.resize(newSize);
            velocities.resize(newSize);
            accelerations.resize(newSize);
            lives.resize(newSize);
            maxLives.resize(newSize);
            sizes.resize(newSize);
            rotations.resize(newSize);
            angularVelocities.resize(newSize);
            colors.resize(newSize);
            textureIndices.resize(newSize);
            flags.resize(newSize);
            generationIds.resize(newSize);
            effectTypes.resize(newSize);
            layers.resize(newSize);
        }

        void reserve(size_t newCapacity) {
            positions.reserve(newCapacity);
            velocities.reserve(newCapacity);
            accelerations.reserve(newCapacity);
            lives.reserve(newCapacity);
            maxLives.reserve(newCapacity);
            sizes.reserve(newCapacity);
            rotations.reserve(newCapacity);
            angularVelocities.reserve(newCapacity);
            colors.reserve(newCapacity);
            textureIndices.reserve(newCapacity);
            flags.reserve(newCapacity);
            generationIds.reserve(newCapacity);
            effectTypes.reserve(newCapacity);
            layers.reserve(newCapacity);
        }

        void push_back(const UnifiedParticle& p) {
            positions.push_back(p.position);
            velocities.push_back(p.velocity);
            accelerations.push_back(p.acceleration);
            lives.push_back(p.life);
            maxLives.push_back(p.maxLife);
            sizes.push_back(p.size);
            rotations.push_back(p.rotation);
            angularVelocities.push_back(p.angularVelocity);
            colors.push_back(p.color);
            textureIndices.push_back(p.textureIndex);
            flags.push_back(p.flags);
            generationIds.push_back(p.generationId);
            effectTypes.push_back(p.effectType);
            layers.push_back(p.layer);
        }

        void clear() {
            positions.clear();
            velocities.clear();
            accelerations.clear();
            lives.clear();
            maxLives.clear();
            sizes.clear();
            rotations.clear();
            angularVelocities.clear();
            colors.clear();
            textureIndices.clear();
            flags.clear();
            generationIds.clear();
            effectTypes.clear();
            layers.clear();
        }

        size_t size() const {
            return positions.size();
        }

        bool empty() const {
            return positions.empty();
        }
    };

    // Double-buffered particle arrays for lock-free updates
    ParticleSoA particles[2];
    std::atomic<size_t> activeBuffer{0};  // Which buffer is currently active
    std::atomic<size_t> particleCount{0}; // Current particle count
    std::atomic<size_t> writeHead{0};     // Next write position
    std::atomic<size_t> capacity{0};      // Current capacity

    // Lock-free ring buffer for new particle requests
    struct alignas(32) ParticleCreationRequest {
      Vector2D position;
      Vector2D velocity;
      uint32_t color;
      float life;
      float size;
      uint8_t flags;
      uint8_t generationId;
      ParticleEffectType effectType;
      std::atomic<bool> ready{false};
    };

    static constexpr size_t CREATION_RING_SIZE = 4096; // Must be power of 2
    std::array<ParticleCreationRequest, CREATION_RING_SIZE> creationRing;
    std::atomic<size_t> creationHead{0};
    std::atomic<size_t> creationTail{0};

    // Memory reclamation using epochs
    std::atomic<uint64_t> currentEpoch{0};
    std::atomic<uint64_t> safeEpoch{0};

    LockFreeParticleStorage() : creationRing{} {
      // Pre-allocate both buffers
      particles[0].reserve(DEFAULT_MAX_PARTICLES);
      particles[1].reserve(DEFAULT_MAX_PARTICLES);
      capacity.store(DEFAULT_MAX_PARTICLES, std::memory_order_relaxed);
    }

    // Lock-free particle creation
    bool tryCreateParticle(const Vector2D &pos, const Vector2D &vel,
                           uint32_t color, float life, float size,
                           uint8_t flags, uint8_t genId,
                           ParticleEffectType effectType) {
      size_t head = creationHead.load(std::memory_order_acquire);
      size_t next = (head + 1) & (CREATION_RING_SIZE - 1);

      if (next == creationTail.load(std::memory_order_acquire)) {
        return false; // Ring buffer full
      }

      auto &req = creationRing[head];
      req.position = pos;
      req.velocity = vel;
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

    // Process creation requests (called from update thread)
    void processCreationRequests() {
      size_t tail = creationTail.load(std::memory_order_acquire);
      size_t head = creationHead.load(std::memory_order_acquire);

      while (tail != head) {
        auto &req = creationRing[tail];
        if (req.ready.load(std::memory_order_acquire)) {
          // Add particle to active buffer
          size_t activeIdx = activeBuffer.load(std::memory_order_relaxed);
          auto &activeParticles = particles[activeIdx];

          if (activeParticles.size() <
              capacity.load(std::memory_order_relaxed)) {
            UnifiedParticle particle;
            particle.position = req.position;
            particle.velocity = req.velocity;
            particle.color = req.color;
            particle.life = req.life;
            particle.maxLife = req.life;
            particle.size = req.size;
            particle.flags = req.flags;
            particle.generationId = req.generationId;
            particle.effectType = req.effectType;

            activeParticles.push_back(particle);
            particleCount.fetch_add(1, std::memory_order_acq_rel);
          }

          req.ready.store(false, std::memory_order_release);
        }
        tail = (tail + 1) & (CREATION_RING_SIZE - 1);
      }

      // Particle processing tracking (debug removed for cleaner output)

      creationTail.store(tail, std::memory_order_release);
    }

    // Get read-only access to particles
    const ParticleSoA &getParticlesForRead() const {
      size_t activeIdx = activeBuffer.load(std::memory_order_acquire);
      return particles[activeIdx];
    }

    // Get writable access to particles (for updates)
    ParticleSoA &getCurrentBuffer() {
      size_t activeIdx = activeBuffer.load(std::memory_order_relaxed);
      return particles[activeIdx];
    }

    // Check if compaction is needed
    bool needsCompaction() const {
        const auto& activeParticles = getParticlesForRead();
        if (activeParticles.empty()) return false;

        size_t inactiveCount = 0;
        for (const auto& flag : activeParticles.flags) {
            if (!(flag & UnifiedParticle::FLAG_ACTIVE)) {
                inactiveCount++;
            }
        }
        return inactiveCount > activeParticles.size() * 0.5;
    }


    // Submit new particle (lock-free)
    bool submitNewParticle(const NewParticleRequest &request) {
      return tryCreateParticle(request.position, request.velocity,
                               request.color, request.life, request.size,
                               UnifiedParticle::FLAG_ACTIVE |
                                   UnifiedParticle::FLAG_VISIBLE,
                               0, request.effectType);
    }

    // Swap buffers for lock-free updates
    void swapBuffers() {
      size_t current = activeBuffer.load(std::memory_order_relaxed);
      size_t next = 1 - current;

      // Copy active particles to next buffer
      particles[next] = particles[current];

      // Atomic swap
      activeBuffer.store(next, std::memory_order_release);

      // Advance epoch for memory reclamation
      currentEpoch.fetch_add(1, std::memory_order_acq_rel);
    }
  };

  // Core storage - now lock-free
  LockFreeParticleStorage m_storage;
  std::unordered_map<ParticleEffectType, ParticleEffectDefinition>
      m_effectDefinitions;
  std::vector<EffectInstance> m_effectInstances;
  std::unordered_map<uint32_t, size_t> m_effectIdToIndex;

  // Texture management
  std::unordered_map<std::string, uint16_t> m_textureIndices;
  std::vector<std::string> m_textureIDs;

  // Performance tracking
  ParticlePerformanceStats m_performanceStats;

  // Threading and synchronization
  std::atomic<bool> m_initialized{false};
  std::atomic<bool> m_isShutdown{false};
  std::atomic<bool> m_globallyPaused{false};
  std::atomic<bool> m_globallyVisible{true};
  std::atomic<bool> m_useThreading{true};
  std::atomic<bool> m_useWorkerBudget{true};
  std::atomic<size_t> m_threadingThreshold{750};
  unsigned int m_maxThreads{0};

  // Frame counter for periodic maintenance (like AIManager)
  std::atomic<uint64_t> m_frameCounter{0};

  // Camera and culling
  struct CameraViewport {
    float x{0}, y{0}, width{1920}, height{1080};
    float margin{100}; // Extra margin for smooth culling
  } m_viewport;

  // Lock-free synchronization - no mutexes needed for particles
  mutable std::shared_mutex
      m_effectsMutex;              // Only for effect definitions (rare writes)
  mutable std::mutex m_statsMutex; // Only for performance stats
  mutable std::mutex m_weatherMutex; // For weather effect changes

  // Update serialization for single-threaded update logic
  static std::mutex
      updateMutex; // Static to prevent multiple update() calls system-wide

  // Constants for optimization
  static constexpr size_t CACHE_LINE_SIZE = 64;
  static constexpr size_t BATCH_SIZE =
      1024; // Increased for better WorkerBudget efficiency
  static constexpr size_t DEFAULT_MAX_PARTICLES =
      100000; // Increased for modern performance
  static constexpr float MIN_VISIBLE_SIZE = 0.5f;

  // Performance optimization structures
  struct alignas(32) BatchUpdateData {
    float deltaTime;
    size_t startIndex;
    size_t endIndex;
    size_t processedCount;
  };

  // Pre-calculated lookup tables for performance
  struct ParticleOptimizationData {
    // Pre-calculated color variations
    std::array<uint32_t, 8> fireColors{{0xFF4500FF, 0xFF6500FF, 0xFFFF00FF,
                                        0xFF8C00FF, 0xFFA500FF, 0xFF0000FF,
                                        0xFFD700FF, 0xFF7F00FF}};
    std::array<uint32_t, 8> smokeColors{{0x404040FF, 0x606060FF, 0x808080FF,
                                         0x202020FF, 0x4A4A4AFF, 0x505050FF,
                                         0x707070FF, 0x303030FF}};
    std::array<uint32_t, 4> sparkColors{
        {0xFFFF00FF, 0xFF8C00FF, 0xFFD700FF, 0xFFA500FF}};

    // Fast random state for each thread
    mutable std::atomic<uint32_t> fastRandSeed{12345};

    ParticleOptimizationData() = default;
  } m_optimizationData;

  // Effect ID generation
  std::atomic<uint32_t> m_nextEffectId{1};

  // Built-in effect state tracking
  uint32_t m_fireEffectId{0};
  uint32_t m_smokeEffectId{0};
  uint32_t m_sparksEffectId{0};
  bool m_fireActive{false};
  bool m_smokeActive{false};
  bool m_sparksActive{false};

  // WorkerBudget threading state
  std::atomic<bool> m_useWorkerBudgetThreading{false};

  // Helper methods
  uint32_t generateEffectId();
  size_t allocateParticle();
  void releaseParticle(size_t index);
  void updateParticleBatch(size_t start, size_t end, float deltaTime);
  void updateParticleBatchOptimized(size_t start, size_t end, float deltaTime);
  void renderParticleBatch(SDL_Renderer *renderer, size_t start, size_t end,
                           float cameraX, float cameraY);
  void emitParticles(EffectInstance &effect,
                     const ParticleEffectDefinition &definition,
                     float deltaTime);
  void updateEffectInstance(EffectInstance &effect, float deltaTime);
  void updateParticle(ParticleData &particle, float deltaTime);
  bool isParticleVisible(const ParticleData &particle, float cameraX,
                         float cameraY) const;
  void swapBuffers();
  void cleanupInactiveParticles();
  void updateEffectInstances(float deltaTime);
  void updateParticlesThreaded(float deltaTime, size_t activeParticleCount);
  void updateParticlesSingleThreaded(float deltaTime,
                                     size_t activeParticleCount);
  void updateParticleRange(LockFreeParticleStorage::ParticleSoA &particles,
                           size_t startIdx, size_t endIdx, float deltaTime);
  void updateParticleWithColdData(ParticleData &particle,
                                  const ParticleColdData &coldData,
                                  float deltaTime);
  void updateUnifiedParticle(UnifiedParticle &particle, float deltaTime);
  void createParticleForEffect(const ParticleEffectDefinition &effectDef,
                               const Vector2D &position,
                               bool isWeatherEffect = false);
  uint16_t getTextureIndex(const std::string &textureID);
  uint32_t interpolateColor(uint32_t color1, uint32_t color2, float factor);
  void recordPerformance(bool isRender, double timeMs, size_t particleCount);
  uint64_t getCurrentTimeNanos() const;

  // Weather type conversion helpers
  ParticleEffectType weatherStringToEnum(const std::string &weatherType,
                                         float intensity) const;
  std::string effectTypeToString(ParticleEffectType type) const;

  // Built-in effect creation helpers
  ParticleEffectDefinition createRainEffect();
  ParticleEffectDefinition createHeavyRainEffect();
  ParticleEffectDefinition createSnowEffect();
  ParticleEffectDefinition createHeavySnowEffect();
  ParticleEffectDefinition createFogEffect();
  ParticleEffectDefinition createCloudyEffect();
  ParticleEffectDefinition createFireEffect();
  ParticleEffectDefinition createSmokeEffect();
  ParticleEffectDefinition createSparksEffect();
  ParticleEffectDefinition createMagicEffect();
};

#endif // PARTICLE_MANAGER_HPP
