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
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g3 -O0 -Og")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -O0 -Og -Wall -Wextra")

# Release build settings
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -flto -fno-strict-aliasing -fomit-frame-pointer -ffunction-sections -fdata-sections -Wl,--gc-sections -ffast-math -ftree-vectorize -fpeel-loops -funroll-loops -march=x86-64-v3 -mtune=generic -msse4.2 -mavx -mavx2 -mfma")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -flto -fno-strict-aliasing -fomit-frame-pointer -ffunction-sections -fdata-sections -Wl,--gc-sections -ffast-math -ftree-vectorize -fpeel-loops -funroll-loops -march=x86-64-v3 -mtune=generic -msse4.2 -mavx -mavx2 -mfma")
#mac Os -mcpu=apple-m3 -mtune=generic

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
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

# Add Boost library using FetchContent
set(BOOST_GIT_TAG "boost-1.88.0")  # Latest stable release as of April 2025
FetchContent_Declare(
    Boost
    GIT_REPOSITORY https://github.com/boostorg/boost.git
    GIT_TAG ${BOOST_GIT_TAG}
)

# Configure Boost build options
set(BOOST_INCLUDE_LIBRARIES
    container
    serialization
    # Add other Boost libraries you need
)
set(BOOST_ENABLE_CMAKE ON)
set(BOOST_ENABLE_MPI OFF)
set(BUILD_TESTING OFF)
set(BOOST_INSTALL_LAYOUT "system")

# Make Boost available
FetchContent_MakeAvailable(Boost)

# Specify the stable release tags for each SDL3 component
# Updated from "main" branch to specific release versions
set(SDL3_GIT_TAG "release-3.2.14")          # Latest stable release for SDL3
set(SDL3_IMAGE_GIT_TAG "release-3.2.4")     # Latest stable release for SDL3_image
set(SDL3_TTF_GIT_TAG "release-3.2.2")       # Latest stable release for SDL3_ttf
set(SDL3_MIXER_GIT_TAG "main")     # Latest stable release for SDL3_mixer

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

# Download and configure SDL3_mixer
FetchContent_Declare(
    SDL3_mixer
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
    GIT_TAG ${SDL3_MIXER_GIT_TAG}
)

# Build options for SDL3_mixer
set(SDL3MIXER_SAMPLES FALSE CACHE BOOL "Build the SDL3_mixer sample programs")
set(SDL3MIXER_VENDORED TRUE CACHE BOOL "Use vendored third-party libraries")
set(SDL3MIXER_DEPS_SHARED FALSE CACHE BOOL "Use shared dependencies")



# Make the SDL3 packages available
FetchContent_MakeAvailable(SDL3 SDL3_image SDL3_ttf SDL3_mixer)

# Find all source files
file(GLOB_RECURSE SOURCES
    "src/core/*.cpp"
    "src/managers/*.cpp"
    "src/gameStates/*.cpp"
    "src/entities/*.cpp"
    "src/events/*.cpp"
    "src/ai/*.cpp"
    "src/ui/*.cpp"
)

# Create executable for your project with proper extensions
if(WIN32)
    set(EXE_EXTENSION "")
elseif(APPLE)
    set(EXE_EXTENSION "")
else()
    set(EXE_EXTENSION "")
endif()

# Set the output name with proper extension
add_executable(${PROJECT_NAME} ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${PROJECT_NAME}${EXE_EXTENSION}"
)

# Configure window icon
# Note: The icon is set programmatically via SDL_SetWindowIcon in GameEngine.cpp
# This section handles platform-specific icon integration
if(WIN32)
    # Add the icon to the executable on Windows
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_SOURCE_DIR}/res/img/icon.ico")

    # Generate a resource file that includes the icon
    configure_file(
    "${CMAKE_SOURCE_DIR}/res/windows_icon.rc.in"
    "${CMAKE_BINARY_DIR}/windows_icon.rc"
    @ONLY
  )

    # Add the resource file to the executable
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/windows_icon.rc")
elseif(APPLE)
    # For macOS, bundle the icon with the application if it exists
    set(APP_ICON_MACOS "${CMAKE_SOURCE_DIR}/res/img/icon.icns")
    if(EXISTS "${APP_ICON_MACOS}")
        set(MACOSX_BUNDLE_ICON_FILE icon.icns)
        set_source_files_properties(${APP_ICON_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        message(STATUS "Using macOS icon: ${APP_ICON_MACOS}")
    else()
        message(STATUS "macOS icon not found at ${APP_ICON_MACOS} - using default icon")
    endif()
endif()

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

# Link against the SDL3 libraries and Boost
target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    SDL3_mixer::SDL3_mixer
    Boost::container
    Boost::serialization
    # Add other Boost libraries as needed
)

# Specify include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
    ${CMAKE_CURRENT_SOURCE_DIR}/include/managers
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gameStates
    ${CMAKE_CURRENT_SOURCE_DIR}/include/entities
    ${CMAKE_CURRENT_SOURCE_DIR}/include/events
    ${CMAKE_CURRENT_SOURCE_DIR}/include/entities/playerStates
    ${CMAKE_CURRENT_SOURCE_DIR}/include/events
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ai/behaviors
    ${CMAKE_CURRENT_SOURCE_DIR}/include/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui
)

# Copy SDL3 runtime libraries to build directory (Windows-specific)
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE:SDL3_image::SDL3_image>
            $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
            $<TARGET_FILE:SDL3_mixer::SDL3_mixer>
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()

# Install SDL3 DLLs on Windows
if(WIN32)
    install(FILES
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE:SDL3_image::SDL3_image>
        $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
        $<TARGET_FILE:SDL3_mixer::SDL3_mixer>
        DESTINATION bin
    )
endif()

# Create output directories if they don't exist
file(MAKE_DIRECTORY ${OUTPUT_DEBUG_DIR})
file(MAKE_DIRECTORY ${OUTPUT_RELEASE_DIR})

# Display status message indicating successful configuration
message(STATUS "SDL3, SDL3_image, SDL3_ttf, SDL3_mixer, and Boost have been configured successfully")
message(STATUS "Debug binaries will be output to: ${OUTPUT_DEBUG_DIR}")
message(STATUS "Release binaries will be output to: ${OUTPUT_RELEASE_DIR}")

# Enable testing
enable_testing()

# Add the tests subdirectory
add_subdirectory(tests)
