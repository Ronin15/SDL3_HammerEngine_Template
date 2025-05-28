# Event Demo State Documentation

## Overview

The EventDemoState is a comprehensive demonstration of the game engine's event system capabilities. It showcases various types of events including weather changes, NPC spawning, scene transitions, and custom event combinations.

## Features

### Event Types Demonstrated

1. **Weather Events**
   - Clear, Cloudy, Rainy, Stormy, Foggy, Snowy weather types
   - Smooth transitions between weather states
   - Configurable transition times and intensities

2. **NPC Spawn Events**
   - Multiple NPC types (Guard, Villager)
   - AI behavior integration (Wander, Patrol, Chase)
   - Automatic behavior assignment based on NPC type
   - Maximum 10 NPCs to prevent performance issues
   - Proximity-based spawning around the player

3. **Scene Transition Events**
   - Multiple scene types (Forest, Village, Castle, Dungeon)
   - Different transition effects (fade, dissolve)
   - Configurable transition durations

4. **Custom Event Combinations**
   - Multiple events triggered simultaneously
   - Complex event sequencing
   - Demonstrates event system flexibility

### Demo Modes

#### Automatic Mode
The demo automatically progresses through different phases:
1. **Initialization** (2 seconds)
2. **Weather Demo** (5 seconds) - Cycles through weather types
3. **NPC Spawn Demo** (5 seconds) - Spawns NPCs with AI behaviors
4. **Scene Transition Demo** (5 seconds) - Demonstrates scene changes
5. **Custom Event Demo** (5 seconds) - Multiple simultaneous events
6. **Interactive Mode** (indefinite) - Manual control mode

**Note**: Auto mode is disabled by default. Press 'A' to enable.

#### Interactive Mode
Manual control over all event types using keyboard inputs.

## Controls

### Phase Navigation
- **SPACE**: Advance to next demo phase manually
- **A**: Toggle between automatic and manual mode
- **R**: Reset demo to beginning

### Event Triggers (Available Anytime)
- **1**: Trigger weather event (cycles through weather types)
- **2**: Trigger NPC spawn event (spawns Guard/Villager with AI, max 10)
- **3**: Trigger scene transition event (cycles through scenes)
- **4**: Trigger custom event (multiple simultaneous events)
- **5**: Reset all events to default state

**Rate Limiting**: 1-second cooldown between manual triggers to prevent spam.

### Navigation
- **ESC**: Exit demo and return to main menu

## How to Access

1. **From Main Menu**: Press 'E' to enter Event Demo
2. **From Script**: Run `./run_event_demo.sh` (builds and launches)
3. **Direct Launch**: Run `./bin/debug/SDL3_Template` and press 'E'

## UI Information

The SDL visual UI displays:
- Current demo phase and timer (shows "Manual Control" for Interactive Mode)
- Auto/manual mode status
- Accurate FPS and performance metrics (fixed timing issues)
- Current weather state
- Number of spawned NPCs (with 10 NPC limit enforcement)
- Event log (last 10 events, with spam prevention)
- Condensed control instructions
- All text centered and color-coded for clarity

## Event System Architecture

### GameEngine Integration
The EventSystem is fully integrated into the game engine lifecycle:
```cpp
// GameEngine.cpp - Initialization (Thread #6)
initTasks.push_back(
    Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        if (!EventSystem::Instance().init()) {
            return false;
        }
        return true;
    }));

// GameEngine.cpp - Update (Every Frame)
void GameEngine::update() {
    EventSystem::Instance().update();
    mp_gameStateManager->update();
}

// GameEngine.cpp - Cleanup (Shutdown)
void GameEngine::clean() {
    EventSystem::Instance().clean();
}
```

### EventSystem Class
Central hub for event management:
- Singleton pattern for global access
- Initialized during GameEngine startup
- Updated every frame automatically
- Event registration and triggering
- Handler management and callbacks
- Integration with EventManager backend

### Event Flow Architecture
```
GameEngine → EventSystem → EventManager → Individual Events
     ↓            ↓              ↓              ↓
   init()     → init()      → init()      → setup()
   update()   → update()    → update()    → execute()
   clean()    → clean()     → clean()     → cleanup()
```

### Event Types
- **WeatherEvent**: Handles atmospheric changes
- **NPCSpawnEvent**: Manages entity creation
- **SceneChangeEvent**: Controls environment transitions
- **Custom Events**: Demonstrates extensibility

