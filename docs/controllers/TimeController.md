# TimeController Documentation

**Where to find the code:**
- Header: `include/controllers/world/TimeController.hpp`
- Implementation: `src/controllers/world/TimeController.cpp`
- Tests: `tests/controllers/TimeControllerTests.cpp`

**Singleton Access:** Use `TimeController::Instance()` to access.

## Overview

TimeController is a lightweight controller that logs GameTime events to the UI event log and updates status labels with formatted time information. It subscribes to time and weather events and formats user-friendly messages for display.

## Event Flow

```
GameTime::dispatchTimeEvents() → TimeEvents (Deferred)
  → TimeController handles them
  → UIManager::addEventLogEntry() to display messages
  → UIManager::setText() to update status label
```

## Quick Start

```cpp
#include "controllers/world/TimeController.hpp"

// In GameState::enter()
TimeController::Instance().subscribe("my_event_log");
TimeController::Instance().setStatusLabel("time_status_label");
TimeController::Instance().setStatusFormatMode(TimeController::StatusFormatMode::Extended);

// In GameState::exit()
TimeController::Instance().unsubscribe();
```

## API Reference

### subscribe()

```cpp
void subscribe(const std::string& eventLogId);
```

Subscribe to time events and set the target event log for messages.

**Parameters:**
- `eventLogId`: ID of the UIManager event log component to write to

**Note:** Called when a world state enters, NOT in GameEngine::init().

### unsubscribe()

```cpp
void unsubscribe();
```

Unsubscribe from all time events.

**Note:** Called when a world state exits.

### setStatusLabel()

```cpp
void setStatusLabel(std::string_view labelId);
```

Set the UI label to update with current time information.

**Parameters:**
- `labelId`: ID of the UIManager label component

**Note:** Updates are event-driven (on hour change), not per-frame.

### setStatusFormatMode()

```cpp
void setStatusFormatMode(StatusFormatMode mode);
```

Set the format mode for the status label.

**Parameters:**
- `mode`: `StatusFormatMode::Default` or `StatusFormatMode::Extended`

### isSubscribed()

```cpp
bool isSubscribed() const;
```

Check if currently subscribed to time events.

## Status Format Modes

### Default Mode

Format: `Day X Month, Year Y | HH:MM | TimeOfDay`

Example: `Day 15 Bloomtide, Year 1 | 14:30 | Day`

### Extended Mode

Format: `Day X Month, Year Y | HH:MM TimeOfDay | Season | TempF | Weather | Day/Night`

Example: `Day 15 Bloomtide, Year 1 | 14:30 Day | Summer | 82F | Clear | Daytime`

## Events Handled

| Event Type | Message Logged |
|------------|----------------|
| HourChangedEvent | Day/night transitions: "Night has fallen" / "Dawn breaks" |
| DayChangedEvent | "Day X of Month has begun" |
| MonthChangedEvent | "Month has begun" |
| SeasonChangedEvent | "Season has arrived" |
| YearChangedEvent | "Year X has begun" |
| WeatherEvent | "Weather: Clear/Rainy/etc." |

## Usage Example

```cpp
// GamePlayState.cpp
#include "controllers/world/TimeController.hpp"
#include "managers/UIManager.hpp"

bool GamePlayState::enter() {
    auto& ui = UIManager::Instance();

    // Create event log for time messages
    ui.createEventLog("gameplay_event_log", {10, 400, 300, 200});

    // Create status label for time display
    ui.createLabel("time_status", {10, 10, 400, 30}, "");

    // Subscribe TimeController
    TimeController& timeCtrl = TimeController::Instance();
    timeCtrl.subscribe("gameplay_event_log");
    timeCtrl.setStatusLabel("time_status");
    timeCtrl.setStatusFormatMode(TimeController::StatusFormatMode::Extended);

    return true;
}

void GamePlayState::exit() {
    TimeController::Instance().unsubscribe();
}
```

## Performance Characteristics

- **Per-frame cost:** Zero (event-driven updates only)
- **Memory:** Single pre-allocated string buffer for status formatting
- **Allocations:** Zero per-frame (uses `clear()` + `reserve()` pattern)

## Related Documentation

- **Controller Pattern:** `docs/controllers/README.md`
- **GameTime:** `docs/core/GameTime.md`
- **UIManager:** `docs/ui/UIManager_Guide.md`
