/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/EventDemoState.hpp"
#include "SDL3/SDL_scancode.h"
#include "ai/behaviors/ChaseBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/WanderBehavior.hpp"
#include "core/GameEngine.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "events/SceneChangeEvent.hpp"
#include "events/WeatherEvent.hpp"
#include "managers/AIManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/ParticleManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/UIManager.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

EventDemoState::EventDemoState() {
  // Initialize member variables that need explicit initialization
}

EventDemoState::~EventDemoState() {
  // Don't call virtual functions from destructors

  try {
    // Note: Proper cleanup should already have happened in exit()
    // This destructor is just a safety measure in case exit() wasn't called

    // Cleanup any resources if needed
    cleanupSpawnedNPCs();

    GAMESTATE_INFO("Exiting EventDemoState in destructor...");
  } catch (const std::exception &e) {
    GAMESTATE_ERROR("Exception in EventDemoState destructor: " +
                    std::string(e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in EventDemoState destructor");
  }
}

bool EventDemoState::enter() {
  std::cout << "Hammer Game Engine - Entering EventDemoState...\n";

  try {
    // Cache GameEngine reference for better performance
    const GameEngine &gameEngine = GameEngine::Instance();

    // Setup world dimensions using logical coordinates for consistency
    m_worldWidth = gameEngine.getLogicalWidth();
    m_worldHeight = gameEngine.getLogicalHeight();

    // Initialize event system
    setupEventSystem();

    // Create player
    m_player = std::make_shared<Player>();
    m_player->initializeInventory(); // Initialize inventory after construction
    m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

    // Cache AIManager reference for better performance
    AIManager &aiMgr = AIManager::Instance();

    // Set player reference in AIManager for distance optimization
    aiMgr.setPlayerForDistanceOptimization(m_player);

    // Initialize timing

    // Setup achievement thresholds for demonstration
    setupResourceAchievements();

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

    // Create simple UI components using auto-detecting methods
    auto &ui = UIManager::Instance();
    ui.createTitleAtTop("event_title", "Event Demo State", 25);
    ui.createLabel("event_phase", {10, 40, 300, 20}, "Phase: Initialization");
    ui.createLabel("event_status", {10, 65, 400, 20},
                   "FPS: -- | Weather: Clear | NPCs: 0");
    ui.createLabel(
        "event_controls", {10, 90, ui.getLogicalWidth() - 20, 20},
        "[B] Exit | [SPACE] Manual | [1-6] Events | [A] Auto | [R] "
        "Reset | [F] Fire | [S] Smoke | [K] Sparks | Inventory Panel →");

    // Create event log component using auto-detected dimensions
    ui.createEventLog("event_log", {10, ui.getLogicalHeight() - 200, 730, 180},
                      7);
    ui.addEventLogEntry("event_log", "Event Demo System Initialized");

    // Create right-aligned inventory panel for resource demo visualization
    int windowWidth = ui.getLogicalWidth();
    int inventoryWidth = 280;
    int inventoryHeight = 320;
    int inventoryX =
        windowWidth - inventoryWidth - 20; // Right-aligned with 20px margin
    int inventoryY = 120;

    ui.createPanel("inventory_panel",
                   {inventoryX, inventoryY, inventoryWidth, inventoryHeight});
    ui.createTitle("inventory_title",
                   {inventoryX + 10, inventoryY + 10, inventoryWidth - 20, 30},
                   "Player Inventory");
    ui.createLabel("inventory_status",
                   {inventoryX + 10, inventoryY + 45, inventoryWidth - 20, 20},
                   "Capacity: 0/50");

    // Create inventory list for smooth updates like GamePlayState
    ui.createList("inventory_list",
                  {inventoryX + 10, inventoryY + 75, inventoryWidth - 20, 220});

    // --- DATA BINDING SETUP ---
    // Bind the inventory capacity label to a function that gets the data
    ui.bindText("inventory_status", [this]() -> std::string {
        if (!m_player || !m_player->getInventory()) return "Capacity: 0/0";
        auto* inventory = m_player->getInventory();
        return "Capacity: " + std::to_string(inventory->getUsedSlots()) + "/" + std::to_string(inventory->getMaxSlots());
    });

    // Bind the inventory list to a function that gets the items, sorts them, and returns them
    ui.bindList("inventory_list", [this]() -> std::vector<std::string> {
        if (!m_player || !m_player->getInventory()) return {"(Empty)"};
        
        auto* inventory = m_player->getInventory();
        auto allResources = inventory->getAllResources();

        if (allResources.empty()) return {"(Empty)"};

        std::vector<std::string> items;
        std::vector<std::pair<std::string, int>> sortedResources;
        for (const auto& [resourceHandle, quantity] : allResources) {
            if (quantity > 0) {
                auto resourceTemplate = ResourceTemplateManager::Instance().getResourceTemplate(resourceHandle);
                std::string resourceName = resourceTemplate ? resourceTemplate->getName() : "Unknown";
                sortedResources.emplace_back(resourceName, quantity);
            }
        }
        std::sort(sortedResources.begin(), sortedResources.end());

        for (const auto& [resourceId, quantity] : sortedResources) {
            items.push_back(resourceId + " x" + std::to_string(quantity));
        }
        return items;
    });


    std::cout
        << "Hammer Game Engine - EventDemoState initialized successfully\n";
    return true;

  } catch (const std::exception &e) {
    std::cerr
        << "Hammer Game Engine - ERROR: Exception in EventDemoState::enter(): "
        << e.what() << std::endl;
    return false;
  } catch (...) {
    std::cerr << "Hammer Game Engine - ERROR: Unknown exception in "
                 "EventDemoState::enter()"
              << std::endl;
    return false;
  }
}

bool EventDemoState::exit() {
  std::cout << "Hammer Game Engine - Exiting EventDemoState...\n";

  try {
    // Reset player
    m_player.reset();

    // Clear spawned NPCs vector and reset limit flag
    m_spawnedNPCs.clear();
    m_limitMessageShown = false;

    // Clear event log
    m_eventLog.clear();
    m_eventStates.clear();

    // Reset demo state
    m_currentPhase = DemoPhase::Initialization;
    m_phaseTimer = 0.0f;

    // Cache EventManager reference for better performance
    EventManager &eventMgr = EventManager::Instance();

    // Use EventManager's prepareForStateTransition for safer cleanup
    // This handles clearing all handlers and events in one operation
    eventMgr.prepareForStateTransition();

    // Use AIManager's prepareForStateTransition for architectural consistency
    AIManager &aiMgr = AIManager::Instance();
    aiMgr.prepareForStateTransition();

    // Simple particle cleanup - let prepareForStateTransition handle everything
    ParticleManager &particleMgr = ParticleManager::Instance();
    if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
      particleMgr.prepareForStateTransition(); // This handles weather effects
                                               // and cleanup
    }

    // Clean up UI components using simplified method
    auto &ui = UIManager::Instance();
    ui.prepareForStateTransition();

    std::cout << "Hammer Game Engine - EventDemoState cleanup complete\n";
    return true;

  } catch (const std::exception &e) {
    std::cerr
        << "Hammer Game Engine - ERROR: Exception in EventDemoState::exit(): "
        << e.what() << std::endl;
    return false;
  } catch (...) {
    std::cerr << "Hammer Game Engine - ERROR: Unknown exception in "
                 "EventDemoState::exit()"
              << std::endl;
    return false;
  }
}

void EventDemoState::update(float deltaTime) {
  // Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();

  // Update timing
  updateDemoTimer(deltaTime);

  // Update player
  if (m_player) {
    m_player->update(deltaTime);
  }

  // AI Manager is updated globally by GameEngine for optimal performance
  // This prevents double-updating AI entities which was causing them to move
  // twice as fast Entity updates are handled by AIManager::update() in
  // GameEngine No need to manually update AIManager here

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
        addLogEntry("Starting weather demo - changes shown: 1/" +
                    std::to_string(m_weatherSequence.size()));
      }
      break;

    case DemoPhase::WeatherDemo:
      if (!m_weatherDemoComplete &&
          (m_totalDemoTime - m_lastEventTriggerTime) >=
              m_weatherChangeInterval) {
        if (m_weatherChangesShown < m_weatherSequence.size()) {
          triggerWeatherDemoAuto();
          m_lastEventTriggerTime = m_totalDemoTime;
          m_weatherChangesShown++;
          m_phaseTimer = 0.0f;

          addLogEntry("Weather changes shown: " +
                      std::to_string(m_weatherChangesShown) + "/" +
                      std::to_string(m_weatherSequence.size()));

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
        addLogEntry(
            "NPC spawn demo complete - Starting Scene Transition Demo Phase");
      }
      break;

    case DemoPhase::SceneTransitionDemo:
      if (m_phaseTimer >= 3.0f &&
          (m_totalDemoTime - m_lastEventTriggerTime) >= m_eventFireInterval) {
        triggerSceneTransitionDemo();
        m_lastEventTriggerTime = m_totalDemoTime;
      }
      if (m_phaseTimer >= m_phaseDuration) {
        m_currentPhase = DemoPhase::ParticleEffectDemo;
        m_phaseTimer = 0.0f;
        addLogEntry("Starting Particle Effect Demo Phase");
      }
      break;

    case DemoPhase::ParticleEffectDemo:
      if (m_phaseTimer >= 2.0f &&
          (m_totalDemoTime - m_lastEventTriggerTime) >= m_eventFireInterval) {
        triggerParticleEffectDemo();
        m_lastEventTriggerTime = m_totalDemoTime;
      }
      if (m_phaseTimer >= m_phaseDuration) {
        m_currentPhase = DemoPhase::ResourceDemo;
        m_phaseTimer = 0.0f;
        addLogEntry("Starting Resource Demo Phase");
      }
      break;

    case DemoPhase::ResourceDemo:
      if (m_phaseTimer >= 2.0f &&
          (m_totalDemoTime - m_lastEventTriggerTime) >= m_eventFireInterval) {
        triggerResourceDemo();
        m_lastEventTriggerTime = m_totalDemoTime;
      }
      if (m_phaseTimer >= m_phaseDuration) {
        m_currentPhase = DemoPhase::CustomEventDemo;
        m_phaseTimer = 0.0f;
        addLogEntry("Advancing to Custom Event Demo Phase");
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

  // Note: EventManager is updated globally by GameEngine in the main update
  // loop for optimal performance and consistency with other global systems (AI,
  // Input)
}

void EventDemoState::render(double alpha) {
  // Get renderer using the standard pattern (consistent with other states)
  auto &gameEngine = GameEngine::Instance();
  SDL_Renderer *renderer = gameEngine.getRenderer();

  // Render background particles first (rain, snow) - behind player/NPCs
  ParticleManager &particleMgr = ParticleManager::Instance();
  if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
    // Render background particles with camera offset (for world-space effects)
    // Using 0,0 for now since EventDemoState doesn't have camera movement
    particleMgr.renderBackground(renderer, 0.0f, 0.0f);
  }

  // Render player
  if (m_player) {
    m_player->render(alpha);
  }

  // Render spawned NPCs
  for (const auto &npc : m_spawnedNPCs) {
    if (npc) {
      npc->render(alpha);
    }
  }

  // Render world-space particles
  if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
    particleMgr.render(renderer, 0.0f, 0.0f);
  }

  // Render foreground particles last (fog) - in front of player/NPCs
  if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
    particleMgr.renderForeground(renderer, 0.0f, 0.0f);
  }

  // Update and render UI components through UIManager using cached renderer for
  // cleaner API
  auto &ui = UIManager::Instance();
  if (!ui.isShutdown()) {
    ui.update(0.0); // UI updates are not time-dependent in this state

    // Update UI displays
    std::stringstream phaseText;
    phaseText << "Phase: " << getCurrentPhaseString();
    ui.setText("event_phase", phaseText.str());

    std::stringstream statusText;
    statusText << "FPS: " << std::fixed << std::setprecision(1)
               << gameEngine.getCurrentFPS()
               << " | Weather: " << getCurrentWeatherString()
               << " | NPCs: " << m_spawnedNPCs.size();
    ui.setText("event_status", statusText.str());

    // Update inventory display
    // updateInventoryUI(); // Now handled by data binding
  }
  ui.render();
}

