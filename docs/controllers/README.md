# Controller Pattern Documentation

**Where to find the code:**
- Headers: `include/controllers/world/*.hpp`
- Implementations: `src/controllers/world/*.cpp`
- Tests: `tests/controllers/*.cpp`

## Overview

Controllers are **state-scoped helpers** that control specific system behaviors through event subscription. Unlike Managers (which have global lifecycle and own data), Controllers subscribe per-GameState and contain game logic without owning underlying data.

## Table of Contents

- [Controllers vs Managers](#controllers-vs-managers)
- [Available Controllers](#available-controllers)
- [Lifecycle Pattern](#lifecycle-pattern)
- [Implementation Guidelines](#implementation-guidelines)
- [Creating New Controllers](#creating-new-controllers)
- [Best Practices](#best-practices)

## Controllers vs Managers

| Aspect | Controllers | Managers |
|--------|-------------|----------|
| **Lifecycle** | State-scoped (subscribe/unsubscribe) | Global (GameEngine init/clean) |
| **Data Ownership** | Do NOT own data | Own system data |
| **Initialization** | Called in GameState::enter() | Called in GameEngine::init() |
| **Cleanup** | Called in GameState::exit() | Cleaned up by GameEngine |
| **Scope** | Game logic specific to current state | System-wide functionality |
| **Pattern** | Event subscriber pattern | Singleton manager pattern |
| **Threading** | No special handling | Handle update/render threads |
| **Memory** | Minimal (event tokens, buffers) | Significant (entity data, caches) |

### When to Use Controllers

Use a **Controller** when:
- Logic is only relevant while in certain game states
- You're reacting to events, not managing core data
- You need state-specific behavior without polluting managers
- Multiple states need similar but independent logic

Use a **Manager** when:
- System needs to exist for the entire game lifecycle
- You're managing core game data (entities, textures, sounds)
- Multiple systems depend on this functionality globally
- Thread coordination or resource pooling is needed

## Available Controllers

### World Controllers (`controllers/world/`)

| Controller | Purpose | Events Subscribed | Events Dispatched |
|------------|---------|-------------------|-------------------|
| [TimeController](TimeController.md) | Log time events to UI | TimeEvent, WeatherEvent | None |
| [WeatherController](WeatherController.md) | Bridge weather checks to changes | TimeEvent (WeatherCheck) | None (calls EventManager) |
| [DayNightController](DayNightController.md) | Track time periods, visual config | TimeEvent (HourChanged) | TimePeriodChangedEvent |

### Future Controller Directories

The architecture supports organizing controllers by system:

```
controllers/
  world/        # Time, weather, day/night (implemented)
  ai/           # AI behavior modifiers (future)
  combat/       # Combat state handling (future)
  inventory/    # Inventory UI coordination (future)
```

## Lifecycle Pattern

### Subscribe/Unsubscribe in GameState

```cpp
// GamePlayState.hpp
class GamePlayState : public GameState {
private:
    EventManager::HandlerToken m_dayNightToken;
};

// GamePlayState.cpp
bool GamePlayState::enter() {
    // Subscribe controllers
    WeatherController::Instance().subscribe();
    TimeController::Instance().subscribe("gameplay_event_log");
    DayNightController::Instance().subscribe();

    // Optional: Subscribe to controller events
    m_dayNightToken = EventManager::Instance().registerHandlerWithToken(
        EventTypeId::Time,
        [this](const EventData& data) { onTimePeriodChanged(data); }
    );

    return true;
}

void GamePlayState::exit() {
    // Unsubscribe in reverse order
    EventManager::Instance().removeHandler(m_dayNightToken);

    DayNightController::Instance().unsubscribe();
    TimeController::Instance().unsubscribe();
    WeatherController::Instance().unsubscribe();
}
```

### Why State-Scoped?

1. **Resource Efficiency**: Event handlers only active when needed
2. **Clean Separation**: State-specific logic stays with states
3. **No Global Pollution**: Managers remain focused on core functionality
4. **Easy Testing**: Controllers can be tested in isolation

## Implementation Guidelines

### Singleton with State Subscription

```cpp
class MyController {
public:
    static MyController& Instance() {
        static MyController instance;
        return instance;
    }

    void subscribe() {
        if (m_subscribed) return;  // Prevent double subscription

        auto& eventMgr = EventManager::Instance();
        auto token = eventMgr.registerHandlerWithToken(
            EventTypeId::MyEventType,
            [this](const EventData& data) { onEvent(data); }
        );
        m_handlerTokens.push_back(token);
        m_subscribed = true;
    }

    void unsubscribe() {
        if (!m_subscribed) return;

        auto& eventMgr = EventManager::Instance();
        for (const auto& token : m_handlerTokens) {
            eventMgr.removeHandler(token);
        }
        m_handlerTokens.clear();
        m_subscribed = false;
    }

    bool isSubscribed() const { return m_subscribed; }

private:
    MyController() = default;
    ~MyController() = default;
    MyController(const MyController&) = delete;
    MyController& operator=(const MyController&) = delete;

    void onEvent(const EventData& data);

    bool m_subscribed{false};
    std::vector<EventManager::HandlerToken> m_handlerTokens;
};
```

### Zero Per-Frame Allocation Pattern

Controllers should minimize allocations, especially for status updates:

```cpp
class TimeController {
private:
    std::string m_statusBuffer{};  // Reused across updates

    void updateStatusText() {
        m_statusBuffer.clear();  // Keeps reserved capacity
        m_statusBuffer.reserve(256);  // One-time allocation

        // Use std::format with back_inserter for zero allocation
        std::format_to(std::back_inserter(m_statusBuffer),
            "Day {} {} | {}",
            gameTime.getDayOfMonth(),
            gameTime.getCurrentMonthName(),
            gameTime.formatCurrentTime(true)
        );

        UIManager::Instance().setText(m_statusLabelId, m_statusBuffer);
    }
};
```

### Event Type Filtering

Use enum-based filtering instead of expensive `dynamic_cast`:

```cpp
void onTimeEvent(const EventData& data) {
    auto timeEvent = std::static_pointer_cast<TimeEvent>(data.event);

    // Early exit for irrelevant events
    if (timeEvent->getTimeEventType() != TimeEventType::HourChanged) {
        return;
    }

    // Now safe to cast to specific type
    auto hourEvent = std::static_pointer_cast<HourChangedEvent>(data.event);
    processHourChange(hourEvent);
}
```

### Deferred Initial Events

Dispatch initial state on subscription so subscribers know current state:

```cpp
void DayNightController::subscribe() {
    // ... register handlers ...

    // Dispatch initial event with current state
    float currentHour = GameTime::Instance().getGameHour();
    m_currentPeriod = hourToTimePeriod(currentHour);

    auto visuals = TimePeriodVisuals::getForPeriod(m_currentPeriod);
    auto event = std::make_shared<TimePeriodChangedEvent>(
        m_currentPeriod, m_previousPeriod, visuals
    );
    EventManager::Instance().dispatchEvent(event, EventManager::DispatchMode::Deferred);

    m_subscribed = true;
}
```

## Creating New Controllers

### Step 1: Define the Header

```cpp
// include/controllers/mySystem/MyController.hpp
#ifndef MY_CONTROLLER_HPP
#define MY_CONTROLLER_HPP

#include "managers/EventManager.hpp"
#include <vector>

class MyController {
public:
    static MyController& Instance();

    void subscribe();
    void unsubscribe();
    bool isSubscribed() const { return m_subscribed; }

    // Controller-specific methods
    void doSomething();

private:
    MyController() = default;
    ~MyController() = default;
    MyController(const MyController&) = delete;
    MyController& operator=(const MyController&) = delete;

    void onEvent(const EventData& data);

    bool m_subscribed{false};
    std::vector<EventManager::HandlerToken> m_handlerTokens;
};

#endif
```

### Step 2: Implement the Controller

```cpp
// src/controllers/mySystem/MyController.cpp
#include "controllers/mySystem/MyController.hpp"

MyController& MyController::Instance() {
    static MyController instance;
    return instance;
}

void MyController::subscribe() {
    if (m_subscribed) return;

    auto& eventMgr = EventManager::Instance();
    auto token = eventMgr.registerHandlerWithToken(
        EventTypeId::MyType,
        [this](const EventData& data) { onEvent(data); }
    );
    m_handlerTokens.push_back(token);
    m_subscribed = true;
}

void MyController::unsubscribe() {
    if (!m_subscribed) return;

    auto& eventMgr = EventManager::Instance();
    for (const auto& token : m_handlerTokens) {
        eventMgr.removeHandler(token);
    }
    m_handlerTokens.clear();
    m_subscribed = false;
}

void MyController::onEvent(const EventData& data) {
    // Handle events
}
```

### Step 3: Add to CMakeLists.txt

```cmake
# In src/CMakeLists.txt or appropriate location
set(CONTROLLER_SOURCES
    src/controllers/mySystem/MyController.cpp
)
```

### Step 4: Use in GameState

```cpp
// In your GameState
#include "controllers/mySystem/MyController.hpp"

bool MyGameState::enter() {
    MyController::Instance().subscribe();
    return true;
}

void MyGameState::exit() {
    MyController::Instance().unsubscribe();
}
```

## Best Practices

### 1. Always Check Subscription State

```cpp
void subscribe() {
    if (m_subscribed) return;  // Prevent double subscription
    // ...
}

void unsubscribe() {
    if (!m_subscribed) return;  // Prevent double unsubscription
    // ...
}
```

### 2. Unsubscribe in Reverse Order

```cpp
// enter(): A, B, C
// exit(): C, B, A
void exit() {
    ControllerC::Instance().unsubscribe();
    ControllerB::Instance().unsubscribe();
    ControllerA::Instance().unsubscribe();
}
```

### 3. Use Zero-Allocation Patterns

```cpp
// GOOD: Reuse buffer
m_buffer.clear();
std::format_to(std::back_inserter(m_buffer), "...", args);

// AVOID: New allocation every call
std::string result = std::format("...", args);
```

### 4. Filter Events Early

```cpp
// GOOD: Filter with enum, then cast
if (timeEvent->getTimeEventType() != TimeEventType::HourChanged) {
    return;
}

// AVOID: Cast first, then check
auto hourEvent = std::dynamic_pointer_cast<HourChangedEvent>(data.event);
if (!hourEvent) return;
```

### 5. Document Event Flow

```cpp
/**
 * Event flow:
 *   SourceSystem::method() → EventType (Deferred)
 *     → ThisController handles it
 *     → Action taken (UI update, dispatch, etc.)
 */
```

### 6. Keep Controllers Focused

Each controller should have a single responsibility:
- TimeController: Log time events to UI
- WeatherController: Bridge weather checks to changes
- DayNightController: Track time periods

---

## Related Documentation

- **GameTime:** `docs/core/GameTime.md` - Time system that controllers react to
- **EventManager:** `docs/events/EventManager.md` - Event subscription system
- **GameStateManager:** `docs/managers/GameStateManager.md` - State lifecycle

---

*This documentation reflects the Controller pattern introduced in the world_time branch, providing state-scoped event handling separate from global Managers.*
