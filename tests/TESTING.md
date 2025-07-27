# Hammer Game Engine Test Framework

This document provides a comprehensive guide to the testing framework used in the Hammer Game Engine project. All tests use the Boost Test Framework for consistency and are organized by component.

**Current Test Coverage:** 21+ individual test suites covering AI systems, AI behaviors, UI performance, core systems, event management, particle systems, and utility components with both functional validation and performance benchmarking.

## Test Suites Overview

The Hammer Game Engine has the following test suites:

1. **AI System Tests**
   - AI Optimization Tests: Verify performance optimizations in the AI system
   - Thread-Safe AI Tests: Validate thread safety of the AI management system
   - Thread-Safe AI Integration Tests: Test integration of AI components with threading
   - AI Benchmark Tests: Measure performance characteristics and scaling capabilities
   - Behavior Functionality Tests: Comprehensive validation of all 8 AI behaviors and their modes
   - ThreadSystem Queue Load Tests: Defensive monitoring to prevent ThreadSystem overload

2. **UI System Tests**
   - UI Stress Tests: Validate UI performance and scalability in headless mode
   - UI Benchmark Tests: Measure UI component processing throughput and memory efficiency

3. **Core Systems Tests**
   - Save Manager Tests: Validate save/load functionality with directory creation and file operations
   - Thread System Tests: Verify multi-threading capabilities and priority scheduling
   - ThreadSystem Load Monitoring: Defensive tests ensuring AI doesn't overwhelm 4096 task limit
   - Event Manager Tests: Validate event handling and integration with threading
   - Event Types Tests: Test specific event type implementations (Weather, Scene Change, NPC Spawn)
   - Weather Event Tests: Focused tests for weather event functionality
   - Event Manager Scaling Benchmark: Performance testing for event system scalability
   - Particle Manager Tests: Comprehensive particle system validation covering core functionality, weather integration, performance, and threading

4. **Utility System Tests**
   - JsonReader Tests: RFC 8259 compliant JSON parser validation with comprehensive error handling and type safety testing

**Test Execution Categories:**
- **Core Tests** (8 suites): Fast functional validation (~3-6 minutes total)
- **Benchmarks** (3 suites): Performance and scalability testing (~5-15 minutes total)
- **Total Coverage**: 17+ test executables with comprehensive automation scripts

## Running Tests

### Available Test Scripts

Each test suite has dedicated scripts in the project root directory:

#### Linux/macOS
```bash
# Core functionality tests (fast execution)
./run_thread_tests.sh                # Thread system tests
./run_buffer_utilization_tests.sh    # WorkerBudget buffer thread utilization tests
./run_thread_safe_ai_tests.sh        # Thread-safe AI tests
./run_thread_safe_ai_integration_tests.sh  # Thread-safe AI integration tests
./run_ai_optimization_tests.sh       # AI optimization tests
./run_behavior_functionality_tests.sh # Comprehensive AI behavior validation tests
./run_save_tests.sh                  # Save manager and BinarySerializer tests
./run_event_tests.sh                 # Event manager tests
./run_json_reader_tests.sh           # JSON parser validation tests

# Performance scaling benchmarks (slow execution)
./run_event_scaling_benchmark.sh     # Event manager scaling benchmark
./run_ai_benchmark.sh                # AI scaling benchmark with realistic automatic threading
./run_ui_stress_tests.sh             # UI stress and performance tests

# Run all tests
./run_all_tests.sh                   # Run all test scripts sequentially

# AI benchmark test examples with automatic threading
./run_ai_benchmark.sh                                   # Full realistic benchmark suite
./run_ai_benchmark.sh --realistic-only                  # Clean realistic performance tests
./run_ai_benchmark.sh --stress-test                     # 100K entity stress test only
./run_ai_benchmark.sh --threshold-test                  # Threading threshold validation (200 entities)

# Individual UI stress test examples
./run_ui_stress_tests.sh --level light --duration 30    # Quick UI test
./run_ui_stress_tests.sh --level heavy --duration 60    # Heavy load test
./run_ui_stress_tests.sh --benchmark                    # UI benchmark suite
./run_ui_stress_tests.sh --level medium --verbose       # Detailed output

# Save manager test examples with BinarySerializer
./run_save_tests.sh --save-test                         # Basic save/load operations
./run_save_tests.sh --serialization-test                # New BinarySerializer system tests
./run_save_tests.sh --performance-test                  # Serialization performance benchmarks
./run_save_tests.sh --integration-test                  # BinarySerializer integration tests
./run_save_tests.sh --error-test                        # Error handling tests
./run_save_tests.sh --verbose                           # Detailed test output

# JSON reader test examples
./run_json_reader_tests.sh --parse-test                 # Basic JSON parsing validation
./run_json_reader_tests.sh --error-test                 # Error handling and malformed JSON tests
./run_json_reader_tests.sh --file-test                  # File loading functionality tests
./run_json_reader_tests.sh --game-test                  # Game item data parsing tests
./run_json_reader_tests.sh --verbose                    # Detailed test output with all test cases
```

