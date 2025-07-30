# ResourceTemplateManager Documentation

**Where to find the code:**
- Implementation: `src/managers/ResourceTemplateManager.cpp`
- Header: `include/managers/ResourceTemplateManager.hpp`
- Factory: `include/managers/ResourceFactory.hpp`, `src/managers/ResourceFactory.cpp`
- Resource Handle: `include/utils/ResourceHandle.hpp`

## Overview

The `ResourceTemplateManager` is a singleton responsible for registering, indexing, and instantiating resource templates (such as item, loot, or resource blueprints) in the game. It provides **high-performance resource handle-based lookups** at runtime, supports thread-safe operations, statistics tracking, and **JSON-based resource loading** for extensible content creation.

**Important Architecture Note:** The system uses a **two-phase approach**:
- **Data Load/Validation Phase**: Uses name-based lookups for JSON loading, validation, and tooling
- **Runtime Phase**: Uses ResourceHandle-based operations for optimal performance and cache-friendliness

## Key Features
- **Resource Handle System**: Fast, cache-friendly lookups using 64-bit handles instead of strings
- **Duplicate Name Detection**: Prevents resource name conflicts during data loading
- **Performance Optimized**: SoA (Structure of Arrays) data layout for frequently accessed properties
- Singleton access pattern
- Register and retrieve resource templates by ResourceHandle
- Query resources by category or type
- Thread-safe operations with shared_mutex
- Resource creation from templates
- Statistics and memory usage tracking
- **JSON-based resource loading and extensible type system**
- Support for all resource types (Equipment, Consumables, Materials, Currency, etc.)

## Resource Handle System

### ResourceHandle Overview
```cpp
namespace HammerEngine {
    class ResourceHandle {
        // 32-bit ID + 16-bit generation for stale reference detection
        // Lightweight, cache-friendly, type-safe resource identification
    };
}
```

### Key Benefits
- **Performance**: 64-bit integer operations vs string hashing/comparison
- **Memory Efficiency**: Handles are 8 bytes vs potentially hundreds for strings
- **Cache Friendly**: Dense data structures, better memory access patterns
- **Stale Reference Detection**: Generation counter prevents use of freed handles
- **Type Safety**: Strong typing prevents accidental string manipulation

### When to Use What
- **Runtime Operations**: Always use ResourceHandle
- **Data Loading/Validation**: Use name-based lookups during JSON loading
- **Development/Tooling**: Name-based lookups for debugging, editor tools

## API Reference

### Singleton Access
```cpp
static ResourceTemplateManager& Instance();
```

### Initialization & Cleanup
```cpp
bool init();
    // Initializes the manager and ResourceFactory. Returns true on success.
bool isInitialized() const;
    // Returns true if initialized.
void clean();
    // Cleans up all resources and resets the manager.
```

### Resource Template Management (Runtime - Use Handles)
```cpp
bool registerResourceTemplate(const ResourcePtr& resource);
    // Registers a new resource template. Returns true on success.
    // Automatically detects duplicate names and rejects them.

ResourcePtr getResourceTemplate(HammerEngine::ResourceHandle handle) const;
    // Retrieves a resource template by handle, or nullptr if not found.

std::vector<ResourcePtr> getResourcesByCategory(ResourceCategory category) const;
    // Returns all templates in a category.

std::vector<ResourcePtr> getResourcesByType(ResourceType type) const;
    // Returns all templates of a given type.

// Fast property access (cache-optimized)
int getMaxStackSize(HammerEngine::ResourceHandle handle) const;
float getValue(HammerEngine::ResourceHandle handle) const;
ResourceCategory getCategory(HammerEngine::ResourceHandle handle) const;
ResourceType getType(HammerEngine::ResourceHandle handle) const;

// Bulk operations for better performance
std::vector<int> getMaxStackSizes(const std::vector<HammerEngine::ResourceHandle>& handles) const;
std::vector<float> getValues(const std::vector<HammerEngine::ResourceHandle>& handles) const;
```