void EventDemoState::setupEventSystem() {
  std::cout << "Hammer Game Engine - EventDemoState: EventManager instance "
               "obtained\n";
  addLogEntry("EventManager singleton obtained");

  // Cache EventManager reference for better performance
  // Note: EventManager is already initialized by GameEngine
  EventManager &eventMgr = EventManager::Instance();

  std::cout << "Hammer Game Engine - EventDemoState: Using pre-initialized "
               "EventManager\n";
  addLogEntry("EventManager ready for use");

  // Register event handlers using new optimized API
  eventMgr.registerHandler(EventTypeId::Weather, [this](const EventData &data) {
    if (data.isActive()) {
      onWeatherChanged("weather_changed");
    }
  });

  eventMgr.registerHandler(EventTypeId::NPCSpawn,
                           [this](const EventData &data) {
                             if (data.isActive()) {
                               onNPCSpawned("npc_spawned");
                             }
                           });

  eventMgr.registerHandler(EventTypeId::SceneChange,
                           [this](const EventData &data) {
                             if (data.isActive()) {
                               onSceneChanged("scene_changed");
                             }
                           });

  eventMgr.registerHandler(EventTypeId::ResourceChange,
                           [this](const EventData &data) {
                             if (data.isActive()) {
                               onResourceChanged(data);
                             }
                           });

  std::cout
      << "Hammer Game Engine - EventDemoState: Event handlers registered\n";
  addLogEntry("Event System Setup Complete - All handlers registered");
}

