/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef EVENT_MANAGER_HPP
#define EVENT_MANAGER_HPP

/**
 * @file EventManager.hpp
 * @brief Central manager for game events and event triggering
 *
 * The EventManager provides a centralized system for:
 * - Registering game events (weather, scene changes, NPC spawning, etc.)
 * - Checking event trigger conditions
 * - Executing events when conditions are met
 * - Managing event priorities and conflicts
 * - Communicating with events via messages
 *
 * Usage example:
 *
 * 1. Register events:
 *    auto weatherEvent = std::make_shared<WeatherEvent>("RainStorm", WeatherType::Rainy);
 *    EventManager::Instance().registerEvent("RainStorm", weatherEvent);
 *
 * 2. Activate/deactivate events:
 *    EventManager::Instance().setEventActive("RainStorm", true);
 *
 * 3. Send message to specific event:
 *    EventManager::Instance().sendMessageToEvent("RainStorm", "intensify");
 */

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <limits>
#include <boost/container/flat_map.hpp>
#include "core/ThreadSystem.hpp"


// Conditional debug logging macros
#ifdef EVENT_DEBUG_LOGGING
    #define EVENT_LOG(x) std::cout << "Forge Game Engine - [Event Manager] " << x << std::endl
    #define EVENT_LOG_DETAIL(x) std::cout << x << std::endl
#else
    #define EVENT_LOG(x)
    #define EVENT_LOG_DETAIL(x)
#endif

// Forward declarations
class Event;
class WeatherEvent;
class SceneChangeEvent;
class NPCSpawnEvent;

using EventPtr = std::shared_ptr<Event>;
using EventWeakPtr = std::weak_ptr<Event>;

// Event handler function type
using EventHandlerFunc = std::function<void(const std::string&)>;



class EventManager {
public:
    /**
     * @brief Get the singleton instance of EventManager
     * @return Reference to the EventManager instance
     * @thread_safety Thread-safe, can be called from any thread
     */
    static EventManager& Instance() {
        static EventManager instance;
        return instance;
    }

    /**
     * @brief Initialize the Event Manager
     * @return True if initialization succeeded, false otherwise
     * @thread_safety Should be called from main thread during initialization
     */
    bool init();

    /**
     * @brief Update all registered events
     *
     * This method is automatically called by the game engine
     * and updates all registered events, checking their conditions
     * and executing them if necessary.
     *
     * @thread_safety Thread-safe, can be called from any thread
     */
    void update();

    /**
     * @brief Reset all events without shutting down the manager
     *
     * This method clears all registered events but keeps the manager initialized.
     * Use this when changing game states or scenes while the game is still running.
     *
     * @thread_safety Must be called when no event updates are in progress
     */
    void resetEvents();

    /**
     * @brief Clean up resources used by the Event Manager
     *
     * @thread_safety Must be called from main thread during shutdown
     */
    void clean();

    // Convenience methods for creating common event types
    /**
     * @brief Register a weather event
     * @param name Unique name for the event
     * @param weatherType Type of weather (Clear, Rainy, Stormy, etc.)
     * @param intensity Intensity of the weather (0.0-1.0)
     */
    void registerWeatherEvent(const std::string& name, const std::string& weatherType, float intensity);
    
    /**
     * @brief Register a scene change event
     * @param name Unique name for the event
     * @param targetScene ID of the target scene
     * @param transitionType Type of transition (fade, dissolve, etc.)
     */
    void registerSceneChangeEvent(const std::string& name, const std::string& targetScene, 
                                 const std::string& transitionType = "fade");
    
    /**
     * @brief Register an NPC spawn event
     * @param name Unique name for the event
     * @param npcType Type of NPC to spawn
     * @param count Number of NPCs to spawn
     * @param spawnRadius Radius around spawn point
     */
    void registerNPCSpawnEvent(const std::string& name, const std::string& npcType, 
                              int count = 1, float spawnRadius = 0.0f);

    /**
     * @brief Register default events for common game scenarios
     * This creates a set of common events that can be used in most games
     */
    void registerDefaultEvents();

    /**
     * @brief Register an event handler for a specific event type
     * @param eventType Type of event to handle
     * @param handler Function to call when event occurs
     */
    void registerEventHandler(const std::string& eventType, EventHandlerFunc handler);
    
    // Direct trigger methods
    /**
     * @brief Trigger an immediate weather change
     * @param weatherType Type of weather to change to
     * @param transitionTime Time in seconds for the transition
     */
    void triggerWeatherChange(const std::string& weatherType, float transitionTime = 5.0f);
    