#### Windows
```
# Core functionality tests (fast execution)
run_thread_tests.bat                 # Thread system tests
run_thread_safe_ai_tests.bat         # Thread-safe AI tests
run_thread_safe_ai_integration_tests.bat  # Thread-safe AI integration tests
run_ai_optimization_tests.bat        # AI optimization tests
run_behavior_functionality_tests.bat # Comprehensive AI behavior validation tests
run_ai_benchmark.bat                 # AI scaling benchmark with realistic automatic threading
run_save_tests.bat                   # Save manager and BinarySerializer tests
run_event_tests.bat                  # Event manager tests
run_json_reader_tests.bat            # JSON parser validation tests

# Performance scaling benchmarks (slow execution)
run_event_scaling_benchmark.bat      # Event manager scaling benchmark
run_ai_benchmark.bat                 # AI scaling benchmark
run_ui_stress_tests.bat              # UI stress and performance tests

# Run all tests
run_all_tests.bat                    # Run all test scripts sequentially

# Individual UI stress test examples
run_ui_stress_tests.bat /l light /d 30    # Quick UI test
run_ui_stress_tests.bat /l heavy /d 60    # Heavy load test
run_ui_stress_tests.bat /b                # UI benchmark suite
run_ui_stress_tests.bat /l medium /v      # Detailed output

# Save manager test examples with BinarySerializer
run_save_tests.bat --save-test                          # Basic save/load operations
run_save_tests.bat --serialization-test                 # New BinarySerializer system tests
run_save_tests.bat --performance-test                   # Serialization performance benchmarks
run_save_tests.bat --integration-test                   # BinarySerializer integration tests
run_save_tests.bat --error-test                         # Error handling tests
run_save_tests.bat --verbose                            # Detailed test output
```

### Test Execution Control

The `run_all_tests.sh` script provides flexible execution control:

#### Test Category Options
| Option | Description | Duration |
|--------|-------------|----------|
| `--core-only` | Run only core functionality tests | ~2-5 minutes |
| `--benchmarks-only` | Run only performance benchmarks (AI, Event, UI) | ~5-15 minutes |
| `--no-benchmarks` | Run core tests but skip benchmarks | ~2-5 minutes |
| *(default)* | Run all tests sequentially | ~7-20 minutes |

#### Examples
```bash
# Quick validation during development
./run_all_tests.sh --core-only

# Skip slow benchmarks in CI
./run_all_tests.sh --no-benchmarks

# Performance testing only
./run_all_tests.sh --benchmarks-only --verbose

# Complete test suite
./run_all_tests.sh
```

### Common Command-Line Options

Individual test scripts support these options:

| Option | Description |
|--------|-------------|
| `--verbose` | Show detailed test output |
| `--release` | Run tests in release mode (optimized) |
| `--clean` | Clean test artifacts before building |
| `--help` | Show help message for the script |

Special options:
- `--extreme` for AI benchmark (runs extended benchmarks)
- `--verbose` for scaling benchmarks (shows detailed performance metrics)
- `--level LEVEL` for UI stress tests (light|medium|heavy|extreme)
- `--duration SECONDS` for UI stress tests (custom test duration)
- `--benchmark` for UI stress tests (runs benchmark suite instead of stress tests)

**Test Execution Strategy:**
The script executes tests in optimal order for efficient development workflow:
1. **Core functionality tests** (fast execution, ~5-30 seconds each)
2. **Performance scaling benchmarks** (slow execution, 1-5+ minutes each)

This ordering provides quick feedback on basic functionality before running time-intensive performance tests.

## Test Output

Test results are saved in the `test_results` directory:

- `ai_optimization_tests_output.txt` - Output from AI optimization tests
- `event_scaling_benchmark_output.txt` - Output from EventManager scaling benchmark
- `ai_optimization_tests_performance_metrics.txt` - Performance metrics from optimization tests
- `thread_safe_ai_test_output.txt` - Output from thread-safe AI tests
- `thread_safe_ai_performance_metrics.txt` - Performance metrics from thread-safe AI tests
- `ai_scaling_benchmark_[timestamp].txt` - AI scaling benchmark results
- `ui_stress/ui_stress_test_[timestamp].log` - UI stress test results with performance metrics
- `save_test_output.txt` - Output from save manager tests
- `thread_test_output.txt` - Output from thread system tests
- `event_test_output.txt` - Output from event manager tests
- `event_types_test_output.txt` - Output from event types tests
- `weather_event_test_output.txt` - Output from weather event tests

