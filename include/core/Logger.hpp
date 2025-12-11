/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef LOGGER_HPP
#define LOGGER_HPP

// Required includes for logging system:
// - string: Used in macro expansions for std::string() conversions
// - cstdio: Required for printf() and fflush() functions
// - cstdint: Required for uint8_t type
// - mutex: Required for thread-safe logging
// - atomic: Required for std::atomic<bool> benchmark mode flag
#include <atomic> // IWYU pragma: keep - Required for std::atomic<bool> benchmark mode flag
#include <cstdint> // IWYU pragma: keep - Required for uint8_t type
#include <cstdio> // IWYU pragma: keep - Required for printf() and fflush() functions
#include <mutex> // IWYU pragma: keep - Required for thread-safe logging
#include <string> // IWYU pragma: keep - Required for std::string() conversions in macros

namespace HammerEngine {
enum class LogLevel : uint8_t {
  CRITICAL = 0,     // Always logs (even in release for crashes)
  ERROR_LEVEL = 1,  // Debug only (renamed to avoid macro conflicts)
  WARNING = 2,      // Debug only
  INFO = 3,         // Debug only
  DEBUG_LEVEL = 4   // Debug only (renamed to avoid macro conflicts)
};

#ifdef DEBUG
// Full logging system in debug builds - lockless for safety
class Logger {
private:
  static std::atomic<bool> s_benchmarkMode;
  static std::mutex s_logMutex;

public:
  static void SetBenchmarkMode(bool enabled) {
    s_benchmarkMode.store(enabled, std::memory_order_relaxed);
  }

  static bool IsBenchmarkMode() {
    return s_benchmarkMode.load(std::memory_order_relaxed);
  }

  static void Log(LogLevel level, const char *system,
                  const std::string &message) {
    if (s_benchmarkMode.load(std::memory_order_relaxed)) {
      return;
    }

    // Thread-safe logging with mutex protection
    std::lock_guard<std::mutex> lock(s_logMutex);
    printf("Hammer Game Engine - [%s] %s: %s\n", system, getLevelString(level),
           message.c_str());
    fflush(stdout);
  }
  static void Log(LogLevel level, const char *system, const char *message) {
    if (s_benchmarkMode.load(std::memory_order_relaxed)) {
      return;
    }

    // Thread-safe logging with mutex protection
    std::lock_guard<std::mutex> lock(s_logMutex);
    printf("Hammer Game Engine - [%s] %s: %s\n", system, getLevelString(level),
           message);
    fflush(stdout);
  }

private:
  static const char *getLevelString(LogLevel level) {
    switch (level) {
    case LogLevel::CRITICAL:
      return "CRITICAL";
    case LogLevel::ERROR_LEVEL:
      return "ERROR";
    case LogLevel::WARNING:
      return "WARNING";
    case LogLevel::INFO:
      return "INFO";
    case LogLevel::DEBUG_LEVEL:
      return "DEBUG";
    default:
      return "UNKNOWN";
    }
  }
};

// Debug build macros - full functionality
#define HAMMER_CRITICAL(system, msg)                                           \
  HammerEngine::Logger::Log(HammerEngine::LogLevel::CRITICAL, system, msg)
#define HAMMER_ERROR(system, msg)                                              \
  HammerEngine::Logger::Log(HammerEngine::LogLevel::ERROR_LEVEL, system, msg)
#define HAMMER_WARN(system, msg)                                               \
  HammerEngine::Logger::Log(HammerEngine::LogLevel::WARNING, system, msg)
#define HAMMER_INFO(system, msg)                                               \
  HammerEngine::Logger::Log(HammerEngine::LogLevel::INFO, system, msg)
#define HAMMER_DEBUG(system, msg)                                              \
  HammerEngine::Logger::Log(HammerEngine::LogLevel::DEBUG_LEVEL, system, msg)

#else
// Release builds - ultra-minimal overhead, lockless
class Logger {
private:
  static std::atomic<bool> s_benchmarkMode;

public:
  static std::mutex s_logMutex; // Public for macro access

