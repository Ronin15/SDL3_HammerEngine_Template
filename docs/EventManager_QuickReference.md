# EventManager Quick Reference Guide

## New Convenience Methods (Recommended)

### Weather Events
```cpp
// Create and register weather events in one call
EventManager::Instance().createWeatherEvent("EventName", "WeatherType", intensity, transitionTime);

// Examples
EventManager::Instance().createWeatherEvent("MorningRain", "Rainy", 0.7f, 5.0f);
EventManager::Instance().createWeatherEvent("EveningFog", "Foggy", 0.8f);
EventManager::Instance().createWeatherEvent("Thunderstorm", "Stormy", 0.9f, 3.0f);
EventManager::Instance().createWeatherEvent("ClearSkies", "Clear", 0.0f);
```

**Parameters:**
- `name`: Unique event identifier
- `weatherType`: "Clear", "Rainy", "Stormy", "Foggy", "Snowy", "Windy"
- `intensity`: 0.0-1.0 (default: 0.5)
- `transitionTime`: Seconds for weather transition (default: 5.0)

### Scene Change Events
```cpp
// Create and register scene change events in one call
EventManager::Instance().createSceneChangeEvent("EventName", "TargetScene", "TransitionType", duration);

// Examples
EventManager::Instance().createSceneChangeEvent("ToMainMenu", "MainMenu", "fade", 2.0f);
EventManager::Instance().createSceneChangeEvent("EnterDungeon", "DungeonLevel1", "dissolve", 1.5f);
EventManager::Instance().createSceneChangeEvent("QuickExit", "SafeHouse", "instant");
EventManager::Instance().createSceneChangeEvent("SlideTransition", "NewArea", "slide", 1.0f);
```

**Parameters:**
- `name`: Unique event identifier
- `targetScene`: ID of the destination scene
- `transitionType`: "fade", "dissolve", "wipe", "slide", "instant" (default: "fade")
- `duration`: Transition duration in seconds (default: 1.0)

## Quick Comparison

### Old Way (Still Supported)
```cpp
// 2-3 lines per event
auto event = EventFactory::Instance().createWeatherEvent("Rain", "Rainy", 0.8f);
EventManager::Instance().registerEvent("Rain", event);
```

### New Way (Recommended)
```cpp
// 1 line per event
EventManager::Instance().createWeatherEvent("Rain", "Rainy", 0.8f);
```

**Result: 50% less code for common use cases!**

## Common Patterns

### Multiple Weather Events
```cpp
void setupWeatherSystem() {
    EventManager::Instance().createWeatherEvent("dawn_clear", "Clear", 1.0f);
    EventManager::Instance().createWeatherEvent("morning_fog", "Foggy", 0.6f, 2.0f);
    EventManager::Instance().createWeatherEvent("afternoon_rain", "Rainy", 0.8f, 4.0f);
    EventManager::Instance().createWeatherEvent("evening_storm", "Stormy", 0.9f, 3.0f);
    EventManager::Instance().createWeatherEvent("night_clear", "Clear", 0.0f, 6.0f);
}
```

### Scene Transition Chain
```cpp
void setupAreaTransitions() {
    EventManager::Instance().createSceneChangeEvent("enter_forest", "ForestEntrance", "fade");
    EventManager::Instance().createSceneChangeEvent("forest_to_cave", "CaveEntrance", "dissolve");
    EventManager::Instance().createSceneChangeEvent("cave_to_underground", "Underground", "wipe");
    EventManager::Instance().createSceneChangeEvent("emergency_exit", "Town", "instant");
}
```

### Game State Events
```cpp
bool GameState::enter() {
    // Quick setup of state-specific events
    EventManager::Instance().createWeatherEvent("state_weather", "Clear", 0.5f);
    EventManager::Instance().createSceneChangeEvent("state_exit", "MainMenu", "fade");
    
    // Register handlers
    EventManager::Instance().registerEventHandler("Weather", 
        [this](const std::string& msg) { handleWeather(msg); });
    
    return true;
}
```

## Advanced Usage

### Batch Creation
```cpp
void createEventBatch() {
    // Weather progression
    EventManager::Instance().createWeatherEvent("weather_1", "Clear", 0.0f, 2.0f);
    EventManager::Instance().createWeatherEvent("weather_2", "Cloudy", 0.3f, 3.0f);
    EventManager::Instance().createWeatherEvent("weather_3", "Rainy", 0.7f, 4.0f);
    EventManager::Instance().createWeatherEvent("weather_4", "Stormy", 1.0f, 2.0f);
    
    // Scene sequence
    EventManager::Instance().createSceneChangeEvent("seq_1", "Area1", "fade", 1.0f);
    EventManager::Instance().createSceneChangeEvent("seq_2", "Area2", "slide", 1.5f);
    EventManager::Instance().createSceneChangeEvent("seq_3", "Area3", "dissolve", 2.0f);
}
```

### Error Handling
```cpp
bool success = EventManager::Instance().createWeatherEvent("test", "Rainy", 0.8f);
if (!success) {
    std::cerr << "Failed to create weather event!" << std::endl;
}

// Check if event was registered
if (EventManager::Instance().hasEvent("test")) {
    std::cout << "Event created successfully!" << std::endl;
}
```

## When to Use Each Method

### Use Convenience Methods When:
- ✅ Creating standard weather/scene events
- ✅ Rapid prototyping
- ✅ Simple event setups
- ✅ Learning the system

### Use Traditional Method When:
- ⚙️ Need complex event configuration
- ⚙️ Custom event properties
- ⚙️ Advanced condition setup
- ⚙️ Custom event types

## Migration Guide

### Updating Existing Code
```cpp
// OLD
auto rain = EventFactory::Instance().createWeatherEvent("rain", "Rainy", 0.8f);
EventManager::Instance().registerEvent("rain", rain);

auto scene = EventFactory::Instance().createSceneChangeEvent("exit", "Menu", "fade");
EventManager::Instance().registerEvent("exit", scene);

// NEW (drop-in replacement)
EventManager::Instance().createWeatherEvent("rain", "Rainy", 0.8f);
EventManager::Instance().createSceneChangeEvent("exit", "Menu", "fade");
```

## See Also
- `docs/EventManager.md` - Complete EventManager documentation
- `docs/EventManagerExamples.cpp` - Detailed code examples
- `docs/EventSystem_Integration.md` - Integration with game states