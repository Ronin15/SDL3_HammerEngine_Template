# ResourceTemplateManager Documentation

**Where to find the code:**
- Implementation: `src/managers/ResourceTemplateManager.cpp`
- Header: `include/managers/ResourceTemplateManager.hpp`

## Overview

The `ResourceTemplateManager` is a singleton responsible for registering, indexing, and instantiating resource templates (such as item, loot, or resource blueprints) in the game. It provides fast lookup by ID, category, or type, and supports thread-safe operations and statistics tracking.

## Key Features
- Singleton access pattern
- Register and retrieve resource templates by ID
- Query resources by category or type
- Thread-safe operations with shared_mutex
- Resource creation from templates
- Statistics and memory usage tracking

## API Reference

### Singleton Access
```cpp
static ResourceTemplateManager& Instance();
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

### Resource Template Management
```cpp
bool registerResourceTemplate(const ResourcePtr& resource);
    // Registers a new resource template. Returns true on success.
ResourcePtr getResourceTemplate(const std::string& resourceId) const;
    // Retrieves a resource template by ID, or nullptr if not found.
std::vector<ResourcePtr> getResourcesByCategory(ResourceCategory category) const;
    // Returns all templates in a category.
std::vector<ResourcePtr> getResourcesByType(ResourceType type) const;
    // Returns all templates of a given type.
```

### Resource Creation
```cpp
ResourcePtr createResource(const std::string& resourceId) const;
    // Creates a new resource instance from a template by ID.
```

### Statistics & Query
```cpp
ResourceStats getStats() const;
    // Returns statistics on templates loaded, resources created/destroyed.
void resetStats();
    // Resets all statistics.
size_t getResourceTemplateCount() const;
    // Returns the number of registered templates.
bool hasResourceTemplate(const std::string& resourceId) const;
    // Checks if a template exists.
size_t getMemoryUsage() const;
    // Returns estimated memory usage in bytes.
```

## Usage Example
```cpp
auto& rtm = ResourceTemplateManager::Instance();
rtm.init();

// Register a template
rtm.registerResourceTemplate(std::make_shared<Resource>("iron_ore", ...));

// Create a resource from template
ResourcePtr iron = rtm.createResource("iron_ore");

// Query by category
auto ores = rtm.getResourcesByCategory(ResourceCategory::Ore);
```

## Thread Safety
All public methods are thread-safe via internal locking. For best performance, batch registrations and queries where possible.

## Best Practices
- Register all templates at game startup.
- Use unique, descriptive IDs for each resource template.
- Use `getStats()` and `getMemoryUsage()` for debugging and optimization.

## See Also
- `Resource` (resource base class)
- `WorldResourceManager` (for tracking resource quantities)
