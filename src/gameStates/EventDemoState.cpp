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
#include "managers/WorldManager.hpp"
#include "utils/Camera.hpp"
#include <algorithm>
#include <ctime>
#include <iomanip>


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
  GAMESTATE_INFO("Entering EventDemoState...");

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

    // Create simple UI components using auto-detecting methods with dramatic spacing
    auto &ui = UIManager::Instance();
    ui.createTitleAtTop("event_title", "Event Demo State", 35);  // Increased title height from 25 to 35
    ui.createLabel("event_phase", {10, 25, 300, 25}, "Phase: Initialization");  // Moved up from y=45 to y=25
    ui.createLabel("event_status", {10, 60, 400, 25},  // Moved up from y=80 to y=60
                   "FPS: -- | Weather: Clear | NPCs: 0");
    ui.createLabel(
        "event_controls", {10, 95, ui.getLogicalWidth() - 20, 25},  // Moved up from y=115 to y=95
        "[B] Exit | [SPACE] Manual | [1-6] Events | [A] Auto | [R] "
        "Reset | [F] Fire | [S] Smoke | [K] Sparks | [I] Inventory");

    // Create event log component using auto-detected dimensions
    ui.createEventLog("event_log", {10, ui.getLogicalHeight() - 200, 730, 180},
                      7);
    ui.addEventLogEntry("event_log", "Event Demo System Initialized");

    // Create right-aligned inventory panel for resource demo visualization with dramatic spacing
    int windowWidth = ui.getLogicalWidth();
    int inventoryWidth = 280;
    int inventoryHeight = 400;  // Increased height dramatically
    int inventoryX = windowWidth - inventoryWidth - 20; // Right-aligned with 20px margin
    int inventoryY = 170;  // Moved down dramatically to avoid overlap

    ui.createPanel("inventory_panel",
                   {inventoryX, inventoryY, inventoryWidth, inventoryHeight});
    ui.setComponentVisible("inventory_panel", false);

    ui.createTitle("inventory_title",
                   {inventoryX + 10, inventoryY + 25, inventoryWidth - 20, 35},  // Increased height from 30 to 35
                   "Player Inventory");
    ui.setComponentVisible("inventory_title", false);

    ui.createLabel("inventory_status",
                   {inventoryX + 10, inventoryY + 75, inventoryWidth - 20, 25},  // Increased height from 20 to 25
                   "Capacity: 0/50");
    ui.setComponentVisible("inventory_status", false);

    // Create inventory list for smooth updates like GamePlayState
    ui.createList("inventory_list",
                  {inventoryX + 10, inventoryY + 110, inventoryWidth - 20, 270});  // Moved down from 95 to 110, increased height
    ui.setComponentVisible("inventory_list", false);

    // --- DATA BINDING SETUP ---
    // Bind the inventory capacity label to a function that gets the data
    ui.bindText("inventory_status", [this]() -> std::string {
        if (!m_player || !m_player->getInventory()) {
            return "Capacity: 0/0";
        }
        const auto* inventory = m_player->getInventory();
        int used = inventory->getUsedSlots();
        int max = inventory->getMaxSlots();
        return "Capacity: " + std::to_string(used) + "/" + std::to_string(max);
    });

    // Bind the inventory list to a function that gets the items, sorts them, and returns them
    ui.bindList("inventory_list", [this]() -> std::vector<std::string> {
        if (!m_player || !m_player->getInventory()) {
            return {"(Empty)"};
        }

        const auto* inventory = m_player->getInventory();
        auto allResources = inventory->getAllResources();

        if (allResources.empty()) {
            return {"(Empty)"};
        }

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

    // Initialize camera for world navigation
    initializeWorld();
    initializeCamera();

    GAMESTATE_INFO("EventDemoState initialized successfully");
    return true;

  } catch (const std::exception &e) {
    GAMESTATE_ERROR("Exception in EventDemoState::enter(): " + std::string(e.what()));
    return false;
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in EventDemoState::enter()");
    return false;
  }
}

