# Controller Pattern Documentation

**Where to find the code:**
- Headers: `include/controllers/world/*.hpp`
- Base class: `include/controllers/ControllerBase.hpp`
- Implementations: `src/controllers/world/*.cpp`
- Tests: `tests/controllers/*.cpp`

## Overview

Controllers are **state-scoped event bridges** that react to events and process logic. Unlike Managers (which have global lifecycle and own data), Controllers are owned by GameStates and handle specific event-driven behavior.

## Architecture

```
GameTime (data source)
    │
    ├──► GamePlayState.update() queries directly → UIManager displays
    │
    └──► dispatches events → EventManager routes to:
            ├──► WeatherController (weather logic)
            └──► DayNightController (day/night visuals)
```

**Key pattern:**
- **Status bar display:** Direct query of GameTime in update()
- **Weather/DayNight effects:** Controllers subscribe to events, process logic

## Available Controllers

### World Controllers (`controllers/world/`)

| Controller | Purpose | Events Subscribed | State Provided |
|------------|---------|-------------------|----------------|
| [WeatherController](WeatherController.md) | Track current weather | TimeEvent (WeatherCheck) | `getCurrentWeather()`, `getCurrentWeatherString()` |
| [DayNightController](DayNightController.md) | Track time periods, provide visuals | TimeEvent (HourChanged) | `getCurrentPeriod()`, `getCurrentVisuals()` |

### ControllerBase

All controllers inherit from `ControllerBase` which provides:
- Auto-unsubscribe on destruction (RAII)
- Handler token management
- Subscription state tracking
- Move semantics (non-copyable)

## Ownership Model

Controllers are **owned by GameStates** (not singletons):

```cpp
// GamePlayState.hpp
class GamePlayState : public GameState {
private:
    WeatherController m_weatherController;      // Owned instance
    DayNightController m_dayNightController;    // Owned instance
};

// GamePlayState.cpp
bool GamePlayState::enter() {
    // Subscribe controllers
    m_weatherController.subscribe();
    m_dayNightController.subscribe();
    return true;
}

// exit() - controllers auto-unsubscribe via ControllerBase destructor
```

## Time Display Pattern

Time status bar uses **direct query** (no controller bridging needed):

```cpp
void GamePlayState::update(float deltaTime) {
    // Direct query of GameTime - simple and efficient
    auto& gt = GameTime::Instance();
    m_statusBuffer.clear();
    std::format_to(std::back_inserter(m_statusBuffer),
        "Day {} {} | {} | {}",
        gt.getDayOfMonth(), gt.getCurrentMonthName(),
        gt.formatCurrentTime(), gt.getSeasonName());

    UIManager::Instance().setText("time_label", m_statusBuffer);
}
```

## Creating New Controllers

### Step 1: Inherit from ControllerBase

```cpp
// include/controllers/mySystem/MyController.hpp
#include "controllers/ControllerBase.hpp"

class MyController : public ControllerBase {
public:
    void subscribe();

    // Controller-specific getters
    MyState getCurrentState() const { return m_state; }

private:
    void onEvent(const EventData& data);
    MyState m_state{};
};
```

### Step 2: Implement subscribe()

```cpp
// src/controllers/mySystem/MyController.cpp
void MyController::subscribe() {
    if (checkAlreadySubscribed()) return;

    auto& eventMgr = EventManager::Instance();
    auto token = eventMgr.registerHandlerWithToken(
        EventTypeId::MyEventType,
        [this](const EventData& data) { onEvent(data); }
    );
    addHandlerToken(token);
    setSubscribed(true);
}
```

### Step 3: Use in GameState

```cpp
class MyGameState : public GameState {
    MyController m_myController;  // Owned instance

    bool enter() override {
        m_myController.subscribe();
        return true;
    }
    // Auto-unsubscribes on destruction
};
```

## Best Practices

### 1. Controllers Do NOT Update UI Directly

Controllers process events and maintain state. GameStates query controllers and update UI.

```cpp
// GOOD: GameState queries controller, updates UI
void GamePlayState::render(...) {
    auto visuals = m_dayNightController.getCurrentVisuals();
    renderOverlay(visuals);
}

// AVOID: Controller updating UI directly
void DayNightController::onEvent(...) {
    UIManager::Instance().setOverlay(...);  // Don't do this
}
```

### 2. Use Direct Query for Continuous Display

For data that updates every frame (status bars, counters), query the source directly:

```cpp
// GOOD: Direct query in update()
auto& gt = GameTime::Instance();
ui.setText("time", gt.formatCurrentTime());

// AVOID: Event subscription for continuous display
// (creates unnecessary overhead)
```

### 3. Use Controllers for Event-Driven Logic

For behavior that changes on specific events (weather changes, day/night transitions):

```cpp
// GOOD: Controller processes event, maintains state
void WeatherController::onWeatherCheck(const EventData& data) {
    auto event = std::static_pointer_cast<WeatherCheckEvent>(data.event);
    if (event->getRecommendedWeather() != m_currentWeather) {
        m_currentWeather = event->getRecommendedWeather();
    }
}
```

## Related Documentation

- **ControllerBase:** `include/controllers/ControllerBase.hpp` - Base class
- **GameTime:** `docs/core/GameTime.md` - Time data source
- **EventManager:** `docs/events/EventManager.md` - Event routing
- **GameStateManager:** `docs/managers/GameStateManager.md` - State lifecycle

---

*Controllers provide state-scoped event handling without singleton patterns. GameStates own controllers and handle UI updates.*
