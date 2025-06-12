/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/EventDemoState.hpp"
#include "SDL3/SDL_scancode.h"
#include "core/GameEngine.hpp"
#include "managers/InputManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/AIManager.hpp"
#include "managers/EventManager.hpp"
#include "events/WeatherEvent.hpp"
#include "events/SceneChangeEvent.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "ai/behaviors/WanderBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/ChaseBehavior.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>


EventDemoState::EventDemoState() {
    // Initialize member variables that need explicit initialization
}

EventDemoState::~EventDemoState() {
    // Cleanup any resources if needed
    cleanupSpawnedNPCs();
}

bool EventDemoState::enter() {
    std::cout << "Forge Game Engine - Entering EventDemoState...\n";

    try {
        // Cache GameEngine reference for better performance
        const GameEngine& gameEngine = GameEngine::Instance();

        // Setup window dimensions
        m_worldWidth = gameEngine.getWindowWidth();
        m_worldHeight = gameEngine.getWindowHeight();

        // Initialize event system
        setupEventSystem();

        // Create player
        m_player = std::make_shared<Player>();
        m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

        // Cache AIManager reference for better performance
        AIManager& aiMgr = AIManager::Instance();

        // Set player reference in AIManager for distance optimization
        aiMgr.setPlayerForDistanceOptimization(m_player);

        // Initialize timing

        // Setup initial demo state
        m_currentPhase = DemoPhase::Initialization;
        m_phaseTimer = 0.0f;
        m_totalDemoTime = 0.0f;
        m_lastEventTriggerTime = -1.0f;
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

        // Create simple UI components
        auto& ui = UIManager::Instance();
        ui.createTitle("event_title", {10, 10, gameEngine.getWindowWidth() - 20, 25}, "Event Demo State");
        ui.setTitleAlignment("event_title", UIAlignment::CENTER_CENTER);
        ui.createLabel("event_phase", {10, 40, 300, 20}, "Phase: Initialization");
        ui.createLabel("event_status", {10, 65, 400, 20}, "FPS: -- | Weather: Clear | NPCs: 0");
        ui.createLabel("event_controls", {10, 90, gameEngine.getWindowWidth() - 20, 20},
                       "[B] Exit | [SPACE] Manual | [1-5] Events | [A] Auto Mode | [R] Reset");

        // Create event log component
        ui.createEventLog("event_log", {10, gameEngine.getWindowHeight() - 200, 730, 180}, 6);
        ui.addEventLogEntry("event_log", "Event Demo System Initialized");

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

        // Reset player
        m_player.reset();

        // Clear event log
        m_eventLog.clear();
        m_eventStates.clear();

        // Reset demo state
        m_currentPhase = DemoPhase::Initialization;
        m_phaseTimer = 0.0f;

        // Cache EventManager reference for better performance
        EventManager& eventMgr = EventManager::Instance();

        // Clean up event handlers
        eventMgr.removeHandlers(EventTypeId::Weather);
        eventMgr.removeHandlers(EventTypeId::NPCSpawn);
        eventMgr.removeHandlers(EventTypeId::SceneChange);

        // Use AIManager's prepareForStateTransition for safer cleanup
        AIManager& aiMgr = AIManager::Instance();
        aiMgr.prepareForStateTransition();

        // Clean up UI components using simplified method
        auto& ui = UIManager::Instance();
        ui.prepareForStateTransition();

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

void EventDemoState::update(float deltaTime) {
    // Cache AIManager reference for better performance
    AIManager& aiMgr = AIManager::Instance();

    // Update timing
    updateDemoTimer(deltaTime);

    // Update player
    if (m_player) {
        m_player->update(deltaTime);
    }

    // AI Manager is updated globally by GameEngine for optimal performance
    // This prevents double-updating AI entities which was causing them to move twice as fast
    // Entity updates are handled by AIManager::update() in GameEngine
    // No need to manually update AIManager here

    // Clean up invalid NPCs
    auto it = m_spawnedNPCs.begin();
    while (it != m_spawnedNPCs.end()) {
        if (*it) {
            ++it;
        } else {
            // Remove dead/invalid NPCs
            try {
                if (aiMgr.entityHasBehavior(*it)) {
                    aiMgr.unassignBehaviorFromEntity(*it);
                }
                aiMgr.unregisterEntityFromUpdates(*it);
            } catch (...) {
                // Ignore errors during cleanup
            }
            it = m_spawnedNPCs.erase(it);
        }
    }

    if (m_autoMode) {
        // Auto mode processing
        switch (m_currentPhase) {
            case DemoPhase::Initialization:
                if (m_phaseTimer >= 2.0f) {
                    m_currentPhase = DemoPhase::WeatherDemo;
                    m_phaseTimer = 0.0f;
                    triggerWeatherDemoAuto();
                    m_lastEventTriggerTime = m_totalDemoTime;
                    m_weatherChangesShown = 1;
                    addLogEntry("Starting weather demo - changes shown: 1/" + std::to_string(m_weatherSequence.size()));
                }
                break;

            case DemoPhase::WeatherDemo:
                if (!m_weatherDemoComplete && (m_totalDemoTime - m_lastEventTriggerTime) >= m_weatherChangeInterval) {
                    if (m_weatherChangesShown < m_weatherSequence.size()) {
                        triggerWeatherDemoAuto();
                        m_lastEventTriggerTime = m_totalDemoTime;
                        m_weatherChangesShown++;
                        m_phaseTimer = 0.0f;

                        addLogEntry("Weather changes shown: " + std::to_string(m_weatherChangesShown) + "/" + std::to_string(m_weatherSequence.size()));

                        if (m_weatherChangesShown >= m_weatherSequence.size()) {
                            m_weatherDemoComplete = true;
                            addLogEntry("Weather demo complete - All weather types shown!");
                        }
                    } else {
                        m_weatherDemoComplete = true;
                        addLogEntry("Weather demo force completed - counter exceeded limit");
                    }
                }
                if (m_weatherDemoComplete && m_phaseTimer >= 2.0f) {
                    m_currentPhase = DemoPhase::NPCSpawnDemo;
                    m_phaseTimer = 0.0f;
                    addLogEntry("Advancing to NPC Spawn Demo Phase");
                }
                break;

            case DemoPhase::NPCSpawnDemo:
                if ((m_totalDemoTime - m_lastEventTriggerTime) >= m_eventFireInterval &&
                    m_spawnedNPCs.size() < 5000) {
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
                if (m_phaseTimer >= 3.0f &&
                    (m_totalDemoTime - m_lastEventTriggerTime) >= m_eventFireInterval &&
                    m_spawnedNPCs.size() < 5000) {
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
                m_phaseTimer = 0.0f;
                break;

            case DemoPhase::Complete:
                break;
        }
    }

    // Update instructions
    updateInstructions();

    // Game logic only - UI updates moved to render() for thread safety

    // Note: EventManager is updated globally by GameEngine in the main update loop
    // for optimal performance and consistency with other global systems (AI, Input)
}

void EventDemoState::render(float deltaTime) {
    // Render player
    if (m_player) {
        m_player->render();
    }

    // Render spawned NPCs
    for (const auto& npc : m_spawnedNPCs) {
        if (npc) {
            npc->render();
        }
    }

    // Update and render UI components through UIManager using cached renderer for cleaner API
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        ui.update(deltaTime); // Use actual deltaTime from update cycle

        // Update UI displays
        auto& gameEngine = GameEngine::Instance();
        std::stringstream phaseText;
        phaseText << "Phase: " << getCurrentPhaseString();
        ui.setText("event_phase", phaseText.str());

        std::stringstream statusText;
        statusText << "FPS: " << std::fixed << std::setprecision(1) << gameEngine.getCurrentFPS()
                   << " | Weather: " << getCurrentWeatherString()
                   << " | NPCs: " << m_spawnedNPCs.size();
        ui.setText("event_status", statusText.str());
    }
    ui.render();
}

void EventDemoState::setupEventSystem() {
    std::cout << "Forge Game Engine - EventDemoState: EventManager instance obtained\n";
    addLogEntry("EventManager singleton obtained");

    // Cache EventManager reference for better performance
    EventManager& eventMgr = EventManager::Instance();

    if (!eventMgr.init()) {
        std::cerr << "Forge Game Engine - ERROR: Failed to initialize EventManager!\n";
        addLogEntry("ERROR: EventManager initialization failed");
        return;
    }

    std::cout << "Forge Game Engine - EventDemoState: EventManager initialized successfully\n";
    addLogEntry("EventManager initialized");

    // Register event handlers using new optimized API
    eventMgr.registerHandler(EventTypeId::Weather,
        [this](const EventData& data) {
            if (data.isActive()) {
                onWeatherChanged("weather_changed");
            }
        });

    eventMgr.registerHandler(EventTypeId::NPCSpawn,
        [this](const EventData& data) {
            if (data.isActive()) {
                onNPCSpawned("npc_spawned");
            }
        });

    eventMgr.registerHandler(EventTypeId::SceneChange,
        [this](const EventData& data) {
            if (data.isActive()) {
                onSceneChanged("scene_changed");
            }
        });

    std::cout << "Forge Game Engine - EventDemoState: Event handlers registered\n";
    addLogEntry("Event System Setup Complete - All handlers registered");
}

void EventDemoState::createTestEvents() {
    addLogEntry("=== Using New Convenience Methods ===");

    // Cache EventManager reference for better performance
    EventManager& eventMgr = EventManager::Instance();

    // Create and register weather events using new convenience methods
    bool success1 = eventMgr.createWeatherEvent("demo_clear", "Clear", 1.0f, 2.0f);
    bool success2 = eventMgr.createWeatherEvent("demo_rainy", "Rainy", 0.8f, 3.0f);
    bool success3 = eventMgr.createWeatherEvent("demo_stormy", "Stormy", 0.9f, 1.5f);
    bool success4 = eventMgr.createWeatherEvent("demo_foggy", "Foggy", 0.6f, 4.0f);

    // Create and register NPC spawn events using new convenience methods
    bool success5 = eventMgr.createNPCSpawnEvent("demo_guard_spawn", "Guard", 1, 20.0f);
    bool success6 = eventMgr.createNPCSpawnEvent("demo_villager_spawn", "Villager", 2, 15.0f);
    bool success7 = eventMgr.createNPCSpawnEvent("demo_merchant_spawn", "Merchant", 1, 25.0f);
    bool success8 = eventMgr.createNPCSpawnEvent("demo_warrior_spawn", "Warrior", 1, 30.0f);

    // Create and register scene change events using new convenience methods
    bool success9 = eventMgr.createSceneChangeEvent("demo_forest", "Forest", "fade", 2.0f);
    bool success10 = eventMgr.createSceneChangeEvent("demo_village", "Village", "slide", 1.5f);
    bool success11 = eventMgr.createSceneChangeEvent("demo_castle", "Castle", "dissolve", 2.5f);

    // Report creation results
    int successCount = success1 + success2 + success3 + success4 + success5 + success6 + success7 + success8 + success9 + success10 + success11;
    addLogEntry("Created " + std::to_string(successCount) + "/11 events using convenience methods");

    if (successCount == 11) {
        addLogEntry("All demo events created successfully");
    } else {
        addLogEntry("Some events failed to create - check logs");
    }

    // Show current event counts by type for monitoring
    size_t weatherCount = eventMgr.getEventCount(EventTypeId::Weather);
    size_t npcCount = eventMgr.getEventCount(EventTypeId::NPCSpawn);
    size_t sceneCount = eventMgr.getEventCount(EventTypeId::SceneChange);

    addLogEntry("Event counts - Weather: " + std::to_string(weatherCount) +
                ", NPC: " + std::to_string(npcCount) +
                ", Scene: " + std::to_string(sceneCount));
}

void EventDemoState::handleInput() {
    // Cache manager references for better performance
    const InputManager& inputMgr = InputManager::Instance();
    GameEngine& gameEngine = GameEngine::Instance();

    // Use InputManager's new event-driven key press detection
    if (inputMgr.wasKeyPressed(SDL_SCANCODE_SPACE)) {
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

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_1) &&
        (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
        if (m_autoMode && m_currentPhase == DemoPhase::WeatherDemo) {
            m_phaseTimer = 0.0f;
        }
        triggerWeatherDemoManual();
        addLogEntry("Manual weather event triggered");
        m_lastEventTriggerTime = m_totalDemoTime;
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_2) &&
        (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f &&
        m_spawnedNPCs.size() < 5000) {
        if (m_autoMode && m_currentPhase == DemoPhase::NPCSpawnDemo) {
            m_phaseTimer = 0.0f;
        }
        triggerNPCSpawnDemo();
        addLogEntry("Manual NPC spawn event triggered");
        m_lastEventTriggerTime = m_totalDemoTime;
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_3) &&
        (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
        if (m_autoMode && m_currentPhase == DemoPhase::SceneTransitionDemo) {
            m_phaseTimer = 0.0f;
        }
        triggerSceneTransitionDemo();
        addLogEntry("Manual scene transition event triggered");
        m_lastEventTriggerTime = m_totalDemoTime;
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_4) &&
        (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f &&
        m_spawnedNPCs.size() < 5000) {
        if (m_autoMode && m_currentPhase == DemoPhase::CustomEventDemo) {
            m_phaseTimer = 0.0f;
        }
        triggerCustomEventDemo();
        addLogEntry("Manual custom event triggered");
        m_lastEventTriggerTime = m_totalDemoTime;
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_4) &&
        (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
        if (m_spawnedNPCs.size() < 100) {
            if (m_autoMode && m_currentPhase == DemoPhase::CustomEventDemo) {
                m_phaseTimer = 0.0f;
            }
            triggerCustomEventDemo();
            addLogEntry("Manual custom event triggered");
            m_lastEventTriggerTime = m_totalDemoTime;
        } else {
            addLogEntry("NPC limit reached (100) - press 'R' to reset or '5' to clear NPCs");
        }
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_5)) {
        resetAllEvents();
        addLogEntry("All events reset");
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_R)) {
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

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_C) &&
        (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
        triggerConvenienceMethodsDemo();
        addLogEntry("Convenience methods demo triggered");
        m_lastEventTriggerTime = m_totalDemoTime;
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_A)) {
        m_autoMode = !m_autoMode;
        addLogEntry(m_autoMode ? "Auto mode enabled" : "Auto mode disabled");
    }

    if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
        gameEngine.getGameStateManager()->setState("MainMenuState");
    }
}

