# Utility Systems Documentation

This directory contains documentation for core utility classes and helper systems used throughout the Hammer Game Engine.

## Available Utilities

### Data Processing
- **[JsonReader](JsonReader.md)** - RFC 8259 compliant JSON parser
  - Custom, dependency-free implementation
  - Type-safe data access with `std::optional` support
  - Robust error handling and detailed error messages
  - File loading capabilities
  - Thread-safe for read operations (separate instances per thread)

### Future Utilities

- **Configuration Manager** (planned)


### Usage in Game Development
- **Configuration Loading**: JSON configuration files for game settings
- **Item/Resource Data**: Define game items, NPCs, and resources in JSON format
- **Save Game Data**: JSON format for human-readable save files (alternative to binary)
- **Asset Metadata**: JSON descriptions for textures, sounds, and other assets
- **Level Data**: JSON format for level layouts and entity placement

### Performance Considerations
- All utilities are designed for minimal overhead
- Header-only implementations where possible
- Move semantics and C++20 optimization
- Memory-efficient data structures
- Single-pass parsing where applicable

### Error Handling Standards
All utilities follow the engine's error handling conventions:
- Use engine logging macros (`GAMEENGINE_ERROR`, `GAMEENGINE_WARNING`, etc.)
- Provide detailed error messages
- Graceful fallback to default values where appropriate
- Non-throwing interfaces with explicit error checking

## Best Practices

1. **Always validate input** before processing
2. **Use type-safe accessors** to prevent runtime errors
3. **Handle missing data gracefully** with sensible defaults
4. **Log errors appropriately** using engine logging system
5. **Reuse utility instances** where possible for performance
6. **Follow engine coding standards** for consistency

## Testing

All utilities include comprehensive test suites:
- Unit tests for core functionality
- Error condition testing
- Performance benchmarks
- Real-world usage scenarios
- Cross-platform compatibility tests

Tests are located in the `tests/utils/` directory and are automatically run as part of the core test suite.

## Contributing

When adding new utilities:
1. Follow the existing documentation structure
2. Include comprehensive examples
3. Add appropriate error handling
4. Create thorough test coverage
5. Update this README with the new utility
6. Follow engine coding standards and conventions

---

For specific utility documentation, see the individual files linked above.