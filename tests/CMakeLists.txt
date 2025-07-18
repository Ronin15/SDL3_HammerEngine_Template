# Tests for the SDL3_Template project

# Set the C++ standard
set(CMAKE_CXX_STANDARD 20) # Match the main project's C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check if we can build tests - require Boost Test for test framework
if(NOT HAVE_BOOST_TEST)
    message(STATUS "Skipping all tests - Boost Test not found (required)")
    return()
endif()

message(STATUS "Building tests with Boost Test support")

# Use linked Boost.Test framework for proper symbol resolution
# Tests use: #include <boost/test/unit_test.hpp>
# Requires Boost unit_test_framework library

include(FetchContent)

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/core
    ${PROJECT_SOURCE_DIR}/include/managers
    ${PROJECT_SOURCE_DIR}/include/states
    ${PROJECT_SOURCE_DIR}/include/entities
    ${PROJECT_SOURCE_DIR}/include/entities/states
    ${PROJECT_SOURCE_DIR}/include/ai
    ${PROJECT_SOURCE_DIR}/include/ai/behaviors
    ${PROJECT_SOURCE_DIR}/include/utils
    ${PROJECT_SOURCE_DIR}/include/io
    ${PROJECT_SOURCE_DIR}/include/ui
    ${PROJECT_SOURCE_DIR}/include/events
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${Boost_INCLUDE_DIRS}
)

# Add test executables
add_executable(save_manager_tests
    SaveManagerTests.cpp
    mocks/MockPlayer.cpp
)

add_executable(thread_system_tests
    ThreadSystemTests.cpp
)

# AI Optimization tests
add_executable(ai_optimization_tests
    AIOptimizationTest.cpp
)

# AI scaling benchmark
add_executable(ai_scaling_benchmark
    AIScalingBenchmark.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/AIManager.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/ChaseBehavior.cpp
    mocks/AIBehavior.cpp
)

# EventManager scaling benchmark
add_executable(event_manager_scaling_benchmark
    EventManagerScalingBenchmark.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/EventManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/ParticleManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/SoundManager.cpp
    ${PROJECT_SOURCE_DIR}/src/events/Event.cpp
    ${PROJECT_SOURCE_DIR}/src/events/WeatherEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/SceneChangeEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/ParticleEffectEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/EventFactory.cpp
    ${PROJECT_SOURCE_DIR}/src/core/GameTime.cpp
    mocks/NPCSpawnEventTest.cpp
    mocks/MockNPC.cpp
    mocks/MockGameEngine.cpp
)

# Thread-safe AI Manager tests
add_executable(thread_safe_ai_manager_tests
    ThreadSafeAIManagerTest.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/AIManager.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/ChaseBehavior.cpp
    mocks/AIBehavior.cpp
)

# Thread-safe AI Integration tests
add_executable(thread_safe_ai_integration_tests
    ThreadSafeAIIntegrationTest.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/AIManager.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/ChaseBehavior.cpp
    mocks/AIBehavior.cpp
)

# Event Manager tests
add_executable(event_manager_tests
    events/EventManagerTest.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/EventManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/ParticleManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/SoundManager.cpp
    ${PROJECT_SOURCE_DIR}/src/events/Event.cpp
    ${PROJECT_SOURCE_DIR}/src/events/WeatherEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/SceneChangeEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/ParticleEffectEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/EventFactory.cpp
    ${PROJECT_SOURCE_DIR}/src/core/GameTime.cpp
    mocks/NPCSpawnEventTest.cpp
    mocks/MockNPC.cpp
    mocks/MockGameEngine.cpp
)

# Event Types tests
add_executable(event_types_tests
    events/EventTypesTest.cpp
    ${PROJECT_SOURCE_DIR}/src/events/Event.cpp
    ${PROJECT_SOURCE_DIR}/src/events/WeatherEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/ParticleManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/SoundManager.cpp
    ${PROJECT_SOURCE_DIR}/src/events/SceneChangeEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/ParticleEffectEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/EventFactory.cpp
    ${PROJECT_SOURCE_DIR}/src/core/GameTime.cpp
    mocks/NPCSpawnEventTest.cpp
    mocks/MockNPC.cpp
    mocks/MockGameEngine.cpp
)