void EventDemoState::updateDemoTimer(float deltaTime) {
    if (m_autoMode) {
        m_phaseTimer += deltaTime;
    }
    m_totalDemoTime += deltaTime;
}



// UI now handled by UIManager components

void EventDemoState::renderEventStatus() const {
    // Event status now displayed through UIManager components
    // Event log functionality could be added as a list component if needed
}

void EventDemoState::renderControls() {
    // Controls now displayed through UIManager components
    // Control instructions are shown in the event_controls label
}

void EventDemoState::triggerWeatherDemo() {
    triggerWeatherDemoManual();
}

void EventDemoState::triggerWeatherDemoAuto() {
    size_t currentIndex = m_currentWeatherIndex;
    WeatherType newWeather = m_weatherSequence[m_currentWeatherIndex];
    m_currentWeatherIndex = (m_currentWeatherIndex + 1) % m_weatherSequence.size();

    // Create and execute weather event directly
    auto weatherEvent = std::make_shared<WeatherEvent>("demo_auto_weather", newWeather);
    WeatherParams params;
    params.transitionTime = m_weatherTransitionTime;
    params.intensity = (newWeather == WeatherType::Clear) ? 0.0f : 0.8f;
    weatherEvent->setWeatherParams(params);
    weatherEvent->execute();

    m_currentWeather = newWeather;
    addLogEntry("Weather changed to: " + getCurrentWeatherString() + " (Auto - Index: " + std::to_string(currentIndex) + ")");
}