void EventDemoState::createTestEvents() {
  addLogEntry("=== Using New Convenience Methods ===");

  // Cache EventManager reference for better performance
  EventManager &eventMgr = EventManager::Instance();

  // Create and register weather events using new convenience methods
  bool success1 =
      eventMgr.createWeatherEvent("demo_clear", "Clear", 1.0f, 2.0f);
  bool success2 =
      eventMgr.createWeatherEvent("demo_rainy", "Rainy", 0.8f, 3.0f);
  bool success3 =
      eventMgr.createWeatherEvent("demo_stormy", "Stormy", 0.9f, 1.5f);
  bool success4 =
      eventMgr.createWeatherEvent("demo_foggy", "Foggy", 0.6f, 4.0f);

  // Create and register NPC spawn events using new convenience methods
  bool success5 =
      eventMgr.createNPCSpawnEvent("demo_guard_spawn", "Guard", 1, 20.0f);
  bool success6 =
      eventMgr.createNPCSpawnEvent("demo_villager_spawn", "Villager", 2, 15.0f);
  bool success7 =
      eventMgr.createNPCSpawnEvent("demo_merchant_spawn", "Merchant", 1, 25.0f);
  bool success8 =
      eventMgr.createNPCSpawnEvent("demo_warrior_spawn", "Warrior", 1, 30.0f);

  // Create and register scene change events using new convenience methods
  bool success9 =
      eventMgr.createSceneChangeEvent("demo_forest", "Forest", "fade", 2.0f);
  bool success10 =
      eventMgr.createSceneChangeEvent("demo_village", "Village", "slide", 1.5f);
  bool success11 = eventMgr.createSceneChangeEvent("demo_castle", "Castle",
                                                   "dissolve", 2.5f);

  // Report creation results
  int successCount = success1 + success2 + success3 + success4 + success5 +
                     success6 + success7 + success8 + success9 + success10 +
                     success11;
  addLogEntry("Created " + std::to_string(successCount) +
              "/11 events using convenience methods");

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
  const InputManager &inputMgr = InputManager::Instance();
  const auto &gameEngine = GameEngine::Instance();

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
      m_currentPhase = DemoPhase::ParticleEffectDemo;
      triggerParticleEffectDemo();
      break;
    case DemoPhase::ParticleEffectDemo:
      m_currentPhase = DemoPhase::ResourceDemo;
      triggerResourceDemo();
      break;
    case DemoPhase::ResourceDemo:
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
      addLogEntry(
          "NPC limit reached (100) - press 'R' to reset or '5' to clear NPCs");
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_5)) {
    resetAllEvents();
    addLogEntry("All events reset");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_6) &&
      (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
    if (m_autoMode && m_currentPhase == DemoPhase::ResourceDemo) {
      m_phaseTimer = 0.0f;
    }
    triggerResourceDemo();
    addLogEntry("Manual resource event triggered");
    m_lastEventTriggerTime = m_totalDemoTime;
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

  // Cache ParticleManager reference for better performance
  ParticleManager &particleMgr = ParticleManager::Instance();

  // Fire effect toggle (F key)
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_F)) {
    particleMgr.toggleFireEffect();
    addLogEntry("Fire effect toggled");
  }

  // Smoke effect toggle (S key)
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_S)) {
    particleMgr.toggleSmokeEffect();
    addLogEntry("Smoke effect toggled");
  }

  // Sparks effect toggle (K key)
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_K)) {
    particleMgr.toggleSparksEffect();
    addLogEntry("Sparks effect toggled");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
    gameEngine.getGameStateManager()->changeState("MainMenuState");
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

void EventDemoState::triggerWeatherDemo() { triggerWeatherDemoManual(); }

void EventDemoState::triggerWeatherDemoAuto() {
  size_t currentIndex = m_currentWeatherIndex;
  WeatherType newWeather = m_weatherSequence[m_currentWeatherIndex];
  std::string customType = m_customWeatherTypes[m_currentWeatherIndex];
  m_currentWeatherIndex =
      (m_currentWeatherIndex + 1) % m_weatherSequence.size();

  // Create weather event - use custom type if specified
  std::shared_ptr<WeatherEvent> weatherEvent;
  if (newWeather == WeatherType::Custom && !customType.empty()) {
    weatherEvent =
        std::make_shared<WeatherEvent>("demo_auto_weather", customType);
  } else {
    weatherEvent =
        std::make_shared<WeatherEvent>("demo_auto_weather", newWeather);
  }

  // Get the default params (which include particle effects) and modify them
  WeatherParams params = weatherEvent->getWeatherParams();
  params.transitionTime = m_weatherTransitionTime;

  // Set intensity based on weather type
  if (customType == "HeavyRain" || customType == "HeavySnow") {
    params.intensity = 0.9f; // High intensity for heavy weather
  } else {
    params.intensity = (newWeather == WeatherType::Clear) ? 0.0f : 0.8f;
  }

  weatherEvent->setWeatherParams(params);
  weatherEvent->execute();

  m_currentWeather = newWeather;
  std::string weatherName =
      customType.empty() ? getCurrentWeatherString() : customType;
  addLogEntry("Weather changed to: " + weatherName +
              " (Auto - Index: " + std::to_string(currentIndex) + ")");
}

void EventDemoState::triggerWeatherDemoManual() {
  static size_t manualWeatherIndex = 0;

  size_t currentIndex = manualWeatherIndex;
  WeatherType newWeather = m_weatherSequence[manualWeatherIndex];
  std::string customType = m_customWeatherTypes[manualWeatherIndex];

  // Debug output
  addLogEntry("Manual weather index: " + std::to_string(currentIndex) + " of " +
              std::to_string(m_weatherSequence.size()));
  if (!customType.empty()) {
    addLogEntry("Using custom weather type: " + customType);
  }

  manualWeatherIndex = (manualWeatherIndex + 1) % m_weatherSequence.size();

  // Create weather event - use custom type if specified
  std::shared_ptr<WeatherEvent> weatherEvent;
  if (newWeather == WeatherType::Custom && !customType.empty()) {
    weatherEvent =
        std::make_shared<WeatherEvent>("demo_manual_weather", customType);
  } else {
    weatherEvent =
        std::make_shared<WeatherEvent>("demo_manual_weather", newWeather);
  }

  // Get the default params (which include particle effects) and modify them
  WeatherParams params = weatherEvent->getWeatherParams();
  params.transitionTime = m_weatherTransitionTime;

  // Set intensity based on weather type
  if (customType == "HeavyRain" || customType == "HeavySnow") {
    params.intensity = 0.9f; // High intensity for heavy weather
    addLogEntry("Setting high intensity (0.9) for heavy weather: " +
                customType);
  } else {
    params.intensity = (newWeather == WeatherType::Clear) ? 0.0f : 0.8f;
  }

  weatherEvent->setWeatherParams(params);
  weatherEvent->execute();

  m_currentWeather = newWeather;
  std::string weatherName =
      customType.empty() ? getCurrentWeatherString() : customType;
  addLogEntry("Weather changed to: " + weatherName +
              " (Manual - Index: " + std::to_string(currentIndex) + ")");
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
  addLogEntry("Spawned NPC: " + npcType + " at (" +
              std::to_string((int)spawnX) + ", " + std::to_string((int)spawnY) +
              ")");
}

void EventDemoState::triggerSceneTransitionDemo() {
  std::string sceneName = m_sceneNames[m_currentSceneIndex];
  m_currentSceneIndex = (m_currentSceneIndex + 1) % m_sceneNames.size();

  // Create and execute scene change event directly
  auto sceneEvent =
      std::make_shared<SceneChangeEvent>("demo_scene_change", sceneName);

  std::vector<TransitionType> transitions = {
      TransitionType::Fade, TransitionType::Slide, TransitionType::Dissolve,
      TransitionType::Wipe};
  TransitionType transitionType =
      transitions[m_currentSceneIndex % transitions.size()];

  sceneEvent->setTransitionType(transitionType);
  TransitionParams params(2.0f, transitionType);
  sceneEvent->setTransitionParams(params);
  sceneEvent->execute();

  std::string transitionName =
      (transitionType == TransitionType::Fade)       ? "fade"
      : (transitionType == TransitionType::Slide)    ? "slide"
      : (transitionType == TransitionType::Dissolve) ? "dissolve"
                                                     : "wipe";

  addLogEntry("Scene transition to: " + sceneName + " (" + transitionName +
              ") executed directly");
}

void EventDemoState::triggerParticleEffectDemo() {
  static std::vector<std::string> effectNames = {"Fire", "Smoke", "Sparks"};
  static size_t effectIndex = 0;
  static std::vector<Vector2D> effectPositions = {
      Vector2D(200, 150), // Top-left area
      Vector2D(600, 150), // Top-right area
      Vector2D(400, 300), // Center
      Vector2D(300, 450), // Bottom-left
      Vector2D(500, 450), // Bottom-right
  };
  static size_t positionIndex = 0;

  // Get current effect and position
  std::string effectName = effectNames[effectIndex];
  Vector2D position = effectPositions[positionIndex];

  // Create ParticleEffectEvent using EventManager convenience method
  EventManager &eventMgr = EventManager::Instance();

  std::string eventName =
      "particle_demo_" + effectName + "_" + std::to_string(positionIndex);
  bool success =
      eventMgr.createParticleEffectEvent(eventName, effectName, position,
                                         1.2f,          // intensity
                                         5.0f,          // duration (5 seconds)
                                         "demo_effects" // group tag
      );

  if (success) {
    // Execute the created event
    eventMgr.executeEvent(eventName);

    addLogEntry("ParticleEffectEvent created and executed: " + effectName +
                " at (" + std::to_string((int)position.getX()) + ", " +
                std::to_string((int)position.getY()) + ") via EventManager");
  } else {
    addLogEntry("Failed to create ParticleEffectEvent: " + effectName);
  }

  // Advance to next effect and position
  effectIndex = (effectIndex + 1) % effectNames.size();
  positionIndex = (positionIndex + 1) % effectPositions.size();
}

void EventDemoState::triggerResourceDemo() {
  static size_t demonstrationStep = 0;
  static bool isAdding = true;

  if (!m_player || !m_player->getInventory()) {
    addLogEntry("Resource demo failed: Player inventory not available");
    return;
  }

  auto *inventory = m_player->getInventory();
  auto &templateManager = ResourceTemplateManager::Instance();

  if (!templateManager.isInitialized()) {
    addLogEntry(
        "Resource demo failed: ResourceTemplateManager not initialized");
    return;
  }

  // Realistic resource discovery: Query system for different categories/types
  ResourcePtr selectedResource = nullptr;
  std::string discoveryMethod;
  int quantity = 1;

  switch (demonstrationStep % 6) {
  case 0: {
    // Discovery pattern: Find currency resources
    auto currencyResources =
        templateManager.getResourcesByCategory(ResourceCategory::Currency);
    if (!currencyResources.empty()) {
      // Real game logic: pick the first currency resource found
      selectedResource = currencyResources[0];
      discoveryMethod = "Currency category query";
      quantity = 50;
    }
    break;
  }
  case 1: {
    // Discovery pattern: Find consumable items
    auto consumables =
        templateManager.getResourcesByType(ResourceType::Consumable);
    if (!consumables.empty()) {
      // Real game logic: find a consumable with reasonable value
      auto it = std::find_if(
          consumables.begin(), consumables.end(), [](const auto &resource) {
            return resource->getValue() > 0 && resource->getValue() < 100;
          });
      if (it != consumables.end()) {
        selectedResource = *it;
      }
      discoveryMethod = "Consumable type query (filtered by value)";
      quantity = 3;
    }
    break;
  }
  case 2: {
    // Discovery pattern: Find raw materials
    auto materials =
        templateManager.getResourcesByType(ResourceType::RawResource);
    if (!materials.empty()) {
      // Real game logic: find stackable materials
      auto it = std::find_if(
          materials.begin(), materials.end(), [](const auto &resource) {
            return resource->isStackable() && resource->getMaxStackSize() >= 50;
          });
      if (it != materials.end()) {
        selectedResource = *it;
      }
      discoveryMethod = "RawResource type query (stackable, high capacity)";
      quantity = 15;
    }
    break;
  }
  case 3: {
    // Discovery pattern: Find building materials
    auto gameResources =
        templateManager.getResourcesByType(ResourceType::BuildingMaterial);
    if (!gameResources.empty()) {
      selectedResource = gameResources[0];
      discoveryMethod = "BuildingMaterial type query";
      quantity = 8;
    }
    break;
  }
  case 4: {
    // Discovery pattern: Find equipment
    auto equipment =
        templateManager.getResourcesByType(ResourceType::Equipment);
    if (!equipment.empty()) {
      selectedResource = equipment[0];
      discoveryMethod = "Equipment type query";
      quantity = 1;
    }
    break;
  }
  case 5: {
    // Discovery pattern: Find valuable gems
    auto gems = templateManager.getResourcesByType(ResourceType::Gem);
    if (!gems.empty()) {
      // Real game logic: find high-value gems
      auto it =
          std::find_if(gems.begin(), gems.end(), [](const auto &resource) {
            return resource->getValue() > 500;
          });
      if (it != gems.end()) {
        selectedResource = *it;
      }
      if (!selectedResource) {
        selectedResource = gems[0]; // fallback
      }
      discoveryMethod = "Gem type query (high value preferred)";
      quantity = 2;
    }
    break;
  }
  }

  if (!selectedResource) {
    addLogEntry("Resource discovery failed: No resources found for step " +
                std::to_string(demonstrationStep));
    demonstrationStep++;
    return;
  }

  // Use the discovered resource
  auto handle = selectedResource->getHandle();
  std::string resourceName = selectedResource->getName();
  int currentQuantity = inventory->getResourceQuantity(handle);

  if (isAdding) {
    // Add resources to player inventory
    addLogEntry("BEFORE ADD (" + discoveryMethod + "): " + resourceName +
                " = " + std::to_string(currentQuantity));

    bool success = inventory->addResource(handle, quantity);
    if (success) {
      int newQuantity = inventory->getResourceQuantity(handle);
      addLogEntry("AFTER ADD: " + resourceName + " = " +
                  std::to_string(newQuantity) + " (+" +
                  std::to_string(quantity) + ")");

      // Create and register ResourceChangeEvent for demonstration
      auto resourceEvent =
          std::make_shared<ResourceChangeEvent>(m_player, handle,
                                                currentQuantity, // old quantity
                                                newQuantity,     // new quantity
                                                "event_demo");

      // Register and execute the event to demonstrate event-based resource
      // management
      EventManager &eventMgr = EventManager::Instance();
      std::string eventName =
          "resource_demo_add_" + std::to_string(demonstrationStep);
      eventMgr.registerEvent(eventName, resourceEvent);
      eventMgr.executeEvent(eventName);

      addLogEntry("ResourceChangeEvent executed for ADD operation");
    } else {
      addLogEntry("FAILED to add " + resourceName + " - inventory may be full");
    }
  } else {
    // Remove resources from player inventory
    int removeQuantity = std::min(quantity, currentQuantity);

    if (removeQuantity > 0) {
      addLogEntry("BEFORE REMOVE (" + discoveryMethod + "): " + resourceName +
                  " = " + std::to_string(currentQuantity));

      bool success = inventory->removeResource(handle, removeQuantity);
      if (success) {
        int newQuantity = inventory->getResourceQuantity(handle);
        addLogEntry("AFTER REMOVE: " + resourceName + " = " +
                    std::to_string(newQuantity) + " (-" +
                    std::to_string(removeQuantity) + ")");

        // Create and register ResourceChangeEvent for demonstration
        auto resourceEvent = std::make_shared<ResourceChangeEvent>(
            m_player, handle,
            currentQuantity, // old quantity
            newQuantity,     // new quantity
            "event_demo");

        // Register and execute the event
        EventManager &eventMgr = EventManager::Instance();
        std::string eventName =
            "resource_demo_remove_" + std::to_string(demonstrationStep);
        eventMgr.registerEvent(eventName, resourceEvent);
        eventMgr.executeEvent(eventName);

        addLogEntry("ResourceChangeEvent executed for REMOVE operation");
      } else {
        addLogEntry("FAILED to remove " + resourceName + " from inventory");
      }
    } else {
      addLogEntry("No " + resourceName + " to remove from inventory");
    }
  }

  // Progress through different discovery patterns and alternate between adding
  // and removing
  demonstrationStep++;
  if (demonstrationStep % 6 == 0) {
    isAdding = !isAdding; // Switch between adding and removing after full cycle
    std::string mode = isAdding ? "ADDING" : "REMOVING";
    addLogEntry("--- Switched to " + mode + " mode (completed " +
                std::to_string(demonstrationStep / 6) + " cycles) ---");
  }

  // Log current inventory summary
  auto allResources = inventory->getAllResources();
  std::string inventoryStatus = "Inventory Summary: ";
  bool hasItems = false;
  for (const auto &[resourceHandle, qty] : allResources) {
    if (qty > 0) {
      if (hasItems)
        inventoryStatus += ", ";

      auto resourceTemplate =
          ResourceTemplateManager::Instance().getResourceTemplate(
              resourceHandle);
      std::string resName =
          resourceTemplate ? resourceTemplate->getName() : "Unknown";

      inventoryStatus += resName + "(" + std::to_string(qty) + ")";
      hasItems = true;
    }
  }
  if (!hasItems) {
    inventoryStatus += "Empty";
  }
  addLogEntry(inventoryStatus);
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

  float spawnX1 = std::max(
      100.0f, std::min(playerPos.getX() + offsetX1, m_worldWidth - 100.0f));
  float spawnY1 = std::max(
      100.0f, std::min(playerPos.getY() + offsetY1, m_worldHeight - 100.0f));
  float spawnX2 = std::max(
      100.0f, std::min(playerPos.getX() + offsetX2, m_worldWidth - 100.0f));
  float spawnY2 = std::max(
      100.0f, std::min(playerPos.getY() + offsetY2, m_worldHeight - 100.0f));

  auto npc1 = createNPCAtPositionWithoutBehavior(npcType1, spawnX1, spawnY1);
  auto npc2 = createNPCAtPositionWithoutBehavior(npcType2, spawnX2, spawnY2);

  // Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();

  if (npc1) {
    std::string behaviorName1 = determineBehaviorForNPCType(npcType1);
    aiMgr.registerEntityForUpdates(npc1, rand() % 9 + 1, behaviorName1);
    addLogEntry("Registered " + npcType1 + " for updates and queued " +
                behaviorName1 + " behavior (global batch)");
  }

  if (npc2) {
    std::string behaviorName2 = determineBehaviorForNPCType(npcType2);
    aiMgr.registerEntityForUpdates(npc2, rand() % 9 + 1, behaviorName2);
    addLogEntry("Registered " + npcType2 + " for updates and queued " +
                behaviorName2 + " behavior (global batch)");
  }

  addLogEntry("Multiple NPCs spawned: " + npcType1 + " and " + npcType2 +
              " (Total NPCs: " + std::to_string(m_spawnedNPCs.size()) + ")");
}

void EventDemoState::triggerConvenienceMethodsDemo() {
  addLogEntry("=== CONVENIENCE METHODS DEMO ===");
  addLogEntry("Creating events with new one-line convenience methods");

  static int demoCounter = 0;
  demoCounter++;
  std::string suffix = std::to_string(demoCounter);

  // Cache EventManager reference for better performance
  EventManager &eventMgr = EventManager::Instance();

  bool success1 =
      eventMgr.createWeatherEvent("conv_fog_" + suffix, "Foggy", 0.7f, 2.5f);
  bool success2 =
      eventMgr.createWeatherEvent("conv_storm_" + suffix, "Stormy", 0.9f, 1.5f);
  bool success3 = eventMgr.createSceneChangeEvent(
      "conv_dungeon_" + suffix, "DungeonDemo", "dissolve", 2.0f);
  bool success4 = eventMgr.createSceneChangeEvent("conv_town_" + suffix,
                                                  "TownDemo", "slide", 1.0f);
  bool success5 =
      eventMgr.createNPCSpawnEvent("conv_guards_" + suffix, "Guard", 2, 30.0f);
  bool success6 = eventMgr.createNPCSpawnEvent("conv_merchants_" + suffix,
                                               "Merchant", 1, 15.0f);

  int successCount =
      success1 + success2 + success3 + success4 + success5 + success6;
  if (successCount == 6) {
    addLogEntry(
        "[OK] All 6 events created successfully with convenience methods");
    addLogEntry("  - Fog weather (intensity: 0.7, transition: 2.5s)");
    addLogEntry("  - Storm weather (intensity: 0.9, transition: 1.5s)");
    addLogEntry("  - Dungeon scene (dissolve transition, 2.0s)");
    addLogEntry("  - Town scene (slide transition, 1.0s)");
    addLogEntry("  - Guard spawn (2 NPCs, radius: 30.0f)");
    addLogEntry("  - Merchant spawn (1 NPC, radius: 15.0f)");

    size_t totalEvents = eventMgr.getEventCount();
    size_t weatherEvents = eventMgr.getEventCount(EventTypeId::Weather);
    addLogEntry("Total events: " + std::to_string(totalEvents) +
                " (Weather: " + std::to_string(weatherEvents) + ")");

    // Create and execute weather event directly for demonstration
    auto weatherEvent =
        std::make_shared<WeatherEvent>("convenience_demo", WeatherType::Foggy);
    WeatherParams params;
    params.transitionTime = 2.5f;
    params.intensity = 0.7f;
    weatherEvent->setWeatherParams(params);
    weatherEvent->execute();

    m_currentWeather = WeatherType::Foggy;
    addLogEntry("Triggered fog weather to demonstrate functionality");
  } else {
    addLogEntry("[FAIL] Created " + std::to_string(successCount) +
                "/6 events - some failed");
  }

  addLogEntry("=== CONVENIENCE DEMO COMPLETE ===");
}

void EventDemoState::resetAllEvents() {
  cleanupSpawnedNPCs();

  // Create and execute clear weather event directly
  auto weatherEvent =
      std::make_shared<WeatherEvent>("reset_weather", WeatherType::Clear);
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

void EventDemoState::onWeatherChanged(const std::string &message) {
  addLogEntry("Weather Event Handler: " + message);
}

void EventDemoState::onNPCSpawned(const std::string &message) {
  addLogEntry("NPC Event Handler: " + message);
}

void EventDemoState::onSceneChanged(const std::string &message) {
  addLogEntry("Scene Event Handler: " + message);
}

void EventDemoState::onResourceChanged(const EventData &data) {
  // Extract ResourceChangeEvent from EventData
  try {
    if (!data.event) {
      addLogEntry("Error: ResourceChangeEvent data is null");
      return;
    }

    // Cast to ResourceChangeEvent to access its data
    auto resourceEvent =
        std::dynamic_pointer_cast<ResourceChangeEvent>(data.event);
    if (!resourceEvent) {
      addLogEntry("Error: Event is not a ResourceChangeEvent");
      return;
    }

    addLogEntry("=== RESOURCE CHANGE EVENT HANDLER ACTIVATED ===");

    // Get the resource change data
    HammerEngine::ResourceHandle handle = resourceEvent->getResourceHandle();
    int oldQty = resourceEvent->getOldQuantity();
    int newQty = resourceEvent->getNewQuantity();
    std::string source = resourceEvent->getChangeReason();

    // Process different aspects of resource changes
    processResourceAchievements(handle, oldQty, newQty);
    checkResourceWarnings(handle, newQty);
    // updateResourceUI(handle, oldQty, newQty); // No longer needed, UI is data-bound
    logResourceAnalytics(handle, oldQty, newQty, source);

    addLogEntry("Resource change handler completed");
  } catch (const std::exception &e) {
    addLogEntry("Error in resource change handler: " + std::string(e.what()));
  }
}

void EventDemoState::setupAIBehaviors() {
  std::cout
      << "EventDemoState: Setting up AI behaviors for NPC integration...\n";

  // Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();

  if (!aiMgr.hasBehavior("Wander")) {
    auto wanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::MEDIUM_AREA, 80.0f);
    wanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("Wander", std::move(wanderBehavior));
    std::cout << "EventDemoState: Registered Wander behavior\n";
  }

  if (!aiMgr.hasBehavior("SmallWander")) {
    auto smallWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::SMALL_AREA, 60.0f);
    smallWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("SmallWander", std::move(smallWanderBehavior));
    std::cout << "EventDemoState: Registered SmallWander behavior\n";
  }

  if (!aiMgr.hasBehavior("LargeWander")) {
    auto largeWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::LARGE_AREA, 100.0f);
    largeWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("LargeWander", std::move(largeWanderBehavior));
    std::cout << "EventDemoState: Registered LargeWander behavior\n";
  }

  if (!aiMgr.hasBehavior("EventWander")) {
    auto eventWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::EVENT_TARGET, 70.0f);
    eventWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("EventWander", std::move(eventWanderBehavior));
    std::cout << "EventDemoState: Registered EventWander behavior\n";
  }

  if (!aiMgr.hasBehavior("Patrol")) {
    auto patrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 75.0f, true);
    patrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("Patrol", std::move(patrolBehavior));
    std::cout << "EventDemoState: Registered Patrol behavior\n";
  }

  if (!aiMgr.hasBehavior("RandomPatrol")) {
    auto randomPatrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::RANDOM_AREA, 85.0f, false);
    randomPatrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("RandomPatrol", std::move(randomPatrolBehavior));
    std::cout << "EventDemoState: Registered RandomPatrol behavior\n";
  }

  if (!aiMgr.hasBehavior("CirclePatrol")) {
    auto circlePatrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::CIRCULAR_AREA, 90.0f, false);
    circlePatrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("CirclePatrol", std::move(circlePatrolBehavior));
    std::cout << "EventDemoState: Registered CirclePatrol behavior\n";
  }

  if (!aiMgr.hasBehavior("EventTarget")) {
    auto eventTargetBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::EVENT_TARGET, 95.0f, false);
    eventTargetBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("EventTarget", std::move(eventTargetBehavior));
    std::cout << "EventDemoState: Registered EventTarget behavior\n";
  }

  if (!aiMgr.hasBehavior("Chase")) {
    auto chaseBehavior = std::make_unique<ChaseBehavior>(120.0f, 500.0f, 50.0f);
    aiMgr.registerBehavior("Chase", std::move(chaseBehavior));
    std::cout << "EventDemoState: Chase behavior registered (will use "
                 "AIManager::getPlayerReference())\n";
  }

  addLogEntry("AI Behaviors configured for NPC integration");
}

