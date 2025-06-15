# AI Behavior Testing Guide

## Overview

The Behavior Functionality Test suite (`BehaviorFunctionalityTest.cpp`) provides comprehensive testing for all 8 AI behaviors implemented in the Forge Game Engine. This test ensures that each behavior works correctly, handles messages properly, and integrates seamlessly with the AIManager.

## Test Structure

### Test Suites

1. **BehaviorRegistrationTests** - Verifies all behaviors are properly registered and assignable
2. **IdleBehaviorTests** - Tests stationary and minimal movement behaviors
3. **MovementBehaviorTests** - Tests basic movement behaviors (Wander, Chase, Flee)
4. **ComplexBehaviorTests** - Tests advanced behaviors (Follow, Guard, Attack)
5. **BehaviorMessageTests** - Tests message handling and communication
6. **BehaviorModeTests** - Tests different modes for each behavior type
7. **BehaviorTransitionTests** - Tests switching between behaviors and state management
8. **BehaviorPerformanceTests** - Tests performance with multiple entities
9. **AdvancedBehaviorFeatureTests** - Tests specialized features like patrol waypoints

### Behaviors Tested

| Behavior | Modes Tested | Key Features |
|----------|--------------|--------------|
| **IdleBehavior** | Stationary, Fidget | Minimal movement, message handling |
| **WanderBehavior** | Small, Medium, Large | Random movement patterns |
| **PatrolBehavior** | Fixed waypoints, Custom routes | Waypoint following |
| **ChaseBehavior** | Standard | Target pursuit |
| **FleeBehavior** | Standard, Evasive, Strategic | Escape behavior |
| **FollowBehavior** | Close, Formation | Companion AI |
| **GuardBehavior** | Standard, Patrol, Alert | Area defense, threat detection |
| **AttackBehavior** | Melee, Ranged, Charge | Combat behavior |

## Running the Tests

### Command Line
```bash
cd build
make behavior_functionality_tests
./behavior_functionality_tests
```

### With CTest
```bash
cd build
ctest -R BehaviorFunctionalityTests -V
```

### Individual Test Suites
```bash
# Run specific test suite
./behavior_functionality_tests --run_test=BehaviorRegistrationTests

# Run with verbose output
./behavior_functionality_tests --log_level=all
```

## Test Features

### Automatic Entity Management
- Test fixture automatically creates test entities
- Proper cleanup of AI assignments and registrations
- Mock player entity for target-based behaviors

### Message System Testing
- Tests behavior-specific messages for all behaviors
- Validates broadcast message handling
- Ensures no crashes during message processing

### Performance Validation
- Tests with 50+ entities simultaneously
- Measures update performance
- Memory management verification

### Behavior Mode Testing
- Validates all registered behavior variants
- Tests mode switching via messages
- Ensures proper mode configuration

## Expected Test Results

### Success Criteria
- ✅ All behaviors register successfully
- ✅ Entities move appropriately for their assigned behavior
- ✅ Message handling works without crashes
- ✅ Behavior transitions occur smoothly
- ✅ Performance remains acceptable with multiple entities
- ✅ Memory management is stable during rapid behavior changes

### Performance Benchmarks
- **50 entities**: Update time < 1000ms for 10 iterations
- **Behavior switching**: No memory leaks during rapid transitions
- **Message processing**: No delays or crashes with broadcast messages

## Test Configuration

### Mock Entities
The test uses `TestEntity` class that provides:
- Position tracking with update counters
- Simple movement simulation
- Required Entity interface methods
- Easy verification of behavior effects

### Test Environment
- AIManager initialization with all behaviors registered
- Mock player entity for target-based behaviors
- Configurable entity positions for specific test scenarios
- Proper cleanup between test cases

## Debugging Test Failures

### Common Issues

1. **Entity Not Moving**
   - Check if behavior is properly assigned
   - Verify entity is registered for updates
   - Ensure sufficient update iterations

2. **Message Handling Crashes**
   - Verify behavior implements message handling
   - Check for null pointer access
   - Ensure thread safety in message processing

3. **Performance Issues**
   - Check for infinite loops in behavior logic
   - Verify proper cleanup of entity state
   - Monitor memory usage patterns

### Debug Output
```bash
# Enable detailed logging
./behavior_functionality_tests --log_level=all --report_level=detailed

# Run single test with maximum verbosity
./behavior_functionality_tests --run_test=MovementBehaviorTests/TestChaseBehavior --log_level=all
```

## Extending the Tests

### Adding New Behavior Tests
1. Create new test case in appropriate suite
2. Register custom behavior if needed
3. Set up entity with specific starting conditions
4. Execute behavior updates
5. Verify expected behavior outcomes

### Example New Test Case
```cpp
BOOST_AUTO_TEST_CASE(TestCustomBehaviorFeature) {
    auto entity = testEntities[0];
    
    // Setup
    AIManager::Instance().assignBehaviorToEntity(entity, "CustomBehavior");
    AIManager::Instance().registerEntityForUpdates(entity, 5);
    
    // Execute
    for (int i = 0; i < 20; ++i) {
        AIManager::Instance().update(0.016f);
    }
    
    // Verify
    BOOST_CHECK(/* your assertion */);
}
```

### Performance Test Guidelines
- Use realistic entity counts (10-100)
- Measure time for multiple update cycles
- Test with varied behavior distributions
- Include cleanup time in measurements

## Integration with CI/CD

The behavior functionality tests are included in the automated test suite and will:
- Run on every commit to validate behavior integrity
- Catch regressions in behavior implementations
- Ensure cross-platform compatibility
- Validate performance characteristics

## Test Coverage

The test suite provides coverage for:
- ✅ **Core Functionality**: All 8 behaviors execute without crashes
- ✅ **Mode Variants**: All behavior modes are tested
- ✅ **Message System**: Behavior-specific and broadcast messages
- ✅ **State Management**: Entity state tracking and cleanup
- ✅ **Performance**: Multi-entity scenarios and rapid transitions
- ✅ **Integration**: Full AIManager integration testing

This comprehensive test suite ensures that the AI behavior system is robust, performant, and ready for production use in game development scenarios.