bool EventDemoState::exit() {
  GAMESTATE_INFO("Exiting EventDemoState...");

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

    // Unregister our specific handlers via tokens
    unregisterEventHandlers();

    // Optional: leave global handlers intact for other states; no blanket clear here

    // Use AIManager's prepareForStateTransition for architectural consistency
    AIManager &aiMgr = AIManager::Instance();
    aiMgr.prepareForStateTransition();

    // Simple particle cleanup - let prepareForStateTransition handle everything
    ParticleManager &particleMgr = ParticleManager::Instance();
    if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
      particleMgr.prepareForStateTransition(); // This handles weather effects
                                               // and cleanup
    }

    // Clean up camera first to stop world rendering
    m_camera.reset();

    // Clean up UI components before world cleanup
    auto &ui = UIManager::Instance();
    ui.prepareForStateTransition();

    // Unload the world when fully exiting, but only if there's actually a world loaded
    // This matches GamePlayState's safety pattern and prevents Metal renderer crashes
    WorldManager &worldMgr = WorldManager::Instance();
    if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
      worldMgr.unloadWorld();
    }

    GAMESTATE_INFO("EventDemoState cleanup complete");
    return true;

  } catch (const std::exception &e) {
    GAMESTATE_ERROR("Exception in EventDemoState::exit(): " + std::string(e.what()));
    return false;
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in EventDemoState::exit()");
    return false;
  }
}

void EventDemoState::unregisterEventHandlers() {
  try {
    EventManager &eventMgr = EventManager::Instance();
    for (const auto &tok : m_handlerTokens) {
      (void)eventMgr.removeHandler(tok);
    }
    m_handlerTokens.clear();
  } catch (...) {
    // Swallow errors to avoid exit() failure
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

  // Update camera (follows player automatically)
  updateCamera(deltaTime);

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

void EventDemoState::render() {
  // Get renderer using the standard pattern (consistent with other states)
  auto &gameEngine = GameEngine::Instance();
  SDL_Renderer *renderer = gameEngine.getRenderer();

  // Calculate camera view rect ONCE for all rendering to ensure perfect synchronization
  HammerEngine::Camera::ViewRect cameraView{0.0f, 0.0f, 0.0f, 0.0f};
  if (m_camera) {
    cameraView = m_camera->getViewRect();
  }

  // Render world first (background layer) using unified camera position
  if (m_camera) {
    auto &worldMgr = WorldManager::Instance();
    if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
      // Use the SAME cameraView calculated above for consistency
      worldMgr.render(renderer,
                     cameraView.x,
                     cameraView.y,
                     gameEngine.getLogicalWidth(),
                     gameEngine.getLogicalHeight());
    }
  }

  // Render background particles first (rain, snow) - behind player/NPCs
  ParticleManager &particleMgr = ParticleManager::Instance();
  if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
    // Use unified camera position for perfect sync with tiles
    particleMgr.renderBackground(renderer, cameraView.x, cameraView.y);
  }

  // Render player using camera-aware rendering
  if (m_player) {
    m_player->render(m_camera.get());
  }

  // Render spawned NPCs using camera-aware rendering
  for (const auto &npc : m_spawnedNPCs) {
    if (npc) {
      npc->render(m_camera.get());
    }
  }

  // Render world-space particles using unified camera position
  if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
    particleMgr.render(renderer, cameraView.x, cameraView.y);
  }

  // Render foreground particles last (fog) - in front of player/NPCs
  if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
    particleMgr.renderForeground(renderer, cameraView.x, cameraView.y);
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
  GAMESTATE_INFO("EventDemoState: EventManager instance obtained");
  addLogEntry("EventManager singleton obtained");

  // Cache EventManager reference for better performance
  // Note: EventManager is already initialized by GameEngine
  EventManager &eventMgr = EventManager::Instance();

  GAMESTATE_INFO("EventDemoState: Using pre-initialized EventManager");
  addLogEntry("EventManager ready for use");

  // Register event handlers using token-based API for easy removal
  m_handlerTokens.push_back(
      eventMgr.registerHandlerWithToken(EventTypeId::Weather, [this](const EventData &data) {
        if (data.isActive()) onWeatherChanged("weather_changed");
      }));

  m_handlerTokens.push_back(
      eventMgr.registerHandlerWithToken(EventTypeId::NPCSpawn, [this](const EventData &data) {
        if (data.isActive()) onNPCSpawned(data);
      }));

  m_handlerTokens.push_back(
      eventMgr.registerHandlerWithToken(EventTypeId::SceneChange, [this](const EventData &data) {
        if (data.isActive()) onSceneChanged("scene_changed");
      }));

  m_handlerTokens.push_back(
      eventMgr.registerHandlerWithToken(EventTypeId::ResourceChange, [this](const EventData &data) {
        if (data.isActive()) onResourceChanged(data);
      }));

  GAMESTATE_INFO("EventDemoState: Event handlers registered");
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
      eventMgr.createWeatherEvent("demo_rainy", "Rainy", 0.1f, 3.0f);
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

  // Register per-name handlers for a few demo events to showcase the API
  m_handlerTokens.push_back(eventMgr.registerHandlerForName("demo_forest", [this](const EventData &data){ if (data.isActive()) onSceneChanged("demo_forest"); }));
  m_handlerTokens.push_back(eventMgr.registerHandlerForName("demo_rainy", [this](const EventData &data){ if (data.isActive()) onWeatherChanged("demo_rainy"); }));

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
  // Provide feedback when NPC cap reached
  else if (inputMgr.wasKeyPressed(SDL_SCANCODE_4) &&
           (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f &&
           m_spawnedNPCs.size() >= 5000) {
    addLogEntry(
        "NPC limit reached (5000) - press 'R' to reset or '5' to clear NPCs");
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

  // Inventory toggle (I key)
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_I)) {
    toggleInventoryDisplay();
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
    gameEngine.getGameStateManager()->changeState("MainMenuState");
  }

  // Mouse input for world interaction
    if (inputMgr.getMouseButtonState(LEFT) && m_camera) {
        Vector2D mousePos = inputMgr.getMousePosition();
        const auto& ui = UIManager::Instance();

        if (!ui.isClickOnUI(mousePos)) {
            Vector2D worldPos = m_camera->screenToWorld(mousePos);
            // TODO: Implement world interaction at worldPos
            // For now, we just log the coordinates
            addLogEntry("World click at: (" + std::to_string(worldPos.getX()) + ", " + std::to_string(worldPos.getY()) + ")");
        }
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

  // Use EventManager hub to change weather
  const EventManager &eventMgr = EventManager::Instance();
  if (newWeather == WeatherType::Custom && !customType.empty()) {
    // Custom type by string
    eventMgr.changeWeather(customType, m_weatherTransitionTime,
                           EventManager::DispatchMode::Deferred);
  } else {
    // Map enum to string name
    const char *wt =
        (newWeather == WeatherType::Clear)   ? "Clear" :
        (newWeather == WeatherType::Cloudy)  ? "Cloudy" :
        (newWeather == WeatherType::Rainy)   ? "Rainy" :
        (newWeather == WeatherType::Stormy)  ? "Stormy" :
        (newWeather == WeatherType::Foggy)   ? "Foggy" :
        (newWeather == WeatherType::Snowy)   ? "Snowy" :
        (newWeather == WeatherType::Windy)   ? "Windy" : "Custom";
    eventMgr.changeWeather(wt, m_weatherTransitionTime,
                           EventManager::DispatchMode::Deferred);
  }

  m_currentWeather = newWeather;
  std::string weatherName =
      customType.empty() ? getCurrentWeatherString() : customType;
  addLogEntry("Weather changed to: " + weatherName +
              " (Auto - Index: " + std::to_string(currentIndex) + ")");
}

