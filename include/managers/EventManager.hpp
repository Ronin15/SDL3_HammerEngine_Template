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

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <atomic>
#include <shared_mutex>
#include <functional>
#include <queue>
#include <unordered_map>

// Forward declarations
class Event;
class WeatherEvent;
class SceneChangeEvent;
class NPCSpawnEvent;
class EventFactory;

using EventPtr = std::shared_ptr<Event>;
using EventWeakPtr = std::weak_ptr<Event>;



/**
 * @brief Strongly typed event type enumeration for fast lookups
 */
enum class EventTypeId : uint8_t {
    Weather = 0,
    SceneChange = 1,
    NPCSpawn = 2,
    Custom = 3,
    COUNT = 4
};

/**
 * @brief Cache-friendly event data structure (data-oriented design)
 * Optimized for 32-byte cache line alignment
 */
struct EventData {
    EventPtr event;                    // Smart pointer to event (8 bytes)
    float lastUpdateTime;             // For delta time calculations (4 bytes)
    uint32_t flags;                   // Active, dirty, etc. (4 bytes)
    uint32_t priority;                // For priority-based processing (4 bytes)
    EventTypeId typeId;               // Type for fast dispatch (1 byte)
    uint8_t padding[11];              // Padding to 32 bytes for cache alignment
    
    // Flags bit definitions
    static constexpr uint32_t FLAG_ACTIVE = 1 << 0;
    static constexpr uint32_t FLAG_DIRTY = 1 << 1;
    static constexpr uint32_t FLAG_PENDING_REMOVAL = 1 << 2;
    
    EventData() : event(nullptr), lastUpdateTime(0.0f), flags(0), priority(0), 
                  typeId(EventTypeId::Custom), padding{} {}
    
    bool isActive() const { return flags & FLAG_ACTIVE; }
    void setActive(bool active) { 
        if (active) flags |= FLAG_ACTIVE; 
        else flags &= ~FLAG_ACTIVE; 
    }
    
    bool isDirty() const { return flags & FLAG_DIRTY; }
    void setDirty(bool dirty) { 
        if (dirty) flags |= FLAG_DIRTY; 
        else flags &= ~FLAG_DIRTY; 
    }
} __attribute__((aligned(32)));

/**
 * @brief Fast event handler function type
 */
using FastEventHandler = std::function<void(const EventData&)>;

/**
 * @brief Event pool for memory-efficient event management
 */
template<typename EventType>
class EventPool {
public:
    std::shared_ptr<EventType> acquire() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (!m_available.empty()) {
            auto event = m_available.front();
            m_available.pop();
            return event;
        }
        
