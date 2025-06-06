# Forge Game Engine Documentation

## Table of Contents

- [Core Systems](#core-systems)
  - [Logger System](#logger-system)
  - [Core Engine Systems](#core-engine-systems)
  - [AI System](#ai-system)
  - [Event System](#event-system) 
  - [UI System](#ui-system)
  - [Threading System](#threading-system)
  - [Manager System Documentation](#manager-system-documentation)
- [Getting Started](#getting-started)
- [Key Features](#key-features)
- [Development Workflow](#development-workflow)
- [Support and Troubleshooting](#support-and-troubleshooting)

## Overview

The Forge Game Engine is a high-performance game development framework built on SDL3, featuring advanced AI systems, event management, threading capabilities, and more.

## Core Systems

### Core Engine Systems
Foundation systems that power the game engine architecture.

- **[GameEngine Documentation](core/GameEngine.md)** - Central engine singleton managing all systems
- **[GameLoop Documentation](core/GameLoop.md)** - Industry-standard timing with fixed/variable timestep

### AI System
The AI system provides flexible, thread-safe behavior management for game entities with individual behavior instances and mode-based configuration.

- **[AI System Overview](ai/AIManager.md)** - Complete AI system documentation
- **[🔥 NEW: Behavior Modes](ai/BehaviorModes.md)** - PatrolBehavior and WanderBehavior mode-based system
- **[Behavior Modes Quick Reference](ai/BehaviorModes_QuickReference.md)** - Quick setup guide for behavior modes
- **[AIManager API](ai/AIManager.md)** - Complete API reference
- **[Batched Behavior Assignment](ai/BATCHED_BEHAVIOR_ASSIGNMENT.md)** - Global batched assignment system
- **[Entity Update Management](ai/EntityUpdateManagement.md)** - Entity update system details
- **[AI Developer Guide](ai/DeveloperGuide.md)** - Advanced AI development patterns and techniques

### Event System
Robust event management system supporting weather events, NPC spawning, scene transitions, and custom events.

- **[Event Manager](events/EventManager.md)** - Core event management system
- **[EventManager Quick Reference](events/EventManager_QuickReference.md)** - Convenience methods guide
- **[EventFactory Complete Guide](events/EventFactory.md)** - Advanced event creation with EventDefinition, sequences, and custom creators
- **[EventFactory Quick Reference](events/EventFactory_QuickReference.md)** - Fast API lookup and examples
- **[Event System Integration](events/EventSystem_Integration.md)** - Integration guidelines
- **[Event Manager Threading](events/EventManager_ThreadSystem.md)** - Threading integration

### UI System
Comprehensive UI system with professional theming, animations, layouts, and event handling for creating polished game interfaces.

- **[UIManager Guide](ui/UIManager_Guide.md)** - Complete user guide with examples and best practices
- **[UIManager Architecture](ui/UIManager_Architecture.md)** - System architecture and integration patterns
- **[UIManager Implementation Summary](ui/UIManager_Implementation_Summary.md)** - Technical implementation details
- **[SDL3 Logical Presentation Modes](ui/SDL3_Logical_Presentation_Modes.md)** - SDL3 logical presentation system guide
- **[UI Stress Testing Guide](ui/UI_Stress_Testing_Guide.md)** - UI performance testing framework

### Threading System
High-performance multithreading framework with priority-based task scheduling.

- **[ThreadSystem Overview](ThreadSystem.md)** - Core threading system documentation
- **[ThreadSystem API](ThreadSystem_API.md)** - Complete API reference
- **[ThreadSystem Optimization](ThreadSystem_Optimization.md)** - Performance tuning guide
- **[Worker Budget System](WorkerBudget_System.md)** - Memory optimization and budget management

### Utility Systems
Core utility classes and helper systems used throughout the engine.

- **[Logger System](Logger.md)** - High-performance logging with zero release overhead and system-specific macros
- **[Worker Budget System](WorkerBudget_System.md)** - Memory optimization and budget management for threading

### Manager System Documentation
- **[AIManager](ai/AIManager.md)** - AI behavior management and entity processing
- **[EventManager](events/EventManager.md)** - High-performance event system with type-indexed storage
- **[UIManager](ui/UIManager_Guide.md)** - Professional UI system with theming and animations
- **[FontManager](FontManager.md)** - Font loading and text rendering system
- **[TextureManager](TextureManager.md)** - Texture loading and sprite rendering system  
- **[SoundManager](SoundManager.md)** - Audio playback and sound management system
- **SaveGameManager** - Game save/load system with slot management and binary serialization



## Getting Started

### System Overview
The Forge Game Engine provides several core systems that work together:
- **Core Engine**: GameEngine singleton and GameLoop timing systems
- **Utility Systems**: Logger, Worker Budget, and core utility classes
- **AI System**: Behavior management for NPCs with threading support
- **Event System**: Global event handling for weather, spawning, and custom events
- **UI System**: Professional interface components with theming and animations
- **Threading System**: Multi-threaded task processing with priority scheduling
- **Manager Systems**: Resource management for fonts, textures, audio, and more

### Quick Links
- **[GameEngine Setup](core/GameEngine.md#quick-start)** - Initialize the engine
- **[GameLoop Setup](core/GameLoop.md#quick-start)** - Configure main game loop
- **[Logger Quick Start](Logger.md#quick-start)** - Essential logging setup
- **[AI Quick Start](ai/BehaviorModes_QuickReference.md)** - Set up AI behaviors in minutes
- **[Event Quick Start](events/EventManager_QuickReference.md)** - Event system essentials
- **[UI Quick Start](ui/UIManager_Guide.md#quick-start)** - Create UI components instantly
- **[Threading Setup](ThreadSystem.md#quick-start)** - Initialize multi-threading

## Key Features

### Modern Architecture
- **Singleton Engine Management**: Centralized system coordination
- **Fixed/Variable Timestep**: Deterministic updates with smooth rendering
- **Zero-Overhead Utilities**: Debug logging and memory management without release impact
- **Individual Behavior Instances**: Each NPC gets isolated behavior state
- **Mode-Based Configuration**: Automatic setup for common patterns
- **Professional UI Theming**: Consistent appearance without manual styling
- **Thread-Safe Operations**: Concurrent access without race conditions
- **Resource Management**: Automatic cleanup and efficient memory usage

### Performance Optimized
- **Scales to 5000+ NPCs**: Linear performance scaling
- **Priority-Based Threading**: Critical tasks processed first
- **Efficient UI Rendering**: Only processes visible components
- **Memory Optimizations**: Smart pointers and efficient containers
- **Batched Operations**: Bulk processing for better performance

### Developer Friendly
- **Comprehensive Documentation**: Detailed guides with examples
- **Quick Reference Guides**: Fast API lookup
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
- Core engine issues: See [GameEngine API Reference](core/GameEngine.md#api-reference)
- Game loop issues: See [GameLoop Best Practices](core/GameLoop.md#best-practices)
- Logger issues: See [Logger Best Practices](Logger.md#best-practices)
- AI issues: See [AI Developer Guide](ai/DeveloperGuide.md)
- Event issues: See [Event System Integration](events/EventSystem_Integration.md)
- UI issues: See [UIManager Architecture](ui/UIManager_Architecture.md#troubleshooting)
- Threading issues: See [ThreadSystem Optimization](ThreadSystem_Optimization.md)

---

For specific system details, see the individual documentation files linked above.