  static void SetBenchmarkMode(bool enabled) {
    s_benchmarkMode.store(enabled, std::memory_order_relaxed);
  }

  static bool IsBenchmarkMode() {
    return s_benchmarkMode.load(std::memory_order_relaxed);
  }

  static void Log(const char *level, const char *system,
                  const std::string &message) {
    if (s_benchmarkMode.load(std::memory_order_relaxed)) {
      return;
    }
    std::lock_guard<std::mutex> lock(s_logMutex);
    printf("Hammer Game Engine - [%s] %s: %s\n", system, level,
           message.c_str());
    fflush(stdout);
  }

  static void Log(const char *level, const char *system, const char *message) {
    if (s_benchmarkMode.load(std::memory_order_relaxed)) {
      return;
    }
    std::lock_guard<std::mutex> lock(s_logMutex);
    printf("Hammer Game Engine - [%s] %s: %s\n", system, level, message);
    fflush(stdout);
  }
};

#define HAMMER_CRITICAL(system, msg)                                           \
  HammerEngine::Logger::Log("CRITICAL", system, msg)

#define HAMMER_ERROR(system, msg)                                              \
  HammerEngine::Logger::Log("ERROR", system, msg)

#define HAMMER_WARN(system, msg) ((void)0)  // Zero overhead
#define HAMMER_INFO(system, msg) ((void)0)  // Zero overhead
#define HAMMER_DEBUG(system, msg) ((void)0) // Zero overhead
#endif

// Static member definitions - shared by both DEBUG and RELEASE builds
inline std::atomic<bool> Logger::s_benchmarkMode{false};
inline std::mutex Logger::s_logMutex{};

// Convenience macros for each manager and core system

// Core Systems
#define GAMELOOP_CRITICAL(msg) HAMMER_CRITICAL("GameLoop", msg)
#define GAMELOOP_ERROR(msg) HAMMER_ERROR("GameLoop", msg)
#define GAMELOOP_WARN(msg) HAMMER_WARN("GameLoop", msg)
#define GAMELOOP_INFO(msg) HAMMER_INFO("GameLoop", msg)
#define GAMELOOP_DEBUG(msg) HAMMER_DEBUG("GameLoop", msg)

#define GAMEENGINE_CRITICAL(msg) HAMMER_CRITICAL("GameEngine", msg)
#define GAMEENGINE_ERROR(msg) HAMMER_ERROR("GameEngine", msg)
#define GAMEENGINE_WARN(msg) HAMMER_WARN("GameEngine", msg)
#define GAMEENGINE_INFO(msg) HAMMER_INFO("GameEngine", msg)
#define GAMEENGINE_DEBUG(msg) HAMMER_DEBUG("GameEngine", msg)

#define THREADSYSTEM_CRITICAL(msg) HAMMER_CRITICAL("ThreadSystem", msg)
#define THREADSYSTEM_ERROR(msg) HAMMER_ERROR("ThreadSystem", msg)
#define THREADSYSTEM_WARN(msg) HAMMER_WARN("ThreadSystem", msg)
#define THREADSYSTEM_INFO(msg) HAMMER_INFO("ThreadSystem", msg)
#define THREADSYSTEM_DEBUG(msg) HAMMER_DEBUG("ThreadSystem", msg)

// Manager Systems
#define TEXTURE_CRITICAL(msg) HAMMER_CRITICAL("TextureManager", msg)
#define TEXTURE_ERROR(msg) HAMMER_ERROR("TextureManager", msg)
#define TEXTURE_WARN(msg) HAMMER_WARN("TextureManager", msg)
#define TEXTURE_INFO(msg) HAMMER_INFO("TextureManager", msg)
#define TEXTURE_DEBUG(msg) HAMMER_DEBUG("TextureManager", msg)

#define SOUND_CRITICAL(msg) HAMMER_CRITICAL("SoundManager", msg)
#define SOUND_ERROR(msg) HAMMER_ERROR("SoundManager", msg)
#define SOUND_WARN(msg) HAMMER_WARN("SoundManager", msg)
#define SOUND_INFO(msg) HAMMER_INFO("SoundManager", msg)
#define SOUND_DEBUG(msg) HAMMER_DEBUG("SoundManager", msg)

#define FONT_CRITICAL(msg) HAMMER_CRITICAL("FontManager", msg)
#define FONT_ERROR(msg) HAMMER_ERROR("FontManager", msg)
#define FONT_WARN(msg) HAMMER_WARN("FontManager", msg)
#define FONT_INFO(msg) HAMMER_INFO("FontManager", msg)
#define FONT_DEBUG(msg) HAMMER_DEBUG("FontManager", msg)

#define PARTICLE_CRITICAL(msg) HAMMER_CRITICAL("ParticleManager", msg)
#define PARTICLE_ERROR(msg) HAMMER_ERROR("ParticleManager", msg)
#define PARTICLE_WARN(msg) HAMMER_WARN("ParticleManager", msg)
#define PARTICLE_INFO(msg) HAMMER_INFO("ParticleManager", msg)
#define PARTICLE_DEBUG(msg) HAMMER_DEBUG("ParticleManager", msg)

#define AI_CRITICAL(msg) HAMMER_CRITICAL("AIManager", msg)
#define AI_ERROR(msg) HAMMER_ERROR("AIManager", msg)
#define AI_WARN(msg) HAMMER_WARN("AIManager", msg)
#define AI_INFO(msg) HAMMER_INFO("AIManager", msg)
#define AI_DEBUG(msg) HAMMER_DEBUG("AIManager", msg)

#define EVENT_CRITICAL(msg) HAMMER_CRITICAL("EventManager", msg)
#define EVENT_ERROR(msg) HAMMER_ERROR("EventManager", msg)
#define EVENT_WARN(msg) HAMMER_WARN("EventManager", msg)
#define EVENT_INFO(msg) HAMMER_INFO("EventManager", msg)
#define EVENT_DEBUG(msg) HAMMER_DEBUG("EventManager", msg)

#define INPUT_CRITICAL(msg) HAMMER_CRITICAL("InputManager", msg)
#define INPUT_ERROR(msg) HAMMER_ERROR("InputManager", msg)
#define INPUT_WARN(msg) HAMMER_WARN("InputManager", msg)
#define INPUT_INFO(msg) HAMMER_INFO("InputManager", msg)
#define INPUT_DEBUG(msg) HAMMER_DEBUG("InputManager", msg)

#define UI_CRITICAL(msg) HAMMER_CRITICAL("UIManager", msg)
#define UI_ERROR(msg) HAMMER_ERROR("UIManager", msg)
#define UI_WARN(msg) HAMMER_WARN("UIManager", msg)
#define UI_INFO(msg) HAMMER_INFO("UIManager", msg)
#define UI_DEBUG(msg) HAMMER_DEBUG("UIManager", msg)

#define CAMERA_CRITICAL(msg) HAMMER_CRITICAL("Camera", msg)
#define CAMERA_ERROR(msg) HAMMER_ERROR("Camera", msg)
#define CAMERA_WARN(msg) HAMMER_WARN("Camera", msg)
#define CAMERA_INFO(msg) HAMMER_INFO("Camera", msg)
#define CAMERA_DEBUG(msg) HAMMER_DEBUG("Camera", msg)

#define SAVEGAME_CRITICAL(msg) HAMMER_CRITICAL("SaveGameManager", msg)
#define SAVEGAME_ERROR(msg) HAMMER_ERROR("SaveGameManager", msg)
#define SAVEGAME_WARN(msg) HAMMER_WARN("SaveGameManager", msg)
#define SAVEGAME_INFO(msg) HAMMER_INFO("SaveGameManager", msg)
#define SAVEGAME_DEBUG(msg) HAMMER_DEBUG("SaveGameManager", msg)

#define RESOURCE_CRITICAL(msg) HAMMER_CRITICAL("ResourceTemplateManager", msg)
#define RESOURCE_ERROR(msg) HAMMER_ERROR("ResourceTemplateManager", msg)
#define RESOURCE_WARN(msg) HAMMER_WARN("ResourceTemplateManager", msg)
#define RESOURCE_INFO(msg) HAMMER_INFO("ResourceTemplateManager", msg)
#define RESOURCE_DEBUG(msg) HAMMER_DEBUG("ResourceTemplateManager", msg)

#define INVENTORY_CRITICAL(msg) HAMMER_CRITICAL("InventoryComponent", msg)
#define INVENTORY_ERROR(msg) HAMMER_ERROR("InventoryComponent", msg)
#define INVENTORY_WARN(msg) HAMMER_WARN("InventoryComponent", msg)
#define INVENTORY_INFO(msg) HAMMER_INFO("InventoryComponent", msg)
#define INVENTORY_DEBUG(msg) HAMMER_DEBUG("InventoryComponent", msg)

#define WORLD_RESOURCE_CRITICAL(msg)                                           \
  HAMMER_CRITICAL("WorldResourceManager", msg)
#define WORLD_RESOURCE_ERROR(msg) HAMMER_ERROR("WorldResourceManager", msg)
#define WORLD_RESOURCE_WARN(msg) HAMMER_WARN("WorldResourceManager", msg)
#define WORLD_RESOURCE_INFO(msg) HAMMER_INFO("WorldResourceManager", msg)
#define WORLD_RESOURCE_DEBUG(msg) HAMMER_DEBUG("WorldResourceManager", msg)

#define WORLD_MANAGER_CRITICAL(msg) HAMMER_CRITICAL("WorldManager", msg)
#define WORLD_MANAGER_ERROR(msg) HAMMER_ERROR("WorldManager", msg)
#define WORLD_MANAGER_WARN(msg) HAMMER_WARN("WorldManager", msg)
#define WORLD_MANAGER_INFO(msg) HAMMER_INFO("WorldManager", msg)
#define WORLD_MANAGER_DEBUG(msg) HAMMER_DEBUG("WorldManager", msg)

// Entity and State Systems
#define GAMESTATE_CRITICAL(msg) HAMMER_CRITICAL("GameStateManager", msg)
#define GAMESTATE_ERROR(msg) HAMMER_ERROR("GameStateManager", msg)
#define GAMESTATE_WARN(msg) HAMMER_WARN("GameStateManager", msg)
#define GAMESTATE_INFO(msg) HAMMER_INFO("GameStateManager", msg)
#define GAMESTATE_DEBUG(msg) HAMMER_DEBUG("GameStateManager", msg)

#define GAMEPLAY_CRITICAL(msg) HAMMER_CRITICAL("GamePlayState", msg)
#define GAMEPLAY_ERROR(msg) HAMMER_ERROR("GamePlayState", msg)
#define GAMEPLAY_WARN(msg) HAMMER_WARN("GamePlayState", msg)
#define GAMEPLAY_INFO(msg) HAMMER_INFO("GamePlayState", msg)
#define GAMEPLAY_DEBUG(msg) HAMMER_DEBUG("GamePlayState", msg)

#define ENTITYSTATE_CRITICAL(msg) HAMMER_CRITICAL("EntityStateManager", msg)
#define ENTITYSTATE_ERROR(msg) HAMMER_ERROR("EntityStateManager", msg)
#define ENTITYSTATE_WARN(msg) HAMMER_WARN("EntityStateManager", msg)
#define ENTITYSTATE_INFO(msg) HAMMER_INFO("EntityStateManager", msg)
#define ENTITYSTATE_DEBUG(msg) HAMMER_DEBUG("EntityStateManager", msg)

// Entity Systems
#define ENTITY_CRITICAL(msg) HAMMER_CRITICAL("Entity", msg)
#define ENTITY_ERROR(msg) HAMMER_ERROR("Entity", msg)
#define ENTITY_WARN(msg) HAMMER_WARN("Entity", msg)
#define ENTITY_INFO(msg) HAMMER_INFO("Entity", msg)
#define ENTITY_DEBUG(msg) HAMMER_DEBUG("Entity", msg)

#define PLAYER_CRITICAL(msg) HAMMER_CRITICAL("Player", msg)
#define PLAYER_ERROR(msg) HAMMER_ERROR("Player", msg)
#define PLAYER_WARN(msg) HAMMER_WARN("Player", msg)
#define PLAYER_INFO(msg) HAMMER_INFO("Player", msg)
#define PLAYER_DEBUG(msg) HAMMER_DEBUG("Player", msg)

#define NPC_CRITICAL(msg) HAMMER_CRITICAL("NPC", msg)
#define NPC_ERROR(msg) HAMMER_ERROR("NPC", msg)
#define NPC_WARN(msg) HAMMER_WARN("NPC", msg)
#define NPC_INFO(msg) HAMMER_INFO("NPC", msg)
#define NPC_DEBUG(msg) HAMMER_DEBUG("NPC", msg)

// Collision and Pathfinding Systems
#define COLLISION_CRITICAL(msg) HAMMER_CRITICAL("CollisionManager", msg)
#define COLLISION_ERROR(msg) HAMMER_ERROR("CollisionManager", msg)
#define COLLISION_WARN(msg) HAMMER_WARN("CollisionManager", msg)
#define COLLISION_INFO(msg) HAMMER_INFO("CollisionManager", msg)
#define COLLISION_DEBUG(msg) HAMMER_DEBUG("CollisionManager", msg)

#define PATHFIND_CRITICAL(msg) HAMMER_CRITICAL("Pathfinding", msg)
#define PATHFIND_ERROR(msg) HAMMER_ERROR("Pathfinding", msg)
#define PATHFIND_WARN(msg) HAMMER_WARN("Pathfinding", msg)
#define PATHFIND_INFO(msg) HAMMER_INFO("Pathfinding", msg)
#define PATHFIND_DEBUG(msg) HAMMER_DEBUG("Pathfinding", msg)

#define SETTINGS_CRITICAL(msg) HAMMER_CRITICAL("SettingsManager", msg)
#define SETTINGS_ERROR(msg) HAMMER_ERROR("SettingsManager", msg)
#define SETTINGS_WARNING(msg) HAMMER_WARN("SettingsManager", msg)
#define SETTINGS_INFO(msg) HAMMER_INFO("SettingsManager", msg)
#define SETTINGS_DEBUG(msg) HAMMER_DEBUG("SettingsManager", msg)

// WeatherController logging
#define WEATHER_CRITICAL(msg) HAMMER_CRITICAL("WeatherController", msg)
#define WEATHER_ERROR(msg) HAMMER_ERROR("WeatherController", msg)
#define WEATHER_WARNING(msg) HAMMER_WARN("WeatherController", msg)
#define WEATHER_INFO(msg) HAMMER_INFO("WeatherController", msg)
#define WEATHER_DEBUG(msg) HAMMER_DEBUG("WeatherController", msg)

// Benchmark mode convenience macros
#define HAMMER_ENABLE_BENCHMARK_MODE()                                         \
  HammerEngine::Logger::SetBenchmarkMode(true)
#define HAMMER_DISABLE_BENCHMARK_MODE()                                        \
  HammerEngine::Logger::SetBenchmarkMode(false)

} // namespace HammerEngine

#endif // LOGGER_HPP