# Particle Manager Core tests
add_executable(particle_manager_core_tests
    particle/ParticleManagerCoreTest.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/ParticleManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/SoundManager.cpp
    ${PROJECT_SOURCE_DIR}/src/events/Event.cpp
    ${PROJECT_SOURCE_DIR}/src/events/WeatherEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/SceneChangeEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/ParticleEffectEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/EventFactory.cpp
    ${PROJECT_SOURCE_DIR}/src/core/GameTime.cpp
    mocks/NPCSpawnEventTest.cpp
    mocks/MockNPC.cpp
)

# Particle Manager Performance tests
add_executable(particle_manager_performance_tests
    particle/ParticleManagerPerformanceTest.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/ParticleManager.cpp
    ${PROJECT_SOURCE_DIR}/src/events/Event.cpp
    ${PROJECT_SOURCE_DIR}/src/events/WeatherEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/SceneChangeEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/EventFactory.cpp
    ${PROJECT_SOURCE_DIR}/src/core/GameTime.cpp
    mocks/NPCSpawnEventTest.cpp
    mocks/MockNPC.cpp
)

# Particle Manager Weather tests
add_executable(particle_manager_weather_tests
    particle/ParticleManagerWeatherTest.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/ParticleManager.cpp
    ${PROJECT_SOURCE_DIR}/src/events/Event.cpp
    ${PROJECT_SOURCE_DIR}/src/events/WeatherEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/SceneChangeEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/EventFactory.cpp
    ${PROJECT_SOURCE_DIR}/src/core/GameTime.cpp
    mocks/NPCSpawnEventTest.cpp
    mocks/MockNPC.cpp
)

# Particle Manager Threading tests
add_executable(particle_manager_threading_tests
    particle/ParticleManagerThreadingTest.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/ParticleManager.cpp
    ${PROJECT_SOURCE_DIR}/src/events/Event.cpp
    ${PROJECT_SOURCE_DIR}/src/events/WeatherEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/SceneChangeEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/EventFactory.cpp
    ${PROJECT_SOURCE_DIR}/src/core/GameTime.cpp
    mocks/NPCSpawnEventTest.cpp
    mocks/MockNPC.cpp
)

# Weather Event tests
add_executable(weather_event_tests
    events/WeatherEventTest.cpp
    ${PROJECT_SOURCE_DIR}/src/events/Event.cpp
    ${PROJECT_SOURCE_DIR}/src/events/WeatherEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/ParticleManager.cpp
    ${PROJECT_SOURCE_DIR}/src/core/GameTime.cpp
)

# UI Stress Test executable (minimal dependencies)
add_executable(ui_stress_test
    ui/ui_stress_test_main.cpp
)

# Buffer Utilization tests
add_executable(buffer_utilization_tests
    BufferUtilizationTest.cpp
)

# Behavior Functionality tests
add_executable(behavior_functionality_tests
    BehaviorFunctionalityTest.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/AIManager.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/IdleBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/WanderBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/PatrolBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/ChaseBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/FleeBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/FollowBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/GuardBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/AttackBehavior.cpp
    mocks/SimpleMockNPC.cpp
    mocks/AIBehavior.cpp
)

# Add compile definitions for Boost.Test
target_compile_definitions(save_manager_tests PRIVATE
)

target_compile_definitions(thread_system_tests PRIVATE
)

# AI Optimization tests definitions
target_compile_definitions(ai_optimization_tests PRIVATE
)

# AI Scaling benchmark definitions
target_compile_definitions(ai_scaling_benchmark PRIVATE
)

target_compile_definitions(event_manager_scaling_benchmark PRIVATE
)

# Thread-safe AI Manager tests definitions
target_compile_definitions(thread_safe_ai_manager_tests PRIVATE
    BOOST_TEST_NO_SIGNAL_HANDLING
)

# Thread-safe AI Integration tests definitions
target_compile_definitions(thread_safe_ai_integration_tests PRIVATE
    BOOST_TEST_NO_SIGNAL_HANDLING
)

# Event Manager tests definitions
target_compile_definitions(event_manager_tests PRIVATE
    BOOST_TEST_NO_SIGNAL_HANDLING
    RUNNING_TESTS
)

# Event Types tests definitions
target_compile_definitions(event_types_tests PRIVATE
    BOOST_TEST_NO_SIGNAL_HANDLING
    RUNNING_TESTS
)

# Particle Manager Core tests definitions
target_compile_definitions(particle_manager_core_tests PRIVATE
    BOOST_TEST_NO_SIGNAL_HANDLING
)

# Particle Manager Performance tests definitions
target_compile_definitions(particle_manager_performance_tests PRIVATE
    BOOST_TEST_NO_SIGNAL_HANDLING
)

