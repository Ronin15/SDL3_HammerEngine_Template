# WeatherController Documentation

**Where to find the code:**
- Header: `include/controllers/world/WeatherController.hpp`
- Implementation: `src/controllers/world/WeatherController.cpp`
- Tests: `tests/controllers/WeatherControllerTests.cpp`

**Singleton Access:** Use `WeatherController::Instance()` to access.

## Overview

WeatherController is a lightweight controller that bridges GameTime weather checks to actual weather changes. It subscribes to `WeatherCheckEvent` (dispatched by GameTime) and triggers actual weather changes via `EventManager::changeWeather()`, which then dispatches `WeatherEvent` for visual effects.

## Event Flow

```
GameTime::checkWeatherUpdate()
  → WeatherCheckEvent (Deferred)
    → WeatherController handles it
      → EventManager::changeWeather() (Deferred)
        → WeatherEvent dispatched
          → ParticleManager handles it → Visual effects rendered
```

## Quick Start

```cpp
#include "controllers/world/WeatherController.hpp"
#include "core/GameTime.hpp"

// In GameState::enter()
GameTime::Instance().enableAutoWeather(true);  // Enable weather checks
WeatherController::Instance().subscribe();

// In GameState::exit()
WeatherController::Instance().unsubscribe();
```

## API Reference

### subscribe()

```cpp
void subscribe();
```

Subscribe to weather check events from GameTime.

**Note:** Called when a world state enters, NOT in GameEngine::init().

### unsubscribe()

```cpp
void unsubscribe();
```

Unsubscribe from weather check events.

**Note:** Called when a world state exits.

### getCurrentWeather()

```cpp
WeatherType getCurrentWeather() const;
```

Get the current weather type.

**Returns:** Current `WeatherType` enum value (defaults to Clear)

### getCurrentWeatherString()

```cpp
const char* getCurrentWeatherString() const;
```

Get current weather as a string (zero allocation).

**Returns:** Static string pointer: "Clear", "Cloudy", "Rainy", "Stormy", "Foggy", "Snowy", or "Windy"

### isSubscribed()

```cpp
bool isSubscribed() const;
```

Check if currently subscribed to weather events.

## Weather Types

```cpp
enum class WeatherType {
    Clear,    // No weather effects
    Cloudy,   // Overcast sky
    Rainy,    // Rain particles
    Stormy,   // Heavy rain + lightning
    Foggy,    // Fog overlay
    Snowy,    // Snow particles
    Windy     // Wind effects on particles
};
```

## Usage Example

```cpp
// GamePlayState.cpp
#include "controllers/world/WeatherController.hpp"
#include "core/GameTime.hpp"

bool GamePlayState::enter() {
    // Enable automatic weather in GameTime
    GameTime::Instance().enableAutoWeather(true);
    GameTime::Instance().setWeatherCheckInterval(4.0f);  // Every 4 game hours

    // Subscribe WeatherController to handle weather checks
    WeatherController::Instance().subscribe();

    return true;
}

void GamePlayState::update(float deltaTime) {
    // Display current weather
    WeatherType weather = WeatherController::Instance().getCurrentWeather();
    const char* weatherStr = WeatherController::Instance().getCurrentWeatherString();

    // Use in UI or game logic
    if (weather == WeatherType::Rainy || weather == WeatherType::Stormy) {
        // Reduce NPC outdoor activity
        m_outdoorActivityMultiplier = 0.5f;
    }
}

void GamePlayState::exit() {
    WeatherController::Instance().unsubscribe();
}
```

## Integration with ParticleManager

When WeatherController calls `EventManager::changeWeather()`, ParticleManager automatically:

1. Receives the `WeatherEvent`
2. Creates appropriate weather particles (rain, snow, etc.)
3. Manages particle lifecycle until weather changes

You don't need to manually create weather particles - just use WeatherController.

## Manual Weather Control

If you want to override automatic weather:

```cpp
// Disable auto weather
GameTime::Instance().enableAutoWeather(false);

// Manually trigger weather change
EventManager::Instance().changeWeather(WeatherType::Stormy);
```

## Performance Characteristics

- **Per-frame cost:** Zero (event-driven only)
- **Memory:** Minimal (handler tokens, current weather state)
- **Allocations:** Zero per-frame

## Related Documentation

- **Controller Pattern:** `docs/controllers/README.md`
- **GameTime:** `docs/core/GameTime.md`
- **ParticleManager:** `docs/managers/ParticleManager.md`
- **EventManager:** `docs/events/EventManager.md`
