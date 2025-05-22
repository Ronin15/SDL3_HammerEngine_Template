# Tests for the SDL3_Template project

# Set the C++ standard
set(CMAKE_CXX_STANDARD 20) # Match the main project's C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Boost
find_package(Boost REQUIRED COMPONENTS unit_test_framework)

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
)

# Add test executables
add_executable(save_manager_tests 
    SaveManagerTests.cpp
    MockPlayer.cpp
)

add_executable(thread_system_tests
    ThreadSystemTests.cpp
)

# AI tests disabled
# add_executable(ai_optimization_tests
#    AIOptimizationTest.cpp
# )

# Add compile definitions for Boost.Test
target_compile_definitions(save_manager_tests PRIVATE
    BOOST_TEST_NO_LIB
)

target_compile_definitions(thread_system_tests PRIVATE
    BOOST_TEST_NO_LIB
)

# AI Optimization tests temporarily disabled due to build issues
# target_compile_definitions(ai_optimization_tests PRIVATE
#    BOOST_TEST_NO_LIB
# )

# Link with required libraries
target_link_libraries(save_manager_tests PRIVATE
    SDL3::SDL3
    Boost::unit_test_framework
)

target_link_libraries(thread_system_tests PRIVATE
    SDL3::SDL3
    Boost::unit_test_framework
)

# AI tests disabled - library not needed
# add_library(ai_system OBJECT
#    ${PROJECT_SOURCE_DIR}/src/managers/AIManager.cpp
# )

# AI tests disabled
# target_include_directories(ai_system PRIVATE
#    ${PROJECT_SOURCE_DIR}/include
#    ${PROJECT_SOURCE_DIR}/include/core
#    ${PROJECT_SOURCE_DIR}/include/managers
#    ${PROJECT_SOURCE_DIR}/include/states
#    ${PROJECT_SOURCE_DIR}/include/entities
#    ${PROJECT_SOURCE_DIR}/include/entities/states
#    ${PROJECT_SOURCE_DIR}/include/ai
#    ${PROJECT_SOURCE_DIR}/include/ai/behaviors
#    ${PROJECT_SOURCE_DIR}/include/utils
#    ${PROJECT_SOURCE_DIR}/include/io
#    ${PROJECT_SOURCE_DIR}/include/ui
# )



# AI tests disabled
# target_link_libraries(ai_system PRIVATE
#    SDL3::SDL3
#    SDL3_image::SDL3_image
#    Boost::container
# )

# AI tests disabled
# target_link_libraries(ai_optimization_tests PRIVATE
#    SDL3::SDL3
#    SDL3_image::SDL3_image
#    Boost::unit_test_framework
#    Boost::container
#    ai_system
# )

# Add the tests to CTest
add_test(NAME SaveManagerTests COMMAND save_manager_tests)
add_test(NAME ThreadSystemTests COMMAND thread_system_tests)
# AI tests disabled
# add_test(NAME AIOptimizationTests COMMAND ai_optimization_tests)