# Particle Manager Weather tests definitions
target_compile_definitions(particle_manager_weather_tests PRIVATE
    BOOST_TEST_NO_SIGNAL_HANDLING
)

# Particle Manager Threading tests definitions
target_compile_definitions(particle_manager_threading_tests PRIVATE
    BOOST_TEST_NO_SIGNAL_HANDLING
)

# Weather Event tests definitions
target_compile_definitions(weather_event_tests PRIVATE
    BOOST_TEST_NO_SIGNAL_HANDLING
)

# UI Stress Test definitions
target_compile_definitions(ui_stress_test PRIVATE
    UI_STRESS_TEST_BUILD
)

# Buffer Utilization tests definitions
target_compile_definitions(buffer_utilization_tests PRIVATE
)

# Behavior Functionality tests definitions
target_compile_definitions(behavior_functionality_tests PRIVATE
    BOOST_TEST_NO_SIGNAL_HANDLING
)



# Link with required libraries
target_link_libraries(save_manager_tests PRIVATE
    SDL3::SDL3
    Boost::unit_test_framework
)

target_link_libraries(thread_system_tests PRIVATE
    SDL3::SDL3
    Boost::unit_test_framework
)

# Link AI optimization tests with required libraries
target_link_libraries(ai_optimization_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
)

# Link AI scaling benchmark with required libraries
target_link_libraries(ai_scaling_benchmark PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
)

target_link_libraries(event_manager_scaling_benchmark PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
    Boost::unit_test_framework
)

# Link Thread-safe AI Manager tests with required libraries
target_link_libraries(thread_safe_ai_manager_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
)

# Link Thread-safe AI Integration tests with required libraries
target_link_libraries(thread_safe_ai_integration_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
)

# Link Event Manager tests with required libraries
target_link_libraries(event_manager_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
    Boost::unit_test_framework
)

# Link Event Types tests with required libraries
target_link_libraries(event_types_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
    Boost::unit_test_framework
)

# Link Particle Manager Core tests with required libraries
target_link_libraries(particle_manager_core_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
    Boost::unit_test_framework
)

# Link Particle Manager Performance tests with required libraries
target_link_libraries(particle_manager_performance_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
)

# Link Particle Manager Weather tests with required libraries
target_link_libraries(particle_manager_weather_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
)

# Link Particle Manager Threading tests with required libraries
target_link_libraries(particle_manager_threading_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
)

# Link Weather Event tests with required libraries
target_link_libraries(weather_event_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
)

# Link UI Stress Test with minimal libraries
target_link_libraries(ui_stress_test PRIVATE
    SDL3::SDL3
    Boost::unit_test_framework
)

# Link Buffer Utilization tests with required libraries
target_link_libraries(buffer_utilization_tests PRIVATE
    SDL3::SDL3
    Boost::unit_test_framework
)

# Link Behavior Functionality tests with required libraries
target_link_libraries(behavior_functionality_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
)

# Add HAVE_BOOST_SERIALIZATION to ui_stress_test as well
target_compile_definitions(ui_stress_test PRIVATE
)

# Add the tests to CTest
add_test(NAME SaveManagerTests COMMAND save_manager_tests)
add_test(NAME ThreadSystemTests COMMAND thread_system_tests)
add_test(NAME AIOptimizationTests COMMAND ai_optimization_tests)
add_test(NAME AIScalingBenchmark COMMAND ai_scaling_benchmark)
add_test(NAME EventManagerScalingBenchmark
    COMMAND event_manager_scaling_benchmark)
add_test(NAME ThreadSafeAIManagerTests
    COMMAND thread_safe_ai_manager_tests)
add_test(NAME ThreadSafeAIIntegrationTests
    COMMAND thread_safe_ai_integration_tests)
add_test(NAME EventManagerTests COMMAND event_manager_tests)
add_test(NAME EventTypesTests COMMAND event_types_tests)
add_test(NAME ParticleManagerCoreTests COMMAND particle_manager_core_tests)
add_test(NAME ParticleManagerPerformanceTests COMMAND particle_manager_performance_tests)
add_test(NAME ParticleManagerWeatherTests COMMAND particle_manager_weather_tests)
add_test(NAME ParticleManagerThreadingTests COMMAND particle_manager_threading_tests)
add_test(NAME WeatherEventTests COMMAND weather_event_tests)
add_test(NAME BufferUtilizationTests COMMAND buffer_utilization_tests)
add_test(NAME BehaviorFunctionalityTests COMMAND behavior_functionality_tests)
