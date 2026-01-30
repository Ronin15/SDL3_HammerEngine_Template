# DayNightController Documentation

**Where to find the code:**
- Header: `include/controllers/world/DayNightController.hpp`
- Implementation: `src/controllers/world/DayNightController.cpp`
- Tests: `tests/controllers/DayNightControllerTests.cpp`

**Ownership:** GameState owns the controller instance (not a singleton).

## Overview

DayNightController tracks time periods (Morning/Day/Evening/Night) and dispatches `TimePeriodChangedEvent` when transitions occur. It also manages smooth lighting interpolation (30-second transitions) and integrates directly with the GPU rendering path.

**Important:** DayNightController now requires `update(dt)` to be called each frame for lighting interpolation.

## Event Flow

```
GameTimeManager::dispatchTimeEvents()
  → HourChangedEvent (Deferred)
    → DayNightController detects period change
      → Sets target lighting values
      → Dispatches TimePeriodChangedEvent with visual config
        → GamePlayState (or other subscribers) handle rendering
```

## Update Pattern (Required)

**Critical:** DayNightController now requires `update(dt)` each frame for smooth lighting interpolation.

```cpp
// In GamePlayState::update()
void GamePlayState::update(float dt) {
    // Update day/night lighting interpolation
    m_dayNightController.update(dt);

    // Other update logic...
}
```

The `update()` method:
1. Interpolates current RGBA values toward target values
2. Updates GPU renderer with current lighting (GPU path only)
3. Transition duration: 30 seconds for full period change

### Lighting Interpolation

When a time period changes, lighting transitions smoothly:

```
Period Change (e.g., Day → Evening)
  → Target values set: {255, 80, 40, 40}
  → update(dt) called each frame:
      current += (target - current) * (dt / 30.0f)
  → After 30 seconds: current == target
```

## GPU Integration

For GPU rendering (`USE_SDL3_GPU`), DayNightController automatically updates the composite shader parameters:

```cpp
void DayNightController::update(float dt) {
    // Interpolate lighting values
    interpolateLighting(dt);

#ifdef USE_SDL3_GPU
    // Update GPU renderer with current lighting
    GPURenderer::Instance().setDayNightParams(
        m_currentR, m_currentG, m_currentB, m_currentA
    );
#endif
}
```

The composite shader applies ambient tinting:
```glsl
vec3 tinted = mix(scene.rgb, scene.rgb * ambientColor.rgb, ambientColor.a);
```

### SDL_Renderer Path

For SDL_Renderer, subscribers still handle rendering via the event system (no change from before):

```cpp
void onTimePeriodChanged(const EventData& data) {
    auto periodEvent = std::static_pointer_cast<TimePeriodChangedEvent>(data.event);
    m_currentVisuals = periodEvent->getVisuals();
}

void render() {
    // Apply time-of-day overlay
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer,
        m_currentVisuals.overlayR, m_currentVisuals.overlayG,
        m_currentVisuals.overlayB, m_currentVisuals.overlayA);
    SDL_RenderFillRect(renderer, nullptr);
}
```

## Quick Start

```cpp
#include "controllers/world/DayNightController.hpp"

// In GamePlayState.hpp - controller as member
class GamePlayState : public GameState {
private:
    DayNightController m_dayNightController;  // Owned by state
    EventHandlerToken m_periodToken;
};

// In GamePlayState::enter()
m_dayNightController.subscribe();

// Subscribe to time period changes for rendering (SDL_Renderer path)
m_periodToken = EventManager::Instance().registerHandlerWithToken(
    EventTypeId::Time,
    [this](const EventData& data) { onTimePeriodChanged(data); }
);

// In GamePlayState::update() - REQUIRED for lighting interpolation
m_dayNightController.update(dt);

// In GamePlayState::exit()
EventManager::Instance().removeHandler(m_periodToken);
m_dayNightController.unsubscribe();
```

## Time Periods

| Period  | Hours       | Visual Overlay (RGBA) | Description |
|---------|-------------|----------------------|-------------|
| Morning | 5:00 - 8:00 | (255, 140, 80, 30)   | Red-orange dawn |
| Day     | 8:00 - 17:00| (255, 255, 200, 8)   | Slight yellow |
| Evening | 17:00 - 21:00| (255, 80, 40, 40)  | Orange-red sunset |
| Night   | 21:00 - 5:00 | (20, 20, 60, 90)    | Dark blue/purple |

## API Reference

### subscribe()

```cpp
void subscribe();
```

Subscribe to time events and start tracking time periods. Dispatches an initial `TimePeriodChangedEvent` with current state.

**Note:** Called when a world state enters, NOT in GameEngine::init().

### unsubscribe()

```cpp
void unsubscribe();
```

Unsubscribe from time events.

**Note:** Called when a world state exits.

### update()

```cpp
void update(float deltaTime);
```

Update lighting interpolation each frame. **Required** for smooth transitions.