void EventDemoState::triggerWeatherDemoManual() {
    static size_t manualWeatherIndex = 0;

    size_t currentIndex = manualWeatherIndex;
    WeatherType newWeather = m_weatherSequence[manualWeatherIndex];
    manualWeatherIndex = (manualWeatherIndex + 1) % m_weatherSequence.size();

    // Create and execute weather event directly
    auto weatherEvent = std::make_shared<WeatherEvent>("demo_manual_weather", newWeather);
    WeatherParams params;
    params.transitionTime = m_weatherTransitionTime;
    params.intensity = (newWeather == WeatherType::Clear) ? 0.0f : 0.8f;
    weatherEvent->setWeatherParams(params);
    weatherEvent->execute();

    m_currentWeather = newWeather;
    addLogEntry("Weather changed to: " + getCurrentWeatherString() + " (Manual - Index: " + std::to_string(currentIndex) + ")");
}

void EventDemoState::triggerNPCSpawnDemo() {
    std::string npcType = m_npcTypes[m_currentNPCTypeIndex];
    m_currentNPCTypeIndex = (m_currentNPCTypeIndex + 1) % m_npcTypes.size();

    Vector2D playerPos = m_player->getPosition();

    size_t npcCount = m_spawnedNPCs.size();
    float offsetX = 200.0f + ((npcCount % 8) * 120.0f);
    float offsetY = 100.0f + ((npcCount % 5) * 80.0f);

    float spawnX = playerPos.getX() + offsetX;
    float spawnY = playerPos.getY() + offsetY;

    spawnX = std::max(100.0f, std::min(spawnX, m_worldWidth - 100.0f));
    spawnY = std::max(100.0f, std::min(spawnY, m_worldHeight - 100.0f));

    createNPCAtPosition(npcType, spawnX, spawnY);
    addLogEntry("Spawned NPC: " + npcType + " at (" + std::to_string((int)spawnX) + ", " + std::to_string((int)spawnY) + ")");
}

