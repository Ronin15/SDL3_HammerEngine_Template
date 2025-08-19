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

include(FetchContent)

# --- Source Groups for Tests ---
set(HAMMER_ENGINE_SOURCES
    ${PROJECT_SOURCE_DIR}/src/managers/EventManager.cpp
    ${PROJECT_SOURCE_DIR}/src/events/Event.cpp
    ${PROJECT_SOURCE_DIR}/src/events/EventFactory.cpp
    ${PROJECT_SOURCE_DIR}/src/events/NPCSpawnEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/WeatherEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/SceneChangeEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/ParticleEffectEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/ResourceChangeEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/HarvestResourceEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/ResourceTemplateManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/ResourceFactory.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/WorldResourceManager.cpp
    ${PROJECT_SOURCE_DIR}/src/entities/Resource.cpp
    ${PROJECT_SOURCE_DIR}/src/entities/resources/ItemResources.cpp
    ${PROJECT_SOURCE_DIR}/src/entities/resources/MaterialResources.cpp
    ${PROJECT_SOURCE_DIR}/src/entities/resources/CurrencyAndGameResources.cpp
    ${PROJECT_SOURCE_DIR}/src/entities/resources/InventoryComponent.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/WorldManager.cpp
    ${PROJECT_SOURCE_DIR}/src/world/WorldGenerator.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/AIManager.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/AIBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/ChaseBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/IdleBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/WanderBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/PatrolBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/FleeBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/FollowBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/GuardBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/ai/behaviors/AttackBehavior.cpp
    ${PROJECT_SOURCE_DIR}/src/entities/NPC.cpp
    ${PROJECT_SOURCE_DIR}/src/entities/Player.cpp
    ${PROJECT_SOURCE_DIR}/src/entities/playerStates/PlayerIdleState.cpp
    ${PROJECT_SOURCE_DIR}/src/entities/playerStates/PlayerRunningState.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/TextureManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/ParticleManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/SoundManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/FontManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/GameStateManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/InputManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/SaveGameManager.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/UIManager.cpp
    ${PROJECT_SOURCE_DIR}/src/core/GameEngine.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/EntityStateManager.cpp
    ${PROJECT_SOURCE_DIR}/src/utils/JsonReader.cpp
    ${PROJECT_SOURCE_DIR}/src/utils/Camera.cpp
    ${PROJECT_SOURCE_DIR}/src/core/GameTime.cpp
    ${PROJECT_SOURCE_DIR}/src/core/TimestepManager.cpp
    ${PROJECT_SOURCE_DIR}/src/gameStates/AIDemoState.cpp
    ${PROJECT_SOURCE_DIR}/src/gameStates/GamePlayState.cpp
    ${PROJECT_SOURCE_DIR}/src/gameStates/MainMenuState.cpp
    ${PROJECT_SOURCE_DIR}/src/gameStates/AdvancedAIDemoState.cpp
    ${PROJECT_SOURCE_DIR}/src/gameStates/LogoState.cpp
    ${PROJECT_SOURCE_DIR}/src/gameStates/EventDemoState.cpp
    ${PROJECT_SOURCE_DIR}/src/gameStates/UIDemoState.cpp
    ${PROJECT_SOURCE_DIR}/src/gameStates/OverlayDemoState.cpp
    ${PROJECT_SOURCE_DIR}/src/gameStates/PauseState.cpp
    ${PROJECT_SOURCE_DIR}/src/core/GameLoop.cpp
)

add_library(HammerEngine STATIC ${HAMMER_ENGINE_SOURCES})

set(MOCK_SOURCES
    mocks/MockAIBehavior.cpp
    mocks/MockPlayer.cpp
    mocks/MockNPC.cpp
    mocks/MockGameEngine.cpp
)

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${Boost_INCLUDE_DIRS}
    ${SDL3_SOURCE_DIR}/include
    ${SDL3_image_SOURCE_DIR}/include
    ${SDL3_ttf_SOURCE_DIR}/include
    ${SDL3_mixer_SOURCE_DIR}/include
)

# --- Test Executables ---

add_executable(save_manager_tests
    SaveManagerTests.cpp
    mocks/MockPlayer.cpp
)

add_executable(thread_system_tests
    ThreadSystemTests.cpp
)

add_executable(game_state_manager_tests
    GameStateManagerTests.cpp
)

add_executable(ai_optimization_tests
    AIOptimizationTest.cpp
)

add_executable(ai_scaling_benchmark
    AIScalingBenchmark.cpp
    ${MOCK_SOURCES}
)

add_executable(event_manager_scaling_benchmark
    EventManagerScalingBenchmark.cpp
    ${MOCK_SOURCES}
)

add_executable(thread_safe_ai_manager_tests
    ThreadSafeAIManagerTest.cpp
    ${MOCK_SOURCES}
)

add_executable(thread_safe_ai_integration_tests
    ThreadSafeAIIntegrationTest.cpp
    ${MOCK_SOURCES}
)

add_executable(event_manager_tests
    events/EventManagerTest.cpp
    ${MOCK_SOURCES}
)

add_executable(event_types_tests
    events/EventTypesTest.cpp
    ${MOCK_SOURCES}
)

add_executable(particle_manager_core_tests
    particle/ParticleManagerCoreTest.cpp
    ${MOCK_SOURCES}
)

add_executable(particle_manager_performance_tests
    particle/ParticleManagerPerformanceTest.cpp
    ${MOCK_SOURCES}
)

add_executable(particle_manager_weather_tests
    particle/ParticleManagerWeatherTest.cpp
    ${MOCK_SOURCES}
)

