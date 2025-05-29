/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/EventDemoState.hpp"
#include "SDL3/SDL_scancode.h"
#include "core/GameEngine.hpp"
#include "managers/InputManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/AIManager.hpp"
#include "managers/EventManager.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "ai/behaviors/WanderBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/ChaseBehavior.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>


EventDemoState::EventDemoState() {
    // EventManager accessed via singleton - no initialization needed
}

EventDemoState::~EventDemoState() {
    // Cleanup will be handled in exit()
}

bool EventDemoState::enter() {
    std::cout << "Forge Game Engine - Entering EventDemoState...\n";

    try {
        // Setup window dimensions
        m_worldWidth = GameEngine::Instance().getWindowWidth();
        m_worldHeight = GameEngine::Instance().getWindowHeight();

        // Initialize event system
        setupEventSystem();

        // Create player
        m_player = std::make_shared<Player>();
        if (!m_player) {
            std::cerr << "Forge Game Engine - ERROR: Failed to create player!\n";
            return false;
        }
        m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

        // Initialize timing
        m_lastFrameTime = std::chrono::steady_clock::now();
        m_demoLastTime = std::chrono::steady_clock::now();
        m_demoStartTime = std::chrono::steady_clock::now();
        m_frameTimes.clear();
        m_frameCount = 0;
        m_currentFPS = 0.0f;
        m_averageFPS = 0.0f;

        // Setup initial demo state
        m_currentPhase = DemoPhase::Initialization;
        m_phaseTimer = 0.0f;
        m_totalDemoTime = 0.0f;
        m_lastEventTriggerTime = -1.0f; // Allow immediate key presses
        m_limitMessageShown = false;
        m_weatherChangesShown = 0;
        m_weatherDemoComplete = false;

        // Setup AI behaviors for integration demo
        setupAIBehaviors();

        // Create test events
        createTestEvents();

        // Setup instructions
        updateInstructions();

        // Add initial log entry
        addLogEntry("Event Demo System Initialized");

        std::cout << "Forge Game Engine - EventDemoState initialized successfully\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - ERROR: Exception in EventDemoState::enter(): " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "Forge Game Engine - ERROR: Unknown exception in EventDemoState::enter()" << std::endl;
        return false;
    }
}

bool EventDemoState::exit() {
    std::cout << "Forge Game Engine - Exiting EventDemoState...\n";

    try {
        // Cleanup spawned NPCs
        cleanupSpawnedNPCs();

        // Note: NPCs created by events will be cleaned up automatically
        // when the event system processes the next update cycle

        // Reset player
        m_player.reset();

        // Clear event log
        m_eventLog.clear();
        m_eventStates.clear();

        // Reset demo state
        m_currentPhase = DemoPhase::Initialization;
        m_phaseTimer = 0.0f;

        std::cout << "Forge Game Engine - EventDemoState cleanup complete\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - ERROR: Exception in EventDemoState::exit(): " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "Forge Game Engine - ERROR: Unknown exception in EventDemoState::exit()" << std::endl;
        return false;
    }
}