void EventDemoState::triggerSceneTransitionDemo() {
    std::string sceneName = m_sceneNames[m_currentSceneIndex];
    m_currentSceneIndex = (m_currentSceneIndex + 1) % m_sceneNames.size();

    // Create and execute scene change event directly
    auto sceneEvent = std::make_shared<SceneChangeEvent>("demo_scene_change", sceneName);

    std::vector<TransitionType> transitions = {TransitionType::Fade, TransitionType::Slide, TransitionType::Dissolve, TransitionType::Wipe};
    TransitionType transitionType = transitions[m_currentSceneIndex % transitions.size()];

    sceneEvent->setTransitionType(transitionType);
    TransitionParams params(2.0f, transitionType);
    sceneEvent->setTransitionParams(params);
    sceneEvent->execute();

    std::string transitionName = (transitionType == TransitionType::Fade) ? "fade" :
                                (transitionType == TransitionType::Slide) ? "slide" :
                                (transitionType == TransitionType::Dissolve) ? "dissolve" : "wipe";

    addLogEntry("Scene transition to: " + sceneName + " (" + transitionName + ") executed directly");
}

void EventDemoState::triggerCustomEventDemo() {
    addLogEntry("Custom event demo - showing event system flexibility");

    triggerWeatherDemoManual();

    if (m_spawnedNPCs.size() >= 5000) {
        addLogEntry("NPC limit reached (5000) - skipping spawn in custom demo");
        return;
    }

    std::string npcType1 = m_npcTypes[m_currentNPCTypeIndex];
    m_currentNPCTypeIndex = (m_currentNPCTypeIndex + 1) % m_npcTypes.size();

    std::string npcType2 = m_npcTypes[m_currentNPCTypeIndex];
    m_currentNPCTypeIndex = (m_currentNPCTypeIndex + 1) % m_npcTypes.size();

    Vector2D playerPos = m_player->getPosition();

    size_t npcCount = m_spawnedNPCs.size();
    float offsetX1 = 150.0f + ((npcCount % 10) * 80.0f);
    float offsetY1 = 80.0f + ((npcCount % 6) * 50.0f);
    float offsetX2 = 250.0f + (((npcCount + 1) % 10) * 80.0f);
    float offsetY2 = 150.0f + (((npcCount + 1) % 6) * 50.0f);

    float spawnX1 = std::max(100.0f, std::min(playerPos.getX() + offsetX1, m_worldWidth - 100.0f));
    float spawnY1 = std::max(100.0f, std::min(playerPos.getY() + offsetY1, m_worldHeight - 100.0f));
    float spawnX2 = std::max(100.0f, std::min(playerPos.getX() + offsetX2, m_worldWidth - 100.0f));
    float spawnY2 = std::max(100.0f, std::min(playerPos.getY() + offsetY2, m_worldHeight - 100.0f));

    auto npc1 = createNPCAtPositionWithoutBehavior(npcType1, spawnX1, spawnY1);
    auto npc2 = createNPCAtPositionWithoutBehavior(npcType2, spawnX2, spawnY2);

    // Cache AIManager reference for better performance
    AIManager& aiMgr = AIManager::Instance();

    if (npc1) {
        std::string behaviorName1 = determineBehaviorForNPCType(npcType1);
        aiMgr.registerEntityForUpdates(npc1, rand() % 9 + 1, behaviorName1);
        addLogEntry("Registered " + npcType1 + " for updates and queued " + behaviorName1 + " behavior (global batch)");
    }

    if (npc2) {
        std::string behaviorName2 = determineBehaviorForNPCType(npcType2);
        aiMgr.registerEntityForUpdates(npc2, rand() % 9 + 1, behaviorName2);
        addLogEntry("Registered " + npcType2 + " for updates and queued " + behaviorName2 + " behavior (global batch)");
    }

    addLogEntry("Multiple NPCs spawned: " + npcType1 + " and " + npcType2 + " (Total NPCs: " + std::to_string(m_spawnedNPCs.size()) + ")");
}

