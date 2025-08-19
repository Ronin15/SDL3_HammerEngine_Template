/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef EVENT_MANAGER_HPP
#define EVENT_MANAGER_HPP

/**
 * @file EventManager.hpp
 * @brief High-performance event manager optimized for speed
 *
 * This is a complete redesign of the EventManager for maximum performance:
 * - Type-indexed storage instead of string lookups
 * - Data-oriented design for cache efficiency
 * - Batch processing like AIManager
 * - Smart pointer usage throughout
 * - Direct function calls to minimize overhead
 */

#include "utils/ResourceHandle.hpp"
#include "utils/Vector2D.hpp"
#include "events/EventTypeId.hpp"
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <queue>
#include <deque>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

// Forward declarations
class Event;
class WeatherEvent;
class SceneChangeEvent;
class NPCSpawnEvent;
class ResourceChangeEvent;
class WorldEvent;
class CameraEvent;
class HarvestResourceEvent;
class EventFactory;
class Entity;

using EventPtr = std::shared_ptr<Event>;
using EventWeakPtr = std::weak_ptr<Event>;
using EntityPtr = std::shared_ptr<Entity>;

// EventTypeId now lives in include/events/EventTypeId.hpp

/**
 * @brief Cache-friendly event data structure (data-oriented design)
 * Optimized for 32-byte cache line alignment
 */
struct alignas(32) EventData {
  EventPtr event;       // Smart pointer to event
  std::string name;     // Stable name for mapping/compaction
  uint32_t flags;       // Active, dirty, etc.
  uint32_t priority;    // For priority-based processing
  EventTypeId typeId;   // Type for fast dispatch
  std::function<void()> onConsumed; // Optional callback after dispatch

  // Flags bit definitions
  static constexpr uint32_t FLAG_ACTIVE = 1 << 0;
  static constexpr uint32_t FLAG_DIRTY = 1 << 1;
  static constexpr uint32_t FLAG_PENDING_REMOVAL = 1 << 2;

  EventData()
      : event(nullptr), name(), flags(0), priority(0), typeId(EventTypeId::Custom) {}

  bool isActive() const { return flags & FLAG_ACTIVE; }
  void setActive(bool active) {
    if (active)
      flags |= FLAG_ACTIVE;
    else
      flags &= ~FLAG_ACTIVE;
  }

  bool isDirty() const { return flags & FLAG_DIRTY; }
  void setDirty(bool dirty) {
    if (dirty)
      flags |= FLAG_DIRTY;
    else
      flags &= ~FLAG_DIRTY;
  }
};

/**
 * @brief Fast event handler function type
 */
using FastEventHandler = std::function<void(const EventData &)>;

/**
 * @brief Event pool for memory-efficient event management
 */
template <typename EventType> class EventPool {
public:
  using Creator = std::function<std::shared_ptr<EventType>()>;
  explicit EventPool(Creator creator = Creator{}) : m_creator(std::move(creator)) {}

  std::shared_ptr<EventType> acquire() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_available.empty()) {
      auto event = m_available.front();
      m_available.pop();
      return event;
    }

    // Create new event via creator if provided
    if (m_creator) {
      auto event = m_creator();
      m_allEvents.push_back(event);
      return event;
    }
    // No creator set; caller must handle nullptr
    return nullptr;
  }

  void release(std::shared_ptr<EventType> event) {
    if (!event)
      return;

    std::lock_guard<std::mutex> lock(m_mutex);
    event->reset(); // Reset event state
    m_available.push(event);
  }

  void clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_available.empty()) {
      m_available.pop();
    }
    m_allEvents.clear();
  }

  void setCreator(Creator creator) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_creator = std::move(creator);
  }

private:
  std::vector<std::shared_ptr<EventType>> m_allEvents;
  std::queue<std::shared_ptr<EventType>> m_available;
  std::mutex m_mutex;
  Creator m_creator;
};

/**
 * @brief Performance statistics for monitoring
 */
struct PerformanceStats {
  double totalTime{0.0};
  uint64_t callCount{0};
  double avgTime{0.0};
  double minTime{std::numeric_limits<double>::max()};
  double maxTime{0.0};

  void addSample(double time) {
    totalTime += time;
    callCount++;
    avgTime = totalTime / callCount;
    minTime = std::min(minTime, time);
    maxTime = std::max(maxTime, time);
  }

  void reset() {
    totalTime = 0.0;
    callCount = 0;
    avgTime = 0.0;
    minTime = std::numeric_limits<double>::max();
    maxTime = 0.0;
  }
};

/**
 * @brief Ultra-high-performance EventManager
 */
class EventManager {
public:
  static EventManager &Instance() {
    static EventManager instance;
    return instance;
  }