When using the `run_all_tests` scripts, combined results are also saved:

- `combined/all_tests_results.txt` - Summary of all test script results

## Test Implementation Details

### AI Optimization Tests

Located in `AIOptimizationTest.cpp`, these tests verify:

1. **Entity Component Caching**: Tests caching mechanisms for faster entity-behavior lookups
2. **Batch Processing**: Validates efficient batch processing of entities with similar behaviors
3. **Early Exit Conditions**: Tests optimizations that skip unnecessary updates
4. **Message Queue System**: Verifies batched message processing for efficient AI communication
5. **Priority-Based Scheduling**: Tests the task priority system integration with AI behaviors

### Thread-Safe AI Tests

Located in `ThreadSafeAIManagerTest.cpp`, these tests verify:

1. **Thread-Safe Behavior Registration**: Tests concurrent registration of behaviors
2. **Thread-Safe Entity Assignment**: Validates behavior assignment from multiple threads
3. **Concurrent Behavior Processing**: Tests running AI behaviors across multiple threads
4. **Thread-Safe Cache Invalidation**: Validates optimization cache in multi-threaded context
5. **Thread-Safe Messaging**: Tests message queuing with concurrent access

Special considerations for thread-safety tests:
- Use atomic operations with proper synchronization
- Disable threading before cleanup to prevent segmentation faults
- Allow time between operations for thread synchronization
- Use timeout when waiting for futures to prevent hanging

### AI Benchmark Tests

Located in `AIScalingBenchmark.cpp`, these tests measure realistic performance characteristics:

1. **Realistic Performance Testing**: Tests automatic threading behavior across key entity counts
   - 100 entities: Validates single-threaded baseline (~170K updates/sec)
   - 200+ entities: Validates automatic threading activation (~750K updates/sec)
   - 1000+ entities: Confirms high-performance threading (~975K updates/sec)
   - 10K entities: Target performance validation (~995K updates/sec, 5.85x improvement)

2. **Realistic Scalability**: Tests clean automatic threading behavior across entity ranges
   - Tests 100-10K entity range with automatic mode selection
   - Validates 200-entity threading threshold effectiveness (4.41x performance jump)
   - Demonstrates consistent high performance scaling (5.29x-5.85x ratios)

3. **Legacy Comparison**: Forced threading modes for comparison with previous benchmarks
   - Single-threaded forced mode for baseline comparison
   - Multi-threaded forced mode for maximum performance comparison

4. **Stress Testing**: 100K entity extreme load testing
   - Validates system stability under extreme stress
   - Tests WorkerBudget system coordination
   - Confirms queue capacity handling (4096 tasks)

**Key Performance Targets:**
- 100 entities: Single-threaded baseline (~170K updates/sec)
- 200 entities: Automatic threading activation (~750K updates/sec)
- 1000 entities: High threading performance (~975K updates/sec)
- 10K entities: Target performance achieved (~995K updates/sec, 5.85x improvement)
- 100K entities: Stress test validation (2.2M+ updates/sec)

### UI Stress Tests

Located in `ui/ui_stress_test_main.cpp`, these tests run in headless mode and measure:

1. **Processing Throughput**: Components processed per second (real UI workload capacity)
2. **Memory Efficiency**: Memory usage per component and total consumption
3. **Scalability**: Performance degradation as component count increases
4. **Layout Performance**: Layout calculations per second for responsive UI
5. **Input Responsiveness**: Collision detection rate for mouse/touch interaction
6. **Iteration Performance**: Time to process all UI components once

**Key Metrics Measured:**
- **Processing Throughput**: ~100k-400k components/sec (indicates UI system capacity)
- **Average Iteration Time**: ~0.25ms (UI overhead per frame, should be <1ms for 60fps)
- **Memory Usage**: Tracks peak memory consumption and growth per component
- **Performance Degradation**: How performance scales with component count (<2x ideal)
- **Layout Calculations/sec**: Algorithm performance for responsive layouts
- **Collision Checks/sec**: Input system responsiveness capacity

**Test Modes:**
- **Stress Test**: Creates components over time while measuring performance
- **Benchmark Suite**: Runs multiple test scenarios and compares results
- **Headless Operation**: No SDL video initialization - perfect for CI/automation