void EventDemoState::triggerConvenienceMethodsDemo() {
    addLogEntry("=== CONVENIENCE METHODS DEMO ===");
    addLogEntry("Creating events with new one-line convenience methods");

    static int demoCounter = 0;
    demoCounter++;
    std::string suffix = std::to_string(demoCounter);

    // Cache EventManager reference for better performance
    EventManager& eventMgr = EventManager::Instance();

    bool success1 = eventMgr.createWeatherEvent("conv_fog_" + suffix, "Foggy", 0.7f, 2.5f);
    bool success2 = eventMgr.createWeatherEvent("conv_storm_" + suffix, "Stormy", 0.9f, 1.5f);
    bool success3 = eventMgr.createSceneChangeEvent("conv_dungeon_" + suffix, "DungeonDemo", "dissolve", 2.0f);
    bool success4 = eventMgr.createSceneChangeEvent("conv_town_" + suffix, "TownDemo", "slide", 1.0f);
    bool success5 = eventMgr.createNPCSpawnEvent("conv_guards_" + suffix, "Guard", 2, 30.0f);
    bool success6 = eventMgr.createNPCSpawnEvent("conv_merchants_" + suffix, "Merchant", 1, 15.0f);

    int successCount = success1 + success2 + success3 + success4 + success5 + success6;
    if (successCount == 6) {
        addLogEntry("[OK] All 6 events created successfully with convenience methods");
        addLogEntry("  - Fog weather (intensity: 0.7, transition: 2.5s)");
        addLogEntry("  - Storm weather (intensity: 0.9, transition: 1.5s)");
        addLogEntry("  - Dungeon scene (dissolve transition, 2.0s)");
        addLogEntry("  - Town scene (slide transition, 1.0s)");
        addLogEntry("  - Guard spawn (2 NPCs, radius: 30.0f)");
        addLogEntry("  - Merchant spawn (1 NPC, radius: 15.0f)");

        size_t totalEvents = eventMgr.getEventCount();
        size_t weatherEvents = eventMgr.getEventCount(EventTypeId::Weather);
        addLogEntry("Total events: " + std::to_string(totalEvents) + " (Weather: " + std::to_string(weatherEvents) + ")");

        // Create and execute weather event directly for demonstration
        auto weatherEvent = std::make_shared<WeatherEvent>("convenience_demo", WeatherType::Foggy);
        WeatherParams params;
        params.transitionTime = 2.5f;
        params.intensity = 0.7f;
        weatherEvent->setWeatherParams(params);
        weatherEvent->execute();

        m_currentWeather = WeatherType::Foggy;
        addLogEntry("Triggered fog weather to demonstrate functionality");
    } else {
        addLogEntry("[FAIL] Created " + std::to_string(successCount) + "/6 events - some failed");
    }

    addLogEntry("=== CONVENIENCE DEMO COMPLETE ===");
}