  // Dispatch control for handler execution
  enum class DispatchMode : uint8_t { Deferred = 0, Immediate = 1 };

  /**
   * @brief Initializes the EventManager and its internal systems
   * @return true if initialization successful, false otherwise
   */
  bool init();

  /**
   * @brief Checks if the Event Manager has been initialized
   * @return true if initialized, false otherwise
   */
  bool isInitialized() const {
    return m_initialized.load(std::memory_order_acquire);
  }

  /**
   * @brief Cleans up all event resources
   */
  void clean();

  /**
   * @brief Prepares for state transition by safely cleaning up events and
   * handlers
   * @details Call this before exit() in game states to avoid issues
   */
  void prepareForStateTransition();

  /**
   * @brief Updates all active events and processes event systems
   */
  void update();

  /**
   * @brief Checks if EventManager has been shut down
   * @return true if manager is shut down, false otherwise
   */
  bool isShutdown() const { return m_isShutdown; }

  /**
   * @brief Registers a generic event with the event system
   * @param name Unique name identifier for the event
   * @param event Shared pointer to the event to register
   * @return true if registration successful, false otherwise
   */
  bool registerEvent(const std::string &name, EventPtr event);

  /**
   * @brief Registers a weather event with the event system
   * @param name Unique name identifier for the weather event
   * @param event Shared pointer to the weather event to register
   * @return true if registration successful, false otherwise
   */
  bool registerWeatherEvent(const std::string &name,
                            std::shared_ptr<WeatherEvent> event);

  /**
   * @brief Registers a scene change event with the event system
   * @param name Unique name identifier for the scene change event
   * @param event Shared pointer to the scene change event to register
   * @return true if registration successful, false otherwise
   */
  bool registerSceneChangeEvent(const std::string &name,
                                std::shared_ptr<SceneChangeEvent> event);

  /**
   * @brief Registers an NPC spawn event with the event system
   * @param name Unique name identifier for the NPC spawn event
   * @param event Shared pointer to the NPC spawn event to register
   * @return true if registration successful, false otherwise
   */
  bool registerNPCSpawnEvent(const std::string &name,
                             std::shared_ptr<NPCSpawnEvent> event);

  /**
   * @brief Registers a resource change event with the event system
   * @param name Unique name identifier for the resource change event
   * @param event Shared pointer to the resource change event to register
   * @return true if registration successful, false otherwise
   */
  bool registerResourceChangeEvent(const std::string &name,
                                   std::shared_ptr<ResourceChangeEvent> event);

  /**
   * @brief Registers a world event with the event system
   * @param name Unique name identifier for the world event
   * @param event Shared pointer to the world event to register
   * @return true if registration successful, false otherwise
   */
  bool registerWorldEvent(const std::string &name,
                         std::shared_ptr<WorldEvent> event);

  /**
   * @brief Registers a camera event with the event system
   * @param name Unique name identifier for the camera event
   * @param event Shared pointer to the camera event to register
   * @return true if registration successful, false otherwise
   */
  bool registerCameraEvent(const std::string &name,
                          std::shared_ptr<CameraEvent> event);

  /**
   * @brief Retrieves an event by its name
   * @param name Name of the event to retrieve
   * @return Shared pointer to the event, or nullptr if not found
   */
  EventPtr getEvent(const std::string &name) const;

  /**
   * @brief Retrieves all events of a specific type by type ID
   * @param typeId Event type identifier
   * @return Vector of shared pointers to events of the specified type
   */
  std::vector<EventPtr> getEventsByType(EventTypeId typeId) const;

  /**
   * @brief Retrieves all events of a specific type by type name
   * @param typeName String name of the event type
   * @return Vector of shared pointers to events of the specified type
   */
  std::vector<EventPtr> getEventsByType(const std::string &typeName) const;

  /**
   * @brief Sets the active state of an event
   * @param name Name of the event to modify
   * @param active New active state for the event
   * @return true if state was changed successfully, false otherwise
   */
  bool setEventActive(const std::string &name, bool active);

  /**
   * @brief Checks if an event is currently active
   * @param name Name of the event to check
   * @return true if event is active, false if inactive or not found
   */
  bool isEventActive(const std::string &name) const;

  /**
   * @brief Removes an event from the event system
   * @param name Name of the event to remove
   * @return true if event was removed successfully, false otherwise
   */
  bool removeEvent(const std::string &name);

  /**
   * @brief Checks if an event is registered in the system
   * @param name Name of the event to check
   * @return true if event exists, false otherwise
   */
  bool hasEvent(const std::string &name) const;

  // Fast execution methods
  bool executeEvent(const std::string &eventName) const;
  int executeEventsByType(EventTypeId typeId) const;
  int executeEventsByType(const std::string &eventType) const;