std::shared_ptr<NPC>
EventDemoState::createNPCAtPositionWithoutBehavior(const std::string &npcType,
                                                   float x, float y) {
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
    npc->initializeInventory(); // Initialize inventory after construction

    npc->setWanderArea(0.0f, 0.0f, m_worldWidth, m_worldHeight);
    npc->setBoundsCheckEnabled(false);

    m_spawnedNPCs.push_back(npc);

    return npc;
  } catch (const std::exception &e) {
    std::cerr << "EXCEPTION in createNPCAtPositionWithoutBehavior: " << e.what()
              << std::endl;
    return nullptr;
  } catch (...) {
    std::cerr << "UNKNOWN EXCEPTION in createNPCAtPositionWithoutBehavior"
              << std::endl;
    return nullptr;
  }
}

std::string
EventDemoState::determineBehaviorForNPCType(const std::string &npcType) {
  static std::unordered_map<std::string, size_t> npcTypeCounters;
  size_t npcCount = npcTypeCounters[npcType]++;

  std::string behaviorName;

  if (npcType == "Guard") {
    std::vector<std::string> guardBehaviors = {
        "Patrol", "RandomPatrol", "CirclePatrol", "SmallWander", "EventTarget"};
    behaviorName = guardBehaviors[npcCount % guardBehaviors.size()];
  } else if (npcType == "Villager") {
    std::vector<std::string> villagerBehaviors = {
        "SmallWander", "Wander", "RandomPatrol", "CirclePatrol"};
    behaviorName = villagerBehaviors[npcCount % villagerBehaviors.size()];
  } else if (npcType == "Merchant") {
    std::vector<std::string> merchantBehaviors = {
        "Wander", "LargeWander", "RandomPatrol", "CirclePatrol"};
    behaviorName = merchantBehaviors[npcCount % merchantBehaviors.size()];
  } else if (npcType == "Warrior") {
    std::vector<std::string> warriorBehaviors = {"EventWander", "EventTarget",
                                                 "LargeWander", "Chase"};
    behaviorName = warriorBehaviors[npcCount % warriorBehaviors.size()];
  } else {
    behaviorName = "Wander";
  }

  return behaviorName;
}

