# SDL3 HammerEngine Architecture

## High-Level System Overview

```mermaid
graph TB
    subgraph Core["Core Layer"]
        GE[GameEngine]
        TS[ThreadSystem]
        TM[TimestepManager]
        WB[WorkerBudget]
        LOG[Logger]
    end

    subgraph Managers["Manager Layer"]
        subgraph EntitySystems["Entity Systems"]
            EDM[EntityDataManager]
            ESM[EntityStateManager]
            BSM[BackgroundSimulationManager]
        end

        subgraph AI["AI & Movement"]
            AIM[AIManager]
            PM[PathfinderManager]
            CM[CollisionManager]
        end

        subgraph Rendering["Rendering & Audio"]
            UIM[UIManager]
            TXM[TextureManager]
            FM[FontManager]
            SM[SoundManager]
            PART[ParticleManager]
        end

        subgraph World["World Systems"]
            WM[WorldManager]
            WRM[WorldResourceManager]
            GTM[GameTimeManager]
        end

        subgraph GameLoop["Game Loop"]
            GSM[GameStateManager]
            IM[InputManager]
            EM[EventManager]
        end

        subgraph Resources["Resource Management"]
            RTM[ResourceTemplateManager]
            RF[ResourceFactory]
            SAVE[SaveGameManager]
            SET[SettingsManager]
        end
    end

    subgraph States["GameState Layer"]
        LOGO[LogoState]
        MAIN[MainMenuState]
        SETTINGS[SettingsMenuState]
        PLAY[GamePlayState]
        PAUSE[PauseState]
        DEMOS[Demo States]
    end

    subgraph Entities["Entity Layer"]
        ENT[Entity Base]
        PLAYER[Player]
        NPC[NPC]
        RESOURCE[Resource]
        ITEM[DroppedItem]
    end

    GE --> TS
    GE --> TM
    GE --> WB
    GE --> LOG
    GE --> GSM

    GSM --> States
    States --> Managers

    AIM --> PM
    AIM --> CM
    AIM --> EDM

    WM --> TXM
    WM --> CM

    PART --> TXM

    UIM --> FM
    UIM --> TXM
    UIM --> IM

    EDM --> Entities
    BSM --> EDM
```

## Initialization Dependency Graph

```mermaid
graph LR
    subgraph Phase1["Phase 1: Core Infrastructure"]
        LOG1[Logger]
        TS1[ThreadSystem]
        IM1[InputManager]
        TXM1[TextureManager]
    end

    subgraph Phase2["Phase 2: Event Infrastructure"]
        EM2[EventManager]
        SET2[SettingsManager]
        SAVE2[SaveManager]
    end

    subgraph Phase3["Phase 3: Game Systems"]
        PM3[PathfinderManager]
        CM3[CollisionManager]
        UIM3[UIManager]
        SM3[SoundManager]
    end

    subgraph Phase4["Phase 4: High-Level Systems"]
        AIM4[AIManager]
        PART4[ParticleManager]
        WM4[WorldManager]
    end

    subgraph Phase5["Phase 5: Post-Init"]
        WM5[WorldManager::setupEventHandlers]
    end

    Phase1 --> Phase2
    Phase2 --> Phase3
    Phase3 --> Phase4
    Phase4 --> Phase5
```

## Update Loop Flow

```mermaid
sequenceDiagram
    participant GE as GameEngine
    participant IM as InputManager
    participant EM as EventManager
    participant GSM as GameStateManager
    participant AIM as AIManager
    participant CM as CollisionManager
    participant BSM as BackgroundSimulationManager

    GE->>IM: update()
    Note over IM: Keyboard/Mouse/Gamepad

    GE->>EM: update()
    Note over EM: Process queued events

    GE->>GSM: update()
    Note over GSM: Active state logic

    GE->>AIM: update()
    Note over AIM: 10K+ entity behaviors

    GE->>CM: update()
    Note over CM: Collision detection

    GE->>BSM: update()
    Note over BSM: Off-screen entities
```

## Render Pipeline

```mermaid
sequenceDiagram
    participant GE as GameEngine
    participant GSM as GameStateManager
    participant SR as SceneRenderer
    participant WM as WorldManager
    participant EDM as EntityDataManager
    participant PART as ParticleManager
    participant UIM as UIManager

    GE->>GSM: render(interpolationAlpha)
    GSM->>SR: beginScene(renderer, camera, alpha)
    Note over SR: Set intermediate texture target
    SR-->>GSM: SceneContext (floored camera)

    GSM->>WM: render(flooredCameraX/Y)
    Note over WM: Tile layers (pixel-aligned)

    GSM->>EDM: render entities
    Note over EDM: Sprites (pixel-aligned)

    GSM->>PART: render()
    Note over PART: Particles

    GSM->>SR: endScene()
    Note over SR: Composite with zoom + sub-pixel offset

    GSM->>UIM: render()
    Note over UIM: UI overlays (at 1.0 scale)

    GE->>GE: SDL_RenderPresent()
```

