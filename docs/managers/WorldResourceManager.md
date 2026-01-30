# WorldResourceManager Documentation

**Where to find the code:**
- Implementation: `src/managers/WorldResourceManager.cpp`
- Header: `include/managers/WorldResourceManager.hpp`

## Overview

The `WorldResourceManager` is a singleton responsible for tracking and manipulating resource quantities across multiple worlds (e.g., for global economy, inventory, or simulation). It supports thread-safe operations for adding, removing, querying, and transferring resources, and provides statistics and validation utilities.

## Key Features
- Singleton access pattern
- Track resource quantities per world and globally
- Add, remove, set, and transfer resources
- Query resource quantities and totals
- Batch operations for efficient resource management
- Thread-safe operations with shared_mutex
- Statistics and memory usage tracking

## Demo Features

### GamePlayState Integration
The WorldResourceManager is showcased in the GamePlayState with a fully interactive inventory system featuring enhanced UI and alignment:

**Inventory Controls:**
- **[I]** - Toggle inventory display on/off
- **[1]** - Add 10 gold to inventory
- **[2]** - Add 1 health potion to inventory  
- **[3]** - Add 5 iron ore to inventory
- **[4]** - Add 3 wood to inventory
- **[5]** - Remove 5 gold from inventory

**Enhanced Inventory UI Features:**
- **Professional alignment** - Fixed title positioning and improved text spacing
- **Real-time inventory display** showing all resources and quantities
- **Auto-updating resource counters** (Gold, Health Potions, Total Items)
- **Visual feedback** for inventory operations with smooth updating
- **Inventory capacity tracking** (used slots / max slots)
- **Responsive layout** - 420px height panel with proper vertical spacing
- **Consistent styling** - Matches EventDemoState inventory with professional appearance

### EventDemoState Integration
The WorldResourceManager is also demonstrated in the EventDemoState as part of the ResourceDemo phase with comprehensive visual inventory tracking:

**Event-Based Resource Management:**
- **[6]** - Trigger resource events that add/remove resources via the EventManager
- Automatic cycling through different resource types (gold, health_potion, iron_ore, wood)
- Demonstrates ResourceChangeEvent creation and execution
- Shows event-driven resource management patterns
- Logs all resource operations to the event log with detailed before/after quantities

**Visual Inventory Integration:**
- **Real-time inventory panel** - Right-aligned 280×320px panel displaying all resources
- **Live resource tracking** - Inventory updates automatically during resource events
- **Enhanced event logging** - Shows before/after quantities and operation results
- **Smooth UI updates** - Uses list component for consistent, glitch-free updating
- **Professional styling** - Matches GamePlayState inventory with responsive positioning

**Demonstration Features:**
- Resource quantities are displayed in real-time as events modify the inventory
- Event log shows detailed transaction information (e.g., "Added 5 gold (10 → 15)")
- Visual feedback demonstrates the engine's event-driven resource architecture
- Inventory panel automatically repositions based on window width for responsive design

## API Reference

### Singleton Access
```cpp
static WorldResourceManager& Instance();
```

### Initialization & Cleanup
```cpp
bool init();
    // Initializes the manager. Returns true on success.
bool isInitialized() const;
    // Returns true if initialized.
void clean();
    // Cleans up all resources and resets the manager.
```

### World Management
```cpp
bool createWorld(const WorldId& worldId);
    // Creates a new world for resource tracking.
bool removeWorld(const WorldId& worldId);
    // Removes a world and all its resources.
bool hasWorld(const WorldId& worldId) const;
    // Checks if a world exists.
std::vector<WorldId> getWorldIds() const;
    // Returns all world IDs.
```

### Resource Quantity Management
```cpp
ResourceTransactionResult addResource(const WorldId& worldId, const HammerEngine::ResourceHandle& resourceHandle, Quantity quantity);
    // Adds quantity to a resource in a world.
ResourceTransactionResult removeResource(const WorldId& worldId, const HammerEngine::ResourceHandle& resourceHandle, Quantity quantity);
    // Removes quantity from a resource in a world.
ResourceTransactionResult setResource(const WorldId& worldId, const HammerEngine::ResourceHandle& resourceHandle, Quantity quantity);
    // Sets the quantity of a resource in a world.
```

### Resource Queries
```cpp
Quantity getResourceQuantity(const WorldId& worldId, const HammerEngine::ResourceHandle& resourceHandle) const;
    // Gets the quantity of a resource in a world.
bool hasResource(const WorldId& worldId, const HammerEngine::ResourceHandle& resourceHandle, Quantity minimumQuantity = 1) const;
    // Checks if a world has at least the given quantity of a resource.
Quantity getTotalResourceQuantity(const HammerEngine::ResourceHandle& resourceHandle) const;
    // Gets the total quantity of a resource across all worlds.
std::unordered_map<HammerEngine::ResourceHandle, Quantity> getWorldResources(const WorldId& worldId) const;
    // Gets all resources and quantities for a world.
std::unordered_map<HammerEngine::ResourceHandle, Quantity> getAllResourceTotals() const;
    // Gets total quantities for all resources across all worlds.
```

### Batch Operations
```cpp
bool transferResource(const WorldId& fromWorldId, const WorldId& toWorldId, const HammerEngine::ResourceHandle& resourceHandle, Quantity quantity);
    // Transfers quantity of a resource from one world to another.
bool transferAllResources(const WorldId& fromWorldId, const WorldId& toWorldId);
    // Transfers all resources from one world to another.
```