### Resource Template Management (Data Load/Validation - Name-based)
```cpp
ResourcePtr getResourceByName(const std::string& name) const;
    // Use ONLY during data loading, validation, or development tools
    // Returns resource template by display name, or nullptr if not found.

ResourcePtr getResourceById(const std::string& id) const;
    // Use ONLY during data loading, validation, or development tools
    // Returns resource template by JSON ID, or nullptr if not found.

HammerEngine::ResourceHandle getHandleByName(const std::string& name) const;
    // Convert name to handle during initialization/validation phase.

HammerEngine::ResourceHandle getHandleById(const std::string& id) const;
    // Convert JSON ID to handle during initialization/validation phase.
```

### Handle Management
```cpp
HammerEngine::ResourceHandle generateHandle();
    // Generates a new unique handle for resource creation.

bool isValidHandle(HammerEngine::ResourceHandle handle) const;
    // Checks if a handle is valid and points to an existing resource.

void releaseHandle(HammerEngine::ResourceHandle handle);
    // Marks handle as freed for reuse (use with caution).
```

### JSON Loading
```cpp
bool loadResourcesFromJson(const std::string& filename);
    // Loads resources from a JSON file. Returns true if all resources loaded successfully.
    // Automatically detects and rejects duplicate resource names.

bool loadResourcesFromJsonString(const std::string& jsonString);
    // Loads resources from a JSON string. Returns true if all resources loaded successfully.
    // Automatically detects and rejects duplicate resource names.
```

### Resource Creation
```cpp
ResourcePtr createResource(HammerEngine::ResourceHandle handle) const;
    // Creates a new resource instance from a template by handle.
```

### Statistics & Query
```cpp
ResourceStats getStats() const;
    // Returns statistics on templates loaded, resources created/destroyed.

void resetStats();
    // Resets all statistics.

size_t getResourceTemplateCount() const;
    // Returns the number of registered templates.

bool hasResourceTemplate(HammerEngine::ResourceHandle handle) const;
    // Checks if a template exists.

size_t getMemoryUsage() const;
    // Returns estimated memory usage in bytes.
```

## JSON Resource Loading

### JSON Schema

Resources are defined in JSON files with the following structure:

```json
{
  "resources": [
    {
      "id": "unique_resource_id",
      "name": "Display Name",
      "category": "Item|Material|Currency|GameResource",
      "type": "Equipment|Consumable|QuestItem|CraftingComponent|RawResource|Gold|Gem|FactionToken|Energy|Mana|BuildingMaterial|Ammunition",
      "description": "Resource description",
      "value": 100.0,
      "maxStackSize": 1,
      "consumable": false,
      "iconTextureId": "texture_id",
      "properties": {
        // Type-specific properties (see below)
      }
    }
  ]
}
```

### Type-Specific Properties

#### Equipment
```json
"properties": {
  "slot": "Weapon|Helmet|Chest|Legs|Boots|Gloves|Ring|Necklace",
  "attackBonus": 15,
  "defenseBonus": 5,
  "speedBonus": 0,
  "durability": 100,
  "maxDurability": 100
}
```

#### Consumable
```json
"properties": {
  "effect": "HealHP|RestoreMP|BoostAttack|BoostDefense|BoostSpeed|Teleport",
  "effectPower": 50,
  "effectDuration": 0
}
```

#### QuestItem
```json
"properties": {
  "questId": "associated_quest_id"
}
```

#### CraftingComponent
```json
"properties": {
  "componentType": "Metal|Wood|Leather|Fabric|Gem|Essence|Crystal",
  "tier": 3,
  "purity": 0.8
}
```

#### RawResource
```json
"properties": {
  "origin": "Mining|Logging|Harvesting|Hunting|Fishing|Monster",
  "tier": 2,
  "rarity": 4
}
```

#### Currency (Gold/Gem/FactionToken)
```json
"properties": {
  "exchangeRate": 100.0,
  "gemType": "Ruby|Emerald|Sapphire|Diamond",  // Gem only
  "clarity": 8,                                  // Gem only
  "factionId": "guild_faction",                  // FactionToken only
  "reputation": 500                              // FactionToken only
}
```