### Event Handlers
The demo registers handlers for each event type:
```cpp
m_eventSystem->registerEventHandler("Weather", 
    [this](const std::string& message) { onWeatherChanged(message); });
```

## Technical Implementation

### State Management
- Inherits from GameState base class
- Proper initialization and cleanup
- Thread-safe event handling
- Resource management for spawned entities

### Performance Considerations
- Frame rate monitoring and display
- Efficient event processing
- Memory management for temporary entities
- Configurable update frequencies

### Error Handling
- Exception safety in all operations
- Graceful degradation on failures
- Comprehensive logging for debugging

## Extensibility

The EventDemoState serves as a template for:
- Adding new event types
- Implementing custom event handlers
- Creating complex event sequences
- Testing event system performance

### Adding New Events
1. Create event class inheriting from Event base
2. Register event type in EventSystem
3. Add demo triggers in EventDemoState
4. Update UI and controls documentation

## Testing Scenarios

The demo tests various scenarios:
- **Rapid Event Triggering**: Rate limiting prevents spam and crashes
- **Event Cleanup**: Proper resource management and NPC cleanup on reset
- **State Persistence**: Events surviving state transitions
- **Performance Impact**: Frame rate monitoring with 10 NPC limit
- **Error Recovery**: Handling invalid event parameters
- **AI Integration**: Automatic behavior assignment and management
- **Reset Functionality**: Complete system reset with timing variable cleanup

## Integration with Game Systems

The event system integrates with:
- **AI System**: Automatic behavior assignment to spawned NPCs
  - Guards: Cycle through Patrol → Wander → Chase behaviors
  - Villagers: Cycle through Wander → Patrol behaviors
  - Chase behavior targets the player automatically
- **Rendering System**: Visual effect triggers and NPC sprite rendering
- **Audio System**: Sound effect playback for events
- **State Management**: Scene transitions and cleanup
- **Input System**: User-triggered events with rate limiting

## Development Notes

- Events are processed on the main thread for SDL compatibility
- SDL text rendering provides real-time visual feedback
- All spawned entities are properly cleaned up on exit
- Event logging helps with debugging and monitoring
- Modular design allows easy addition of new event types
- EventSystem automatically initialized and managed by GameEngine
- Thread-safe event processing via EventManager backend

## Visual UI Layout

The SDL window displays a professional, centered layout:

```
=== EVENT DEMO STATE ===
Phase: Weather Demo (2.5s / 5s)
Auto Mode: ON
FPS: 60.0 (Avg: 58.5)
Weather: Rainy | Spawned NPCs: 3

=== CONTROLS ===
SPACE: Next phase | 1: Weather | 2: NPC spawn | 3: Scene change
4: Custom event | 5: Reset | R: Restart | A: Auto toggle | ESC: Exit

=== EVENT LOG ===
• AI Behaviors configured for NPC integration
• Weather changed to: Rainy  
• Created Guard at (450, 320) with AI
• Guard assigned Patrol behavior
• Scene Change Event: Forest
• Cannot spawn - NPC limit reached (10)
```

### Color Coding
- **Yellow**: Titles and performance metrics
- **White**: General information text
- **Green**: Status indicators (Auto Mode)
- **Cyan**: Section headers

### Layout Features
- All text properly centered for professional appearance
- Optimal 22px line spacing prevents overlap
- Condensed controls save vertical space
- Recent event log with bullet points
- Real-time updates without flicker

## Recent Fixes and Improvements

### Stability Fixes
- Fixed runaway event triggering that caused crashes
- Implemented proper rate limiting (3-second intervals for auto, 1-second for manual)
- Added NPC limit enforcement (maximum 10) for performance
- Fixed phase transition timing issues
- Corrected FPS calculation for accurate performance monitoring

### AI Integration
- Added automatic AI behavior assignment to spawned NPCs
- Guards cycle through Patrol, Wander, and Chase behaviors
- Villagers cycle through Wander and Patrol behaviors
- Chase behavior automatically targets the player
- Smooth NPC movement without boundary bouncing

### User Experience
- Fixed Interactive Mode UI to show "Manual Control" instead of confusing timer
- Added proper reset functionality that clears all timing variables
- Improved event log with spam prevention for repeated messages
- Enhanced auto mode with reliable phase transitions
- Clear feedback when hitting NPC spawn limits

## Future Enhancements

Potential improvements:
- Save/load event configurations
- Event scripting system
- Network event synchronization
- Additional NPC types with unique AI behaviors
- Visual event timeline
- Performance profiling tools