        // Create new event if pool is empty
        auto event = std::make_shared<EventType>();
        m_allEvents.push_back(event);
        return event;
    }
    
    void release(std::shared_ptr<EventType> event) {
        if (!event) return;
        
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
    
private:
    std::vector<std::shared_ptr<EventType>> m_allEvents;
    std::queue<std::shared_ptr<EventType>> m_available;
    std::mutex m_mutex;
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
    static EventManager& Instance() {
        static EventManager instance;
        return instance;
    }

    /**
     * @brief Initializes the EventManager and its internal systems
     * @return true if initialization successful, false otherwise
     */
    bool init();
    
    /**
     * @brief Checks if the Event Manager has been initialized
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const { return m_initialized.load(std::memory_order_acquire); }
    
    /**
     * @brief Cleans up all event resources
     */
    void clean();
    
    /**
     * @brief Prepares for state transition by safely cleaning up events and handlers
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
    bool registerEvent(const std::string& name, EventPtr event);
    
    /**
     * @brief Registers a weather event with the event system
     * @param name Unique name identifier for the weather event
     * @param event Shared pointer to the weather event to register
     * @return true if registration successful, false otherwise
     */
    bool registerWeatherEvent(const std::string& name, std::shared_ptr<WeatherEvent> event);
    
    /**
     * @brief Registers a scene change event with the event system
     * @param name Unique name identifier for the scene change event
     * @param event Shared pointer to the scene change event to register
     * @return true if registration successful, false otherwise
     */
    bool registerSceneChangeEvent(const std::string& name, std::shared_ptr<SceneChangeEvent> event);
    
    /**
     * @brief Registers an NPC spawn event with the event system
     * @param name Unique name identifier for the NPC spawn event
     * @param event Shared pointer to the NPC spawn event to register
     * @return true if registration successful, false otherwise
     */
    bool registerNPCSpawnEvent(const std::string& name, std::shared_ptr<NPCSpawnEvent> event);

    /**
     * @brief Retrieves an event by its name
     * @param name Name of the event to retrieve
     * @return Shared pointer to the event, or nullptr if not found
     */
    EventPtr getEvent(const std::string& name) const;
    
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
    std::vector<EventPtr> getEventsByType(const std::string& typeName) const;

    /**
     * @brief Sets the active state of an event
     * @param name Name of the event to modify
     * @param active New active state for the event
     * @return true if state was changed successfully, false otherwise
     */
    bool setEventActive(const std::string& name, bool active);
    
    /**
     * @brief Checks if an event is currently active
     * @param name Name of the event to check
     * @return true if event is active, false if inactive or not found
     */
    bool isEventActive(const std::string& name) const;
    
    /**
     * @brief Removes an event from the event system
     * @param name Name of the event to remove
     * @return true if event was removed successfully, false otherwise
     */
    bool removeEvent(const std::string& name);
    
    /**
     * @brief Checks if an event is registered in the system
     * @param name Name of the event to check
     * @return true if event exists, false otherwise
     */
    bool hasEvent(const std::string& name) const;

    // Fast execution methods
    bool executeEvent(const std::string& eventName) const;
    int executeEventsByType(EventTypeId typeId) const;
    int executeEventsByType(const std::string& eventType) const;

    // Handler registration (type-safe)
    void registerHandler(EventTypeId typeId, FastEventHandler handler);
    void removeHandlers(EventTypeId typeId);
    void clearAllHandlers();
    size_t getHandlerCount(EventTypeId typeId) const;

    // Batch processing (AIManager-style)
    void updateWeatherEvents();
    void updateSceneChangeEvents();
    void updateNPCSpawnEvents();
    void updateCustomEvents();

    // Threading control
    void enableThreading(bool enable) { m_threadingEnabled.store(enable); }
    bool isThreadingEnabled() const { return m_threadingEnabled.load(); }
    void setThreadingThreshold(size_t threshold) { m_threadingThreshold = threshold; }

    // High-level convenience methods
    bool changeWeather(const std::string& weatherType, float transitionTime = 5.0f) const;
    bool changeScene(const std::string& sceneId, const std::string& transitionType = "fade", float transitionTime = 1.0f) const;
    bool spawnNPC(const std::string& npcType, float x, float y) const;

    // Event creation convenience methods (create and register in one call using EventFactory)
    bool createWeatherEvent(const std::string& name, const std::string& weatherType, float intensity = 1.0f, float transitionTime = 5.0f);
    bool createSceneChangeEvent(const std::string& name, const std::string& targetScene, const std::string& transitionType = "fade", float transitionTime = 1.0f);
    bool createNPCSpawnEvent(const std::string& name, const std::string& npcType, int count = 1, float spawnRadius = 0.0f);

    // Alternative trigger methods (aliases for compatibility)
    bool triggerWeatherChange(const std::string& weatherType, float transitionTime = 5.0f) const { return changeWeather(weatherType, transitionTime); }
    bool triggerSceneChange(const std::string& sceneId, const std::string& transitionType = "fade", float transitionTime = 1.0f) const { return changeScene(sceneId, transitionType, transitionTime); }
    bool triggerNPCSpawn(const std::string& npcType, float x, float y) const { return spawnNPC(npcType, x, y); }

    // Performance monitoring
    PerformanceStats getPerformanceStats(EventTypeId typeId) const;
    void resetPerformanceStats();
    size_t getEventCount() const;
    size_t getEventCount(EventTypeId typeId) const;

    // Memory management
    void compactEventStorage();
    void clearEventPools();

private:
    EventManager() = default;
    
    // Shutdown state
    bool m_isShutdown{false};
    ~EventManager() {
        if (!m_isShutdown) {
            clean();
        }
    }
    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;

    // Core data structures (cache-friendly, type-indexed)
    std::array<std::vector<EventData>, static_cast<size_t>(EventTypeId::COUNT)> m_eventsByType;
    std::unordered_map<std::string, size_t> m_nameToIndex; // Name -> index in type array
    std::unordered_map<std::string, EventTypeId> m_nameToType; // Name -> type for fast lookup

    // Event pools for memory efficiency
    EventPool<WeatherEvent> m_weatherPool;
    EventPool<SceneChangeEvent> m_sceneChangePool;
    EventPool<NPCSpawnEvent> m_npcSpawnPool;

    // Handler storage (type-indexed for speed)
    std::array<std::vector<FastEventHandler>, static_cast<size_t>(EventTypeId::COUNT)> m_handlersByType;

    // Threading and synchronization
    mutable std::shared_mutex m_eventsMutex;
    mutable std::mutex m_handlersMutex;
    std::atomic<bool> m_threadingEnabled{true};
    std::atomic<bool> m_initialized{false};
    size_t m_threadingThreshold{50}; // Thread for medium+ event counts (consistent with buffer threshold)

    // Performance monitoring
    mutable std::array<PerformanceStats, static_cast<size_t>(EventTypeId::COUNT)> m_performanceStats;
    mutable std::mutex m_perfMutex;

    // Timing
    std::atomic<uint64_t> m_lastUpdateTime{0};
    
    // Double buffering for lock-free reads during updates
    std::atomic<size_t> m_currentBuffer{0};
    std::array<std::vector<EventData>, 2> m_updateBuffers[static_cast<size_t>(EventTypeId::COUNT)];

    // Helper methods
    EventTypeId getEventTypeId(const EventPtr& event) const;
    std::string getEventTypeName(EventTypeId typeId) const;
    void updateEventTypeBatch(EventTypeId typeId);
    void updateEventTypeBatchThreaded(EventTypeId typeId);
    void processEventDirect(EventData& eventData);
    void recordPerformance(EventTypeId typeId, double timeMs) const;
    uint64_t getCurrentTimeNanos() const;
    
    // Internal registration helper
    bool registerEventInternal(const std::string& name, EventPtr event, EventTypeId typeId);
};

#endif // EVENT_MANAGER_HPP