## GameState Flow

```mermaid
stateDiagram-v2
    [*] --> LogoState
    LogoState --> MainMenuState

    MainMenuState --> GamePlayState: New Game
    MainMenuState --> SettingsMenuState: Settings
    MainMenuState --> AIDemoState: AI Demo
    MainMenuState --> UIDemoState: UI Demo
    MainMenuState --> EventDemoState: Event Demo

    SettingsMenuState --> MainMenuState: Back

    GamePlayState --> PauseState: ESC
    PauseState --> GamePlayState: Resume
    PauseState --> MainMenuState: Quit
    PauseState --> SettingsMenuState: Settings

    AIDemoState --> MainMenuState: Back
    UIDemoState --> MainMenuState: Back
    EventDemoState --> MainMenuState: Back
```

## Entity Hierarchy

```mermaid
classDiagram
    class Entity {
        <<abstract>>
        +EntityHandle handle
        +EntityKind kind
        +update()
        +render()
    }

    class Player {
        +InventoryComponent inventory
        +updateInput()
        +updateCombat()
    }

    class NPC {
        +AIBehavior* behavior
        +NPCState* currentState
        +updateAI()
    }

    class DroppedItem {
        +ItemData itemData
        +pickUp()
    }

    class Resource {
        +ResourceType type
        +int amount
        +harvest()
    }

    Entity <|-- Player
    Entity <|-- NPC
    Entity <|-- DroppedItem
    Entity <|-- Resource

    class EntityKind {
        <<enumeration>>
        Player
        NPC
        DroppedItem
        Container
        Harvestable
        Projectile
        AreaEffect
        Prop
        Trigger
        StaticObstacle
    }
```

## AI System Architecture

```mermaid
graph TB
    subgraph AIManager["AIManager"]
        DISPATCH[Behavior Dispatch]
        BATCH[SIMD Batch Processing]
        PRIORITY[Priority Queue]
    end

    subgraph Behaviors["AI Behaviors"]
        IDLE[IdleBehavior]
        PATROL[PatrolBehavior]
        WANDER[WanderBehavior]
        GUARD[GuardBehavior]
        FOLLOW[FollowBehavior]
        CHASE[ChaseBehavior]
        FLEE[FleeBehavior]
        ATTACK[AttackBehavior]
    end

    subgraph Pathfinding["PathfinderManager"]
        GRID[PathfindingGrid]
        ASTAR[A* Algorithm]
        SMOOTH[PathSmoother]
        CACHE[Path Cache]
    end

    subgraph Data["Entity Data"]
        EDM[EntityDataManager]
        HOT[AIHotData]
        COLD[AIColdData]
    end

    AIManager --> Behaviors
    AIManager --> Pathfinding
    AIManager --> Data

    PATROL --> GRID
    CHASE --> GRID
    FLEE --> GRID
    FOLLOW --> GRID
```

## Event System

```mermaid
graph TB
    subgraph EventManager["EventManager"]
        QUEUE[Event Queue]
        DISPATCH[Type-Indexed Dispatch]
        PRIORITY[Priority Processing]
    end

    subgraph CameraEvents["Camera Events"]
        CE1[CameraMovedEvent]
        CE2[CameraZoomChangedEvent]
        CE3[CameraShakeStartedEvent]
        CE4[CameraShakeEndedEvent]
    end

    subgraph WorldEvents["World Events"]
        WE1[WorldEvent]
        WE2[WorldTriggerEvent]
        WE3[WeatherEvent]
        WE4[TimeEvent]
    end

    subgraph EntityEvents["Entity Events"]
        EE1[NPCSpawnEvent]
        EE2[CombatEvent]
        EE3[HarvestResourceEvent]
        EE4[ResourceChangeEvent]
    end

    subgraph CollisionEvents["Collision Events"]
        COL1[CollisionEvent]
        COL2[CollisionObstacleChangedEvent]
    end

    subgraph EffectEvents["Effect Events"]
        FX1[ParticleEffectEvent]
        FX2[SceneChangeEvent]
    end

    EventManager --> CameraEvents
    EventManager --> WorldEvents
    EventManager --> EntityEvents
    EventManager --> CollisionEvents
    EventManager --> EffectEvents
```

## Threading Model