void EventDemoState::addLogEntry(const std::string &entry) {
  if (entry.empty())
    return;

  try {
    // Add timestamp and send to UI event log component
    std::string timestampedEntry =
        "[" + std::to_string((int)m_totalDemoTime) + "s] " + entry;
    auto &ui = UIManager::Instance();
    ui.addEventLogEntry("event_log", timestampedEntry);

    // Also log to console for debugging
    std::cout << "EventDemo: " << timestampedEntry << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Error adding log entry: " << e.what() << std::endl;
  }
}

std::string EventDemoState::getCurrentPhaseString() const {
  switch (m_currentPhase) {
  case DemoPhase::Initialization:
    return "Initialization";
  case DemoPhase::WeatherDemo:
    return "Weather Demo";
  case DemoPhase::NPCSpawnDemo:
    return "NPC Spawn Demo";
  case DemoPhase::SceneTransitionDemo:
    return "Scene Transition Demo";
  case DemoPhase::ParticleEffectDemo:
    return "Particle Effect Demo";
  case DemoPhase::ResourceDemo:
    return "Resource Demo";
  case DemoPhase::CustomEventDemo:
    return "Custom Event Demo";
  case DemoPhase::InteractiveMode:
    return "Interactive Mode";
  case DemoPhase::Complete:
    return "Complete";
  default:
    return "Unknown";
  }
}

