# Tests for the SDL3_Template project

# Set the C++ standard
set(CMAKE_CXX_STANDARD 20) # Match the main project's C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check if we can build tests - require Boost Test for test framework
find_package(Boost REQUIRED COMPONENTS container)
if(NOT HAVE_BOOST_TEST)
    message(STATUS "Skipping all tests - Boost Test not found (required)")
    return()
endif()

message(STATUS "Building tests with Boost Test support")

# --- Create TestMocks Library ---
set(MOCK_SOURCES
    mocks/MockAIBehavior.cpp
    mocks/MockPlayer.cpp
    mocks/MockNPC.cpp
    mocks/MockGameEngine.cpp
)

add_library(TestMocks STATIC ${MOCK_SOURCES})
target_include_directories(TestMocks PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(TestMocks PUBLIC HammerEngineLib)

# --- Test Executables ---

# Simple tests (no mocks needed)
set(SIMPLE_TESTS
    save_manager_tests
    thread_system_tests
    game_state_manager_tests
    ai_optimization_tests
    buffer_utilization_tests
    resource_template_manager_tests
    resource_factory_tests
    resource_template_manager_json_tests
    world_resource_manager_tests
    resource_change_event_tests
    resource_edge_case_tests
    json_reader_tests
    world_generator_tests
    world_manager_tests
    world_manager_event_integration_tests
    ui_stress_test
)

# Complex tests (need mocks)
set(MOCK_TESTS
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
)

# Tests that need special mock combinations
set(INVENTORY_TESTS
    inventory_component_tests
    resource_integration_tests
)

# Create simple test executables
foreach(test_name ${SIMPLE_TESTS})
    # Determine source file based on test name
    if(${test_name} STREQUAL "save_manager_tests")
        set(test_source "SaveManagerTests.cpp")
    elseif(${test_name} STREQUAL "thread_system_tests")
        set(test_source "ThreadSystemTests.cpp")
    elseif(${test_name} STREQUAL "game_state_manager_tests")
        set(test_source "GameStateManagerTests.cpp")
    elseif(${test_name} STREQUAL "ai_optimization_tests")
        set(test_source "AIOptimizationTest.cpp")
    elseif(${test_name} STREQUAL "buffer_utilization_tests")
        set(test_source "BufferUtilizationTest.cpp")
    elseif(${test_name} STREQUAL "ui_stress_test")
        set(test_source "ui/ui_stress_test_main.cpp")
    elseif(${test_name} MATCHES "^resource_")
        string(REPLACE "resource_" "" suffix ${test_name})
        string(REPLACE "_tests" "" suffix ${suffix})
        string(REPLACE "_" "" suffix ${suffix})
        if(${suffix} STREQUAL "templatemanager")
            set(test_source "resources/ResourceTemplateManagerTests.cpp")
        elseif(${suffix} STREQUAL "factory")
            set(test_source "resources/ResourceFactoryTests.cpp")
        elseif(${suffix} STREQUAL "templatemanagerjson")
            set(test_source "resources/ResourceTemplateManagerJsonTests.cpp")
        elseif(${suffix} STREQUAL "changeevent")
            set(test_source "resources/ResourceChangeEventTests.cpp")
        elseif(${suffix} STREQUAL "edgecase")
            set(test_source "resources/ResourceEdgeCaseTests.cpp")
        endif()
    elseif(${test_name} STREQUAL "world_resource_manager_tests")
        set(test_source "resources/WorldResourceManagerTests.cpp")
    elseif(${test_name} STREQUAL "json_reader_tests")
        set(test_source "utils/JsonReaderTest.cpp")
    elseif(${test_name} STREQUAL "world_generator_tests")
        set(test_source "world/WorldGeneratorTests.cpp")
    elseif(${test_name} STREQUAL "world_manager_tests")
        set(test_source "world/WorldManagerTests.cpp")
    elseif(${test_name} STREQUAL "world_manager_event_integration_tests")
        set(test_source "world/WorldManagerEventIntegrationTests.cpp")
    endif()

    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE HammerEngineLib Boost::unit_test_framework)
    target_compile_definitions(${test_name} PRIVATE BOOST_TEST_NO_SIGNAL_HANDLING)
