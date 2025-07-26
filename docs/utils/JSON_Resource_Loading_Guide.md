# JSON Resource Loading - Quick Start Guide

This guide shows how to use the new JSON-based resource loading system in HammerEngine.

## Overview

The HammerEngine now supports loading different resource types from JSON files, making it easy to add new items, materials, currency, and game resources without recompiling the engine.

## Quick Setup

### 1. Initialize the System
```cpp
#include "managers/ResourceTemplateManager.hpp"

// In your game initialization code
auto& resourceManager = ResourceTemplateManager::Instance();
resourceManager.init(); // This also initializes the ResourceFactory
```

### 2. Load Resources from JSON
```cpp
// Load from file
bool success = resourceManager.loadResourcesFromJson("res/data/items.json");
if (!success) {
    GAMEENGINE_ERROR("Failed to load items.json");
}

// Or load from string
std::string jsonData = R"({
  "resources": [
    {
      "id": "health_potion",
      "name": "Health Potion",
      "category": "Item",
      "type": "Consumable",
      "description": "Restores health when consumed",
      "value": 50,
      "maxStackSize": 10,
      "consumable": true,
      "properties": {
        "effect": "HealHP",
        "effectPower": 50,
        "effectDuration": 0
      }
    }
  ]
})";

resourceManager.loadResourcesFromJsonString(jsonData);
```

### 3. Use Loaded Resources
```cpp
// Get a resource template
auto healthPotion = resourceManager.getResourceTemplate("health_potion");
if (healthPotion) {
    std::cout << "Found: " << healthPotion->getName() << std::endl;
    
    // Check if it's a consumable
    auto consumable = std::dynamic_pointer_cast<Consumable>(healthPotion);
    if (consumable) {
        std::cout << "Effect Power: " << consumable->getEffectPower() << std::endl;
    }
}

// Create instances from templates
auto potionInstance = resourceManager.createResource("health_potion");
```

## JSON Schema Overview

All resources share these common fields:
- `id`: Unique identifier (string)
- `name`: Display name (string) 
- `category`: Resource category ("Item", "Material", "Currency", "GameResource")
- `type`: Specific type (see supported types below)
- `description`: Description text (optional)
- `value`: Monetary value (optional, default 0)
- `maxStackSize`: Maximum stack size (optional, default 1)
- `consumable`: Whether resource can be consumed (optional, default false)
- `iconTextureId`: Texture ID for icon (optional)
- `properties`: Type-specific properties (optional)

## Supported Resource Types

### Items
- **Equipment**: Weapons, armor, accessories
- **Consumable**: Potions, food, scrolls  
- **QuestItem**: Keys, documents, special objects

### Materials
- **CraftingComponent**: Processed materials for crafting
- **RawResource**: Unprocessed materials from gathering

### Currency
- **Gold**: Base currency
- **Gem**: Precious stones
- **FactionToken**: Reputation-based currency

### Game Resources
- **Energy**: Stamina, action points
- **Mana**: Magical energy
- **BuildingMaterial**: Construction materials
- **Ammunition**: Arrows, bullets, projectiles

## Example JSON Files

See the example files in `res/data/`:
- `items.json` - Equipment, consumables, quest items
- `materials_and_currency.json` - Materials, currency, game resources

## Error Handling

```cpp
bool success = resourceManager.loadResourcesFromJson("my_resources.json");
if (!success) {
    // Check logs for specific error messages
    // Invalid resources are skipped, valid ones are still loaded
    std::cout << "Some resources failed to load, check logs" << std::endl;
}
```

## Adding Custom Resource Types

```cpp
// Register a custom creator function
ResourceFactory::registerCreator("MyCustomType", [](const JsonValue& json) -> ResourcePtr {
    std::string id = json["id"].asString();
    std::string name = json["name"].asString();
    
    // Create your custom resource
    auto resource = std::make_shared<MyCustomResource>(id, name);
    
    // Set common properties
    ResourceFactory::setCommonProperties(resource, json);
    
    return resource;
});
```

## Performance Tips

- Load all JSON resources at startup, not during gameplay
- Use the `getStats()` method to monitor memory usage
- Organize resources into logical files (items.json, materials.json, etc.)
- Validate JSON syntax before deploying

## Testing

Run the tests to verify functionality:
```bash
./bin/debug/ResourceFactoryTests
./bin/debug/ResourceTemplateManagerJsonTests  
```

For more detailed information, see the full ResourceTemplateManager documentation.