#### GameResource (Energy/Mana/BuildingMaterial/Ammunition)
```json
"properties": {
  "regenerationRate": 1.5,
  "maxEnergy": 200,                              // Energy only
  "manaType": "Arcane|Divine|Nature|Dark",       // Mana only
  "maxMana": 150,                                // Mana only
  "materialType": "Wood|Stone|Metal|Crystal",    // BuildingMaterial only
  "durability": 100,                             // BuildingMaterial only
  "ammoType": "Arrow|Bolt|Bullet|ThrowingKnife|MagicMissile",  // Ammunition only
  "damage": 25                                   // Ammunition only
}
```

### Loading Examples

#### Programmatic Loading
```cpp
auto& rtm = ResourceTemplateManager::Instance();
rtm.init();

// Load from file
bool success = rtm.loadResourcesFromJson("res/data/items.json");
if (success) {
    std::cout << "All resources loaded successfully!" << std::endl;
}

// Load from string
std::string jsonData = R"({
  "resources": [
    {
      "id": "magic_sword",
      "name": "Magic Sword",
      "category": "Item",
      "type": "Equipment",
      "value": 500,
      "properties": {
        "slot": "Weapon",
        "attackBonus": 25
      }
    }
  ]
})";

bool success2 = rtm.loadResourcesFromJsonString(jsonData);
```

#### Usage After Loading
```cpp
// Phase 1: Data Loading (name-based lookups allowed)
ResourcePtr magicSwordTemplate = rtm.getResourceByName("Magic Sword");
if (magicSwordTemplate) {
    std::cout << "Found: " << magicSwordTemplate->getName() << std::endl;
    
    // Convert to handle for runtime use
    HammerEngine::ResourceHandle swordHandle = magicSwordTemplate->getHandle();
    
    // Check if it's an Equipment
    auto equipment = std::dynamic_pointer_cast<Equipment>(magicSwordTemplate);
    if (equipment) {
        std::cout << "Attack Bonus: " << equipment->getAttackBonus() << std::endl;
    }
}

// Phase 2: Runtime Operations (handle-based only)
HammerEngine::ResourceHandle swordHandle = rtm.getHandleByName("Magic Sword");
if (swordHandle.isValid()) {
    // Fast property access
    int stackSize = rtm.getMaxStackSize(swordHandle);
    float value = rtm.getValue(swordHandle);
    ResourceType type = rtm.getType(swordHandle);
    
    // Create instance from template
    ResourcePtr newSword = rtm.createResource(swordHandle);
}
```

## ResourceFactory

The `ResourceFactory` handles JSON deserialization and type mapping:

### Key Features
- Automatic type registration for all built-in resource types
- Extensible creator registration system
- Safe JSON parsing with error handling
- Fallback to base classes for unknown types

### Custom Type Registration
```cpp
// Register a custom creator
ResourceFactory::registerCreator("MyCustomType", [](const JsonValue& json) -> ResourcePtr {
    // Custom creation logic
    return std::make_shared<MyCustomResource>(/* ... */);
});
```

## Usage Example

### Recommended Pattern: Two-Phase Approach
```cpp
auto& rtm = ResourceTemplateManager::Instance();
rtm.init();

// Phase 1: Data Loading (name-based lookups allowed)
rtm.loadResourcesFromJson("res/data/items.json");
rtm.loadResourcesFromJson("res/data/materials_and_currency.json");

// Convert names to handles during initialization
HammerEngine::ResourceHandle goldHandle = rtm.getHandleByName("Gold");
HammerEngine::ResourceHandle healthPotionHandle = rtm.getHandleByName("Health Potion");
HammerEngine::ResourceHandle swordHandle = rtm.getHandleByName("Magic Sword");

// Phase 2: Runtime Operations (handle-based only)
class GameLogic {
private:
    HammerEngine::ResourceHandle m_goldHandle;
    HammerEngine::ResourceHandle m_healthPotionHandle;
    
public:
    void init() {
        auto& rtm = ResourceTemplateManager::Instance();
        m_goldHandle = rtm.getHandleByName("Gold");  // One-time lookup
        m_healthPotionHandle = rtm.getHandleByName("Health Potion");
    }
    
    void gameplayOperation() {
        auto& rtm = ResourceTemplateManager::Instance();
        
        // Runtime: Fast handle-based operations
        if (m_goldHandle.isValid()) {
            ResourcePtr gold = rtm.createResource(m_goldHandle);
            float goldValue = rtm.getValue(m_goldHandle);  // Cache-optimized
            int maxStack = rtm.getMaxStackSize(m_goldHandle);
        }
        
        // Bulk operations for better performance
        std::vector<HammerEngine::ResourceHandle> handles = {m_goldHandle, m_healthPotionHandle};
        auto values = rtm.getValues(handles);  // Single optimized call
        auto stackSizes = rtm.getMaxStackSizes(handles);
    }
};
```

