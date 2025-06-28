# Hammer Game Engine Documentation

## Table of Contents

- [Core Systems](#core-systems)
  - [GameEngine & Core Systems](#gameengine--core-systems)
  - [AI System](#ai-system)
  - [Event System](#event-system)
  - [UI System](#ui-system)
  - [Threading System](#threading-system)
  - [Manager Systems](#manager-systems)
  - [Utility Systems](#utility-systems)
- [Getting Started](#getting-started)
- [Key Features](#key-features)
- [Development Workflow](#development-workflow)
- [Support and Troubleshooting](#support-and-troubleshooting)

## Overview

The Hammer Game Engine is a high-performance game development framework built on SDL3, featuring advanced AI systems, event management, threading capabilities, and comprehensive UI support with auto-sizing and DPI awareness.

## Core Systems

### GameEngine & Core Systems
Foundation systems that power the game engine architecture and timing.

- **[GameEngine](GameEngine.md)** - Central engine singleton managing all systems and coordination
- **[GameLoop](GameLoop.md)** - Industry-standard timing with fixed/variable timestep support
- **[TimestepManager](TimestepManager.md)** - Simplified timing system with 1:1 frame mapping

### AI System
The AI system provides flexible, thread-safe behavior management for game entities with individual behavior instances and mode-based configuration.

- **[AI System Overview](ai/AIManager.md)** - Complete AI system documentation with unified architecture, performance optimization, and threading support
- **[Behavior Modes](ai/BehaviorModes.md)** - Comprehensive documentation for all 8 AI behaviors with 32 total modes, configuration examples, and best practices
- **[Behavior Quick Reference](ai/BehaviorQuickReference.md)** - Streamlined quick lookup for all behaviors, modes, and setup patterns

### Event System
Comprehensive event management system with EventManager as the single source of truth for weather events, NPC spawning, scene transitions, and custom events.

- **[EventManager Overview](events/EventManager.md)** - Complete unified documentation for EventManager as the single source of truth
- **[EventManager Quick Reference](events/EventManager_QuickReference.md)** - Fast API lookup for all event functionality through EventManager
- **[EventManager Advanced](events/EventManager_Advanced.md)** - Advanced topics like threading, performance optimization, and complex event patterns
- **[EventManager Examples](events/EventManager_Examples.cpp)** - Comprehensive code examples and best practices

### UI System
Comprehensive UI system with professional theming, animations, layouts, content-aware auto-sizing, and event handling for creating polished game interfaces.

- **[UIManager Guide](ui/UIManager_Guide.md)** - Complete UI system guide with auto-sizing, theming, and SDL3 integration
- **[Auto-Sizing System](ui/Auto_Sizing_System.md)** - Content-aware component sizing with multi-line text support and font-based measurements
- **[DPI-Aware Font System](ui/DPI_Aware_Font_System.md)** - Automatic display detection and font scaling for crisp text on all display types
- **[SDL3 Logical Presentation](ui/SDL3_Logical_Presentation_Modes.md)** - SDL3 presentation system integration and coordinate handling

### Threading System
High-performance multithreading framework with intelligent WorkerBudget allocation and priority-based task scheduling.

- **[ThreadSystem](ThreadSystem.md)** - Complete threading system documentation with WorkerBudget allocation, buffer thread utilization, priority scheduling, engine integration, implementation details, and production best practices
- **WorkerBudget System** - Tiered resource allocation strategy (Tier 1: single-threaded, Tier 2: minimal allocation, Tier 3: AI 60%/Events 30% of remaining workers)
- **Priority-Based Scheduling** - Five-level priority system (Critical, High, Normal, Low, Idle) for optimal task ordering
- **Buffer Thread Utilization** - Intelligent scaling based on workload thresholds (AI: >1000 entities, Events: >100 events)
- **Hardware Adaptive** - Automatic scaling from ultra low-end (single-threaded) to high-end (multi-threaded) systems

### Manager Systems
Resource management systems for fonts, textures, audio, particles, and game data.

- **[ParticleManager](ParticleManager.md)** - High-performance particle system with weather effects, visual effects, WorkerBudget threading, and EventManager integration
- **[FontManager](managers/FontManager.md)** - Font loading, text rendering, and measurement utilities with DPI-aware scaling and auto-sizing integration
- **[SoundManager](managers/SoundManager.md)** - Audio playback and sound management system with volume control and state integration
- **[TextureManager](managers/TextureManager.md)** - Texture loading and sprite rendering system
- **[SaveGameManager](SaveGameManager.md)** - Comprehensive binary save/load system with slot management, metadata extraction, and cross-platform compatibility

### Utility Systems
Core utility classes and helper systems used throughout the engine.

- **[Logger System](Logger.md)** - Comprehensive logging system with debug/release optimization and system-specific macros
- **[Binary Serialization](SERIALIZATION.md)** - Fast, header-only serialization system for game data
- **[Performance Changelog](PERFORMANCE_CHANGELOG.md)** - Detailed performance optimization history and benchmarks

## Getting Started

### System Overview
The Hammer Game Engine provides several core systems that work together:
- **Core Engine**: GameEngine singleton, GameLoop, and TimestepManager timing systems
- **AI System**: Behavior management for NPCs with threading support and distance optimization
- **Event System**: Global event handling for weather, spawning, and custom events
- **UI System**: Professional interface components with theming, animations, and auto-sizing
- **Threading System**: Multi-threaded task processing with priority scheduling and worker budgets
- **Manager Systems**: Resource management for fonts, textures, audio, and more

### Quick Links
- **[GameEngine Setup](GameEngine.md#quick-start)** - Initialize the engine
- **[GameLoop Setup](GameLoop.md#quick-start)** - Configure main game loop
- **[TimestepManager Setup](TimestepManager.md#quick-start)** - Timing system configuration

- **[AI Quick Start](ai/BehaviorQuickReference.md)** - Set up AI behaviors in minutes
- **[Event Quick Start](events/EventManager_QuickReference.md)** - Event system essentials with EventManager
- **[UI Quick Start](ui/UIManager_Guide.md#quick-start)** - Create UI components with auto-sizing
- **[Threading Setup](ThreadSystem.md#quick-start)** - Initialize multi-threading

## Key Features

### Modern Architecture
- **Singleton Engine Management**: Centralized system coordination through GameEngine
- **Fixed/Variable Timestep**: Deterministic updates with smooth rendering via GameLoop
- **Simplified Timing System**: 1:1 frame-to-update mapping eliminates timing drift
- **Zero-Overhead Utilities**: Debug logging and memory management without release impact
- **Individual Behavior Instances**: Each NPC gets isolated behavior state via clone()
- **Mode-Based Configuration**: Automatic setup for common AI and UI patterns
- **Professional UI Theming**: Consistent appearance without manual styling
- **Content-Aware Auto-Sizing**: Components automatically size to fit content
- **DPI-Aware Font System**: Crisp text rendering on all display types
- **SDL3 Coordinate Integration**: Accurate mouse input with logical presentation
- **Thread-Safe Operations**: Concurrent access without race conditions
- **Resource Management**: Automatic cleanup and efficient memory usage

### Performance Optimized
- **Scales to 10,000+ NPCs**: Linear performance scaling with distance optimization and WorkerBudget allocation
- **Priority-Based Threading**: Critical tasks processed first with tiered worker allocation and optimal resource distribution
- **WorkerBudget Allocation**: Intelligent tiered resource distribution (Engine: 1-2 workers, AI: 60% of remaining, Events: 30% of remaining, Buffer: dynamic scaling)
- **Efficient UI Rendering**: Only processes visible components with auto-sizing
- **Memory Optimizations**: Smart pointers and cache-friendly data structures
- **Batched Operations**: Bulk processing for better performance across all systems

### Developer Friendly
- **Comprehensive Documentation**: Detailed guides with examples and quick references
- **Quick Reference Guides**: Fast API lookup for all major systems
- **Debug Tools**: Built-in diagnostics and performance monitoring
- **Best Practice Guides**: Proven patterns and techniques
- **Migration Support**: Guides for updating existing code

## Development Workflow

1. **Read the appropriate system guide** for detailed information
2. **Check quick reference** for API lookups
3. **Review examples** in the documentation
4. **Use debug tools** to monitor performance
5. **Follow best practices** for optimal results

## Support and Troubleshooting

For issues with specific systems, see the troubleshooting sections in each system's documentation:
- Core engine issues: See [GameEngine Documentation](GameEngine.md)
- Game loop issues: See [GameLoop Best Practices](GameLoop.md#best-practices)
- Timing issues: See [TimestepManager Best Practices](TimestepManager.md#best-practices)
- Logger issues: See [Logger Best Practices](Logger.md#best-practices)
- AI issues: See [AI System Overview](ai/AIManager.md) and [Behavior Modes](ai/BehaviorModes.md)
- Event issues: See [EventManager Overview](events/EventManager.md) and [EventManager Advanced](events/EventManager_Advanced.md)
- UI issues: See [UIManager Guide](ui/UIManager_Guide.md), [Auto-Sizing System](ui/Auto_Sizing_System.md), and [DPI-Aware Font System](ui/DPI_Aware_Font_System.md)
- Threading issues: See [ThreadSystem](ThreadSystem.md)

---

For specific system details, see the individual documentation files linked above.
