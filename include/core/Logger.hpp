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
#include <string> // IWYU pragma: keep - Required for std::string() conversions in macros
#include <cstdio> // IWYU pragma: keep - Required for printf() and fflush() functions
#include <cstdint> // IWYU pragma: keep - Required for uint8_t type
#include <mutex> // IWYU pragma: keep - Required for thread-safe logging
#include <atomic> // IWYU pragma: keep - Required for std::atomic<bool> benchmark mode flag

namespace Forge {
    enum class LogLevel : uint8_t {
        CRITICAL = 0,  // Always logs (even in release for crashes)
        ERROR = 1,     // Debug only
        WARNING = 2,   // Debug only  
        INFO = 3,      // Debug only
        DEBUG_LEVEL = 4      // Debug only (renamed to avoid macro conflicts)
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
        
        static void Log(LogLevel level, const char* system, const std::string& message) {
            if (s_benchmarkMode.load(std::memory_order_relaxed)) {
                return;
            }
            
            // Thread-safe logging with mutex protection
            std::lock_guard<std::mutex> lock(s_logMutex);
            printf("Forge Game Engine - [%s] %s: %s\n", system, getLevelString(level), message.c_str());
            fflush(stdout);
        }
        
        static void Log(LogLevel level, const char* system, const char* message) {
            if (s_benchmarkMode.load(std::memory_order_relaxed)) {
                return;
            }
            
            // Thread-safe logging with mutex protection
            std::lock_guard<std::mutex> lock(s_logMutex);
            printf("Forge Game Engine - [%s] %s: %s\n", system, getLevelString(level), message);
            fflush(stdout);
        }
        
    private:
        static const char* getLevelString(LogLevel level) {
            switch(level) {
                case LogLevel::CRITICAL: return "CRITICAL";
                case LogLevel::ERROR: return "ERROR";
                case LogLevel::WARNING: return "WARNING";
                case LogLevel::INFO: return "INFO";
                case LogLevel::DEBUG_LEVEL: return "DEBUG";
                default: return "UNKNOWN";
            }
        }
    };
    
    // Initialize static members
    inline std::atomic<bool> Logger::s_benchmarkMode{false};
    inline std::mutex Logger::s_logMutex{};
    
    // Debug build macros - full functionality
    #define FORGE_CRITICAL(system, msg) Forge::Logger::Log(Forge::LogLevel::CRITICAL, system, std::string(msg))
    #define FORGE_ERROR(system, msg) Forge::Logger::Log(Forge::LogLevel::ERROR, system, std::string(msg))
    #define FORGE_WARN(system, msg) Forge::Logger::Log(Forge::LogLevel::WARNING, system, std::string(msg))
    #define FORGE_INFO(system, msg) Forge::Logger::Log(Forge::LogLevel::INFO, system, std::string(msg))
    #define FORGE_DEBUG(system, msg) Forge::Logger::Log(Forge::LogLevel::DEBUG_LEVEL, system, std::string(msg))

#else
    // Release builds - ultra-minimal overhead, lockless
    class Logger {
    private:
        static std::atomic<bool> s_benchmarkMode;
        
    public:
        static std::mutex s_logMutex;  // Public for macro access
        
        static void SetBenchmarkMode(bool enabled) {
            s_benchmarkMode.store(enabled, std::memory_order_relaxed);
        }
        
        static bool IsBenchmarkMode() {
            return s_benchmarkMode.load(std::memory_order_relaxed);
        }
    };
    
    // Initialize static members
    inline std::atomic<bool> Logger::s_benchmarkMode{false};
    inline std::mutex Logger::s_logMutex{};
    
    #define FORGE_CRITICAL(system, msg) do { \
        if (!Forge::Logger::IsBenchmarkMode()) { \
            std::lock_guard<std::mutex> lock(Forge::Logger::s_logMutex); \
            printf("Forge Game Engine - [%s] CRITICAL: %s\n", system, std::string(msg).c_str()); \
            fflush(stdout); \
        } \
    } while(0)
    
    #define FORGE_ERROR(system, msg) do { \
        if (!Forge::Logger::IsBenchmarkMode()) { \
            std::lock_guard<std::mutex> lock(Forge::Logger::s_logMutex); \
            printf("Forge Game Engine - [%s] ERROR: %s\n", system, std::string(msg).c_str()); \
            fflush(stdout); \
        } \
    } while(0)
    
    #define FORGE_WARN(system, msg) ((void)0)       // Zero overhead
    #define FORGE_INFO(system, msg) ((void)0)       // Zero overhead
    #define FORGE_DEBUG(system, msg) ((void)0)      // Zero overhead