**Real-World Application:**
- Mobile games: Validate UI memory stays under 50MB
- Desktop games: Ensure UI uses <1% of 60fps frame budget
- Complex UIs: Test thousands of interactive elements
- Performance regression: Detect UI optimization regressions

**Meaningful Headless Metrics:**
The UI stress tests run in headless mode and provide meaningful performance metrics instead of artificially high FPS numbers:
- **Processing Throughput** replaces meaningless FPS - shows actual UI workload capacity
- **Average Iteration Time** shows real UI system overhead per frame
- **Memory per Component** tracks actual resource consumption
- **Performance Degradation** measures scalability characteristics
- **Layout/Collision rates** show algorithm performance for real UI operations

This approach provides actionable insights for UI system optimization rather than misleading FPS metrics that don't reflect real rendering constraints.

**Usage Examples:**
```bash
# Quick development validation
./run_ui_stress_tests.sh --level light --duration 10

# CI/CD pipeline validation
./run_ui_stress_tests.sh --level medium --duration 30

# Performance profiling
./run_ui_stress_tests.sh --benchmark --verbose

# Stress testing with custom parameters
./run_ui_stress_tests.sh --level extreme --duration 120 --max-components 2000
```

### Save Manager Tests

Located in `SaveManagerTests.cpp` with supporting `MockPlayer` class, these tests verify the **BinarySerializer integration** that replaces Boost serialization:

1. **BinarySerializer Integration**: Tests the new fast, header-only serialization system:
   - Tests `BinarySerial::Writer` and `BinarySerial::Reader` classes
   - Verifies smart pointer-based memory management
   - Tests convenience functions `saveToFile()` and `loadFromFile()`
   - Validates cross-platform binary compatibility

2. **Data Serialization**: Tests serialization of game objects using the new system:
   - `TestNewSerializationSystem`: Vector2D and custom object serialization
   - `TestBinaryWriterReader`: Primitive types, strings, and direct Writer/Reader usage
   - `TestVectorSerialization`: Container serialization with safety checks
   - `TestBinarySerializerIntegration`: End-to-end serialization testing

3. **Performance Testing**: `TestPerformanceComparison` benchmarks:
   - 100 serialization operations timing (typically ~13-25ms)
   - Memory usage validation
   - Cross-platform performance verification

4. **File Operations**: Tests using BinarySerializer for save file management:
   - `TestSaveAndLoad`: Basic save/load operations with MockPlayer
   - `TestSlotOperations`: Multiple file handling and cleanup
   - File existence validation and proper error handling

5. **Error Handling**: `TestErrorHandling` covers:
   - Non-existent file handling
   - Invalid file path recovery
   - Corrupted data detection through the BinarySerializer safety checks

**Key Features Tested:**
- **Fast Performance**: 10x faster than Boost for primitives, 4x faster for strings
- **Smart Pointer Safety**: Automatic memory management with `std::shared_ptr` and `std::unique_ptr`
- **Type Safety**: Compile-time checks for trivially copyable types
- **Logging Integration**: Proper integration with Forge logging system (`SAVEGAME_*` macros)
- **Cross-Platform**: Header-only solution works on Windows, macOS, and Linux

### Thread System Tests

Located in `ThreadSystemTests.cpp`, these tests verify:

1. **Task Scheduling**: Tests scheduling and execution of tasks
2. **Thread Safety**: Tests synchronization mechanisms
3. **Performance**: Tests scaling with different numbers of threads
4. **Error Handling**: Tests recovery from failed tasks
5. **Priority System**: Tests the task priority levels (Critical, High, Normal, Low, Idle)
6. **Priority Scheduling**: Verifies that higher priority tasks execute before lower priority ones

### WorkerBudget Buffer Utilization Tests

Located in `BufferUtilizationTest.cpp`, these tests verify the intelligent buffer thread utilization system:

1. **Hardware Tier Classification**: Tests allocation strategies across ultra low-end to very high-end systems
2. **Dynamic Buffer Scaling**: Validates workload-based buffer utilization (AI: >1000 entities, Events: >100 events)
3. **Conservative Burst Strategy**: Tests systems using maximum 50% of base allocation from buffer
4. **Resource Allocation Logic**: Verifies no over-allocation and proper fallback behavior
5. **Graceful Degradation**: Tests single-threaded fallback on resource-constrained systems

**Test Coverage:**
- **Ultra Low-End (1-2 workers)**: GameLoop priority, AI/Events single-threaded
- **Low-End (3-4 workers)**: Conservative allocation with limited buffer
- **Target Minimum (7 workers)**: Optimal allocation (GameLoop: 2, AI: 3, Events: 1, Buffer: 1)
- **High-End (12+ workers)**: Full buffer utilization for burst capacity