void EventDemoState::triggerWeatherDemoManual() {
  size_t currentIndex = m_manualWeatherIndex;
  WeatherType newWeather = m_weatherSequence[m_manualWeatherIndex];
  std::string customType = m_customWeatherTypes[m_manualWeatherIndex];

  // Debug output
  addLogEntry("Manual weather index: " + std::to_string(currentIndex) + " of " +
              std::to_string(m_weatherSequence.size()));
  if (!customType.empty()) {
    addLogEntry("Using custom weather type: " + customType);
  }

  m_manualWeatherIndex = (m_manualWeatherIndex + 1) % m_weatherSequence.size();

  // Use EventManager hub to change weather
  const EventManager &eventMgr2 = EventManager::Instance();
  if (newWeather == WeatherType::Custom && !customType.empty()) {
    eventMgr2.changeWeather(customType, m_weatherTransitionTime,
                            EventManager::DispatchMode::Deferred);
  } else {
    const char *wt =
        (newWeather == WeatherType::Clear)   ? "Clear" :
        (newWeather == WeatherType::Cloudy)  ? "Cloudy" :
        (newWeather == WeatherType::Rainy)   ? "Rainy" :
        (newWeather == WeatherType::Stormy)  ? "Stormy" :
        (newWeather == WeatherType::Foggy)   ? "Foggy" :
        (newWeather == WeatherType::Snowy)   ? "Snowy" :
        (newWeather == WeatherType::Windy)   ? "Windy" : "Custom";
    eventMgr2.changeWeather(wt, m_weatherTransitionTime,
                            EventManager::DispatchMode::Deferred);
  }

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

  // Use EventManager to spawn NPC via the unified event hub
  const EventManager &eventMgr = EventManager::Instance();
  eventMgr.spawnNPC(npcType, spawnX, spawnY);
  addLogEntry("Spawned NPC: " + npcType + " at (" +
              std::to_string((int)spawnX) + ", " + std::to_string((int)spawnY) +
              ")");
}

