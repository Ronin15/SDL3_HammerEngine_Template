# ResourceFactory Documentation

**Where to find the code:**
- Implementation: `src/managers/ResourceFactory.cpp`
- Header: `include/managers/ResourceFactory.hpp`

## Overview

The `ResourceFactory` is a static class that provides a centralized mechanism for creating `Resource` instances from JSON data. It uses a registry of creator functions to map JSON "type" fields to C++ resource class constructors, allowing for an extensible resource loading system.

## Table of Contents

- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [API Reference](#api-reference)
- [Best Practices](#best-practices)

## Quick Start

### Registering a Custom Resource

```cpp
// In your game's initialization code
#include "managers/ResourceFactory.hpp"
#include "MyCustomResource.hpp"

// Register a creator function for your custom resource type
HammerEngine::ResourceFactory::registerCreator("MyCustomType", [](const JsonValue& json) -> ResourcePtr {
    // Your custom creation logic here
    auto resource = std::make_shared<MyCustomResource>();
    // ... populate resource from json ...
    return resource;
});
```

### Creating a Resource from JSON

```cpp
// Assuming you have a JsonValue object from a JsonReader
JsonValue resourceJson = ...;

// Create the resource using the factory
ResourcePtr resource = HammerEngine::ResourceFactory::createFromJson(resourceJson);

if (resource) {
    // Use the created resource
}
```

## Architecture

The `ResourceFactory` is a static class, meaning you don't need to create an instance of it. It maintains a static map of resource type names to creator functions. When `createFromJson` is called, it looks up the appropriate creator function based on the "type" field in the JSON data and invokes it to create the resource instance.

The factory is initialized with a set of default resource creators for the engine's built-in resource types.

## API Reference

### Core Methods

- `static ResourcePtr createFromJson(const JsonValue& json)`: Creates a `Resource` instance from a `JsonValue`.
- `static bool registerCreator(const std::string& typeName, ResourceCreator creator)`: Registers a new resource creator function.
- `static bool hasCreator(const std::string& typeName)`: Checks if a creator is registered for a given type.
- `static std::vector<std::string> getRegisteredTypes()`: Returns a list of all registered resource types.
- `static void initialize()`: Initializes the factory with default resource creators.
- `static void clear()`: Clears all registered creators (for testing purposes only).

## Adding Custom Resource Types

To add a new resource type to the engine, follow these steps:

### Step 1: Create Resource Class

Create a class that inherits from the `Resource` base class:

```cpp
// include/resources/MyCustomResource.hpp
#pragma once
#include "resources/Resource.hpp"

namespace HammerEngine {

class MyCustomResource : public Resource {
public:
    MyCustomResource() = default;

    // Required: Resource type identifier
    std::string getType() const override { return "custom_type"; }

    // Custom properties
    float getDamage() const { return m_damage; }
    void setDamage(float damage) { m_damage = damage; }

    int getDurability() const { return m_durability; }
    void setDurability(int durability) { m_durability = durability; }

private:
    float m_damage{10.0f};
    int m_durability{100};
};

} // namespace HammerEngine
```

### Step 2: Implement JSON Parsing

Create a factory function that parses JSON and creates the resource:

```cpp
// In your initialization code or a dedicated registration file
#include "managers/ResourceFactory.hpp"
#include "resources/MyCustomResource.hpp"
#include "utils/JsonReader.hpp"

ResourcePtr createCustomResourceFromJson(const JsonValue& json) {
    auto resource = std::make_shared<MyCustomResource>();

    // Parse base Resource properties (name, description, etc.)
    if (json.contains("name")) {
        resource->setName(json["name"].get<std::string>());
    }

    // Parse custom properties
    if (json.contains("damage")) {
        resource->setDamage(json["damage"].get<float>());
    }
    if (json.contains("durability")) {
        resource->setDurability(json["durability"].get<int>());
    }

    return resource;
}
```

### Step 3: Register with Factory

Register your creator function during game initialization:

```cpp
// In Game::init() or similar initialization function
void registerCustomResources() {
    HammerEngine::ResourceFactory::registerCreator(
        "custom_type",
        createCustomResourceFromJson
    );
}
```

### Step 4: Define in JSON

Create resource definitions in your data files:

```json
// res/data/custom_resources.json
{
    "resources": [
        {
            "type": "custom_type",
            "name": "Fire Sword",
            "description": "A sword wreathed in flames",
            "damage": 25.0,
            "durability": 150
        },
        {
            "type": "custom_type",
            "name": "Ice Dagger",
            "description": "A dagger of frozen steel",
            "damage": 15.0,
            "durability": 80
        }
    ]
}
```

### Step 5: Load Resources

Load your resources using ResourceTemplateManager:

```cpp
// Load all resources from file
auto& rtm = ResourceTemplateManager::Instance();
rtm.loadFromFile("res/data/custom_resources.json");

// Access by name
ResourceHandle fireHandle = rtm.getHandleByName("Fire Sword");
const Resource* fireSword = rtm.getResource(fireHandle);

// Cast to specific type if needed
const auto* customRes = dynamic_cast<const MyCustomResource*>(fireSword);
if (customRes) {
    float damage = customRes->getDamage();
}
```

### Registration Order

Resources should be registered **before** loading any JSON files that use them:

```cpp
void Game::init() {
    // 1. Initialize managers
    ResourceFactory::initialize();  // Registers built-in types

    // 2. Register custom types
    registerCustomResources();

    // 3. Load resource files (now includes custom types)
    ResourceTemplateManager::Instance().loadFromFile("res/data/resources.json");
}
```

## Best Practices

- **Register Early**: Register all your custom resource creators during your game's initialization phase.
- **Handle Null**: Always check if `createFromJson` returns a `nullptr`, which indicates that the resource creation failed.
- **Extensibility**: Use the factory to create all your resource types to maintain a consistent and extensible resource loading pipeline.
- **Testing**: When writing tests for your resources, you can use the `clear()` method to isolate your tests from the default registered types.
- **Type Safety**: Always validate JSON fields exist before accessing them to avoid runtime errors.
- **Consistent Naming**: Use snake_case for JSON type names (e.g., "custom_type", "fire_weapon").