**Validation Examples:**
```bash
# 12-worker system test results:
Base allocations - GameLoop: 2, AI: 6, Events: 3, Buffer: 1
Low workload (500 entities): 6 workers    # Uses base allocation
High workload (5000 entities): 7 workers  # Uses base + buffer

# 16-worker system test results:
Very high workload burst: 10 workers      # AI gets 8 base + 2 buffer
```

The tests ensure the WorkerBudget system provides guaranteed minimum performance while enabling intelligent scaling for high workloads without resource conflicts.

### Event Manager Tests

Located in `events/EventManagerTest.cpp`, `events/EventTypesTest.cpp`, `events/WeatherEventTest.cpp`, and `EventManagerScalingBenchmark.cpp`, these tests verify:

1. **Event Registration**: Tests registering different types of events with the EventManager
2. **Event Conditions**: Tests condition-based event triggering
3. **Event Execution**: Validates proper execution of event sequences
4. **Thread-Safe Processing**: Tests concurrent event processing using ThreadSystem
5. **Message System**: Tests the event messaging system for communication between events
6. **Priority-Based Scheduling**: Tests task priority integration with event processing
7. **NPCSpawnEvent Integration**: Tests NPC spawning functionality with mocked dependencies
8. **Performance Scaling**: Comprehensive benchmarking from small to extreme scales (see EventManager Scaling Benchmark below)

#### Event Manager Test Details

**EventManagerTest.cpp** focuses on EventManager integration:
- Event registration and retrieval
- Event activation/deactivation
- Event messaging system
- Thread-safe event processing
- NPCSpawnEvent integration with EventManager

**EventTypesTest.cpp** tests specific event type implementations:
- WeatherEvent creation and parameter setting
- SceneChangeEvent functionality
- NPCSpawnEvent creation, spawn parameters, conditions, and limits
- EventFactory event creation methods
- Event sequences and cooldown functionality

**WeatherEventTest.cpp** provides focused weather event testing:
- Weather state transitions
- Particle effect integration
- Sound effect integration
- Time-based weather conditions

#### Mock Infrastructure for NPCSpawnEvent Tests

The NPCSpawnEvent tests use a comprehensive mocking infrastructure to avoid dependencies on the full game engine:

**Mock Components:**
- `MockNPC.hpp/cpp`: Mock NPC class implementing Entity interface without game engine dependencies
- `MockGameEngine.hpp/cpp`: Mock GameEngine providing basic window dimensions for testing
- `NPCSpawnEventTest.cpp`: Test-specific NPCSpawnEvent implementation using mocks

**Mock Features:**
- Complete NPCSpawnEvent functionality testing without real game engine
- Proper Vector2D usage with `getX()/getY()` and `setX()/setY()` methods
- Correct Event base class implementation
- Mock entity creation and management
- Spawn parameter validation and condition testing
- Respawn logic and timer functionality
- Proximity and area-based spawning tests

**Test Coverage:**
- Spawn parameters (count, radius, positioning)
- Spawn conditions (proximity, time of day, custom conditions)
- Spawn limits and counting
- Respawn functionality and timing
- Spawn area management (points, rectangles, circles)
- Entity lifecycle and cleanup
- Message-based spawn requests

#### EventManager Scaling Benchmark

Located in `EventManagerScalingBenchmark.cpp`, this comprehensive performance test validates the EventManager's scalability across various workloads:

**Test Scenarios:**
- **Basic Performance**: Small scale validation (100 events, immediate vs batched)
- **Medium Scale**: Moderate load testing (5,000 events, 25,000 handler calls)
- **Scalability Suite**: Progressive testing from minimal to very large scales
- **Concurrency Test**: Multi-threaded event generation and processing
- **Extreme Scale**: Large simulation testing (100,000 events, 5,000,000 handler calls)

**Performance Metrics Measured:**
- Events per second throughput
- Handler calls per second
- Time per event and per handler call
- Memory usage and cache performance
- Thread safety validation
- Batching vs immediate processing comparison

**Key Features Tested:**
- Handler batching system performance
- `boost::flat_map` storage optimization
- Thread-safe concurrent event queuing
- Lock-free handler execution
- Cache locality optimization through event type grouping

**Running the Scaling Benchmark:**
```bash
# Run just the scaling benchmark
ctest -R EventManagerScaling

# Or run the executable directly for detailed output
./bin/debug/event_manager_scaling_benchmark
```

**Expected Performance (on modern hardware):**
- Small scale: ~540K events/sec
- Medium scale: ~78K events/sec
- Large scale: ~39K events/sec
- Extreme scale: ~7.8K events/sec with 5M handler calls