void EventDemoState::triggerSceneTransitionDemo() {
  std::string sceneName = m_sceneNames[m_currentSceneIndex];
  m_currentSceneIndex = (m_currentSceneIndex + 1) % m_sceneNames.size();

  // Use EventManager hub to change scenes
  std::vector<TransitionType> transitions = {
      TransitionType::Fade, TransitionType::Slide, TransitionType::Dissolve,
      TransitionType::Wipe};
  TransitionType t = transitions[m_currentSceneIndex % transitions.size()];
  const char *transitionName =
      (t == TransitionType::Fade)       ? "fade" :
      (t == TransitionType::Slide)      ? "slide" :
      (t == TransitionType::Dissolve)   ? "dissolve" : "wipe";

  const EventManager &eventMgr3 = EventManager::Instance();
  eventMgr3.changeScene(sceneName, transitionName, 2.0f,
                        EventManager::DispatchMode::Deferred);

  addLogEntry("Scene transition to: " + sceneName + " (" + std::string(transitionName) +
              ") via EventManager");
}

void EventDemoState::triggerParticleEffectDemo() {
  // Get current effect and position
  std::string effectName = m_particleEffectNames[m_particleEffectIndex];
  Vector2D position = m_particleEffectPositions[m_particlePositionIndex];

  // Trigger particle effect via EventManager (deferred by default)
  const EventManager &eventMgr = EventManager::Instance();
  bool queued = eventMgr.triggerParticleEffect(effectName, position,
                                               1.2f, 5.0f, "demo_effects");
  if (queued) {
    addLogEntry("Particle effect queued: " + effectName + " at (" +
                std::to_string((int)position.getX()) + ", " +
                std::to_string((int)position.getY()) + ")");
  } else {
    addLogEntry("No particle handlers registered; effect not queued");
  }

  // Advance to next effect and position
  m_particleEffectIndex = (m_particleEffectIndex + 1) % m_particleEffectNames.size();
  m_particlePositionIndex = (m_particlePositionIndex + 1) % m_particleEffectPositions.size();
}