void EventDemoState::update() {
    // Update timing
    updateDemoTimer();
    updateFrameRate();

    // Handle input
    handleInput();

    // Update player
    if (m_player) {
        m_player->update();
    }

    // Update spawned NPCs (remove any that are no longer valid)
    auto it = m_spawnedNPCs.begin();
    while (it != m_spawnedNPCs.end()) {
        if (*it) { // Check if NPC is valid
            try {
                (*it)->update();
                ++it;
            } catch (...) {
                // If update fails, remove the NPC safely
                try {
                    if (AIManager::Instance().entityHasBehavior(*it)) {
                        AIManager::Instance().unassignBehaviorFromEntity(*it);
                    }
                } catch (...) {
                    // Ignore errors during cleanup
                }
                it = m_spawnedNPCs.erase(it);
            }
        } else {
            // Remove dead/invalid NPCs
            it = m_spawnedNPCs.erase(it);
        }
    }
    // Update event system -- Moved to GameEngine processBackgroudTasks

    // Handle demo phases (separate from event rate limiting)
    if (m_autoMode) {
        // Auto mode processing (no artificial limits)

        switch (m_currentPhase) {
            case DemoPhase::Initialization:
                if (m_phaseTimer >= 2.0f) {
                    m_currentPhase = DemoPhase::WeatherDemo;
                    m_phaseTimer = 0.0f;
                    // Always trigger first weather demo immediately
                    triggerWeatherDemoAuto(); // Use auto version for phase progression
                    m_lastEventTriggerTime = m_totalDemoTime;
                    m_weatherChangesShown = 1; // Count the first weather change
                    addLogEntry("Starting weather demo - changes shown: 1/" + std::to_string(m_weatherSequence.size()));
                }
                break;

            case DemoPhase::WeatherDemo:
                // Trigger weather changes at regular intervals (auto mode only)
                if (!m_weatherDemoComplete && (m_totalDemoTime - m_lastEventTriggerTime) >= m_weatherChangeInterval) {
                    // Ensure we don't exceed the weather sequence length
                    if (m_weatherChangesShown < m_weatherSequence.size()) {
                        triggerWeatherDemoAuto(); // Use separate method for auto progression
                        m_lastEventTriggerTime = m_totalDemoTime;
                        m_weatherChangesShown++;
                        m_phaseTimer = 0.0f; // Reset timer for each weather change to show progress

                        // Debug logging
                        addLogEntry("Weather changes shown: " + std::to_string(m_weatherChangesShown) + "/" + std::to_string(m_weatherSequence.size()));

                        // Check if we've shown all weather types
                        if (m_weatherChangesShown >= m_weatherSequence.size()) {
                            m_weatherDemoComplete = true;
                            addLogEntry("Weather demo complete - All weather types shown!");
                        }
                    } else {
                        // Safety: Mark complete if counter somehow exceeded
                        m_weatherDemoComplete = true;
                        addLogEntry("Weather demo force completed - counter exceeded limit");
                    }
                }
                // Only advance after showing all weather types and some time has passed
                if (m_weatherDemoComplete && m_phaseTimer >= 2.0f) {
                    m_currentPhase = DemoPhase::NPCSpawnDemo;
                    m_phaseTimer = 0.0f;
                    addLogEntry("Advancing to NPC Spawn Demo Phase");
                }
                break;

            case DemoPhase::NPCSpawnDemo:
                // Spawn NPCs at regular intervals but limit total spawns in this phase
                if ((m_totalDemoTime - m_lastEventTriggerTime) >= m_eventFireInterval &&
                    m_spawnedNPCs.size() < 5000) { // Limit to 5000 NPCs in this phase
                    triggerNPCSpawnDemo();
                    m_lastEventTriggerTime = m_totalDemoTime;
                }
                if (m_phaseTimer >= m_phaseDuration) {
                    m_currentPhase = DemoPhase::SceneTransitionDemo;
                    m_phaseTimer = 0.0f;
                    addLogEntry("NPC spawn demo complete - Starting Scene Transition Demo Phase");
                }
                break;

            case DemoPhase::SceneTransitionDemo:
                if (m_phaseTimer >= 3.0f &&
                    (m_totalDemoTime - m_lastEventTriggerTime) >= m_eventFireInterval) {
                    triggerSceneTransitionDemo();
                    m_lastEventTriggerTime = m_totalDemoTime;
                }
                if (m_phaseTimer >= m_phaseDuration) {
                    m_currentPhase = DemoPhase::CustomEventDemo;
                    m_phaseTimer = 0.0f;
                }
                break;

            case DemoPhase::CustomEventDemo:
                // Only trigger custom event demo a few times, not continuously
                if (m_phaseTimer >= 3.0f &&
                    (m_totalDemoTime - m_lastEventTriggerTime) >= m_eventFireInterval &&
                    m_spawnedNPCs.size() < 5000) { // Limit to 5000 total NPCs
                    triggerCustomEventDemo();
                    m_lastEventTriggerTime = m_totalDemoTime;
                }
                if (m_phaseTimer >= m_phaseDuration) {
                    m_currentPhase = DemoPhase::InteractiveMode;
                    m_phaseTimer = 0.0f;
                    addLogEntry("Entering Interactive Mode - Use keys 1-5 to test events");
                }
                break;

            case DemoPhase::InteractiveMode:
                // Stay in interactive mode indefinitely
                // Stop the phase timer since this mode doesn't have a duration
                m_phaseTimer = 0.0f;
                break;

            case DemoPhase::Complete:
                // Demo complete
                break;
        }
    }

    // Update instructions
    updateInstructions();

    // Note: Pending behavior assignments are now processed globally by GameEngine
    // via AIManager::processPendingBehaviorAssignments() in processBackgroundTasks()
}

std::shared_ptr<NPC> EventDemoState::createNPCAtPositionWithoutBehavior(const std::string& npcType, float x, float y) {

    try {
        // Get the texture ID for this NPC type
        std::string textureID;
        if (npcType == "Guard") {
            textureID = "guard";
        } else if (npcType == "Villager") {
            textureID = "villager";
        } else if (npcType == "Merchant") {
            textureID = "merchant";
        } else if (npcType == "Warrior") {
            textureID = "warrior";
        } else {
            textureID = "npc";
        }

        Vector2D position(x, y);
        auto npc = std::make_shared<NPC>(textureID, position, 64, 64);

        if (npc) {
            npc->setWanderArea(0.0f, 0.0f, m_worldWidth, m_worldHeight);
            npc->setBoundsCheckEnabled(false);

            // Add to collection
            m_spawnedNPCs.push_back(npc);


            return npc;
        } else {
            std::cerr << "ERROR: Failed to create NPC object of type: " << npcType << std::endl;
            return nullptr;
        }
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION in createNPCAtPositionWithoutBehavior: " << e.what() << std::endl;
        return nullptr;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION in createNPCAtPositionWithoutBehavior" << std::endl;
        return nullptr;
    }
}

std::string EventDemoState::determineBehaviorForNPCType(const std::string& npcType) {
    std::string behaviorName;

    if (npcType == "Guard") {
        size_t npcCount = m_spawnedNPCs.size();
        std::vector<std::string> guardBehaviors = {"Patrol", "Wander", "Chase"};
        behaviorName = guardBehaviors[npcCount % guardBehaviors.size()];
    } else if (npcType == "Villager") {
        size_t npcCount = m_spawnedNPCs.size();
        std::vector<std::string> villagerBehaviors = {"Wander", "Patrol"};
        behaviorName = villagerBehaviors[npcCount % villagerBehaviors.size()];
    } else {
        behaviorName = "Wander";
    }

    return behaviorName;
}



void EventDemoState::render() {
    // Render player
    if (m_player) {
        m_player->render();
    }

    // Render spawned NPCs
    for (auto& npc : m_spawnedNPCs) {
        if (npc) {
            npc->render();
        }
    }

    // Render UI
    renderUI();
}