void EventDemoState::resetAllEvents() {
    cleanupSpawnedNPCs();

    // Create and execute clear weather event directly
    auto weatherEvent = std::make_shared<WeatherEvent>("reset_weather", WeatherType::Clear);
    WeatherParams params;
    params.transitionTime = 1.0f;
    params.intensity = 0.0f;
    weatherEvent->setWeatherParams(params);
    weatherEvent->execute();

    m_currentWeather = WeatherType::Clear;

    m_currentWeatherIndex = 0;
    m_currentNPCTypeIndex = 0;
    m_currentSceneIndex = 0;

    m_lastEventTriggerTime = 0.0f;
    m_limitMessageShown = false;

    addLogEntry("All events reset");
}

void EventDemoState::onWeatherChanged(const std::string& message) {
    addLogEntry("Weather Event Handler: " + message);
}

void EventDemoState::onNPCSpawned(const std::string& message) {
    addLogEntry("NPC Event Handler: " + message);
}

void EventDemoState::onSceneChanged(const std::string& message) {
    addLogEntry("Scene Event Handler: " + message);
}

void EventDemoState::setupAIBehaviors() {
    std::cout << "EventDemoState: Setting up AI behaviors for NPC integration...\n";

    // Cache AIManager reference for better performance
    AIManager& aiMgr = AIManager::Instance();

    if (!aiMgr.hasBehavior("Wander")) {
        auto wanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 80.0f);
        wanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("Wander", std::move(wanderBehavior));
        std::cout << "EventDemoState: Registered Wander behavior\n";
    }

    if (!aiMgr.hasBehavior("SmallWander")) {
        auto smallWanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::SMALL_AREA, 60.0f);
        smallWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("SmallWander", std::move(smallWanderBehavior));
        std::cout << "EventDemoState: Registered SmallWander behavior\n";
    }

    if (!aiMgr.hasBehavior("LargeWander")) {
        auto largeWanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::LARGE_AREA, 100.0f);
        largeWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("LargeWander", std::move(largeWanderBehavior));
        std::cout << "EventDemoState: Registered LargeWander behavior\n";
    }

    if (!aiMgr.hasBehavior("EventWander")) {
        auto eventWanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::EVENT_TARGET, 70.0f);
        eventWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("EventWander", std::move(eventWanderBehavior));
        std::cout << "EventDemoState: Registered EventWander behavior\n";
    }

    if (!aiMgr.hasBehavior("Patrol")) {
        auto patrolBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 75.0f, true);
        patrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("Patrol", std::move(patrolBehavior));
        std::cout << "EventDemoState: Registered Patrol behavior\n";
    }

    if (!aiMgr.hasBehavior("RandomPatrol")) {
        auto randomPatrolBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::RANDOM_AREA, 85.0f, false);
        randomPatrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("RandomPatrol", std::move(randomPatrolBehavior));
        std::cout << "EventDemoState: Registered RandomPatrol behavior\n";
    }

    if (!aiMgr.hasBehavior("CirclePatrol")) {
        auto circlePatrolBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::CIRCULAR_AREA, 90.0f, false);
        circlePatrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("CirclePatrol", std::move(circlePatrolBehavior));
        std::cout << "EventDemoState: Registered CirclePatrol behavior\n";
    }

    if (!aiMgr.hasBehavior("EventTarget")) {
        auto eventTargetBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::EVENT_TARGET, 95.0f, false);
        eventTargetBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
        aiMgr.registerBehavior("EventTarget", std::move(eventTargetBehavior));
        std::cout << "EventDemoState: Registered EventTarget behavior\n";
    }

    if (!aiMgr.hasBehavior("Chase")) {
        auto chaseBehavior = std::make_unique<ChaseBehavior>(120.0f, 500.0f, 50.0f);
        aiMgr.registerBehavior("Chase", std::move(chaseBehavior));
        std::cout << "EventDemoState: Chase behavior registered (will use AIManager::getPlayerReference())\n";
    }

    addLogEntry("AI Behaviors configured for NPC integration");
}

