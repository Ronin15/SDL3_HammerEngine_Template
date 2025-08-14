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

## Best Practices

- **Register Early**: Register all your custom resource creators during your game's initialization phase.
- **Handle Null**: Always check if `createFromJson` returns a `nullptr`, which indicates that the resource creation failed.
- **Extensibility**: Use the factory to create all your resource types to maintain a consistent and extensible resource loading pipeline.
- **Testing**: When writing tests for your resources, you can use the `clear()` method to isolate your tests from the default registered types.