The benchmark validates that the EventManager can handle large-scale simulations (MMOs, city simulations, real-time strategy games) while maintaining consistent performance characteristics.

### Particle Manager Tests

Located in `particle/` directory, these tests provide comprehensive validation of the ParticleManager system covering core functionality, weather integration, performance benchmarks, and threading safety.

#### Test Suites Overview

**1. Core Tests (`ParticleManagerCoreTest.cpp`)**
**14 test cases** covering basic ParticleManager functionality:
- Initialization and cleanup
- Effect registration and management  
- Particle creation and lifecycle
- Global pause/resume functionality
- Performance statistics
- State transition handling

**2. Weather Integration Tests (`ParticleManagerWeatherTest.cpp`)**
**9 test cases** covering weather system integration:
- Weather effect triggering (Rain, Snow, Fog, Cloudy, Stormy, Clear)
- Weather transitions and timing
- Weather-specific particle behavior
- Intensity scaling
- Weather effect cleanup
- Multiple weather effect handling

**3. Performance Tests (`ParticleManagerPerformanceTest.cpp`)**
**8 test cases** covering performance characteristics:
- Large-scale particle simulation (1000+ particles)
- Update performance scaling
- Memory usage efficiency
- Sustained performance benchmarks
- Different effect type performance
- Cleanup performance

**4. Threading Tests (`ParticleManagerThreadingTest.cpp`)**
**7 test cases** covering multi-threading safety:
- Concurrent particle creation
- Thread-safe effect management
- Concurrent weather changes (using `triggerWeatherEffect()` for accurate marking)
- Parallel statistics access
- Mixed concurrent operations
- Enhanced resource cleanup safety with proper weather effect stopping

#### Running Particle Manager Tests

**Quick Commands:**
```bash
# Run all particle manager tests (4-6 minutes)
./tests/test_scripts/run_particle_manager_tests.sh

# Quick core validation (30 seconds)
./tests/test_scripts/run_particle_manager_tests.sh --core

# Weather functionality only (45 seconds)
./tests/test_scripts/run_particle_manager_tests.sh --weather

# Performance benchmarks (2-3 minutes)
./tests/test_scripts/run_particle_manager_tests.sh --performance

# Threading safety tests (1-2 minutes)
./tests/test_scripts/run_particle_manager_tests.sh --threading

# Verbose output
./tests/test_scripts/run_particle_manager_tests.sh --verbose
```

**Cross-Platform Support:**
- **Linux/macOS:** `run_particle_manager_tests.sh`
- **Windows:** `run_particle_manager_tests.bat`

Both scripts support identical command-line options and functionality.

**Test Results:**
Test results are automatically saved to:
- `test_results/particle_manager/` - Individual test outputs
- `test_results/particle_manager/all_particle_tests_results.txt` - Combined summary

#### Integration with Main Test Runner

The particle manager tests are integrated into the main test runner:
```bash
# All tests (includes particle manager tests)
./tests/test_scripts/run_all_tests.sh

# Core functionality only (includes particle manager core tests)
./tests/test_scripts/run_all_tests.sh --core-only
```

#### Test Status Summary

**✅ Passing Test Suites:**
- **Core Tests:** 14/14 passing - Basic functionality verified
- **Weather Tests:** 9/9 passing - Weather integration working correctly

**⚠️ Known Issues:**
- **Performance Tests:** Some tests may fail on slower systems or under load
- **Threading Tests:** Concurrency tests may be sensitive to system timing

These issues don't affect core functionality but may require system-specific tuning.

#### Performance Characteristics

**Expected Performance (Debug builds):**
- **Core tests:** Complete in ~30 seconds
- **Weather tests:** Complete in ~45 seconds
- **Performance tests:** Complete in 2-3 minutes
- **Threading tests:** Complete in 1-2 minutes

**Scaling Behavior:**
The ParticleManager is designed to handle:
- 1000+ active particles efficiently
- Multiple concurrent weather effects
- Thread-safe operations across multiple cores
- Sustained 60 FPS performance with realistic particle loads

Performance tests validate these capabilities and benchmark actual system performance.

#### Weather Test Fixes Applied

Recent fixes to weather tests addressed:
1. **Timing issues:** Tests now use multiple update cycles for particle emission
2. **Weather type mapping:** Consistent use of weather type names (e.g., "Rainy" vs "Rain")
3. **Low emission rates:** Special handling for effects like "Cloudy" with very low emission rates
4. **Particle lifecycle:** Proper handling of particle fade and cleanup behavior