std::shared_ptr<NPC> EventDemoState::createNPCAtPositionWithoutBehavior(const std::string& npcType, float x, float y) {
    try {
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

        npc->setWanderArea(0.0f, 0.0f, m_worldWidth, m_worldHeight);
        npc->setBoundsCheckEnabled(false);

        m_spawnedNPCs.push_back(npc);

        return npc;
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION in createNPCAtPositionWithoutBehavior: " << e.what() << std::endl;
        return nullptr;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION in createNPCAtPositionWithoutBehavior" << std::endl;
        return nullptr;
    }
}

std::string EventDemoState::determineBehaviorForNPCType(const std::string& npcType) {
    static std::unordered_map<std::string, size_t> npcTypeCounters;
    size_t npcCount = npcTypeCounters[npcType]++;

    std::string behaviorName;

    if (npcType == "Guard") {
        std::vector<std::string> guardBehaviors = {"Patrol", "RandomPatrol", "CirclePatrol", "SmallWander", "EventTarget"};
        behaviorName = guardBehaviors[npcCount % guardBehaviors.size()];
    } else if (npcType == "Villager") {
        std::vector<std::string> villagerBehaviors = {"SmallWander", "Wander", "RandomPatrol", "CirclePatrol"};
        behaviorName = villagerBehaviors[npcCount % villagerBehaviors.size()];
    } else if (npcType == "Merchant") {
        std::vector<std::string> merchantBehaviors = {"Wander", "LargeWander", "RandomPatrol", "CirclePatrol"};
        behaviorName = merchantBehaviors[npcCount % merchantBehaviors.size()];
    } else if (npcType == "Warrior") {
        std::vector<std::string> warriorBehaviors = {"EventWander", "EventTarget", "LargeWander", "Chase"};
        behaviorName = warriorBehaviors[npcCount % warriorBehaviors.size()];
    } else {
        behaviorName = "Wander";
    }

    return behaviorName;
}