std::string EventDemoState::getCurrentWeatherString() const {
  switch (m_currentWeather) {
  case WeatherType::Clear:
    return "Clear";
  case WeatherType::Cloudy:
    return "Cloudy";
  case WeatherType::Rainy:
    return "Rainy";
  case WeatherType::Stormy:
    return "Stormy";
  case WeatherType::Foggy:
    return "Foggy";
  case WeatherType::Snowy:
    return "Snowy";
  case WeatherType::Windy:
    return "Windy";
  case WeatherType::Custom:
    return "Custom";
  default:
    return "Unknown";
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
  case DemoPhase::ParticleEffectDemo:
    m_instructions.push_back("Demonstrating particle effects via EventManager");
    m_instructions.push_back("Particles triggered at various coordinates");
    break;
  case DemoPhase::ResourceDemo:
    m_instructions.push_back(
        "Demonstrating resource management via EventManager");
    m_instructions.push_back("Resources added/removed with events fired");
    m_instructions.push_back("Watch inventory panel for real-time changes");
    break;
  case DemoPhase::CustomEventDemo:
    m_instructions.push_back("Demonstrating custom event combinations");
    m_instructions.push_back("Multiple events triggered together");
    break;
  case DemoPhase::InteractiveMode:
    m_instructions.push_back("Interactive Mode - Manual Control (Permanent)");
    m_instructions.push_back("Use number keys 1-6 to trigger events");
    m_instructions.push_back("Press 'C' for convenience methods demo");
    m_instructions.push_back("Press 'A' to toggle auto mode on/off");
    m_instructions.push_back("Press 'R' to reset all events");
    break;
  default:
    break;
  }
}

