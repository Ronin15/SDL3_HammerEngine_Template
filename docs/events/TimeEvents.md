# Time Events Documentation

**Where to find the code:**
- Header: `include/events/TimeEvent.hpp`
- Dispatched by: `src/core/GameTime.cpp`
- Handled by: Controllers in `src/controllers/world/`

## Overview

Time events are dispatched by the GameTime system to notify subscribers of time-related changes. All time events inherit from `TimeEvent` and are dispatched with `Deferred` mode to ensure consistent processing.

## Event Type Hierarchy

```
Event (base)
  └── TimeEvent (base for all time events)
        ├── HourChangedEvent
        ├── DayChangedEvent
        ├── MonthChangedEvent
        ├── SeasonChangedEvent
        ├── YearChangedEvent
        ├── WeatherCheckEvent
        └── TimePeriodChangedEvent
```

## TimeEventType Enum

```cpp
enum class TimeEventType {
    HourChanged,        // Every in-game hour
    DayChanged,         // When day advances
    MonthChanged,       // When month changes
    SeasonChanged,      // When season changes
    YearChanged,        // When year increments
    WeatherCheck,       // Periodic weather roll
    TimePeriodChanged   // Time period changes (Morning/Day/Evening/Night)
};
```

## Event Details

### HourChangedEvent

Fired every time the game hour changes.

```cpp
class HourChangedEvent : public TimeEvent {
    int getHour() const;      // 0-23
    bool isNight() const;     // True if between sunset and sunrise
};
```

**Use Cases:**
- Update UI clocks
- Trigger hourly game events
- Check for day/night transitions

### DayChangedEvent

Fired when a new day begins.

```cpp
class DayChangedEvent : public TimeEvent {
    int getDay() const;                    // Total days elapsed (1-based)
    int getDayOfMonth() const;             // Day within current month (1-30)
    int getMonth() const;                  // Current month index (0-based)
    const std::string& getMonthName() const;  // "Bloomtide", etc.
};
```

**Use Cases:**
- Daily quests/events
- Shop inventory refresh
- NPC schedule updates

### MonthChangedEvent

Fired when the month changes.

```cpp
class MonthChangedEvent : public TimeEvent {
    int getMonth() const;                  // Month index (0-based)
    const std::string& getMonthName() const;  // "Bloomtide", "Sunpeak", etc.
    Season getSeason() const;              // Season for this month
};
```

**Use Cases:**
- Monthly events
- Rent/tax collection
- Seasonal preparation

### SeasonChangedEvent

Fired when the season changes.

```cpp
class SeasonChangedEvent : public TimeEvent {
    Season getSeason() const;              // New season
    Season getPreviousSeason() const;      // Previous season
    const std::string& getSeasonName() const;  // "Spring", "Summer", etc.
};
```

**Use Cases:**
- Swap seasonal textures (WorldManager)
- Adjust NPC behaviors
- Enable/disable weather types
- Update ambient sounds

### YearChangedEvent

Fired when a new year begins.

```cpp
class YearChangedEvent : public TimeEvent {
    int getYear() const;  // Year number (starts at 1)
};
```

**Use Cases:**
- Anniversary events
- Long-term game statistics
- Achievement triggers

### WeatherCheckEvent

Fired when GameTime performs an automatic weather roll.

```cpp
class WeatherCheckEvent : public TimeEvent {
    Season getSeason() const;              // Current season
    WeatherType getRecommendedWeather() const;  // Weather rolled
};
```

**Use Cases:**
- WeatherController handles this to apply weather changes
- Custom weather logic can intercept and modify

### TimePeriodChangedEvent

Fired when the time period changes (Morning/Day/Evening/Night).

```cpp
class TimePeriodChangedEvent : public TimeEvent {
    TimePeriod getPeriod() const;          // New period
    TimePeriod getPreviousPeriod() const;  // Previous period
    const TimePeriodVisuals& getVisuals() const;  // Overlay colors
    const char* getPeriodName() const;     // "Morning", "Day", etc.
};
```

**Use Cases:**
- Apply day/night overlay tints
- Spawn ambient particles (fireflies at night)
- Update lighting
- Change NPC behaviors

## TimePeriod Enum

```cpp
enum class TimePeriod : uint8_t {
    Morning = 0,   // 5:00 - 8:00
    Day = 1,       // 8:00 - 17:00
    Evening = 2,   // 17:00 - 21:00
    Night = 3      // 21:00 - 5:00
};
```

## TimePeriodVisuals Struct

Visual configuration for time-of-day overlay effects:

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

## Subscribing to Time Events

### Using EventManager Directly

```cpp
#include "events/TimeEvent.hpp"
#include "managers/EventManager.hpp"

// Subscribe
m_timeToken = EventManager::Instance().registerHandlerWithToken(
    EventTypeId::Time,
    [this](const EventData& data) {
        auto timeEvent = std::static_pointer_cast<TimeEvent>(data.event);

        switch (timeEvent->getTimeEventType()) {
            case TimeEventType::HourChanged:
                handleHourChanged(std::static_pointer_cast<HourChangedEvent>(data.event));
                break;
            case TimeEventType::SeasonChanged:
                handleSeasonChanged(std::static_pointer_cast<SeasonChangedEvent>(data.event));
                break;
            // ... other cases
        }
    }
);

// Unsubscribe
EventManager::Instance().removeHandler(m_timeToken);
```

### Using Controllers (Recommended)

Controllers handle common patterns with zero per-frame allocations:

```cpp
// Time logging to UI
TimeController::Instance().subscribe("my_event_log");

// Weather coordination
WeatherController::Instance().subscribe();

// Day/night visual effects
DayNightController::Instance().subscribe();
```

## Event Filtering Pattern

Use `TimeEventType` for efficient filtering instead of `dynamic_cast`:

```cpp
void onTimeEvent(const EventData& data) {
    auto timeEvent = std::static_pointer_cast<TimeEvent>(data.event);

    // Fast enum check (no virtual call)
    if (timeEvent->getTimeEventType() != TimeEventType::SeasonChanged) {
        return;  // Early exit
    }

    // Now safe to cast
    auto seasonEvent = std::static_pointer_cast<SeasonChangedEvent>(data.event);
    // Handle season change...
}
```

## Dispatch Mode

All time events are dispatched with `EventManager::DispatchMode::Deferred`:

- Events are queued during GameTime::update()
- Processed after update completes
- Ensures consistent game state during handling
- Prevents immediate side effects during time advancement

## Event Flow

```
GameTime::update(deltaTime)
  └── Time advances
        └── Change detected (hour/day/month/season/year)
              └── dispatchTimeEvents()
                    └── EventManager::dispatchEvent(event, Deferred)
                          └── Event queued for processing
                                └── EventManager::update() processes queue
                                      └── Handlers called (Controllers, GameStates)
```

## Related Documentation

- **GameTime**: `docs/core/GameTime.md` - Time system that dispatches events
- **EventManager**: `docs/events/EventManager.md` - Event subscription system
- **TimeController**: `docs/controllers/TimeController.md` - Time event logging
- **WeatherController**: `docs/controllers/WeatherController.md` - Weather handling
- **DayNightController**: `docs/controllers/DayNightController.md` - Time period tracking

---

*This documentation reflects the time event system introduced in the world_time branch.*
