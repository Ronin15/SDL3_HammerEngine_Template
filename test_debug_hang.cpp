#include <iostream>
#include <chrono>
#include <thread>
#include "managers/WorldManager.hpp"
#include "managers/EventManager.hpp"
#include "world/WorldData.hpp"

using namespace HammerEngine;

int main() {
    std::cout << "=== Debug WorldManager Hanging Issue ===" << std::endl;
    
    // Initialize managers
    std::cout << "Initializing WorldManager..." << std::endl;
    bool worldInit = WorldManager::Instance().init();
    std::cout << "WorldManager init result: " << worldInit << std::endl;
    
    std::cout << "Initializing EventManager..." << std::endl;
    bool eventInit = EventManager::Instance().init();
    std::cout << "EventManager init result: " << eventInit << std::endl;
    
    if (!worldInit || !eventInit) {
        std::cout << "Initialization failed!" << std::endl;
        return 1;
    }
    
    // Create a minimal world config
    WorldGenerationConfig config{};
    config.width = 3;
    config.height = 3;
    config.seed = 12345;
    config.elevationFrequency = 0.1f;
    config.humidityFrequency = 0.1f;
    config.waterLevel = 0.3f;
    config.mountainLevel = 0.7f;
    
    std::cout << "Starting world generation (3x3)..." << std::endl;
    
    // Run in separate thread with timeout
    std::atomic<bool> completed{false};
    std::atomic<bool> success{false};
    
    std::thread worldGenThread([&]() {
        try {
            success.store(WorldManager::Instance().loadNewWorld(config));
            completed.store(true);
            std::cout << "World generation completed in thread" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Exception in world generation: " << e.what() << std::endl;
            completed.store(true);
        }
    });
    
    // Wait for completion or timeout
    auto start = std::chrono::steady_clock::now();
    while (!completed.load() && 
           std::chrono::steady_clock::now() - start < std::chrono::seconds(15)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "." << std::flush;
    }
    
    if (completed.load()) {
        std::cout << "\nWorld generation result: " << success.load() << std::endl;
        worldGenThread.join();
    } else {
        std::cout << "\nTIMEOUT: World generation is hanging!" << std::endl;
        std::cout << "This indicates a deadlock or infinite loop." << std::endl;
        worldGenThread.detach(); // Don't wait for hanging thread
    }
    
    // Clean up
    std::cout << "Cleaning up..." << std::endl;
    WorldManager::Instance().clean();
    EventManager::Instance().clean();
    
    return 0;
}