void EventDemoState::triggerResourceDemo() {
  if (!m_player || !m_player->getInventory()) {
    addLogEntry("Resource demo failed: Player inventory not available");
    return;
  }

  auto *inventory = m_player->getInventory();
  const auto &templateManager = ResourceTemplateManager::Instance();

  if (!templateManager.isInitialized()) {
    addLogEntry(
        "Resource demo failed: ResourceTemplateManager not initialized");
    return;
  }

  // Realistic resource discovery: Query system for different categories/types
  ResourcePtr selectedResource = nullptr;
  std::string discoveryMethod;
  int quantity = 1;

  switch (m_resourceDemonstrationStep % 6) {
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
                std::to_string(m_resourceDemonstrationStep));
    m_resourceDemonstrationStep++;
    return;
  }

  // Use the discovered resource
  auto handle = selectedResource->getHandle();
  std::string resourceName = selectedResource->getName();
  int currentQuantity = inventory->getResourceQuantity(handle);

  if (m_resourceIsAdding) {
    // Add resources to player inventory
    addLogEntry("BEFORE ADD (" + discoveryMethod + "): " + resourceName +
                " = " + std::to_string(currentQuantity));

    bool success = inventory->addResource(handle, quantity);
    if (success) {
      int newQuantity = inventory->getResourceQuantity(handle);
      addLogEntry("AFTER ADD: " + resourceName + " = " +
                  std::to_string(newQuantity) + " (+" +
                  std::to_string(quantity) + ")");

      // Trigger resource change via EventManager (deferred by default)
      const EventManager &eventMgr = EventManager::Instance();
      eventMgr.triggerResourceChange(m_player, handle, currentQuantity,
                                     newQuantity, "event_demo");

      addLogEntry("ResourceChangeEvent queued for ADD operation");
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

        // Trigger resource change via EventManager (deferred by default)
        const EventManager &eventMgr = EventManager::Instance();
        eventMgr.triggerResourceChange(m_player, handle, currentQuantity,
                                       newQuantity, "event_demo");

        addLogEntry("ResourceChangeEvent queued for REMOVE operation");
      } else {
        addLogEntry("FAILED to remove " + resourceName + " from inventory");
      }
    } else {
      addLogEntry("No " + resourceName + " to remove from inventory");
    }
  }

  // Progress through different discovery patterns and alternate between adding
  // and removing
  m_resourceDemonstrationStep++;
  if (m_resourceDemonstrationStep % 6 == 0) {
    m_resourceIsAdding = !m_resourceIsAdding; // Switch between adding and removing after full cycle
    std::string mode = m_resourceIsAdding ? "ADDING" : "REMOVING";
    addLogEntry("--- Switched to " + mode + " mode (completed " +
                std::to_string(m_resourceDemonstrationStep / 6) + " cycles) ---");
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

  m_convenienceDemoCounter++;
  std::string suffix = std::to_string(m_convenienceDemoCounter);

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

    // Trigger via EventManager for demonstration
    eventMgr.changeWeather("Foggy", 2.5f, EventManager::DispatchMode::Deferred);

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

  // Trigger clear weather via EventManager
  EventManager::Instance().changeWeather("Clear", 1.0f,
                                         EventManager::DispatchMode::Deferred);

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

void EventDemoState::onNPCSpawned(const EventData &data) {
  try {
    if (!data.event) {
      addLogEntry("Error: NPCSpawnEvent data is null");
      return;
    }

    auto npcEvent = std::dynamic_pointer_cast<NPCSpawnEvent>(data.event);
    if (!npcEvent) {
      addLogEntry("Error: Event is not an NPCSpawnEvent");
      return;
    }

    const auto &params = npcEvent->getSpawnParameters();
    std::string npcType = params.npcType.empty() ? std::string("NPC") : params.npcType;
    int count = std::max(1, params.count);

    // Determine spawn anchors: event-provided points or player position
    std::vector<Vector2D> anchors = npcEvent->getSpawnPoints();
    if (anchors.empty()) {
      Vector2D fallback = m_player ? m_player->getPosition()
                                   : Vector2D(m_worldWidth * 0.5f, m_worldHeight * 0.5f);
      anchors.push_back(fallback);
    }

    // Deterministic offset pattern based on radius/count for visible spread
    float base = (params.spawnRadius > 0.0f) ? params.spawnRadius : 30.0f;
    float stepX = 0.6f * base + 20.0f;
    float stepY = 0.4f * base + 15.0f;

    int spawned = 0;
    AIManager &aiMgr = AIManager::Instance();

    for (const auto &anchor : anchors) {
      for (int i = 0; i < count; ++i) {
        float offX = ((spawned % 8) - 4) * stepX;
        float offY = (((spawned / 8) % 6) - 3) * stepY;

        float x = std::clamp(anchor.getX() + offX, 100.0f, m_worldWidth - 100.0f);
        float y = std::clamp(anchor.getY() + offY, 100.0f, m_worldHeight - 100.0f);

        auto npc = createNPCAtPositionWithoutBehavior(npcType, x, y);
        if (npc) {
          std::string behavior = params.aiBehavior.empty()
                                     ? determineBehaviorForNPCType(npcType)
                                     : params.aiBehavior;
          aiMgr.registerEntityForUpdates(npc, rand() % 9 + 1, behavior);
          spawned++;
        }
      }
    }

    addLogEntry("NPCSpawn handled: " + npcType + " x" + std::to_string(spawned));
  } catch (const std::exception &e) {
    addLogEntry(std::string("Error in NPC spawn handler: ") + e.what());
  }
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
  //TODO: need to make sure that this loigic is moved out of the gamestate. Maybe on AI Manager init. init/configure all availible behviors
  GAMESTATE_INFO("EventDemoState: Setting up AI behaviors for NPC integration...");
  // Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();

  if (!aiMgr.hasBehavior("Wander")) {
    auto wanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::MEDIUM_AREA, 80.0f);
    wanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("Wander", std::move(wanderBehavior));
    GAMESTATE_INFO("EventDemoState: Registered Wander behavior");
  }

  if (!aiMgr.hasBehavior("SmallWander")) {
    auto smallWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::SMALL_AREA, 60.0f);
    smallWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("SmallWander", std::move(smallWanderBehavior));
    GAMESTATE_INFO("EventDemoState: Registered SmallWander behavior");
  }

  if (!aiMgr.hasBehavior("LargeWander")) {
    auto largeWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::LARGE_AREA, 100.0f);
    largeWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("LargeWander", std::move(largeWanderBehavior));
    GAMESTATE_INFO("EventDemoState: Registered LargeWander behavior");
  }

  if (!aiMgr.hasBehavior("EventWander")) {
    auto eventWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::EVENT_TARGET, 70.0f);
    eventWanderBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("EventWander", std::move(eventWanderBehavior));
    GAMESTATE_INFO("EventDemoState: Registered EventWander behavior");
  }

  if (!aiMgr.hasBehavior("Patrol")) {
    auto patrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 75.0f, true);
    patrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("Patrol", std::move(patrolBehavior));
    GAMESTATE_INFO("EventDemoState: Registered Patrol behavior");
  }

  if (!aiMgr.hasBehavior("RandomPatrol")) {
    auto randomPatrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::RANDOM_AREA, 85.0f, false);
    randomPatrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("RandomPatrol", std::move(randomPatrolBehavior));
    GAMESTATE_INFO("EventDemoState: Registered RandomPatrol behavior");
  }

  if (!aiMgr.hasBehavior("CirclePatrol")) {
    auto circlePatrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::CIRCULAR_AREA, 90.0f, false);
    circlePatrolBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("CirclePatrol", std::move(circlePatrolBehavior));
    GAMESTATE_INFO("EventDemoState: Registered CirclePatrol behavior");
  }

  if (!aiMgr.hasBehavior("EventTarget")) {
    auto eventTargetBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::EVENT_TARGET, 95.0f, false);
    eventTargetBehavior->setScreenDimensions(m_worldWidth, m_worldHeight);
    aiMgr.registerBehavior("EventTarget", std::move(eventTargetBehavior));
    GAMESTATE_INFO("EventDemoState: Registered EventTarget behavior");
  }

  if (!aiMgr.hasBehavior("Chase")) {
    auto chaseBehavior = std::make_unique<ChaseBehavior>(120.0f, 500.0f, 50.0f);
    aiMgr.registerBehavior("Chase", std::move(chaseBehavior));
    GAMESTATE_INFO("EventDemoState: Chase behavior registered (will use AIManager::getPlayerReference())");
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
    GAMESTATE_ERROR("EXCEPTION in createNPCAtPositionWithoutBehavior: " + std::string(e.what()));
    return nullptr;
  } catch (...) {
    GAMESTATE_ERROR("UNKNOWN EXCEPTION in createNPCAtPositionWithoutBehavior");
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

    // Also log to console for debugging (avoid flushing)
    GAMESTATE_DEBUG("EventDemo: " + timestampedEntry);
  } catch (const std::exception &e) {
    GAMESTATE_ERROR("Error adding log entry: " + std::string(e.what()));
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
    GAMESTATE_ERROR("EXCEPTION in createNPCAtPosition: " + std::string(e.what()) +
                    ", NPC type: " + npcType + ", position: (" + std::to_string(x) + ", " + std::to_string(y) + ")");
  } catch (...) {
    GAMESTATE_ERROR(std::string("UNKNOWN EXCEPTION in createNPCAtPosition") + ", NPC type: " + npcType + ", position: (" + std::to_string(x) + ", " + std::to_string(y) + ")");
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

void EventDemoState::initializeWorld() {
  // Create world manager and generate a world for event demo
  WorldManager& worldManager = WorldManager::Instance();

  // Create a moderately-sized world configuration for event demo (focused on events, but with exploration)
  HammerEngine::WorldGenerationConfig config;
  config.width = 100;  // Increased from 50 to 100 for more exploration
  config.height = 100; // Increased from 50 to 100 for more exploration
  config.seed = static_cast<int>(std::time(nullptr)); // Random seed for variety
  config.elevationFrequency = 0.1f;
  config.humidityFrequency = 0.1f;
  config.waterLevel = 0.25f;
  config.mountainLevel = 0.75f;

  if (!worldManager.loadNewWorld(config)) {
    GAMESTATE_ERROR("Failed to load new world in EventDemoState");
    // Continue anyway - event demo can function without world
  } else {
    GAMESTATE_INFO("Successfully loaded event demo world with seed: " + std::to_string(config.seed));

    // Setup camera to work with the world (will be called in initializeCamera)
  }
}

void EventDemoState::initializeCamera() {
  const auto &gameEngine = GameEngine::Instance();

  // Initialize camera at player's position to avoid any interpolation jitter
  Vector2D playerPosition = m_player ? m_player->getPosition() : Vector2D(0, 0);

  // Create camera starting at player position
  m_camera = std::make_unique<HammerEngine::Camera>(
    playerPosition.getX(), playerPosition.getY(), // Start at player position
    static_cast<float>(gameEngine.getLogicalWidth()),
    static_cast<float>(gameEngine.getLogicalHeight())
  );

  // Configure camera to follow player
  if (m_player && m_camera) {
    // Match GamePlayState: disable camera event firing for consistency
    m_camera->setEventFiringEnabled(false);

    // Set target and enable follow mode
    std::weak_ptr<Entity> playerAsEntity = std::static_pointer_cast<Entity>(m_player);
    m_camera->setTarget(playerAsEntity);
    m_camera->setMode(HammerEngine::Camera::Mode::Follow);

    // Set up camera configuration for fast, smooth following (match GamePlayState)
    HammerEngine::Camera::Config config;
    config.followSpeed = 8.0f;         // Faster follow for action gameplay
    config.deadZoneRadius = 0.0f;      // No dead zone - always follow
    config.smoothingFactor = 0.80f;    // Quicker response smoothing
    config.maxFollowDistance = 9999.0f; // No distance limit
    config.clampToWorldBounds = true; // Keep camera within world
    m_camera->setConfig(config);

    // Set up world bounds for demo
    setupCameraForWorld();
  }
}

void EventDemoState::updateCamera(float deltaTime) {
  if (m_camera) {
    m_camera->update(deltaTime);
  }
}

void EventDemoState::setupCameraForWorld() {
  if (!m_camera) {
    return;
  }

  // Get actual world bounds from WorldManager
  const WorldManager& worldManager = WorldManager::Instance();

  HammerEngine::Camera::Bounds worldBounds;
  float minX, minY, maxX, maxY;

  if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
    // Convert tile coordinates to pixel coordinates (WorldManager returns tile coords)
    // TileRenderer uses 32px per tile
    const float TILE_SIZE = 32.0f;
    worldBounds.minX = minX * TILE_SIZE;
    worldBounds.minY = minY * TILE_SIZE;
    worldBounds.maxX = maxX * TILE_SIZE;
    worldBounds.maxY = maxY * TILE_SIZE;
  } else {
    // Fall back to demo world dimensions if no world is loaded
    // EventDemoState uses 100x100 tiles
    const float TILE_SIZE = 32.0f;
    worldBounds.minX = 0.0f;
    worldBounds.minY = 0.0f;
    worldBounds.maxX = 100.0f * TILE_SIZE;  // 100 tiles * 32px = 3200px
    worldBounds.maxY = 100.0f * TILE_SIZE;  // 100 tiles * 32px = 3200px
  }

  m_camera->setWorldBounds(worldBounds);
}

void EventDemoState::applyCameraTransformation() {
  if (!m_camera) {
    return;
  }

  // Calculate camera offset for later use in rendering
  auto viewRect = m_camera->getViewRect();
  m_cameraOffsetX = viewRect.x;
  m_cameraOffsetY = viewRect.y;
}

void EventDemoState::toggleInventoryDisplay() {
  auto &ui = UIManager::Instance();
  m_showInventory = !m_showInventory;

  ui.setComponentVisible("inventory_panel", m_showInventory);
  ui.setComponentVisible("inventory_title", m_showInventory);
  ui.setComponentVisible("inventory_status", m_showInventory);
  ui.setComponentVisible("inventory_list", m_showInventory);

  addLogEntry("Inventory " + std::string(m_showInventory ? "shown" : "hidden"));
}