    /**
     * @brief Trigger an immediate scene change
     * @param sceneId ID of the scene to change to
     * @param transitionType Type of transition to use
     * @param duration Duration of the transition in seconds
     */
    void triggerSceneChange(const std::string& sceneId, const std::string& transitionType = "fade", 
                           float duration = 1.0f);
    
    /**
     * @brief Trigger an immediate NPC spawn
     * @param npcType Type of NPC to spawn
     * @param x X coordinate for spawn position
     * @param y Y coordinate for spawn position
     */
    void triggerNPCSpawn(const std::string& npcType, float x, float y);

    /**
     * @brief Configure threading options for event processing
     * @param useThreading Whether to use ThreadSystem for parallel event updates
     * @param maxThreads Maximum number of concurrent tasks (0 = use ThreadSystem default)
     * @param priority Priority level for event processing tasks (default: Normal)
     * @thread_safety Must be called before any event updates begin
     * @note This method will initialize ThreadSystem if needed when enabling threading
     */
    void configureThreading(bool useThreading, unsigned int maxThreads = 0, 
                           Forge::TaskPriority priority = Forge::TaskPriority::Normal);

    // Event management
    /**
     * @brief Register a new event
     * @param eventName Unique identifier for the event
     * @param event Shared pointer to the event implementation
     * @thread_safety Thread-safe, but events should ideally be registered at startup
     */
    void registerEvent(const std::string& eventName, EventPtr event);

    /**
     * @brief Check if an event exists
     * @param eventName Name of the event to check
     * @return True if the event exists, false otherwise
     * @thread_safety Thread-safe, can be called from any thread
     */
    bool hasEvent(const std::string& eventName) const;

    /**
     * @brief Get a shared pointer to an event
     * @param eventName Name of the event to retrieve
     * @return Shared pointer to the event, or nullptr if not found
     * @thread_safety Thread-safe, can be called from any thread
     */
    EventPtr getEvent(const std::string& eventName) const;

    /**
     * @brief Get all events of a specific type
     * @param eventType Type of events to retrieve (e.g., "Weather", "SceneChange")
     * @return Vector of event shared pointers
     * @thread_safety Thread-safe, can be called from any thread
     */
    std::vector<EventPtr> getEventsByType(const std::string& eventType) const;

    /**
     * @brief Remove a registered event
     * @param eventName Name of the event to remove
     * @return True if the event was removed, false if it wasn't found
     * @thread_safety Thread-safe, can be called from any thread
     */
    bool removeEvent(const std::string& eventName);

    /**
     * @brief Set whether an event is active
     * @param eventName Name of the event
     * @param active Whether the event should be active
     * @return True if the event was found and updated, false otherwise
     * @thread_safety Thread-safe, can be called from any thread
     */
    bool setEventActive(const std::string& eventName, bool active);

    /**
     * @brief Check if an event is active
     * @param eventName Name of the event
     * @return True if the event is active, false otherwise
     * @thread_safety Thread-safe, can be called from any thread
     */
    bool isEventActive(const std::string& eventName) const;

    /**
     * @brief Execute an event immediately, regardless of its conditions
     * @param eventName Name of the event to execute
     * @return True if the event was found and executed, false otherwise
     * @thread_safety Thread-safe, can be called from any thread
     */
    bool executeEvent(const std::string& eventName);

    /**
     * @brief Execute all events of a specific type
     * @param eventType Type of events to execute
     * @return Number of events executed
     * @thread_safety Thread-safe, can be called from any thread
     */
    int executeEventsByType(const std::string& eventType);

    // Advanced features
    /**
     * @brief Send a message to a specific event
     * @param eventName Target event name
     * @param message Message string
     * @param immediate If true, delivers immediately; if false, queues for next update
     * @thread_safety Thread-safe, can be called from any thread
     */
    void sendMessageToEvent(const std::string& eventName, const std::string& message, bool immediate = false);

    /**
     * @brief Send a message to all events of a specific type
     * @param eventType Type of events to receive the message
     * @param message Message string to send
     * @param immediate If true, delivers immediately; if false, queues for next update
     * @thread_safety Thread-safe, can be called from any thread
     */
    void broadcastMessageToType(const std::string& eventType, const std::string& message, bool immediate = false);

    /**
     * @brief Send a message to all events
     * @param message Message string to broadcast
     * @param immediate If true, delivers immediately; if false, queues for next update
     * @thread_safety Thread-safe, can be called from any thread
     */
    void broadcastMessage(const std::string& message, bool immediate = false);

