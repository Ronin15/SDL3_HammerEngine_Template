# Manager Documentation Index

This directory contains documentation for all major manager systems in the Hammer Engine. Each manager is responsible for a specific aspect of resource, state, or system management. For a high-level overview of the engine, see the [main documentation hub](../README.md).

---

## Manager Documentation

- **[EntityStateManager](EntityStateManager.md)**  
  Manages named state machines for entities (e.g., player, NPCs). Supports safe state transitions, update delegation, and memory management for entity states.

- **[FontManager](FontManager.md)**  
  Centralized font loading, management, and text rendering. Integrates with DPI-aware systems and UI for crisp, professional text across all displays.

- **[GameStateManager](GameStateManager.md)**  
  Handles the collection and switching of game states/screens (main menu, gameplay, pause, etc). Ensures only one state is active at a time and delegates update/render/input.

- **[InputManager](InputManager.md)**  
  Centralized input handling for keyboard, mouse, and gamepad. Provides event-driven detection, coordinate conversion, and seamless UI integration.

- **[ParticleManager](ParticleManager.md)**  
  High-performance particle system for real-time visual effects (weather, fire, smoke, sparks). Optimized for thousands of particles with threading and memory management.

- **[ResourceTemplateManager](ResourceTemplateManager.md)**  
  Singleton for registering, indexing, and instantiating resource templates (items, loot, blueprints). Supports fast lookup, thread safety, and statistics tracking.

- **[SaveGameManager](SaveGameManager.md)**
  Comprehensive save/load system with binary format, slot management, metadata extraction, and robust error handling. Integrates with the engine's BinarySerializer.

- **[SettingsManager](SettingsManager.md)**
  Thread-safe settings management system with JSON persistence, category organization, type-safe access (int/float/bool/string), and change listener callbacks. Integrates with SettingsMenuState for user configuration.

- **[SoundManager](SoundManager.md)**
  Centralized audio system for sound effects and music playback. Supports multiple formats, volume control, and efficient resource management.

- **[TextureManager](TextureManager.md)**  
  Handles loading, management, and rendering of textures (PNG). Supports batch loading, sprite animation, parallax scrolling, and memory cleanup.

- **[TimestepManager](TimestepManager.md)**  
  Provides consistent game timing with fixed timestep updates and variable timestep rendering. Eliminates timing drift and micro-stuttering.

- **[WorldResourceManager](WorldResourceManager.md)**  
  Singleton for tracking and manipulating resource quantities across multiple worlds. Supports thread-safe operations, batch transfers, and statistics.

---

## Entity Data Systems

These managers provide the core Data-Oriented Design infrastructure for entity management:

- **[EntityDataManager](EntityDataManager.md)**
  Central data authority for all entity data using Structure-of-Arrays (SoA) storage. Provides cache-optimal 64-byte EntityHotData structs, simulation tier management (Active/Background/Hibernated), and thread-safe index-based accessors for parallel batch processing. Single source of truth eliminating 4x position duplication.

- **[BackgroundSimulationManager](BackgroundSimulationManager.md)**
  Processes off-screen (Background tier) entities with simplified simulation. Updates entity positions at 10Hz, handles tier reassignment every 60 frames, and provides power-efficient processing that scales to 100K+ entities.

---

## Additional Major Managers (Documented in Subfolders)

Some advanced managers have their own dedicated documentation folders:

- **[AIManager](../ai/AIManager.md)**  
  High-performance, multi-threaded AI system for managing autonomous entity behaviors, with advanced optimizations and performance monitoring.

- **[EventManager](../events/EventManager.md)**  
  Centralized, type-indexed event system for all game events, with batch processing, threading, and performance tracking.

- **[UIManager](../ui/UIManager_Guide.md)**  
  Comprehensive UI system for SDL3 games, supporting auto-sizing, theming, animation, and advanced layout management.

---

**Note:** All manager documentation is indexed here for discoverability. Some managers are documented in subfolders for clarity and depth.

For a complete overview of all systems, see the [main documentation index](../README.md).