```mermaid
graph TB
    subgraph MainThread["Main Thread"]
        UPDATE[Update Loop]
        RENDER[Render Loop]
    end

    subgraph WorkerBudget["WorkerBudget"]
        ALLOC[Worker Allocation]
        ADAPT[Adaptive Batch Sizing]
    end

    subgraph ThreadSystem["ThreadSystem"]
        POOL[Worker Pool]
        QUEUE[Job Queue]
    end

    subgraph ParallelSystems["Parallel Systems"]
        AI_BATCH[AI Batch Processing]
        PATH_ASYNC[Async Pathfinding]
        PART_SIMD[Particle SIMD Physics]
        EVENT_BATCH[Event Batch Processing]
        COL_BATCH[Collision Batch]
    end

    MainThread --> WorkerBudget
    WorkerBudget --> ThreadSystem
    ThreadSystem --> ParallelSystems
```

## Collision System

```mermaid
graph TB
    subgraph CollisionManager["CollisionManager"]
        BROAD[Broad Phase]
        NARROW[Narrow Phase]
        RESPONSE[Collision Response]
    end

    subgraph SpatialHash["HierarchicalSpatialHash"]
        GRID[Grid Cells]
        LEVELS[Hierarchy Levels]
        CACHE[Obstacle Cache]
    end

    subgraph CollisionData["Collision Data"]
        AABB[AABB Bounds]
        BODY[CollisionBody]
        TAG[TriggerTag]
        INFO[CollisionInfo]
    end

    CollisionManager --> SpatialHash
    SpatialHash --> CollisionData
    BROAD --> GRID
    NARROW --> AABB
```

## Data-Oriented Design (EntityDataManager)

```mermaid
graph TB
    subgraph EntityDataManager["EntityDataManager - Single Source of Truth"]
        subgraph SOA["Structure of Arrays"]
            TRANS[TransformData Array]
            HOT[EntityHotData Array]
            CHAR[CharacterData Array]
            ITEM[ItemData Array]
            PROJ[ProjectileData Array]
        end

        subgraph Tiers["Simulation Tiers"]
            ACTIVE[Active - Full Simulation]
            BG[Background - Position Only]
            HIBER[Hibernated - Stored Only]
        end
    end

    subgraph Performance["Performance"]
        CACHE[Cache-Line Friendly]
        SIMD[SIMD Processing]
        PREFETCH[Batch Prefetch]
    end

    EntityDataManager --> Performance
```

## Directory Structure

```mermaid
graph TB
    subgraph Source["src/"]
        CORE[core/]
        MGR[managers/]
        CTRL[controllers/]
        GS[gameStates/]
        ENT[entities/]
        AI[ai/]
        EVT[events/]
        COL[collisions/]
        UTL[utils/]
        WLD[world/]
    end

    subgraph Include["include/"]
        I_CORE[core/]
        I_MGR[managers/]
        I_CTRL[controllers/]
        I_GS[gameStates/]
        I_ENT[entities/]
        I_AI[ai/]
        I_EVT[events/]
        I_COL[collisions/]
        I_UTL[utils/]
        I_WLD[world/]
    end

    subgraph Tests["tests/"]
        T_UNIT[Unit Tests]
        T_BENCH[Benchmarks]
        T_SCRIPT[Test Scripts]
    end

    subgraph Resources["res/"]
        R_IMG[images/]
        R_FONT[fonts/]
        R_SND[sounds/]
        R_DATA[data/]
    end
```

## Manager Responsibilities

| Manager | Responsibility | Key Features |
|---------|---------------|--------------|
| **GameEngine** | Main loop orchestrator | Fixed timestep, SDL init |
| **GameStateManager** | State machine | Push/pop/change states |
| **EntityDataManager** | DoD entity storage | SOA, simulation tiers |
| **AIManager** | 10K+ entity AI | SIMD batch, behavior dispatch |
| **PathfinderManager** | A* pathfinding | Async grid rebuilds, caching |
| **CollisionManager** | Spatial collision | Hierarchical hash, AABB |
| **EventManager** | Event dispatch | Type-indexed, priority queue |
| **ParticleManager** | 10K+ particles | SIMD physics, camera culling |
| **WorldManager** | Tile rendering | Chunk caching, procedural gen |
| **UIManager** | UI components | DPI scaling, theming |
| **GameTimeManager** | Day/night cycle | Weather scheduling |

## Utility Classes

| Utility | Responsibility | Key Features |
|---------|---------------|--------------|
| **SceneRenderer** | Pixel-perfect zoomed rendering | Intermediate texture, sub-pixel scrolling |
| **Camera** | View management | Follow mode, zoom levels, interpolation |

## Performance Characteristics

- **AI Scale**: 10K+ entities @ 60+ FPS (SIMD batch processing)
- **Particles**: 10K+ particles with camera-aware culling
- **Collision**: O(1) spatial hash lookups
- **Memory**: ~5MB for 100K entities (DoD) vs ~30MB traditional
- **Threading**: Adaptive batch sizing via throughput monitoring