These fixes ensure reliable weather functionality testing across different systems.
5. **Test Adjustments:** Weather effects are now created using `triggerWeatherEffect()` to ensure proper marking and cleanup consistent with expected ParticleManager behavior.

#### Development Guidelines

**Adding New Tests:**
1. **Core functionality:** Add to `ParticleManagerCoreTest.cpp`
2. **Weather features:** Add to `ParticleManagerWeatherTest.cpp`
3. **Performance cases:** Add to `ParticleManagerPerformanceTest.cpp`
4. **Threading scenarios:** Add to `ParticleManagerThreadingTest.cpp`

**Test Fixture Pattern:**
All test files use the same fixture pattern:
```cpp
struct ParticleManagerTestFixture {
    ParticleManagerTestFixture() {
        manager = &ParticleManager::Instance();
        if (manager->isInitialized()) {
            manager->clean();
        }
    }
    
    ~ParticleManagerTestFixture() {
        if (manager->isInitialized()) {
            manager->clean();
        }
    }
    
    ParticleManager* manager;
};
```

**Best Practices:**
1. **Always clean up:** Use RAII pattern in test fixtures
2. **Multiple update cycles:** Weather and effect tests need multiple `update()` calls
3. **Timing considerations:** Performance tests should account for system variations
4. **Resource cleanup:** Add delays between resource-intensive tests

## Adding New Tests

1. Choose the appropriate test file for your component
2. Add test cases using the `BOOST_AUTO_TEST_CASE` macro
3. Follow the existing pattern for setup, execution, and verification
4. Run tests with `--clean` to ensure your changes are compiled
5. For directory or file operation tests, always clean up created resources when tests complete
6. For tests requiring game engine dependencies, consider creating mock implementations

### Creating Mock Dependencies

When testing components that depend on complex systems (like NPCSpawnEvent depending on NPC and GameEngine):

1. **Create Mock Headers**: Define mock classes that implement the same interface as real dependencies
2. **Mock Implementation**: Provide minimal functionality needed for testing
3. **Test-Specific Compilation**: Create test-specific source files that use mocks instead of real implementations
4. **CMake Integration**: Update `tests/CMakeLists.txt` to include mock files and dependencies

**Example Mock Structure:**
```cpp
// MockNPC.hpp - Mock implementation of NPC for testing
class MockNPC : public Entity {
public:
    MockNPC(const std::string& textureID, const Vector2D& position, int width, int height);

    // Mock the required interface methods
    void setWanderArea(float x1, float y1, float x2, float y2);
    void setBoundsCheckEnabled(bool enabled);

    // Factory method like real class
    static std::shared_ptr<MockNPC> create(const std::string& textureID, const Vector2D& position, int width, int height);
};

// Define alias for testing
using NPC = MockNPC;
```

**Benefits of Mocking:**
- Tests run faster without full game engine initialization
- Tests are more focused and isolated
- Easier to reproduce specific conditions
- No external dependencies like graphics, audio, or file systems
- Better test reliability and maintainability

For directory management tests (like in SaveManagerTests):
- Use a dedicated test directory that's different from production directories
- Implement proper cleanup in test class destructors or at the end of test cases
- Add detailed logging to identify filesystem operation failures
- Test both successful cases and error cases (e.g., permission denied, disk full)

### Basic Test Structure

```cpp
// Define module name
#define BOOST_TEST_MODULE YourTestName
#include <boost/test/included/unit_test.hpp>

// For thread-safe tests, disable signal handling
#define BOOST_TEST_NO_SIGNAL_HANDLING

// Global fixture for setup/teardown
struct TestFixture {
    TestFixture() {
        // Setup code
    }

    ~TestFixture() {
        // Cleanup code
    }
};

BOOST_GLOBAL_FIXTURE(TestFixture);

// Test cases
BOOST_AUTO_TEST_CASE(TestSomething) {
    // Test code
    BOOST_CHECK(condition);  // Continues test if failed
    BOOST_REQUIRE(condition);  // Stops test if failed
}
```

## Singleton and Factory Lifecycle Management

When working with singleton factories (like ResourceFactory) in tests, follow these important guidelines:

### Factory Lifecycle Best Practices

1. **Test Isolation**: Use `clear()` methods in test fixtures to ensure test isolation
2. **Production Safety**: **NEVER** call factory `clear()` methods from production code or other singleton destructors
3. **Static Object Destruction**: Let static factories clean themselves up at program exit

**Safe Test Pattern:**
```cpp
class ResourceFactoryTestFixture {
public:
    ResourceFactoryTestFixture() {
        ResourceFactory::initialize();  // Safe in tests
    }
    
    ~ResourceFactoryTestFixture() {
        ResourceFactory::clear();      // Safe in tests for isolation
    }
};
```

