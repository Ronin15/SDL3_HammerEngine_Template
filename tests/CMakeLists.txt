# Tests for the SDL3_Template project

# Set the C++ standard
set(CMAKE_CXX_STANDARD 20) # Match the main project's C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Boost
find_package(Boost REQUIRED COMPONENTS unit_test_framework serialization)

# We standardize on Boost Test Framework for all tests
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
    mocks/AIBehavior.cpp
)

# Thread-safe AI Manager tests
add_executable(thread_safe_ai_manager_tests
    ThreadSafeAIManagerTest.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/AIManager.cpp
    mocks/AIBehavior.cpp
)

# Thread-safe AI Integration tests
add_executable(thread_safe_ai_integration_tests
    ThreadSafeAIIntegrationTest.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/AIManager.cpp
    mocks/AIBehavior.cpp
)

# Event Manager tests
add_executable(event_manager_tests
    events/EventManagerTest.cpp
    ${PROJECT_SOURCE_DIR}/src/managers/EventManager.cpp
    ${PROJECT_SOURCE_DIR}/src/events/Event.cpp
    ${PROJECT_SOURCE_DIR}/src/events/WeatherEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/SceneChangeEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/NPCSpawnEvent.cpp
)

# Event Types tests
add_executable(event_types_tests
    events/EventTypesTest.cpp
    ${PROJECT_SOURCE_DIR}/src/events/Event.cpp
    ${PROJECT_SOURCE_DIR}/src/events/WeatherEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/SceneChangeEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/NPCSpawnEvent.cpp
    ${PROJECT_SOURCE_DIR}/src/events/EventFactory.cpp
)

# Weather Event tests
add_executable(weather_event_tests
    events/WeatherEventTest.cpp
    ${PROJECT_SOURCE_DIR}/src/events/Event.cpp
    ${PROJECT_SOURCE_DIR}/src/events/WeatherEvent.cpp
)

# Add compile definitions for Boost.Test
target_compile_definitions(save_manager_tests PRIVATE
    BOOST_TEST_NO_LIB
)

target_compile_definitions(thread_system_tests PRIVATE
    BOOST_TEST_NO_LIB
)

# AI Optimization tests definitions
target_compile_definitions(ai_optimization_tests PRIVATE
    BOOST_TEST_NO_LIB
)

# AI Scaling benchmark definitions
target_compile_definitions(ai_scaling_benchmark PRIVATE
    BOOST_TEST_NO_LIB
)

# Thread-safe AI Manager tests definitions
target_compile_definitions(thread_safe_ai_manager_tests PRIVATE
    BOOST_TEST_NO_LIB
    BOOST_TEST_NO_SIGNAL_HANDLING
)

# Thread-safe AI Integration tests definitions
target_compile_definitions(thread_safe_ai_integration_tests PRIVATE
    BOOST_TEST_NO_LIB
    BOOST_TEST_NO_SIGNAL_HANDLING
)

# Event Manager tests definitions
target_compile_definitions(event_manager_tests PRIVATE
    BOOST_TEST_NO_LIB
    BOOST_TEST_NO_SIGNAL_HANDLING
)

# Event Types tests definitions
target_compile_definitions(event_types_tests PRIVATE
    BOOST_TEST_NO_LIB
    BOOST_TEST_NO_SIGNAL_HANDLING
)

# Weather Event tests definitions
target_compile_definitions(weather_event_tests PRIVATE
    BOOST_TEST_NO_LIB
    BOOST_TEST_NO_SIGNAL_HANDLING
)

# Link with required libraries
target_link_libraries(save_manager_tests PRIVATE
    SDL3::SDL3
    Boost::unit_test_framework
    Boost::serialization
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
    Boost::container
)

# Link AI scaling benchmark with required libraries
target_link_libraries(ai_scaling_benchmark PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
    Boost::container
)

# Link Thread-safe AI Manager tests with required libraries
target_link_libraries(thread_safe_ai_manager_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
    Boost::container
)

# Link Thread-safe AI Integration tests with required libraries
target_link_libraries(thread_safe_ai_integration_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
    Boost::container
)

# Link Event Manager tests with required libraries
target_link_libraries(event_manager_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
    Boost::container
)

# Link Event Types tests with required libraries
target_link_libraries(event_types_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
    Boost::container
)

# Link Weather Event tests with required libraries
target_link_libraries(weather_event_tests PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    Boost::unit_test_framework
    Boost::container
)

# Add the tests to CTest
add_test(NAME SaveManagerTests COMMAND save_manager_tests)
add_test(NAME ThreadSystemTests COMMAND thread_system_tests)
add_test(NAME AIOptimizationTests COMMAND ai_optimization_tests)
add_test(NAME AIScalingBenchmark COMMAND ai_scaling_benchmark)
add_test(NAME ThreadSafeAIManagerTests COMMAND thread_safe_ai_manager_tests)
add_test(NAME ThreadSafeAIIntegrationTests COMMAND thread_safe_ai_integration_tests)
add_test(NAME EventManagerTests COMMAND event_manager_tests)
add_test(NAME EventTypesTests COMMAND event_types_tests)
add_test(NAME WeatherEventTests COMMAND weather_event_tests)