### Statistics & Query
```cpp
WorldResourceStats getStats() const;
    // Returns statistics on resources tracked, transactions, etc.
void resetStats();
    // Resets all statistics.
size_t getMemoryUsage() const;
    // Returns estimated memory usage in bytes.
```

### Validation
```cpp
bool isValidWorldId(const WorldId& worldId) const;
bool isValidResourceHandle(const HammerEngine::ResourceHandle& resourceHandle) const;
bool isValidQuantity(Quantity quantity) const;
```

## Usage Example
```cpp
auto& wrm = WorldResourceManager::Instance();
wrm.init();

// Get resource handles from ResourceTemplateManager first
auto& rtm = ResourceTemplateManager::Instance();
auto ironOreHandle = rtm.getHandleByName("iron_ore");

// Create a world and add resources using handles
wrm.createWorld("main_world");
wrm.addResource("main_world", ironOreHandle, 100);

// Query resource totals
auto totalIron = wrm.getTotalResourceQuantity(ironOreHandle);

// Transfer resources between worlds
wrm.createWorld("secondary_world");
wrm.transferResource("main_world", "secondary_world", ironOreHandle, 50);
```

## Thread Safety
All public methods are thread-safe via internal locking. For best performance, batch operations where possible.

## Best Practices
- Use unique, descriptive world IDs (WorldId is a string type).
- Always obtain ResourceHandles from ResourceTemplateManager before using them with WorldResourceManager.
- Use `getStats()` and `getMemoryUsage()` for debugging and optimization.
- Clean up unused worlds to free memory.
- Cache ResourceHandles in your game objects rather than looking them up repeatedly.

## Resource System Architecture

The resource system uses a three-tier architecture:

```
┌─────────────────────────────────────────────────────────────────┐
│                      ResourceFactory                             │
│  - Creates Resource instances from JSON                         │
│  - Registry of type → creator functions                         │
│  - Extensible for custom resource types                         │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                   ResourceTemplateManager                        │
│  - Stores Resource templates (immutable definitions)            │
│  - Loads from JSON files                                        │
│  - Returns ResourceHandle for efficient lookups                 │
│  - Single source of truth for "what resources exist"            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    WorldResourceManager                          │
│  - Tracks resource quantities per world                         │
│  - Uses ResourceHandle (not strings) for efficiency             │
│  - Manages runtime resource state (inventories, world drops)    │
│  - Thread-safe add/remove/transfer operations                   │
└─────────────────────────────────────────────────────────────────┘
```

### Resource Flow Example

```cpp
// 1. FACTORY: Define resource types (startup)
ResourceFactory::registerCreator("ore", createOreFromJson);

// 2. TEMPLATE MANAGER: Load resource definitions (game load)
auto& rtm = ResourceTemplateManager::Instance();
rtm.loadFromFile("res/data/resources.json");

// 3. Get handle for efficient runtime use
ResourceHandle ironHandle = rtm.getHandleByName("iron_ore");

// 4. WORLD RESOURCE MANAGER: Track quantities (runtime)
auto& wrm = WorldResourceManager::Instance();
wrm.createWorld("main_world");
wrm.addResource("main_world", ironHandle, 50);

// 5. Query quantities
int ironCount = wrm.getResourceQuantity("main_world", ironHandle);
```

### ResourceHandle-Based API

WorldResourceManager uses `ResourceHandle` instead of string names for all operations:

```cpp
// WRONG: String-based lookups (slow, error-prone)
wrm.addResource("main_world", "iron_ore", 10);  // NOT SUPPORTED

// CORRECT: Handle-based operations (fast, type-safe)
ResourceHandle handle = rtm.getHandleByName("iron_ore");
wrm.addResource("main_world", handle, 10);
```

**Why Handles?**
- **O(1) lookup**: Handle is an index, not a string hash
- **Type safety**: Invalid handles are detected at runtime
- **Memory efficient**: 4-byte handle vs variable-length string
- **Cache friendly**: Handle comparisons are integer comparisons

### Caching Handles in Game Objects

For frequently-used resources, cache handles in your game objects:

```cpp
class Player {
private:
    ResourceHandle m_goldHandle;     // Cached at creation
    ResourceHandle m_healthPotHandle;

public:
    void init() {
        auto& rtm = ResourceTemplateManager::Instance();
        m_goldHandle = rtm.getHandleByName("gold");
        m_healthPotHandle = rtm.getHandleByName("health_potion");
    }

    void addGold(int amount) {
        auto& wrm = WorldResourceManager::Instance();
        wrm.addResource("player_inventory", m_goldHandle, amount);
    }
};
```

### Querying World Resources

```cpp
auto& wrm = WorldResourceManager::Instance();

// Get all resources in a world
auto worldResources = wrm.getWorldResources("main_world");
for (const auto& [handle, quantity] : worldResources) {
    const Resource* res = rtm.getResource(handle);
    std::cout << res->getName() << ": " << quantity << "\n";
}

// Get totals across all worlds
auto totals = wrm.getAllResourceTotals();

// Check if player has enough
bool canBuy = wrm.hasResource("player_inventory", goldHandle, 100);
```

## See Also
- `ResourceTemplateManager` (for resource templates)
- `ResourceFactory` (for creating resources from JSON)
- `Resource` (resource base class)