#endif

    // Convenience macros for each manager and core system
    
    // Core Systems
    #define GAMELOOP_CRITICAL(msg) FORGE_CRITICAL("GameLoop", msg)
    #define GAMELOOP_ERROR(msg) FORGE_ERROR("GameLoop", msg)
    #define GAMELOOP_WARN(msg) FORGE_WARN("GameLoop", msg)
    #define GAMELOOP_INFO(msg) FORGE_INFO("GameLoop", msg)
    #define GAMELOOP_DEBUG(msg) FORGE_DEBUG("GameLoop", msg)
    
    #define GAMEENGINE_CRITICAL(msg) FORGE_CRITICAL("GameEngine", msg)
    #define GAMEENGINE_ERROR(msg) FORGE_ERROR("GameEngine", msg)
    #define GAMEENGINE_WARN(msg) FORGE_WARN("GameEngine", msg)
    #define GAMEENGINE_INFO(msg) FORGE_INFO("GameEngine", msg)
    #define GAMEENGINE_DEBUG(msg) FORGE_DEBUG("GameEngine", msg)
    
    #define THREADSYSTEM_CRITICAL(msg) FORGE_CRITICAL("ThreadSystem", msg)
    #define THREADSYSTEM_ERROR(msg) FORGE_ERROR("ThreadSystem", msg)
    #define THREADSYSTEM_WARN(msg) FORGE_WARN("ThreadSystem", msg)
    #define THREADSYSTEM_INFO(msg) FORGE_INFO("ThreadSystem", msg)
    #define THREADSYSTEM_DEBUG(msg) FORGE_DEBUG("ThreadSystem", msg)
    
    // Manager Systems
    #define TEXTURE_CRITICAL(msg) FORGE_CRITICAL("TextureManager", msg)
    #define TEXTURE_ERROR(msg) FORGE_ERROR("TextureManager", msg)
    #define TEXTURE_WARN(msg) FORGE_WARN("TextureManager", msg)
    #define TEXTURE_INFO(msg) FORGE_INFO("TextureManager", msg)
    #define TEXTURE_DEBUG(msg) FORGE_DEBUG("TextureManager", msg)
    
    #define SOUND_CRITICAL(msg) FORGE_CRITICAL("SoundManager", msg)
    #define SOUND_ERROR(msg) FORGE_ERROR("SoundManager", msg)
    #define SOUND_WARN(msg) FORGE_WARN("SoundManager", msg)
    #define SOUND_INFO(msg) FORGE_INFO("SoundManager", msg)
    #define SOUND_DEBUG(msg) FORGE_DEBUG("SoundManager", msg)
    
    #define FONT_CRITICAL(msg) FORGE_CRITICAL("FontManager", msg)
    #define FONT_ERROR(msg) FORGE_ERROR("FontManager", msg)
    #define FONT_WARN(msg) FORGE_WARN("FontManager", msg)
    #define FONT_INFO(msg) FORGE_INFO("FontManager", msg)
    #define FONT_DEBUG(msg) FORGE_DEBUG("FontManager", msg)
    
    #define AI_CRITICAL(msg) FORGE_CRITICAL("AIManager", msg)
    #define AI_ERROR(msg) FORGE_ERROR("AIManager", msg)
    #define AI_WARN(msg) FORGE_WARN("AIManager", msg)
    #define AI_INFO(msg) FORGE_INFO("AIManager", msg)
    #define AI_DEBUG(msg) FORGE_DEBUG("AIManager", msg)
    
    #define EVENT_CRITICAL(msg) FORGE_CRITICAL("EventManager", msg)
    #define EVENT_ERROR(msg) FORGE_ERROR("EventManager", msg)
    #define EVENT_WARN(msg) FORGE_WARN("EventManager", msg)
    #define EVENT_INFO(msg) FORGE_INFO("EventManager", msg)
    #define EVENT_DEBUG(msg) FORGE_DEBUG("EventManager", msg)
    
    #define INPUT_CRITICAL(msg) FORGE_CRITICAL("InputManager", msg)
    #define INPUT_ERROR(msg) FORGE_ERROR("InputManager", msg)
    #define INPUT_WARN(msg) FORGE_WARN("InputManager", msg)
    #define INPUT_INFO(msg) FORGE_INFO("InputManager", msg)
    #define INPUT_DEBUG(msg) FORGE_DEBUG("InputManager", msg)
    
    #define UI_CRITICAL(msg) FORGE_CRITICAL("UIManager", msg)
    #define UI_ERROR(msg) FORGE_ERROR("UIManager", msg)
    #define UI_WARN(msg) FORGE_WARN("UIManager", msg)
    #define UI_INFO(msg) FORGE_INFO("UIManager", msg)
    #define UI_DEBUG(msg) FORGE_DEBUG("UIManager", msg)
    
    #define SAVEGAME_CRITICAL(msg) FORGE_CRITICAL("SaveGameManager", msg)
    #define SAVEGAME_ERROR(msg) FORGE_ERROR("SaveGameManager", msg)
    #define SAVEGAME_WARN(msg) FORGE_WARN("SaveGameManager", msg)
    #define SAVEGAME_INFO(msg) FORGE_INFO("SaveGameManager", msg)
    #define SAVEGAME_DEBUG(msg) FORGE_DEBUG("SaveGameManager", msg)
    
    // Entity and State Systems
    #define GAMESTATE_CRITICAL(msg) FORGE_CRITICAL("GameStateManager", msg)
    #define GAMESTATE_ERROR(msg) FORGE_ERROR("GameStateManager", msg)
    #define GAMESTATE_WARN(msg) FORGE_WARN("GameStateManager", msg)
    #define GAMESTATE_INFO(msg) FORGE_INFO("GameStateManager", msg)
    #define GAMESTATE_DEBUG(msg) FORGE_DEBUG("GameStateManager", msg)
    
    #define ENTITYSTATE_CRITICAL(msg) FORGE_CRITICAL("EntityStateManager", msg)
    #define ENTITYSTATE_ERROR(msg) FORGE_ERROR("EntityStateManager", msg)
    #define ENTITYSTATE_WARN(msg) FORGE_WARN("EntityStateManager", msg)
    #define ENTITYSTATE_INFO(msg) FORGE_INFO("EntityStateManager", msg)
    #define ENTITYSTATE_DEBUG(msg) FORGE_DEBUG("EntityStateManager", msg)
    
    // Entity Systems
    #define ENTITY_CRITICAL(msg) FORGE_CRITICAL("Entity", msg)
    #define ENTITY_ERROR(msg) FORGE_ERROR("Entity", msg)
    #define ENTITY_WARN(msg) FORGE_WARN("Entity", msg)
    #define ENTITY_INFO(msg) FORGE_INFO("Entity", msg)
    #define ENTITY_DEBUG(msg) FORGE_DEBUG("Entity", msg)
    
    #define PLAYER_CRITICAL(msg) FORGE_CRITICAL("Player", msg)
    #define PLAYER_ERROR(msg) FORGE_ERROR("Player", msg)
    #define PLAYER_WARN(msg) FORGE_WARN("Player", msg)
    #define PLAYER_INFO(msg) FORGE_INFO("Player", msg)
    #define PLAYER_DEBUG(msg) FORGE_DEBUG("Player", msg)
    
    #define NPC_CRITICAL(msg) FORGE_CRITICAL("NPC", msg)
    #define NPC_ERROR(msg) FORGE_ERROR("NPC", msg)
    #define NPC_WARN(msg) FORGE_WARN("NPC", msg)
    #define NPC_INFO(msg) FORGE_INFO("NPC", msg)
    #define NPC_DEBUG(msg) FORGE_DEBUG("NPC", msg)
    
    // Benchmark mode convenience macros
    #define FORGE_ENABLE_BENCHMARK_MODE() Forge::Logger::SetBenchmarkMode(true)
    #define FORGE_DISABLE_BENCHMARK_MODE() Forge::Logger::SetBenchmarkMode(false)

} // namespace Forge

#endif // LOGGER_HPP