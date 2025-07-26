# ResourceTemplateManager Documentation

**Where to find the code:**
- Implementation: `src/managers/ResourceTemplateManager.cpp`
- Header: `include/managers/ResourceTemplateManager.hpp`
- Factory: `include/managers/ResourceFactory.hpp`, `src/managers/ResourceFactory.cpp`

## Overview

The `ResourceTemplateManager` is a singleton responsible for registering, indexing, and instantiating resource templates (such as item, loot, or resource blueprints) in the game. It provides fast lookup by ID, category, or type, supports thread-safe operations, statistics tracking, and **JSON-based resource loading** for extensible content creation.

## Key Features
- Singleton access pattern
- Register and retrieve resource templates by ID
- Query resources by category or type
- Thread-safe operations with shared_mutex
- Resource creation from templates
- Statistics and memory usage tracking
- **JSON-based resource loading and extensible type system**
- Support for all resource types (Equipment, Consumables, Materials, Currency, etc.)

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

### JSON Loading (NEW)
```cpp
bool loadResourcesFromJson(const std::string& filename);
    // Loads resources from a JSON file. Returns true if all resources loaded successfully.
bool loadResourcesFromJsonString(const std::string& jsonString);
    // Loads resources from a JSON string. Returns true if all resources loaded successfully.
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
// Get loaded resource
ResourcePtr magicSword = rtm.getResourceTemplate("magic_sword");
if (magicSword) {
    std::cout << "Found: " << magicSword->getName() << std::endl;
    
    // Check if it's an Equipment
    auto equipment = std::dynamic_pointer_cast<Equipment>(magicSword);
    if (equipment) {
        std::cout << "Attack Bonus: " << equipment->getAttackBonus() << std::endl;
    }
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
```cpp
auto& rtm = ResourceTemplateManager::Instance();
rtm.init();

// Load resources from JSON files
rtm.loadResourcesFromJson("res/data/items.json");
rtm.loadResourcesFromJson("res/data/materials_and_currency.json");

// Register a custom template programmatically
rtm.registerResourceTemplate(std::make_shared<Resource>("custom_item", ...));

// Create a resource from template
ResourcePtr sword = rtm.createResource("magic_sword");

// Query by category
auto weapons = rtm.getResourcesByCategory(ResourceCategory::Item);
```

## Thread Safety
All public methods are thread-safe via internal locking. For best performance, batch registrations and queries where possible.

## Best Practices
- **Load JSON resources at game startup** after initializing the manager
- Use unique, descriptive IDs for each resource template
- Organize resources into logical JSON files (items.json, materials.json, etc.)
- Use `getStats()` and `getMemoryUsage()` for debugging and optimization
- **Validate JSON files** before deploying to catch syntax errors early
- Use the `properties` object for type-specific data to keep the schema extensible

## Error Handling
- JSON loading methods return `false` if any resource fails to load
- Invalid resources are skipped but valid ones are still loaded
- Detailed error messages are logged for debugging
- Unknown resource types fall back to base `Resource` class

## See Also
- `Resource` (resource base class)
- `ResourceFactory` (JSON deserialization)
- `WorldResourceManager` (for tracking resource quantities)
- `JsonReader` (JSON parsing utility)