### Legacy Pattern (Discouraged for Runtime)
```cpp
// DON'T DO THIS in runtime-critical code:
void slowGameplayOperation() {
    auto& rtm = ResourceTemplateManager::Instance();
    
    // SLOW: String-based lookup every frame
    ResourcePtr gold = rtm.getResourceByName("Gold");  // Hash lookup, string comparison
    if (gold) {
        float value = gold->getValue();  // Shared_ptr dereferencing
    }
}
```

## Thread Safety
All public methods are thread-safe via internal locking. For best performance, batch registrations and queries where possible.

## Best Practices

### Performance Guidelines
- **Use ResourceHandles for all runtime operations** - avoid name-based lookups during gameplay
- **Cache handles during initialization** - convert names to handles once, store handles in member variables
- **Use bulk operations** when accessing multiple resources for better cache performance
- **Prefer fast property access methods** (`getMaxStackSize(handle)`) over template dereferencing

### Resource Handle Management
- **Validate handles** before use with `isValidHandle()` or `handle.isValid()`
- **Store handles in game objects** instead of resource names or IDs
- **Use handle comparison** (`handle1 == handle2`) instead of string comparison
- **Convert names to handles** only during data loading, initialization, or validation

### Data Loading Best Practices
- **Load JSON resources at game startup** after initializing the manager
- Use unique, descriptive names for each resource template (enforced by duplicate detection)
- Organize resources into logical JSON files (items.json, materials.json, etc.)
- **Validate JSON files** before deploying to catch syntax errors and duplicate names early
- Use the `properties` object for type-specific data to keep the schema extensible

### Development and Debugging
- Use `getStats()` and `getMemoryUsage()` for debugging and optimization
- **Name-based lookups are acceptable** for development tools, editor interfaces, and debugging
- Use `getResourceByName()` for console commands, admin tools, and data validation
- Monitor resource handle generation to detect potential handle exhaustion

### Thread Safety
All public methods are thread-safe via internal locking. For best performance, batch registrations and queries where possible.

## Error Handling
- JSON loading methods return `false` if any resource fails to load
- **Duplicate resource names are automatically detected and rejected** during loading
- Invalid resources are skipped but valid ones are still loaded  
- Detailed error messages are logged for debugging
- Unknown resource types fall back to base `Resource` class
- **Invalid ResourceHandles return sensible defaults** (e.g., `getMaxStackSize()` returns 1)
- **Stale handles are detected** via generation counters (future enhancement)

## Thread Safety
All public methods are thread-safe via internal locking. For best performance, batch registrations and queries where possible.

## Performance Characteristics

### ResourceHandle Operations (Recommended)
- **Handle validation**: O(1) - simple integer check
- **Property access**: O(1) - direct array/map lookup by handle  
- **Resource creation**: O(1) - direct template lookup
- **Bulk operations**: Cache-optimized, vectorized access patterns

### Name-based Operations (Use Sparingly)
- **Name to handle lookup**: O(1) - hash table lookup
- **Resource by name**: O(1) - hash table + handle lookup
- **Note**: String operations have higher constant factors than integer operations

### Memory Layout
- **SoA optimization**: Frequently accessed properties stored in separate arrays for cache efficiency
- **Handle density**: 64-bit handles vs potentially hundreds of bytes for strings
- **Index locality**: Related data stored contiguously for better prefetching

## See Also
- `Resource` (resource base class)
- `ResourceFactory` (JSON deserialization)
- `WorldResourceManager` (for tracking resource quantities)
- `JsonReader` (JSON parsing utility)
