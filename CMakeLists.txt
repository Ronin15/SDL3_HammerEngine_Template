cmake_minimum_required(VERSION 3.28)
project(SDL3_Template VERSION 1.0.0 LANGUAGES C CXX)

# Set build configurations
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
endif()

# Define available build configurations
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

# Set output directory paths based on build type
set(OUTPUT_BASE_DIR "${CMAKE_SOURCE_DIR}/bin")
set(OUTPUT_DEBUG_DIR "${OUTPUT_BASE_DIR}/debug")
set(OUTPUT_RELEASE_DIR "${OUTPUT_BASE_DIR}/release")

# Configure output directories based on the current build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DEBUG_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DEBUG_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DEBUG_DIR})
else() # Release build
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_RELEASE_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_RELEASE_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_RELEASE_DIR})
endif()

# Debug build settings
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g3 -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O0 -Wall -Wextra")

# Release build settings
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -s -mtune=native -flto")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -s -mtune=native -flto")

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile commands for use with tools like clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Copy compile_commands.json to project root after build
add_custom_target(copy-compile-commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/compile_commands.json
)
# Include FetchContent module
include(FetchContent)

# Specify the commit hash for each SDL3 component
# (Update these commit hashes to the latest versions as needed)
set(SDL3_GIT_TAG "main")           # Using main branch for SDL3
set(SDL3_IMAGE_GIT_TAG "main")     # Using main branch for SDL3_image
set(SDL3_TTF_GIT_TAG "main")       # Using main branch for SDL3_ttf

# Download and configure SDL3
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${SDL3_GIT_TAG}
)

# Build options for SDL3
set(SDL_SHARED TRUE CACHE BOOL "Build a SDL shared library")
set(SDL_STATIC FALSE CACHE BOOL "Build a SDL static library")
set(SDL_TEST FALSE CACHE BOOL "Build the SDL test framework")

# Download and configure SDL3_image
FetchContent_Declare(
    SDL3_image
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
    GIT_TAG ${SDL3_IMAGE_GIT_TAG}
)

# Build options for SDL3_image
set(SDL3IMAGE_SAMPLES FALSE CACHE BOOL "Build the SDL3_image sample programs")
set(SDL3IMAGE_VENDORED FALSE CACHE BOOL "Use vendored third-party libraries")
set(SDL3IMAGE_DEPS_SHARED FALSE CACHE BOOL "Use shared dependencies")

# Download and configure SDL3_ttf
FetchContent_Declare(
    SDL3_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG ${SDL3_TTF_GIT_TAG}
)

# Build options for SDL3_ttf
set(SDL3TTF_SAMPLES FALSE CACHE BOOL "Build the SDL3_ttf sample programs")
set(SDL3TTF_VENDORED TRUE CACHE BOOL "Use vendored third-party libraries")
set(SDL3TTF_DEPS_SHARED FALSE CACHE BOOL "Use shared dependencies")

# Make the packages available
FetchContent_MakeAvailable(SDL3 SDL3_image SDL3_ttf)

# FMOD Configuration
# Set FMOD API directory - adjust this path to your FMOD installation
set(FMOD_ROOT_DIR "${CMAKE_SOURCE_DIR}/vendor/fmod" CACHE PATH "Path to FMOD API root directory")
message(STATUS "FMOD_ROOT_DIR: ${FMOD_ROOT_DIR}")

# Set platform-specific variables for FMOD
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(FMOD_LIBRARY_PATH "${FMOD_ROOT_DIR}/api/core/lib/x64")
    else()
        set(FMOD_LIBRARY_PATH "${FMOD_ROOT_DIR}/api/core/lib/x86")
    endif()
    
    set(FMOD_LIBRARY "${FMOD_LIBRARY_PATH}/fmod_vc.lib")
    set(FMOD_DLL "${FMOD_LIBRARY_PATH}/fmod.dll")
elseif(APPLE)
    set(FMOD_LIBRARY_PATH "${FMOD_ROOT_DIR}/api/core/lib/macOS")
else() # Linux
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(FMOD_LIBRARY_PATH "${FMOD_ROOT_DIR}/api/core/lib/linux_64")
    else()
        set(FMOD_LIBRARY_PATH "${FMOD_ROOT_DIR}/api/core/lib/x86")
    endif()
endif()