  // Handler registration (type-safe)
  void registerHandler(EventTypeId typeId, FastEventHandler handler);
  void removeHandlers(EventTypeId typeId);
  void clearAllHandlers();
  size_t getHandlerCount(EventTypeId typeId) const;
  // Per-name handler management
  void removeNameHandlers(const std::string& name);

  // Token-based handler management (extensible removal)
  struct HandlerToken { EventTypeId typeId; uint64_t id; bool forName{false}; std::string name; };
  HandlerToken registerHandlerWithToken(EventTypeId typeId, FastEventHandler handler);
  HandlerToken registerHandlerForName(const std::string& name, FastEventHandler handler);
  bool removeHandler(const HandlerToken& token);

  // Batch processing (AIManager-style)
  void updateWeatherEvents();
  void updateSceneChangeEvents();
  void updateNPCSpawnEvents();
  void updateResourceChangeEvents();
  void updateWorldEvents();
  void updateCameraEvents();
  void updateHarvestEvents();
  void updateCustomEvents();

  // Threading control
  void enableThreading(bool enable) { m_threadingEnabled.store(enable); }
  bool isThreadingEnabled() const { return m_threadingEnabled.load(); }
  void setThreadingThreshold(size_t threshold) {
    m_threadingThreshold = threshold;
  }

  // High-level convenience methods
  bool changeWeather(const std::string &weatherType,
                     float transitionTime = 5.0f,
                     DispatchMode mode = DispatchMode::Deferred) const;
  bool changeScene(const std::string &sceneId,
                   const std::string &transitionType = "fade",
                   float transitionTime = 1.0f,
                   DispatchMode mode = DispatchMode::Deferred) const;
  bool spawnNPC(const std::string &npcType, float x, float y,
                DispatchMode mode = DispatchMode::Deferred) const;

  // Particle effect trigger (stateless, no registration required)
  bool triggerParticleEffect(const std::string &effectName, float x, float y,
                             float intensity = 1.0f, float duration = -1.0f,
                             const std::string &groupTag = "",
                             DispatchMode mode = DispatchMode::Deferred) const;
  bool triggerParticleEffect(const std::string &effectName,
                             const Vector2D &position,
                             float intensity = 1.0f, float duration = -1.0f,
                             const std::string &groupTag = "",
                             DispatchMode mode = DispatchMode::Deferred) const;

  // Event creation convenience methods (create and register in one call using
  // EventFactory)
  bool createWeatherEvent(const std::string &name,
                          const std::string &weatherType,
                          float intensity = 1.0f, float transitionTime = 5.0f);
  bool createSceneChangeEvent(const std::string &name,
                              const std::string &targetScene,
                              const std::string &transitionType = "fade",
                              float transitionTime = 1.0f);
  bool createNPCSpawnEvent(const std::string &name, const std::string &npcType,
                           int count = 1, float spawnRadius = 0.0f);

  // Resource change convenience methods
  bool createResourceChangeEvent(const std::string &name, EntityPtr owner,
                                 HammerEngine::ResourceHandle resourceHandle,
                                 int oldQuantity, int newQuantity,
                                 const std::string &changeReason = "");

  // Particle effect convenience methods
  bool createParticleEffectEvent(const std::string &name,
                                 const std::string &effectName, float x,
                                 float y, float intensity = 1.0f,
                                 float duration = -1.0f,
                                 const std::string &groupTag = "");
  bool createParticleEffectEvent(const std::string &name,
                                 const std::string &effectName,
                                 const Vector2D &position,
                                 float intensity = 1.0f, float duration = -1.0f,
                                 const std::string &groupTag = "");

  // World event convenience methods
  bool createWorldLoadedEvent(const std::string &name, const std::string &worldId,
                             int width, int height);
  bool createWorldUnloadedEvent(const std::string &name, const std::string &worldId);
  bool createTileChangedEvent(const std::string &name, int x, int y,
                             const std::string &changeType);
  bool createWorldGeneratedEvent(const std::string &name, const std::string &worldId,
                                int width, int height, float generationTime);

  // Camera event convenience methods
  bool createCameraMovedEvent(const std::string &name, const Vector2D &newPos,
                             const Vector2D &oldPos);
  bool createCameraModeChangedEvent(const std::string &name, int newMode, int oldMode);
  bool createCameraShakeEvent(const std::string &name, float duration, float intensity);

