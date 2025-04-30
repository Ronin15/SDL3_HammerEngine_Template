# Tests for the SDL3_Template project

# Set the C++ standard
set(CMAKE_CXX_STANDARD 20) # Match the main project's C++ standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Boost
find_package(Boost REQUIRED COMPONENTS unit_test_framework)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Add test executable
add_executable(save_manager_tests 
    SaveManagerTests.cpp
    MockPlayer.cpp
)

# Add compile definitions for Boost.Test
target_compile_definitions(save_manager_tests PRIVATE
    BOOST_TEST_NO_LIB
)

# Link with required libraries
target_link_libraries(save_manager_tests PRIVATE
    SDL3::SDL3
    Boost::unit_test_framework
)

# Add the test to CTest
add_test(NAME SaveManagerTests COMMAND save_manager_tests)