**Parameters:**
- `deltaTime`: Time since last frame in seconds

**Actions:**
1. Interpolates RGBA values toward target (30-second transition)
2. Updates GPURenderer with current lighting (GPU path only)

### getCurrentPeriod()

```cpp
TimePeriod getCurrentPeriod() const;
```

Get the current time period.

**Returns:** `TimePeriod::Morning`, `Day`, `Evening`, or `Night`

### getCurrentPeriodString()

```cpp
const char* getCurrentPeriodString() const;
```

Get current time period as a string (zero allocation).

**Returns:** Static string pointer: "Morning", "Day", "Evening", or "Night"

### getCurrentVisuals()

```cpp
TimePeriodVisuals getCurrentVisuals() const;
```

Get the visual configuration for the current period.

**Returns:** `TimePeriodVisuals` struct with overlay RGBA values

### isSubscribed()

```cpp
bool isSubscribed() const;
```

Check if currently subscribed to time events.

## TimePeriodVisuals Struct

```cpp
struct TimePeriodVisuals {
    uint8_t overlayR{0};
    uint8_t overlayG{0};
    uint8_t overlayB{0};
    uint8_t overlayA{0};  // Alpha 0 = no tint

    // Factory methods
    static TimePeriodVisuals getMorning();   // {255, 140, 80, 30}
    static TimePeriodVisuals getDay();       // {255, 255, 200, 8}
    static TimePeriodVisuals getEvening();   // {255, 80, 40, 40}
    static TimePeriodVisuals getNight();     // {20, 20, 60, 90}

    static TimePeriodVisuals getForPeriod(TimePeriod period);
};
```

## Usage Example

```cpp
// GamePlayState.cpp
#include "controllers/world/DayNightController.hpp"

class GamePlayState : public GameState {
private:
    EventManager::HandlerToken m_dayNightToken;
    TimePeriodVisuals m_currentVisuals;

public:
    bool enter() override {
        // Subscribe DayNightController
        DayNightController::Instance().subscribe();

        // Subscribe to time period changes
        m_dayNightToken = EventManager::Instance().registerHandlerWithToken(
            EventTypeId::Time,
            [this](const EventData& data) { onTimePeriodChanged(data); }
        );

        return true;
    }

    void exit() override {
        EventManager::Instance().removeHandler(m_dayNightToken);
        DayNightController::Instance().unsubscribe();
    }

private:
    void onTimePeriodChanged(const EventData& data) {
        auto timeEvent = std::static_pointer_cast<TimeEvent>(data.event);
        if (timeEvent->getTimeEventType() != TimeEventType::TimePeriodChanged) {
            return;
        }

        auto periodEvent = std::static_pointer_cast<TimePeriodChangedEvent>(data.event);
        m_currentVisuals = periodEvent->getVisuals();

        // Optional: Trigger ambient particles
        if (periodEvent->getPeriod() == TimePeriod::Night) {
            spawnFireflies();
        }
    }

    void render() {
        // Apply time-of-day overlay
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer,
            m_currentVisuals.overlayR,
            m_currentVisuals.overlayG,
            m_currentVisuals.overlayB,
            m_currentVisuals.overlayA
        );
        SDL_RenderFillRect(renderer, nullptr);  // Full-screen tint
    }
};
```

## Initial Event on Subscribe

When you call `subscribe()`, DayNightController immediately dispatches a `TimePeriodChangedEvent` with the current time period. This ensures subscribers know the initial state without waiting for the next hour change.

```cpp
// DayNightController::subscribe() automatically dispatches:
TimePeriodChangedEvent(currentPeriod, previousPeriod, currentVisuals)
```

## Period Transition Detection

The controller monitors `HourChangedEvent` and detects when the hour crosses a period boundary:

- 5:00 → Morning begins
- 8:00 → Day begins
- 17:00 → Evening begins
- 21:00 → Night begins

## Performance Characteristics

- **Per-frame cost:** ~0.01ms (interpolation math + GPU uniform update)
- **Memory:** Minimal (handler tokens, current/target lighting state)
- **Allocations:** Zero per-frame
- **Transition duration:** 30 seconds for full period change

## Customizing Visuals

If you need custom overlay colors:

```cpp
void onTimePeriodChanged(const EventData& data) {
    auto periodEvent = std::static_pointer_cast<TimePeriodChangedEvent>(data.event);
    TimePeriod period = periodEvent->getPeriod();

    // Override with custom colors
    switch (period) {
        case TimePeriod::Night:
            // Custom darker night
            m_customVisuals = {10, 10, 40, 120};
            break;
        default:
            m_customVisuals = periodEvent->getVisuals();
    }
}
```

## Related Documentation

- **Controller Pattern:** `docs/controllers/README.md`
- **GameTime:** `docs/core/GameTime.md`
- **TimeEvents:** `docs/events/TimeEvents.md`
- **GPURendering:** `docs/gpu/GPURendering.md` - GPU composite shader integration