    /**
     * @brief Process all queued messages
     * This happens automatically during update() but can be called manually
     * @thread_safety Thread-safe, can be called from any thread
     */
    void processMessageQueue();

    // Weather-specific methods
    /**
     * @brief Force a weather change immediately
     * @param weatherType Type of weather to change to
     * @param transitionTime Time in seconds for the transition
     * @return True if successful, false otherwise
     * @thread_safety Thread-safe, can be called from any thread
     */
    bool changeWeather(const std::string& weatherType, float transitionTime = 5.0f);

    // Scene-specific methods
    /**
     * @brief Force a scene change immediately
     * @param sceneId ID of the scene to change to
     * @param transitionType Type of transition to use
     * @param transitionTime Time in seconds for the transition
     * @return True if successful, false otherwise
     * @thread_safety Thread-safe, can be called from any thread
     */
    bool changeScene(const std::string& sceneId, const std::string& transitionType = "fade", float transitionTime = 1.0f);

    // NPC spawn methods
    /**
     * @brief Spawn an NPC at a specific position
     * @param npcType Type of NPC to spawn
     * @param x X coordinate
     * @param y Y coordinate
     * @return True if successful, false otherwise
     * @thread_safety Thread-safe, can be called from any thread
     */
    bool spawnNPC(const std::string& npcType, float x, float y);

    // Utility methods
    /**
     * @brief Get the number of registered events
     * @return Count of events
     * @thread_safety Thread-safe, can be called from any thread
     */
    size_t getEventCount() const;

    /**
     * @brief Get the number of active events
     * @return Count of active events
     * @thread_safety Thread-safe, can be called from any thread
     */
    size_t getActiveEventCount() const;

private:
    // Singleton constructor/destructor
    EventManager();
    ~EventManager();

    // Delete copy constructor and assignment operator
    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;

    // Storage for events
    // ThreadAccess: ReadOnly after initialization for m_events
    boost::container::flat_map<std::string, EventPtr> m_events{};

    // Index for event types (for fast lookup by type)
    boost::container::flat_map<std::string, std::vector<std::string>> m_eventTypeIndex{};

    // Thread synchronization for event map
    mutable std::shared_mutex m_eventsMutex{};

    // Performance tracing for event operations
    struct PerformanceStats {
        double totalUpdateTimeMs{0.0};
        double averageUpdateTimeMs{0.0};
        uint64_t updateCount{0};
        double maxUpdateTimeMs{0.0};
        double minUpdateTimeMs{std::numeric_limits<double>::max()};

        // Constructor to ensure proper initialization
        PerformanceStats()
            : totalUpdateTimeMs(0.0)
            , averageUpdateTimeMs(0.0)
            , updateCount(0)
            , maxUpdateTimeMs(0.0)
            , minUpdateTimeMs(std::numeric_limits<double>::max())
        {}

        void addSample(double timeMs) {
            totalUpdateTimeMs += timeMs;
            updateCount++;
            averageUpdateTimeMs = totalUpdateTimeMs / updateCount;
            maxUpdateTimeMs = std::max(maxUpdateTimeMs, timeMs);
            minUpdateTimeMs = std::min(minUpdateTimeMs, timeMs);
        }

        void reset() {
            totalUpdateTimeMs = 0.0;
            averageUpdateTimeMs = 0.0;
            updateCount = 0;
            maxUpdateTimeMs = 0.0;
            minUpdateTimeMs = std::numeric_limits<double>::max();
        }
    };

    // Cache for quick lookup of active events (optimization)
    struct EventCache {
        EventWeakPtr event;
        std::string_view eventName;  // Using string_view to avoid copies
        std::string_view eventType;  // Type of the event

        // Performance statistics
        uint64_t lastUpdateTime{0};
        PerformanceStats perfStats;

        // Constructor to ensure proper initialization
        EventCache()
            : event()
            , eventName()
            , eventType()
            , lastUpdateTime(0)
            , perfStats()
        {}
    };
    std::vector<EventCache> m_activeEventCache{};
    std::atomic<bool> m_cacheValid{false};
    mutable std::mutex m_cacheMutex{};

    // Multithreading support
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_useThreading{true}; // Controls whether updates use ThreadSystem
    unsigned int m_maxThreads{0}; // 0 = use ThreadSystem default thread count
    Forge::TaskPriority m_eventTaskPriority{Forge::TaskPriority::Normal}; // Default priority for event tasks

