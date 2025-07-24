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
ResourceTransactionResult addResource(const WorldId& worldId, const ResourceId& resourceId, Quantity quantity);
    // Adds quantity to a resource in a world.
ResourceTransactionResult removeResource(const WorldId& worldId, const ResourceId& resourceId, Quantity quantity);
    // Removes quantity from a resource in a world.
ResourceTransactionResult setResource(const WorldId& worldId, const ResourceId& resourceId, Quantity quantity);
    // Sets the quantity of a resource in a world.
```

### Resource Queries
```cpp
Quantity getResourceQuantity(const WorldId& worldId, const ResourceId& resourceId) const;
    // Gets the quantity of a resource in a world.
bool hasResource(const WorldId& worldId, const ResourceId& resourceId, Quantity minimumQuantity = 1) const;
    // Checks if a world has at least the given quantity of a resource.
Quantity getTotalResourceQuantity(const ResourceId& resourceId) const;
    // Gets the total quantity of a resource across all worlds.
std::unordered_map<ResourceId, Quantity> getWorldResources(const WorldId& worldId) const;
    // Gets all resources and quantities for a world.
std::unordered_map<ResourceId, Quantity> getAllResourceTotals() const;
    // Gets total quantities for all resources across all worlds.
```

### Batch Operations
```cpp
bool transferResource(const WorldId& fromWorldId, const WorldId& toWorldId, const ResourceId& resourceId, Quantity quantity);
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
bool isValidResourceId(const ResourceId& resourceId) const;
bool isValidQuantity(Quantity quantity) const;
```

## Usage Example
```cpp
auto& wrm = WorldResourceManager::Instance();
wrm.init();

// Create a world and add resources
wrm.createWorld("main_world");
wrm.addResource("main_world", "iron_ore", 100);

// Query resource totals
auto totalIron = wrm.getTotalResourceQuantity("iron_ore");

// Transfer resources between worlds
wrm.createWorld("secondary_world");
wrm.transferResource("main_world", "secondary_world", "iron_ore", 50);
```

## Thread Safety
All public methods are thread-safe via internal locking. For best performance, batch operations where possible.

## Best Practices
- Use unique, descriptive world and resource IDs.
- Use `getStats()` and `getMemoryUsage()` for debugging and optimization.
- Clean up unused worlds to free memory.

## See Also
- `ResourceTemplateManager` (for resource templates)
- `Resource` (resource base class)