add_executable(particle_manager_threading_tests
    particle/ParticleManagerThreadingTest.cpp
    ${MOCK_SOURCES}
)

add_executable(weather_event_tests
    events/WeatherEventTest.cpp
    ${MOCK_SOURCES}
)

add_executable(ui_stress_test
    ui/ui_stress_test_main.cpp
)

add_executable(buffer_utilization_tests
    BufferUtilizationTest.cpp
)

add_executable(behavior_functionality_tests
    BehaviorFunctionalityTest.cpp
    ${MOCK_SOURCES}
)

add_executable(resource_template_manager_tests
    resources/ResourceTemplateManagerTests.cpp
)

add_executable(resource_factory_tests
    resources/ResourceFactoryTests.cpp
)

add_executable(resource_template_manager_json_tests
    resources/ResourceTemplateManagerJsonTests.cpp
)

add_executable(world_resource_manager_tests
    resources/WorldResourceManagerTests.cpp
)

add_executable(inventory_component_tests
    resources/InventoryComponentTests.cpp
    mocks/MockPlayer.cpp
)

add_executable(resource_change_event_tests
    resources/ResourceChangeEventTests.cpp
)

add_executable(resource_integration_tests
    resources/ResourceIntegrationTests.cpp
    mocks/MockPlayer.cpp
)

add_executable(resource_edge_case_tests
    resources/ResourceEdgeCaseTests.cpp
)

add_executable(json_reader_tests
    utils/JsonReaderTest.cpp
)

add_executable(world_generator_tests
    world/WorldGeneratorTests.cpp
)

add_executable(world_manager_tests
    world/WorldManagerTests.cpp
)

add_executable(world_manager_event_integration_tests
    world/WorldManagerEventIntegrationTests.cpp
)

# --- Compile Definitions ---
target_compile_definitions(event_manager_tests PRIVATE RUNNING_TESTS)
target_compile_definitions(event_types_tests PRIVATE RUNNING_TESTS)
target_compile_definitions(ui_stress_test PRIVATE UI_STRESS_TEST_BUILD)

# All Boost.Test executables need this definition
foreach(test_name
    save_manager_tests
    thread_system_tests
    game_state_manager_tests
    ai_optimization_tests
    ai_scaling_benchmark
    event_manager_scaling_benchmark
    thread_safe_ai_manager_tests
    thread_safe_ai_integration_tests
    event_manager_tests
    event_types_tests
    particle_manager_core_tests
    particle_manager_performance_tests
    particle_manager_weather_tests
    particle_manager_threading_tests
    weather_event_tests
    buffer_utilization_tests
    behavior_functionality_tests
    resource_template_manager_tests
    resource_factory_tests
    resource_template_manager_json_tests
    world_resource_manager_tests
    inventory_component_tests
    resource_change_event_tests
    resource_integration_tests
    resource_edge_case_tests
    json_reader_tests
    world_generator_tests
    world_manager_tests
    world_manager_event_integration_tests
)
    target_compile_definitions(${test_name} PRIVATE BOOST_TEST_NO_SIGNAL_HANDLING)
endforeach()


# --- Link Libraries ---

set(FULL_ENGINE_TESTS
    ai_scaling_benchmark
    event_manager_scaling_benchmark
    thread_safe_ai_manager_tests
    thread_safe_ai_integration_tests
    event_manager_tests
    event_types_tests
    particle_manager_core_tests
    particle_manager_performance_tests
    particle_manager_weather_tests
    particle_manager_threading_tests
    weather_event_tests
    behavior_functionality_tests
    world_resource_manager_tests
    inventory_component_tests
    resource_integration_tests
    resource_edge_case_tests
    world_manager_tests
    world_manager_event_integration_tests
)

foreach(test_name ${FULL_ENGINE_TESTS})
    target_link_libraries(${test_name} PRIVATE
        HammerEngine
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        SDL3_mixer::SDL3_mixer
        Boost::unit_test_framework
    )
endforeach()

foreach(test_name
    save_manager_tests
    thread_system_tests
    game_state_manager_tests
    ai_optimization_tests
    ui_stress_test
    buffer_utilization_tests
    resource_template_manager_tests
    resource_factory_tests
    resource_template_manager_json_tests
    resource_change_event_tests
    json_reader_tests
    world_generator_tests
)
    target_link_libraries(${test_name} PRIVATE
        HammerEngine
        SDL3::SDL3
        Boost::unit_test_framework
    )
endforeach()

# --- CTest Registration ---
foreach(test_name
    SaveManagerTests
    ThreadSystemTests
    GameStateManagerTests
    AIOptimizationTests
    AIScalingBenchmark
    EventManagerScalingBenchmark
    ThreadSafeAIManagerTests
    ThreadSafeAIIntegrationTests
    EventManagerTests
    EventTypesTests
    ParticleManagerCoreTests
    ParticleManagerPerformanceTests
    ParticleManagerWeatherTests
    ParticleManagerThreadingTests
    WeatherEventTests
    BufferUtilizationTests
    BehaviorFunctionalityTests
    ResourceTemplateManagerTests
    ResourceFactoryTests
    ResourceTemplateManagerJsonTests
    WorldResourceManagerTests
    InventoryComponentTests
    ResourceChangeEventTests
    ResourceIntegrationTests
    ResourceEdgeCaseTests
    JsonReaderTests
    WorldGeneratorTests
    WorldManagerTests
    WorldManagerEventIntegrationTests
)
    string(TOLOWER ${test_name} test_command)
    add_test(NAME ${test_name} COMMAND ${test_command})
endforeach()