message(STATUS "FMOD_LIBRARY_PATH: ${FMOD_LIBRARY_PATH}")

# Find FMOD library
find_library(FMOD_LIB 
    NAMES fmod libfmod fmod_vc
    PATHS ${FMOD_LIBRARY_PATH}
    NO_DEFAULT_PATH
)

# Set FMOD include directories
set(FMOD_INCLUDE_DIRS
    "${FMOD_ROOT_DIR}/api/core/inc"
)

# Check if FMOD library was found
if(NOT FMOD_LIB)
    message(WARNING "FMOD core library not found. Please check your FMOD_ROOT_DIR path.")
else()
    message(STATUS "Found FMOD core library: ${FMOD_LIB}")
endif()

# Find all source files
file(GLOB SOURCES "src/*.cpp")

# Create executable for your project
add_executable(${PROJECT_NAME} ${SOURCES})

if(UNIX AND NOT APPLE)
    # For Linux targets
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # Enable SDL console output for Linux debug builds
        target_compile_definitions(${PROJECT_NAME} PRIVATE SDL_MAIN_HANDLED)

        # No need for subsystem flags on Linux - console is the default

        message(STATUS "SDL console output enabled for debug build")
    endif()
endif()

if(WIN32)
    # For Windows targets
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(DLL_DESTINATION ${OUTPUT_DEBUG_DIR})
        else()
            set(DLL_DESTINATION ${OUTPUT_RELEASE_DIR})
        endif()

        # Enable SDL console output for Windows debug builds
        target_compile_definitions(${PROJECT_NAME} PRIVATE SDL_MAIN_HANDLED)

        # Set the subsystem to console for debug builds
        set_target_properties(${PROJECT_NAME} PROPERTIES
      WIN32_EXECUTABLE FALSE
    )

        message(STATUS "SDL console output enabled for debug build")
    else()
        # Use Windows subsystem for Release builds (no console)
        set_target_properties(${PROJECT_NAME} PROPERTIES
      WIN32_EXECUTABLE TRUE
    )
    endif()
endif()

if(APPLE)
    # For macOS targets
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # Enable SDL console output for macOS debug builds
        target_compile_definitions(${PROJECT_NAME} PRIVATE SDL_MAIN_HANDLED)

        # On macOS, applications are bundles by default, but we can ensure
        # that console output works properly in debug mode
        set_target_properties(${PROJECT_NAME} PROPERTIES
      MACOSX_BUNDLE FALSE      # Don't create a bundle in debug mode
      BUILD_WITH_INSTALL_RPATH TRUE
      BUILD_RPATH "@executable_path/../debug"
      INSTALL_RPATH "@executable_path/../debug"
    )

        # Add a post-build message
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND echo "macOS console output enabled for debug build"
    )

        message(STATUS "SDL console output enabled for macOS debug build")
    else()
        # For release builds, create a proper macOS application bundle
        set_target_properties(${PROJECT_NAME} PROPERTIES
      MACOSX_BUNDLE TRUE
      BUILD_WITH_INSTALL_RPATH TRUE
      BUILD_RPATH "@executable_path/../Frameworks"
      INSTALL_RPATH "@executable_path/../Frameworks"
    )
    endif()
endif()

# Link against the SDL3 libraries and FMOD
target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    ${FMOD_LIB}
)

# Specify include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FMOD_INCLUDE_DIRS}
)

# Copy SDL3 and FMOD runtime libraries to build directory (Windows-specific)
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE:SDL3_image::SDL3_image>
            $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
            ${FMOD_DLL}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()

# Install SDL3 DLLs and FMOD DLLs on Windows
if(WIN32)
    install(FILES
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE:SDL3_image::SDL3_image>
        $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
        ${FMOD_DLL}
        DESTINATION bin
    )
endif()

# Create output directories if they don't exist
file(MAKE_DIRECTORY ${OUTPUT_DEBUG_DIR})
file(MAKE_DIRECTORY ${OUTPUT_RELEASE_DIR})

# Display status message indicating successful configuration
message(STATUS "SDL3, SDL3_image, SDL3_ttf, and FMOD Core have been configured successfully")
message(STATUS "Debug binaries will be output to: ${OUTPUT_DEBUG_DIR}")
message(STATUS "Release binaries will be output to: ${OUTPUT_RELEASE_DIR}")