**Unsafe Production Pattern (DO NOT DO):**
```cpp
// WRONG: Don't call factory clear() from other singletons
void SomeManager::clean() {
    ResourceFactory::clear();  // ❌ Can cause crashes due to undefined destruction order
}
```

**Why This Matters:**
- Static object destruction order is undefined between translation units
- Calling `clear()` from other destructors can cause double-free or use-after-free errors
- Test crashes like "Abort trap: 6" often indicate this anti-pattern

## Thread Safety Considerations

For thread-safety tests, follow these guidelines:

1. **Test Initialization Order**
   - Initialize ThreadSystem first, then other systems
   - Enable threading only after initialization is complete
   - Configure task priorities during initialization if needed

2. **Test Cleanup Order**
   - Disable threading before cleanup
   - Wait for threads to complete with appropriate timeouts
   - Clean up managers before cleaning up ThreadSystem

3. **Preventing Deadlocks and Race Conditions**
   - Use atomic operations with proper synchronization
   - Add sleep between operations to allow threads to complete
   - Use timeouts when waiting for futures instead of blocking calls
   - Use `compare_exchange_strong` instead of simple `exchange` for atomics
   - Be careful about task priority assignments to avoid priority inversion

4. **Boost Test Options**
   - Add `#define BOOST_TEST_NO_SIGNAL_HANDLING` before including Boost.Test
   - Use `--catch_system_errors=no --no_result_code --detect_memory_leak=0` test options

## Troubleshooting Common Issues

### Boost.Test Configuration Issues

**Problem**: Errors related to Boost.Test initialization or missing symbols.

**Solution**:
- Add `#define BOOST_TEST_MODULE YourModuleName` before including Boost.Test
- Use `#include <boost/test/included/unit_test.hpp>` for header-only approach
- Check CMakeLists.txt has correct Boost linking
- Use correct test macros (`BOOST_AUTO_TEST_CASE`, `BOOST_CHECK`, etc.)

### Thread-Related Segmentation Faults

**Problem**: Tests crash with signal 11 (segmentation fault) during cleanup.

**Solution**:
- Add `#define BOOST_TEST_NO_SIGNAL_HANDLING` before including Boost.Test
- Run with `--catch_system_errors=no --no_result_code --detect_memory_leak=0`
- Disable threading before cleanup: `AIManager::Instance().configureThreading(false)`
- Add sleep between operations: `std::this_thread::sleep_for(std::chrono::milliseconds(100))`
- Use timeout for futures: `future.wait_for(std::chrono::seconds(1))`
- Don't register SIGSEGV handler in test code

### Filesystem Operation Issues

**Problem**: Directory creation, file operations, or save/load functions fail during tests.

**Solution**:
- Check working directory: Tests might run from a different directory than expected
- Print and verify absolute paths: `std::filesystem::absolute(path).string()`
- Ensure parent directories exist before creating files
- Verify write permissions on directories with a small test file
- Use detailed logging to identify exactly which operation is failing
- Add proper error handling with try/catch blocks around all filesystem operations
- Always clean up test files/directories in both success and failure scenarios
- On Windows, ensure paths don't exceed MAX_PATH (260 characters)

### Build Issues

**Problem**: Tests won't build or can't find dependencies.

**Solution**:
- Run with `--clean` to ensure clean rebuilding
- Check that all required libraries are installed and linked in CMakeLists.txt
- Check proper include paths are set in CMakeLists.txt

## Running All Tests

The `run_all_tests` scripts provide a convenient way to run all test suites sequentially:

### Linux/macOS
```bash
./run_all_tests.sh [options]
```

### Windows
```
run_all_tests.bat [options]
```

These scripts:
1. Run each test script one by one, giving them time to complete
2. Pass along the `--verbose` option to individual test scripts if specified
3. Generate a summary showing which tests passed or failed
4. Save combined results to `test_results/combined/all_tests_results.txt`
5. Return a non-zero exit code if any tests fail

## CMake Configuration

Tests are configured in `tests/CMakeLists.txt` with the following structure:

1. Define test executables with source files
2. Set compiler definitions for Boost.Test
3. Link necessary libraries
4. Register tests with CTest

For thread-safe tests, ensure `BOOST_TEST_NO_SIGNAL_HANDLING` is defined.

## Additional Documentation

For specific testing scenarios and troubleshooting, refer to these additional documents:

- `AI_TESTING.md` - Specific guidance for AI thread testing scenarios
- `TROUBLESHOOTING.md` - Common issues and their solutions across all components