void EventDemoState::cleanupSpawnedNPCs() {
  for (const auto &npc : m_spawnedNPCs) {
    if (npc) {
      try {
        // Cache AIManager reference for better performance
        AIManager &aiMgr = AIManager::Instance();

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

void EventDemoState::createNPCAtPosition(const std::string &npcType, float x,
                                         float y) {
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
    npc->initializeInventory(); // Initialize inventory after construction

    npc->setWanderArea(0.0f, 0.0f, m_worldWidth, m_worldHeight);
    npc->setBoundsCheckEnabled(false);

    std::string behaviorName = determineBehaviorForNPCType(npcType);

    // Cache AIManager reference for better performance
    AIManager &aiMgr = AIManager::Instance();
    aiMgr.registerEntityForUpdates(npc, rand() % 9 + 1, behaviorName);

    addLogEntry("Registered entity for updates and queued " + behaviorName +
                " behavior assignment (random priority)");

    m_spawnedNPCs.push_back(npc);
  } catch (const std::exception &e) {
    std::cerr << "EXCEPTION in createNPCAtPosition: " << e.what() << std::endl;
    std::cerr << "NPC type: " << npcType << ", position: (" << x << ", " << y
              << ")" << std::endl;
  } catch (...) {
    std::cerr << "UNKNOWN EXCEPTION in createNPCAtPosition" << std::endl;
    std::cerr << "NPC type: " << npcType << ", position: (" << x << ", " << y
              << ")" << std::endl;
  }
}

void EventDemoState::setupResourceAchievements() {
  // Set up achievement thresholds for different resource types for
  // demonstration
  const auto &templateManager = ResourceTemplateManager::Instance();

  if (!templateManager.isInitialized()) {
    addLogEntry(
        "Cannot setup achievements: ResourceTemplateManager not initialized");
    return;
  }

  // Find some common resources and set thresholds
  auto currencies =
      templateManager.getResourcesByCategory(ResourceCategory::Currency);
  auto materials =
      templateManager.getResourcesByType(ResourceType::RawResource);
  auto consumables =
      templateManager.getResourcesByType(ResourceType::Consumable);

  // Set achievement thresholds
  for (const auto &resource : currencies) {
    m_achievementThresholds[resource->getHandle()] =
        100; // First 100 currency units
  }

  for (const auto &resource : materials) {
    m_achievementThresholds[resource->getHandle()] = 25; // First 25 materials
  }

  for (const auto &resource : consumables) {
    m_achievementThresholds[resource->getHandle()] = 10; // First 10 consumables
  }

  addLogEntry("Achievement thresholds set for " +
              std::to_string(m_achievementThresholds.size()) +
              " resource types");
}

void EventDemoState::processResourceAchievements(
    HammerEngine::ResourceHandle handle, int oldQty, int newQty) {
  auto thresholdIt = m_achievementThresholds.find(handle);
  if (thresholdIt == m_achievementThresholds.end()) {
    return; // No achievement for this resource
  }

  int threshold = thresholdIt->second;
  bool wasUnlocked = m_achievementsUnlocked[handle];

  // Check if we crossed the achievement threshold
  if (!wasUnlocked && oldQty < threshold && newQty >= threshold) {
    m_achievementsUnlocked[handle] = true;

    // Get resource name for achievement message
    auto resourceTemplate =
        ResourceTemplateManager::Instance().getResourceTemplate(handle);
    std::string resourceName =
        resourceTemplate ? resourceTemplate->getName() : "Unknown";

    addLogEntry("🏆 ACHIEVEMENT UNLOCKED: First " + std::to_string(threshold) +
                " " + resourceName + "!");

    // In a real game, this could trigger UI popups, sound effects, save to
    // profile, etc.
  }
}

void EventDemoState::checkResourceWarnings(HammerEngine::ResourceHandle handle,
                                           int newQty) {
  // Check for low resource warnings
  auto resourceTemplate =
      ResourceTemplateManager::Instance().getResourceTemplate(handle);
  if (!resourceTemplate)
    return;

  std::string resourceName = resourceTemplate->getName();

  // Warning for low quantities of important resources
  if (resourceTemplate->getType() == ResourceType::Consumable && newQty <= 2 &&
      newQty > 0) {
    addLogEntry("⚠️ LOW SUPPLY WARNING: Only " + std::to_string(newQty) + " " +
                resourceName + " remaining!");
  }

  // Warning when resources are completely depleted
  if (newQty == 0) {
    addLogEntry("❌ DEPLETED: " + resourceName + " is now empty!");
  }

  // Warning for high-value resources reaching storage limits
  if (resourceTemplate->isStackable() &&
      resourceTemplate->getMaxStackSize() > 0) {
    int maxStack = resourceTemplate->getMaxStackSize();
    if (newQty >= maxStack * 0.9f) { // 90% of stack limit
      addLogEntry("📦 STORAGE WARNING: " + resourceName +
                  " approaching stack limit (" + std::to_string(newQty) + "/" +
                  std::to_string(maxStack) + ")");
    }
  }
}



void EventDemoState::logResourceAnalytics(HammerEngine::ResourceHandle handle,
                                          int oldQty, int newQty,
                                          const std::string &source) {
  auto resourceTemplate =
      ResourceTemplateManager::Instance().getResourceTemplate(handle);
  if (!resourceTemplate)
    return;

  std::string resourceName = resourceTemplate->getName();
  int change = newQty - oldQty;

  // Create detailed analytics entry
  std::string analyticsEntry =
      "📈 ANALYTICS: [" + source + "] " + resourceName + " changed by " +
      std::to_string(change) +
      " (value: " + std::to_string(resourceTemplate->getValue() * change) +
      " coins)";

  m_resourceLog.push_back(analyticsEntry);
  addLogEntry(analyticsEntry);

  // Keep log size manageable
  if (m_resourceLog.size() > 50) {
    m_resourceLog.erase(m_resourceLog.begin());
  }

  // In a real game, this could:
  // - Send data to analytics servers
  // - Update economy balancing metrics
  // - Track player behavior patterns
  // - Generate reports for game designers
}