void EventDemoState::setupEventSystem() {
    std::cout << "Forge Game Engine - EventDemoState: EventManager instance obtained\n";
    addLogEntry("EventManager singleton obtained");

    // The EventManager should already be initialized by GameEngine
    // But we'll verify and initialize if needed
    if (!EventManager::Instance().init()) {
        std::cerr << "Forge Game Engine - ERROR: Failed to initialize EventManager!\n";
        addLogEntry("ERROR: EventManager initialization failed");
        return;
    }

    std::cout << "Forge Game Engine - EventDemoState: EventManager initialized successfully\n";
    addLogEntry("EventManager initialized");

    // Register event handlers
    EventManager::Instance().registerEventHandler("Weather",
        [this](const std::string& message) { onWeatherChanged(message); });

    EventManager::Instance().registerEventHandler("NPCSpawn",
        [this](const std::string& message) { onNPCSpawned(message); });

    EventManager::Instance().registerEventHandler("SceneChange",
        [this](const std::string& message) { onSceneChanged(message); });

    std::cout << "Forge Game Engine - EventDemoState: Event handlers registered\n";
    addLogEntry("Event System Setup Complete - All handlers registered");
}

void EventDemoState::createTestEvents() {
    // Register weather events
    EventManager::Instance().registerWeatherEvent("demo_clear", "Clear", 1.0f);
    EventManager::Instance().registerWeatherEvent("demo_rainy", "Rainy", 0.8f);
    EventManager::Instance().registerWeatherEvent("demo_stormy", "Stormy", 1.0f);

    // Register NPC spawn events with limits to prevent runaway spawning
    EventManager::Instance().registerNPCSpawnEvent("demo_guard_spawn", "Guard", 1, 25.0f);
    EventManager::Instance().registerNPCSpawnEvent("demo_villager_spawn", "Villager", 1, 25.0f);
    EventManager::Instance().registerNPCSpawnEvent("demo_merchant_spawn", "Merchant", 1, 25.0f);
    EventManager::Instance().registerNPCSpawnEvent("demo_warrior_spawn", "Warrior", 1, 25.0f);

    // Register scene change events
    EventManager::Instance().registerSceneChangeEvent("demo_forest", "Forest", "fade");
    EventManager::Instance().registerSceneChangeEvent("demo_village", "Village", "dissolve");

    addLogEntry("Test Events Created - Ready for manual triggering");
}