void EventDemoState::addLogEntry(const std::string& entry) {
    if (entry.empty()) return;

    try {
        // Add timestamp and send to UI event log component
        std::string timestampedEntry = "[" + std::to_string((int)m_totalDemoTime) + "s] " + entry;
        auto& ui = UIManager::Instance();
        ui.addEventLogEntry("event_log", timestampedEntry);

        // Also log to console for debugging
        std::cout << "EventDemo: " << timestampedEntry << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error adding log entry: " << e.what() << std::endl;
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
            m_instructions.push_back("Press 'C' for convenience methods demo");
            m_instructions.push_back("Press 'A' to toggle auto mode on/off");
            m_instructions.push_back("Press 'R' to reset all events");
            break;
        default:
            break;
    }
}

void EventDemoState::cleanupSpawnedNPCs() {
    for (const auto& npc : m_spawnedNPCs) {
        if (npc) {
            try {
                // Cache AIManager reference for better performance
                AIManager& aiMgr = AIManager::Instance();

                if (aiMgr.entityHasBehavior(npc)) {
                    aiMgr.unassignBehaviorFromEntity(npc);
                }
                aiMgr.unregisterEntityFromUpdates(npc);
            } catch (...) {
                // Ignore errors during cleanup to prevent double-free issues
            }
        }
    }

    m_spawnedNPCs.clear();
    m_limitMessageShown = false;
}

void EventDemoState::createNPCAtPosition(const std::string& npcType, float x, float y) {
    try {
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

        npc->setWanderArea(0.0f, 0.0f, m_worldWidth, m_worldHeight);
        npc->setBoundsCheckEnabled(false);

        std::string behaviorName = determineBehaviorForNPCType(npcType);

        // Cache AIManager reference for better performance
        AIManager& aiMgr = AIManager::Instance();
        aiMgr.registerEntityForUpdates(npc, rand() % 9 + 1, behaviorName);

        addLogEntry("Registered entity for updates and queued " + behaviorName + " behavior assignment (random priority)");

        m_spawnedNPCs.push_back(npc);
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION in createNPCAtPosition: " << e.what() << std::endl;
        std::cerr << "NPC type: " << npcType << ", position: (" << x << ", " << y << ")" << std::endl;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION in createNPCAtPosition" << std::endl;
        std::cerr << "NPC type: " << npcType << ", position: (" << x << ", " << y << ")" << std::endl;
    }
}