    // For batch processing optimization
    using EventBatch = std::vector<EventPtr>;
    boost::container::flat_map<std::string, EventBatch> m_eventTypeBatches{};
    std::atomic<bool> m_batchesValid{false};
    mutable std::mutex m_batchesMutex{};

    // Private helper methods for optimizations
    void rebuildActiveEventCache();
    void rebuildEventTypeBatches();
    void invalidateOptimizationCaches();
    void ensureOptimizationCachesValid();
    void updateEventTypeBatch(const std::string_view& eventType, const EventBatch& batch);

    // State tracking for EventSystem functionality
    uint64_t m_lastUpdateTime{0};
    
    // Event handlers for different event types
    std::unordered_map<std::string, std::vector<EventHandlerFunc>> m_eventHandlers{};
    mutable std::mutex m_eventHandlersMutex{};
    
    // Helper methods for EventSystem functionality
    void registerSystemEventHandlers();
    void updateEventTimers(float deltaTime);
    void processSystemEvents();
    static uint64_t getCurrentTimeMs();

    // Performance monitoring
    boost::container::flat_map<std::string, PerformanceStats> m_eventTypePerformanceStats;
    mutable std::mutex m_perfStatsMutex{};
    void recordEventTypePerformance(const std::string_view& eventType, double timeMs);

    // Utility method to get current high-precision time
    static inline uint64_t getCurrentTimeNanos() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }

    // Message structure for event communication
    struct QueuedMessage {
        std::string targetEvent;  // empty string for broadcast
        std::string targetType;   // empty string for non-type-specific message
        std::string message;
        uint64_t timestamp{0};

        // Explicitly initialize all members
        QueuedMessage() : targetEvent(), targetType(), message(), timestamp(0) {}

        QueuedMessage(const std::string& target, const std::string& type, const std::string& msg, uint64_t time)
            : targetEvent(target), targetType(type), message(msg), timestamp(time) {}
    };

    // Message queue system with thread-safe double-buffering
    class ThreadSafeMessageQueue {
    public:
        void enqueueMessage(const std::string& target, const std::string& message) {
            std::lock_guard<std::mutex> lock(m_incomingMutex);
            m_incomingQueue.emplace_back(target, "", message, EventManager::getCurrentTimeNanos());
        }

        void enqueueTypeMessage(const std::string& targetType, const std::string& message) {
            std::lock_guard<std::mutex> lock(m_incomingMutex);
            m_incomingQueue.emplace_back("", targetType, message, EventManager::getCurrentTimeNanos());
        }

        void enqueueBroadcastMessage(const std::string& message) {
            std::lock_guard<std::mutex> lock(m_incomingMutex);
            m_incomingQueue.emplace_back("", "", message, EventManager::getCurrentTimeNanos());
        }

        void swapBuffers() {
            if (m_swapInProgress.exchange(true)) return;

            std::lock_guard<std::mutex> lock(m_incomingMutex);
            m_processingQueue.clear();
            m_processingQueue.swap(m_incomingQueue);

            m_swapInProgress.store(false);
        }

        const std::vector<QueuedMessage>& getProcessingQueue() const {
            return m_processingQueue;
        }

        bool isEmpty() const {
            std::lock_guard<std::mutex> lock(m_incomingMutex);
            return m_incomingQueue.empty() && m_processingQueue.empty();
        }

        void clear() {
            std::lock_guard<std::mutex> lock(m_incomingMutex);
            m_incomingQueue.clear();
            m_processingQueue.clear();
        }

    private:
        std::vector<QueuedMessage> m_incomingQueue;
        std::vector<QueuedMessage> m_processingQueue;
        mutable std::mutex m_incomingMutex;
        std::atomic<bool> m_swapInProgress{false};
    };

    // Thread-safe message queue implementation
    ThreadSafeMessageQueue m_messageQueue;
    PerformanceStats m_messageQueueStats;
    std::atomic<bool> m_processingMessages{false};

    // Message delivery helpers
    void deliverMessageToEvent(const std::string& eventName, const std::string& message);
    size_t deliverMessageToEventType(const std::string& eventType, const std::string& message);
    size_t deliverBroadcastMessage(const std::string& message);

    // Helper methods for event processing
    void updateEvent(const EventPtr& event, const std::string_view& eventName);
    bool checkEventConditions(const EventPtr& event);
    void executeEventIfConditionsMet(const EventPtr& event);
};

#endif // EVENT_MANAGER_HPP