void EventDemoState::handleInput() {
    // Store previous input state
    m_lastInput = m_input;

    // Get current input state
    m_input.space = InputManager::Instance().isKeyDown(SDL_SCANCODE_SPACE);
    m_input.enter = InputManager::Instance().isKeyDown(SDL_SCANCODE_RETURN);
    m_input.tab = InputManager::Instance().isKeyDown(SDL_SCANCODE_TAB);
    m_input.num1 = InputManager::Instance().isKeyDown(SDL_SCANCODE_1);
    m_input.num2 = InputManager::Instance().isKeyDown(SDL_SCANCODE_2);
    m_input.num3 = InputManager::Instance().isKeyDown(SDL_SCANCODE_3);
    m_input.num4 = InputManager::Instance().isKeyDown(SDL_SCANCODE_4);
    m_input.num5 = InputManager::Instance().isKeyDown(SDL_SCANCODE_5);
    m_input.escape = InputManager::Instance().isKeyDown(SDL_SCANCODE_B);
    m_input.r = InputManager::Instance().isKeyDown(SDL_SCANCODE_R);
    m_input.a = InputManager::Instance().isKeyDown(SDL_SCANCODE_A);

    // Handle key presses (only on press, not hold)
    if (isKeyPressed(m_input.space, m_lastInput.space)) {
        // Advance to next phase manually
        switch (m_currentPhase) {
            case DemoPhase::Initialization:
                m_currentPhase = DemoPhase::WeatherDemo;
                triggerWeatherDemo();
                break;
            case DemoPhase::WeatherDemo:
                m_currentPhase = DemoPhase::NPCSpawnDemo;
                triggerNPCSpawnDemo();
                break;
            case DemoPhase::NPCSpawnDemo:
                m_currentPhase = DemoPhase::SceneTransitionDemo;
                triggerSceneTransitionDemo();
                break;
            case DemoPhase::SceneTransitionDemo:
                m_currentPhase = DemoPhase::CustomEventDemo;
                triggerCustomEventDemo();
                break;
            case DemoPhase::CustomEventDemo:
                m_currentPhase = DemoPhase::InteractiveMode;
                break;
            default:
                break;
        }
        m_phaseTimer = 0.0f;
    }

    if (isKeyPressed(m_input.num1, m_lastInput.num1) &&
        (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
        // Reset phase timer if we're in auto mode weather phase
        if (m_autoMode && m_currentPhase == DemoPhase::WeatherDemo) {
            m_phaseTimer = 0.0f;
        }
        triggerWeatherDemoManual(); // Use manual version that doesn't affect phase counter
        addLogEntry("Manual weather event triggered");
        m_lastEventTriggerTime = m_totalDemoTime;
    }

    if (isKeyPressed(m_input.num2, m_lastInput.num2) &&
        (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f &&
        m_spawnedNPCs.size() < 5000) { // Limit manual spawning to 5000 NPCs total
        // Reset phase timer if we're in auto mode NPC spawn phase
        if (m_autoMode && m_currentPhase == DemoPhase::NPCSpawnDemo) {
            m_phaseTimer = 0.0f;
        }
        triggerNPCSpawnDemo();
        addLogEntry("Manual NPC spawn event triggered");
        m_lastEventTriggerTime = m_totalDemoTime;
    }

    if (isKeyPressed(m_input.num3, m_lastInput.num3) &&
        (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
        // Reset phase timer if we're in auto mode scene transition phase
        if (m_autoMode && m_currentPhase == DemoPhase::SceneTransitionDemo) {
            m_phaseTimer = 0.0f;
        }
        triggerSceneTransitionDemo();
        addLogEntry("Manual scene transition event triggered");
        m_lastEventTriggerTime = m_totalDemoTime;
    }

    if (isKeyPressed(m_input.num4, m_lastInput.num4) &&
        (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f &&
        m_spawnedNPCs.size() < 5000) { // Limit manual custom events to 5000 NPCs total
        // Prevent conflicts with auto mode phase progression
        if (m_autoMode && m_currentPhase == DemoPhase::CustomEventDemo) {
            m_phaseTimer = 0.0f; // Reset phase timer to prevent auto conflicts
        }

        triggerCustomEventDemo();
        addLogEntry("Manual custom event triggered");
        m_lastEventTriggerTime = m_totalDemoTime;
    }

    if (isKeyPressed(m_input.num5, m_lastInput.num5)) {
        resetAllEvents();
        addLogEntry("All events reset");
    }

    if (isKeyPressed(m_input.r, m_lastInput.r)) {
        resetAllEvents();
        m_currentPhase = DemoPhase::Initialization;
        m_phaseTimer = 0.0f;
        m_totalDemoTime = 0.0f;
        m_lastEventTriggerTime = 0.0f;
        m_limitMessageShown = false;
        m_weatherChangesShown = 0;
        m_weatherDemoComplete = false;
        addLogEntry("Demo reset to beginning");
    }

    if (isKeyPressed(m_input.a, m_lastInput.a)) {
        m_autoMode = !m_autoMode;
        addLogEntry(m_autoMode ? "Auto mode enabled" : "Auto mode disabled");
    }

    if (isKeyPressed(m_input.escape, m_lastInput.escape)) {
        GameEngine::Instance().getGameStateManager()->setState("MainMenuState");
    }
}

void EventDemoState::updateDemoTimer() {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - m_demoLastTime);
    float deltaTime = duration.count() / 1000000.0f; // Convert to seconds

    // Only update phase timer if auto mode is enabled, otherwise it just counts indefinitely
    if (m_autoMode) {
        m_phaseTimer += deltaTime;
    }
    m_totalDemoTime += deltaTime;

    m_demoLastTime = now;
}

void EventDemoState::updateFrameRate() {
    // Calculate time since last frame
    auto currentTime = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float, std::chrono::milliseconds::period>(currentTime - m_lastFrameTime).count();
    m_lastFrameTime = currentTime;

    // Convert to seconds for FPS calculation
    float deltaTimeSeconds = deltaTime / 1000.0f;

    // Skip extreme values that might be from debugging pauses
    if (deltaTimeSeconds > 0.0f && deltaTimeSeconds < 1.0f) {
        // Calculate current FPS
        m_currentFPS = 1.0f / deltaTimeSeconds;

        // Add to rolling average
        m_frameTimes.push_back(m_currentFPS);

        // Keep only the last MAX_FRAME_SAMPLES frames
        if (m_frameTimes.size() > MAX_FRAME_SAMPLES) {
            m_frameTimes.pop_front();
        }

        // Calculate average FPS
        float sum = 0.0f;
        for (float fps : m_frameTimes) {
            sum += fps;
        }
        m_averageFPS = sum / m_frameTimes.size();
    }

    m_frameCount++;
}

void EventDemoState::renderUI() {
    SDL_Color whiteColor = {255, 255, 255, 255};
    SDL_Color yellowColor = {255, 255, 0, 255};
    SDL_Color greenColor = {0, 255, 0, 255};

    int windowWidth = GameEngine::Instance().getWindowWidth();
    int yPos = 20;
    int lineHeight = 25;

    // Title
    FontManager::Instance().drawText(
        "=== EVENT DEMO STATE ===",
        "fonts_Arial",
        windowWidth / 2,
        yPos,
        yellowColor,
        GameEngine::Instance().getRenderer());
    yPos += lineHeight * 2;

    // Phase information
    std::stringstream phaseInfo;
    if (m_currentPhase == DemoPhase::InteractiveMode) {
        phaseInfo << "Phase: " << getCurrentPhaseString() << " (Manual Control)";
    } else if (m_currentPhase == DemoPhase::WeatherDemo) {
        phaseInfo << "Phase: " << getCurrentPhaseString() << " ("
                  << m_weatherChangesShown << " / " << m_weatherSequence.size() << " weather types, "
                  << std::fixed << std::setprecision(1) << m_phaseTimer << "s / "
                  << m_weatherChangeInterval << "s)";
    } else {
        phaseInfo << "Phase: " << getCurrentPhaseString() << " ("
                  << std::fixed << std::setprecision(1) << m_phaseTimer << "s / "
                  << m_phaseDuration << "s)";
    }
    FontManager::Instance().drawText(
        phaseInfo.str(),
        "fonts_Arial",
        windowWidth / 2,
        yPos,
        whiteColor,
        GameEngine::Instance().getRenderer());
    yPos += lineHeight;

    // Auto mode status
    std::string autoModeText = "Auto Mode: " + std::string(m_autoMode ? "ON" : "OFF");
    FontManager::Instance().drawText(
        autoModeText,
        "fonts_Arial",
        windowWidth / 2,
        yPos,
        greenColor,
        GameEngine::Instance().getRenderer());
    yPos += lineHeight;

    // FPS information
    std::stringstream fpsInfo;
    fpsInfo << "FPS: " << std::fixed << std::setprecision(1) << m_currentFPS
            << " (Avg: " << m_averageFPS << ")";
    FontManager::Instance().drawText(
        fpsInfo.str(),
        "fonts_Arial",
        windowWidth / 2,
        yPos,
        yellowColor,
        GameEngine::Instance().getRenderer());
    yPos += lineHeight;

    // Weather and NPC info
    std::stringstream statusInfo;
    statusInfo << "Weather: " << getCurrentWeatherString()
               << " | Spawned NPCs: " << m_spawnedNPCs.size();
    FontManager::Instance().drawText(
        statusInfo.str(),
        "fonts_Arial",
        windowWidth / 2,
        yPos,
        whiteColor,
        GameEngine::Instance().getRenderer());
    yPos += lineHeight * 2;

    renderControls();
    renderEventStatus();
}

void EventDemoState::renderEventStatus() {
    SDL_Color cyanColor = {0, 255, 255, 255};
    SDL_Color whiteColor = {255, 255, 255, 255};

    int windowWidth = GameEngine::Instance().getWindowWidth();
    int windowHeight = GameEngine::Instance().getWindowHeight();
    int yPos = windowHeight - 250; // Start from bottom area with more space
    int lineHeight = 22;

    // Event log title
    FontManager::Instance().drawText(
        "=== EVENT LOG ===",
        "fonts_Arial",
        windowWidth / 2,
        yPos,
        cyanColor,
        GameEngine::Instance().getRenderer());
    yPos += lineHeight + 10;

    // Render last few log entries
    int maxEntries = 6; // Show last 6 entries to prevent overlap
    int startIndex = std::max(0, static_cast<int>(m_eventLog.size()) - maxEntries);

    for (int i = startIndex; i < static_cast<int>(m_eventLog.size()); ++i) {
        std::string logEntry = "• " + m_eventLog[i];
        FontManager::Instance().drawText(
            logEntry,
            "fonts_Arial",
            windowWidth / 2,
            yPos,
            whiteColor,
            GameEngine::Instance().getRenderer());
        yPos += lineHeight;
    }
}

void EventDemoState::renderControls() {
    SDL_Color cyanColor = {0, 255, 255, 255};
    SDL_Color whiteColor = {255, 255, 255, 255};

    int windowWidth = GameEngine::Instance().getWindowWidth();
    int yPos = 180; // Position below status info with more space
    int lineHeight = 22;

    // Controls title
    FontManager::Instance().drawText(
        "=== CONTROLS ===",
        "fonts_Arial",
        windowWidth / 2,
        yPos,
        cyanColor,
        GameEngine::Instance().getRenderer());
    yPos += lineHeight + 10;

    // Control instructions
    std::vector<std::string> controls = {
        "SPACE: Next phase | 1: Weather | 2: NPC spawn | 3: Scene change",
        "4: Custom event | 5: Reset | R: Restart | A: Auto toggle | B: Exit"
    };

    for (const auto& control : controls) {
        FontManager::Instance().drawText(
            control,
            "fonts_Arial",
            windowWidth / 2,
            yPos,
            whiteColor,
            GameEngine::Instance().getRenderer());
        yPos += lineHeight;
    }
}

void EventDemoState::triggerWeatherDemo() {
    // Manual weather trigger - doesn't affect auto demo progression
    triggerWeatherDemoManual();
}

void EventDemoState::triggerWeatherDemoAuto() {
    // Auto demo weather progression - affects phase counter
    size_t currentIndex = m_currentWeatherIndex;  // Store current index before incrementing
    WeatherType newWeather = m_weatherSequence[m_currentWeatherIndex];
    m_currentWeatherIndex = (m_currentWeatherIndex + 1) % m_weatherSequence.size();

    switch (newWeather) {
        case WeatherType::Clear:
            EventManager::Instance().triggerWeatherChange("Clear", m_weatherTransitionTime);
            break;
        case WeatherType::Cloudy:
            EventManager::Instance().triggerWeatherChange("Cloudy", m_weatherTransitionTime);
            break;
        case WeatherType::Rainy:
            EventManager::Instance().triggerWeatherChange("Rainy", m_weatherTransitionTime);
            break;
        case WeatherType::Stormy:
            EventManager::Instance().triggerWeatherChange("Stormy", m_weatherTransitionTime);
            break;
        case WeatherType::Foggy:
            EventManager::Instance().triggerWeatherChange("Foggy", m_weatherTransitionTime);
            break;
        case WeatherType::Snowy:
            EventManager::Instance().triggerWeatherChange("Snowy", m_weatherTransitionTime);
            break;
        case WeatherType::Windy:
            EventManager::Instance().triggerWeatherChange("Windy", m_weatherTransitionTime);
            break;
        case WeatherType::Custom:
            EventManager::Instance().triggerWeatherChange("Custom", m_weatherTransitionTime);
            break;
    }

    m_currentWeather = newWeather;
    addLogEntry("Weather changed to: " + getCurrentWeatherString() + " (Auto - Index: " + std::to_string(currentIndex) + ")");
}

void EventDemoState::triggerWeatherDemoManual() {
    // Manual weather trigger - cycles through weather types but doesn't affect auto demo phase progression
    static size_t manualWeatherIndex = 0;

    size_t currentIndex = manualWeatherIndex;  // Store current index before incrementing
    WeatherType newWeather = m_weatherSequence[manualWeatherIndex];
    manualWeatherIndex = (manualWeatherIndex + 1) % m_weatherSequence.size();

    switch (newWeather) {
        case WeatherType::Clear:
            EventManager::Instance().triggerWeatherChange("Clear", m_weatherTransitionTime);
            break;
        case WeatherType::Cloudy:
            EventManager::Instance().triggerWeatherChange("Cloudy", m_weatherTransitionTime);
            break;
        case WeatherType::Rainy:
            EventManager::Instance().triggerWeatherChange("Rainy", m_weatherTransitionTime);
            break;
        case WeatherType::Stormy:
            EventManager::Instance().triggerWeatherChange("Stormy", m_weatherTransitionTime);
            break;
        case WeatherType::Foggy:
            EventManager::Instance().triggerWeatherChange("Foggy", m_weatherTransitionTime);
            break;
        case WeatherType::Snowy:
            EventManager::Instance().triggerWeatherChange("Snowy", m_weatherTransitionTime);
            break;
        case WeatherType::Windy:
            EventManager::Instance().triggerWeatherChange("Windy", m_weatherTransitionTime);
            break;
        case WeatherType::Custom:
            EventManager::Instance().triggerWeatherChange("Custom", m_weatherTransitionTime);
            break;
    }

    m_currentWeather = newWeather;
    addLogEntry("Weather changed to: " + getCurrentWeatherString() + " (Manual - Index: " + std::to_string(currentIndex) + ")");
}

void EventDemoState::triggerNPCSpawnDemo() {
    std::string npcType = m_npcTypes[m_currentNPCTypeIndex];
    m_currentNPCTypeIndex = (m_currentNPCTypeIndex + 1) % m_npcTypes.size();

    // Calculate spawn position using existing NPC count for safety
    Vector2D playerPos = m_player->getPosition();

    size_t npcCount = m_spawnedNPCs.size();
    float offsetX = 200.0f + ((npcCount % 8) * 120.0f);  // Cycle every 8 NPCs
    float offsetY = 100.0f + ((npcCount % 5) * 80.0f);   // Cycle every 5 NPCs

    float spawnX = playerPos.getX() + offsetX;
    float spawnY = playerPos.getY() + offsetY;

    // Ensure spawn position is within window bounds
    spawnX = std::max(100.0f, std::min(spawnX, m_worldWidth - 100.0f));
    spawnY = std::max(100.0f, std::min(spawnY, m_worldHeight - 100.0f));

    // EventDemoState creates and owns the NPC directly
    createNPCAtPosition(npcType, spawnX, spawnY);

    addLogEntry("Created NPC: " + npcType + " at (" + std::to_string((int)spawnX) + ", " + std::to_string((int)spawnY) + ")");
}

void EventDemoState::triggerSceneTransitionDemo() {
    // Cycle through scene names
    std::string sceneName = m_sceneNames[m_currentSceneIndex];
    m_currentSceneIndex = (m_currentSceneIndex + 1) % m_sceneNames.size();

    EventManager::Instance().triggerSceneChange(sceneName, "fade", 2.0f);

    addLogEntry("Scene transition to: " + sceneName);
}

void EventDemoState::triggerCustomEventDemo() {
    // Demonstrate custom event handling
    addLogEntry("Custom event demo - showing event system flexibility");

    // Example: demonstrate custom event handling
    triggerWeatherDemoManual();
// Spawn NPCs without artificial limits
std::string npcType1 = m_npcTypes[m_currentNPCTypeIndex];
m_currentNPCTypeIndex = (m_currentNPCTypeIndex + 1) % m_npcTypes.size();

std::string npcType2 = m_npcTypes[m_currentNPCTypeIndex];
m_currentNPCTypeIndex = (m_currentNPCTypeIndex + 1) % m_npcTypes.size();

// Calculate spawn positions for custom event
Vector2D playerPos = m_player->getPosition();

// Use existing NPC count for safe, bounded offset calculation
size_t npcCount = m_spawnedNPCs.size();
float offsetX1 = 150.0f + ((npcCount % 10) * 80.0f);  // Cycle every 10 positions
float offsetY1 = 80.0f + ((npcCount % 6) * 50.0f);   // Cycle every 6 positions
float offsetX2 = 250.0f + (((npcCount + 1) % 10) * 80.0f);
float offsetY2 = 150.0f + (((npcCount + 1) % 6) * 50.0f);

float spawnX1 = std::max(100.0f, std::min(playerPos.getX() + offsetX1, m_worldWidth - 100.0f));
float spawnY1 = std::max(100.0f, std::min(playerPos.getY() + offsetY1, m_worldHeight - 100.0f));
float spawnX2 = std::max(100.0f, std::min(playerPos.getX() + offsetX2, m_worldWidth - 100.0f));
float spawnY2 = std::max(100.0f, std::min(playerPos.getY() + offsetY2, m_worldHeight - 100.0f));

// Create NPCs without AI behavior assignment first
auto npc1 = createNPCAtPositionWithoutBehavior(npcType1, spawnX1, spawnY1);
auto npc2 = createNPCAtPositionWithoutBehavior(npcType2, spawnX2, spawnY2);

// Queue behavior assignments for batch processing using global system
if (npc1) {
    std::string behaviorName1 = determineBehaviorForNPCType(npcType1);
    AIManager::Instance().queueBehaviorAssignment(npc1, behaviorName1);
    addLogEntry("Queued " + behaviorName1 + " behavior for " + npcType1 + " (global batch)");
}

if (npc2) {
    std::string behaviorName2 = determineBehaviorForNPCType(npcType2);
    AIManager::Instance().queueBehaviorAssignment(npc2, behaviorName2);
    addLogEntry("Queued " + behaviorName2 + " behavior for " + npcType2 + " (global batch)");
}

    addLogEntry("Multiple NPCs spawned: " + npcType1 + " and " + npcType2);
}

void EventDemoState::resetAllEvents() {
    // Clear spawned NPCs
    cleanupSpawnedNPCs();

    // Reset weather to clear
    EventManager::Instance().triggerWeatherChange("Clear", 1.0f);
    m_currentWeather = WeatherType::Clear;

    // Reset indices
    m_currentWeatherIndex = 0;
    m_currentNPCTypeIndex = 0;
    m_currentSceneIndex = 0;

    // Reset timing variables to allow auto mode to resume
    m_lastEventTriggerTime = 0.0f;
    m_limitMessageShown = false;

    addLogEntry("All events reset");
}

void EventDemoState::onWeatherChanged(const std::string& message) {
    addLogEntry("Weather Event: " + message);

    // Update current weather state for UI display
    if (message == "Clear") {
        m_currentWeather = WeatherType::Clear;
    } else if (message == "Cloudy") {
        m_currentWeather = WeatherType::Cloudy;
    } else if (message == "Rainy") {
        m_currentWeather = WeatherType::Rainy;
    } else if (message == "Stormy") {
        m_currentWeather = WeatherType::Stormy;
    } else if (message == "Foggy") {
        m_currentWeather = WeatherType::Foggy;
    } else if (message == "Snowy") {
        m_currentWeather = WeatherType::Snowy;
    } else if (message == "Windy") {
        m_currentWeather = WeatherType::Windy;
    } else if (message == "Custom") {
        m_currentWeather = WeatherType::Custom;
    }
}

void EventDemoState::onNPCSpawned(const std::string& message) {
    addLogEntry("NPC Spawn Event: " + message);

    // NPCSpawnEvent creates the NPC, but we need to track it for rendering and AI assignment
    std::string npcType = message;

    // Get recently spawned NPCs from the event system
    auto spawnEvents = EventManager::Instance().getEventsByType("NPCSpawn");

    for (auto event : spawnEvents) {
        auto npcSpawnEvent = std::dynamic_pointer_cast<NPCSpawnEvent>(event);
        if (npcSpawnEvent) {
            auto spawnedEntities = npcSpawnEvent->getSpawnedEntities();

            // Find the most recently spawned NPC that matches our type and add it to tracking
            for (auto it = spawnedEntities.rbegin(); it != spawnedEntities.rend(); ++it) {
                if (auto entityPtr = it->lock()) {
                    auto npc = std::dynamic_pointer_cast<NPC>(entityPtr);
                    if (npc) {
                        // Check if we already have this NPC tracked
                        bool alreadyTracked = false;
                        for (const auto& trackedNpc : m_spawnedNPCs) {
                            if (trackedNpc == npc) {
                                alreadyTracked = true;
                                break;
                            }
                        }

                        if (!alreadyTracked) {
                            // Assign AI behavior based on NPC type
                            assignAIBehaviorToNPC(npc, npcType);

                            // Add to our tracking list for rendering
                            m_spawnedNPCs.push_back(npc);

                            std::cout << "EventDemoState tracked and assigned AI to " << npcType << std::endl;
                            return; // Only track the most recent one
                        }
                    }
                }
            }
        }
    }
}

void EventDemoState::onSceneChanged(const std::string& message) {
    addLogEntry("Scene Change Event: " + message);
}

void EventDemoState::setupAIBehaviors() {
    std::cout << "EventDemoState: Setting up AI behaviors for NPC integration...\n";

    // Only set up if AIManager doesn't already have these behaviors
    if (!AIManager::Instance().hasBehavior("Wander")) {
        // Create and register wander behavior matching AIDemoState settings
        auto wanderBehavior = std::make_unique<WanderBehavior>(2.0f, 3000.0f, 200.0f);
        wanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        wanderBehavior->setOffscreenProbability(0.2f); // Match AIDemoState: 20% chance to wander offscreen
        AIManager::Instance().registerBehavior("Wander", std::move(wanderBehavior));
        std::cout << "EventDemoState: Registered Wander behavior\n";
    }

    if (!AIManager::Instance().hasBehavior("Patrol")) {
        // Create patrol points well within screen boundaries to avoid edge bouncing
        boost::container::small_vector<Vector2D, 10> patrolPoints;
        float margin = 100.0f; // Stay away from edges
        patrolPoints.push_back(Vector2D(margin, margin));
        patrolPoints.push_back(Vector2D(m_worldWidth - margin, margin));
        patrolPoints.push_back(Vector2D(m_worldWidth - margin, m_worldHeight - margin));
        patrolPoints.push_back(Vector2D(margin, m_worldHeight - margin));

        auto patrolBehavior = std::make_unique<PatrolBehavior>(patrolPoints, 1.5f, true);
        patrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        AIManager::Instance().registerBehavior("Patrol", std::move(patrolBehavior));
        std::cout << "EventDemoState: Registered Patrol behavior\n";
    }

    if (!AIManager::Instance().hasBehavior("Chase")) {
        // Create chase behavior matching AIDemoState settings
        auto chaseBehavior = std::make_unique<ChaseBehavior>(m_player, 2.0f, 500.0f, 50.0f);
        AIManager::Instance().registerBehavior("Chase", std::move(chaseBehavior));
        std::cout << "EventDemoState: Registered Chase behavior\n";
    }

    addLogEntry("AI Behaviors configured for NPC integration");
}

void EventDemoState::assignAIBehaviorToNPC(std::shared_ptr<NPC> npc, const std::string& npcType) {
    if (!npc) {
        std::cerr << "ERROR: assignAIBehaviorToNPC called with null NPC!" << std::endl;
        return;
    }

    std::string behaviorName;

    try {
        // Assign different default behaviors based on NPC type
        if (npcType == "Guard") {
            // Guards patrol by default, but cycle through behaviors
            // FIXED: Use NPC count instead of static variable to prevent infinite growth
            size_t npcCount = m_spawnedNPCs.size();
            std::vector<std::string> guardBehaviors = {"Patrol", "Wander", "Chase"};
            behaviorName = guardBehaviors[npcCount % guardBehaviors.size()];
        } else if (npcType == "Villager") {
            // Villagers wander by default, but also cycle
            // FIXED: Use NPC count instead of static variable to prevent infinite growth
            size_t npcCount = m_spawnedNPCs.size();
            std::vector<std::string> villagerBehaviors = {"Wander", "Patrol"};
            behaviorName = villagerBehaviors[npcCount % villagerBehaviors.size()];
        } else {
            // Default to wander for unknown types
            behaviorName = "Wander";
        }

        // Assign the behavior
        AIManager::Instance().assignBehaviorToEntity(npc, behaviorName);
        addLogEntry(npcType + " assigned " + behaviorName + " behavior");

    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION in assignAIBehaviorToNPC: " << e.what() << std::endl;
        std::cerr << "NPC type: " << npcType << ", behavior: " << behaviorName << std::endl;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION in assignAIBehaviorToNPC" << std::endl;
        std::cerr << "NPC type: " << npcType << ", behavior: " << behaviorName << std::endl;
    }
}



bool EventDemoState::isKeyPressed(bool current, bool previous) const {
    return current && !previous;
}

void EventDemoState::addLogEntry(const std::string& entry) {
    m_eventLog.push_back(entry);
    if (m_eventLog.size() > m_maxLogEntries) {
        m_eventLog.erase(m_eventLog.begin());
    }
}

std::string EventDemoState::getCurrentPhaseString() const {
    switch (m_currentPhase) {
        case DemoPhase::Initialization: return "Initialization";
        case DemoPhase::WeatherDemo: return "Weather Demo";
        case DemoPhase::NPCSpawnDemo: return "NPC Spawn Demo";
        case DemoPhase::SceneTransitionDemo: return "Scene Transition Demo";
        case DemoPhase::CustomEventDemo: return "Custom Event Demo";
        case DemoPhase::InteractiveMode: return "Interactive Mode";
        case DemoPhase::Complete: return "Complete";
        default: return "Unknown";
    }
}

std::string EventDemoState::getCurrentWeatherString() const {
    switch (m_currentWeather) {
        case WeatherType::Clear: return "Clear";
        case WeatherType::Cloudy: return "Cloudy";
        case WeatherType::Rainy: return "Rainy";
        case WeatherType::Stormy: return "Stormy";
        case WeatherType::Foggy: return "Foggy";
        case WeatherType::Snowy: return "Snowy";
        case WeatherType::Windy: return "Windy";
        case WeatherType::Custom: return "Custom";
        default: return "Unknown";
    }
}

void EventDemoState::updateInstructions() {
    m_instructions.clear();

    switch (m_currentPhase) {
        case DemoPhase::Initialization:
            m_instructions.push_back("Initializing event system...");
            m_instructions.push_back("Press SPACE to start weather demo");
            break;
        case DemoPhase::WeatherDemo:
            m_instructions.push_back("Demonstrating weather events");
            m_instructions.push_back("Watch the weather change over time");
            break;
        case DemoPhase::NPCSpawnDemo:
            m_instructions.push_back("Demonstrating NPC spawn events");
            m_instructions.push_back("NPCs will spawn around the player");
            break;
        case DemoPhase::SceneTransitionDemo:
            m_instructions.push_back("Demonstrating scene transition events");
            m_instructions.push_back("Scene changes will be logged");
            break;
        case DemoPhase::CustomEventDemo:
            m_instructions.push_back("Demonstrating custom event combinations");
            m_instructions.push_back("Multiple events triggered together");
            break;
        case DemoPhase::InteractiveMode:
            m_instructions.push_back("Interactive Mode - Manual Control (Permanent)");
            m_instructions.push_back("Use number keys 1-5 to trigger events");
            m_instructions.push_back("Press 'A' to toggle auto mode on/off");
            m_instructions.push_back("Press 'R' to reset all events");
            break;
        default:
            break;
    }
}

void EventDemoState::cleanupSpawnedNPCs() {
    // First, unassign all behaviors from AI Manager to prevent race conditions
    for (auto& npc : m_spawnedNPCs) {
        if (npc) {
            try {
                if (AIManager::Instance().entityHasBehavior(npc)) {
                    AIManager::Instance().unassignBehaviorFromEntity(npc);
                }
            } catch (...) {
                // Ignore errors during cleanup to prevent double-free issues
            }
        }
    }

    // Then clear the vector - this will trigger NPC destructors safely
    m_spawnedNPCs.clear();
    m_limitMessageShown = false; // Reset limit message flag when NPCs are cleaned
}

void EventDemoState::createNPCAtPosition(const std::string& npcType, float x, float y) {
    try {
        // Get the texture ID for this NPC type (match actual loaded texture names)
        std::string textureID;
        if (npcType == "Guard") {
            textureID = "guard";
        } else if (npcType == "Villager") {
            textureID = "villager";
        } else if (npcType == "Merchant") {
            textureID = "merchant";
        } else if (npcType == "Warrior") {
            textureID = "warrior";
        } else {
            textureID = "npc"; // Default fallback to working texture
        }

        // EventDemoState creates and owns the NPC directly like AIDemoState
        Vector2D position(x, y);
        auto npc = std::make_shared<NPC>(textureID, position, 64, 64);

        if (npc) {


            // Configure NPC properties
            npc->setWanderArea(0.0f, 0.0f, m_worldWidth, m_worldHeight);
            npc->setBoundsCheckEnabled(false); // Let AI behaviors handle movement

            // Determine behavior for this NPC type
            std::string behaviorName = determineBehaviorForNPCType(npcType);

            // Queue behavior assignment using global batching system (critical for stability)
            AIManager::Instance().queueBehaviorAssignment(npc, behaviorName);
            addLogEntry("Queued " + behaviorName + " behavior assignment (global batch)");

            // EventDemoState owns this NPC
            m_spawnedNPCs.push_back(npc);


        } else {
            std::cerr << "ERROR: Failed to create NPC object of type: " << npcType << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION in createNPCAtPosition: " << e.what() << std::endl;
        std::cerr << "NPC type: " << npcType << ", position: (" << x << ", " << y << ")" << std::endl;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION in createNPCAtPosition" << std::endl;
        std::cerr << "NPC type: " << npcType << ", position: (" << x << ", " << y << ")" << std::endl;
    }
}