endforeach()

# Create mock test executables
foreach(test_name ${MOCK_TESTS})
    # Determine source file based on test name
    if(${test_name} STREQUAL "ai_scaling_benchmark")
        set(test_source "AIScalingBenchmark.cpp")
    elseif(${test_name} STREQUAL "event_manager_scaling_benchmark")
        set(test_source "EventManagerScalingBenchmark.cpp")
    elseif(${test_name} STREQUAL "thread_safe_ai_manager_tests")
        set(test_source "ThreadSafeAIManagerTest.cpp")
    elseif(${test_name} STREQUAL "thread_safe_ai_integration_tests")
        set(test_source "ThreadSafeAIIntegrationTest.cpp")
    elseif(${test_name} STREQUAL "event_manager_tests")
        set(test_source "events/EventManagerTest.cpp")
    elseif(${test_name} STREQUAL "event_types_tests")
        set(test_source "events/EventTypesTest.cpp")
    elseif(${test_name} STREQUAL "weather_event_tests")
        set(test_source "events/WeatherEventTest.cpp")
    elseif(${test_name} STREQUAL "behavior_functionality_tests")
        set(test_source "BehaviorFunctionalityTest.cpp")
    elseif(${test_name} MATCHES "^particle_manager_")
        string(REPLACE "particle_manager_" "" suffix ${test_name})
        string(REPLACE "_tests" "" suffix ${suffix})
        if(${suffix} STREQUAL "core")
            set(test_source "particle/ParticleManagerCoreTest.cpp")
        elseif(${suffix} STREQUAL "performance")
            set(test_source "particle/ParticleManagerPerformanceTest.cpp")
        elseif(${suffix} STREQUAL "weather")
            set(test_source "particle/ParticleManagerWeatherTest.cpp")
        elseif(${suffix} STREQUAL "threading")
            set(test_source "particle/ParticleManagerThreadingTest.cpp")
        endif()
    endif()

    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE HammerEngineLib TestMocks Boost::unit_test_framework)
    target_compile_definitions(${test_name} PRIVATE BOOST_TEST_NO_SIGNAL_HANDLING)
endforeach()

# Create inventory tests (need specific mock combination)
foreach(test_name ${INVENTORY_TESTS})
    if(${test_name} STREQUAL "inventory_component_tests")
        set(test_source "resources/InventoryComponentTests.cpp")
    elseif(${test_name} STREQUAL "resource_integration_tests")
        set(test_source "resources/ResourceIntegrationTests.cpp")
    endif()

    add_executable(${test_name} ${test_source} mocks/MockPlayer.cpp)
    target_link_libraries(${test_name} PRIVATE HammerEngineLib Boost::unit_test_framework)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
    target_compile_definitions(${test_name} PRIVATE BOOST_TEST_NO_SIGNAL_HANDLING)
endforeach()

# Special configurations
target_link_libraries(ui_stress_test PRIVATE Boost::container)
target_compile_definitions(event_manager_tests PRIVATE RUNNING_TESTS)
target_compile_definitions(event_types_tests PRIVATE RUNNING_TESTS)
target_compile_definitions(ui_stress_test PRIVATE UI_STRESS_TEST_BUILD)

# Add save_manager_tests special mock
target_sources(save_manager_tests PRIVATE mocks/MockPlayer.cpp)
target_include_directories(save_manager_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks)

# --- CTest Registration ---
set(ALL_TESTS ${SIMPLE_TESTS} ${MOCK_TESTS} ${INVENTORY_TESTS})
foreach(test_name ${ALL_TESTS})
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()