  // Alternative trigger methods (aliases for compatibility)
  bool triggerWeatherChange(const std::string &weatherType,
                            float transitionTime = 5.0f) const {
    return changeWeather(weatherType, transitionTime);
  }
  bool triggerSceneChange(const std::string &sceneId,
                          const std::string &transitionType = "fade",
                          float transitionTime = 1.0f) const {
    return changeScene(sceneId, transitionType, transitionTime);
  }
  bool triggerNPCSpawn(const std::string &npcType, float x, float y) const {
    return spawnNPC(npcType, x, y);
  }

  // Resource change convenience method
  bool triggerResourceChange(EntityPtr owner,
                             HammerEngine::ResourceHandle resourceHandle,
                             int oldQuantity, int newQuantity,
                             const std::string &changeReason = "",
                             DispatchMode mode = DispatchMode::Deferred) const;

  // Performance monitoring
  PerformanceStats getPerformanceStats(EventTypeId typeId) const;
  void resetPerformanceStats();
  size_t getEventCount() const;
  size_t getEventCount(EventTypeId typeId) const;

  // Memory management
  void compactEventStorage();
  void clearEventPools();

  // Test-only methods
  void clearAllEvents();

private:
  EventManager() = default;

  // Shutdown state
  bool m_isShutdown{false};
  ~EventManager() {
    if (!m_isShutdown) {
      clean();
    }
  }
  EventManager(const EventManager &) = delete;
  EventManager &operator=(const EventManager &) = delete;

  // Core data structures (cache-friendly, type-indexed)
  std::array<std::vector<EventData>, static_cast<size_t>(EventTypeId::COUNT)>
      m_eventsByType;
  std::unordered_map<std::string, size_t>
      m_nameToIndex; // Name -> index in type array
  std::unordered_map<std::string, EventTypeId>
      m_nameToType; // Name -> type for fast lookup

  // Event pools for memory efficiency
  mutable EventPool<WeatherEvent> m_weatherPool;
  mutable EventPool<SceneChangeEvent> m_sceneChangePool;
  mutable EventPool<NPCSpawnEvent> m_npcSpawnPool;
  mutable EventPool<ResourceChangeEvent> m_resourceChangePool;
  mutable EventPool<WorldEvent> m_worldPool;
  mutable EventPool<CameraEvent> m_cameraPool;

  // Handler storage (type-indexed). Keep parallel id vectors for token removal
  std::array<std::vector<FastEventHandler>,
             static_cast<size_t>(EventTypeId::COUNT)>
      m_handlersByType;
  std::array<std::vector<uint64_t>, static_cast<size_t>(EventTypeId::COUNT)>
      m_handlerIdsByType;
  std::atomic<uint64_t> m_nextHandlerId{1};

  // Per-name handlers
  std::unordered_map<std::string, std::vector<FastEventHandler>> m_nameHandlers;
  std::unordered_map<std::string, std::vector<uint64_t>> m_nameHandlerIds;

  // Threading and synchronization
  mutable std::shared_mutex m_eventsMutex;
  mutable std::mutex m_handlersMutex;
  std::atomic<bool> m_threadingEnabled{true};
  std::atomic<bool> m_initialized{false};
  size_t m_threadingThreshold{
      50}; // Thread for medium+ event counts (consistent with buffer threshold)

  // Performance monitoring
  mutable std::array<PerformanceStats, static_cast<size_t>(EventTypeId::COUNT)>
      m_performanceStats;
  mutable std::mutex m_perfMutex;

  // Timing
  std::atomic<uint64_t> m_lastUpdateTime{0};

  // Thread allocation tracking for debug output
  std::atomic<size_t> m_lastOptimalWorkerCount{0};
  std::atomic<size_t> m_lastAvailableWorkers{0};
  std::atomic<size_t> m_lastEventBudget{0};
  std::atomic<bool> m_lastWasThreaded{false};

  // Deferred dispatch queue (processed in update())
  struct PendingDispatch {
    EventTypeId typeId;
    EventData data;
  };
  mutable std::mutex m_dispatchMutex;
  mutable std::deque<PendingDispatch> m_pendingDispatch;
  size_t m_maxDispatchQueue{8192};

  // Helper methods
  EventTypeId getEventTypeId(const EventPtr &event) const;
  std::string getEventTypeName(EventTypeId typeId) const;
  void updateEventTypeBatch(EventTypeId typeId);
  void updateEventTypeBatchThreaded(EventTypeId typeId);
  void processEventDirect(EventData &eventData);
  void recordPerformance(EventTypeId typeId, double timeMs) const;
  uint64_t getCurrentTimeNanos() const;
  void enqueueDispatch(EventTypeId typeId, const EventData &data) const;
  void drainDispatchQueueWithBudget();

  // Internal registration helper
  bool registerEventInternal(const std::string &name, EventPtr event,
                             EventTypeId typeId);
};

#endif // EVENT_MANAGER_HPP
