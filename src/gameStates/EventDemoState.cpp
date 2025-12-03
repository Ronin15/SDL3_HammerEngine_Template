/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/EventDemoState.hpp"
#include "gameStates/LoadingState.hpp"
#include "SDL3/SDL_scancode.h"
#include "ai/behaviors/ChaseBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/WanderBehavior.hpp"
#include "core/GameEngine.hpp"
#include "core/GameTime.hpp"
#include "core/Logger.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "events/SceneChangeEvent.hpp"
#include "events/WeatherEvent.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/ParticleManager.hpp"
#include "managers/PathfinderManager.hpp"
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

  // Reset transition flag when entering state
  m_transitioningToLoading = false;

  // Check if already initialized (resuming after LoadingState)
  if (m_initialized) {
    GAMESTATE_INFO("Already initialized - resuming EventDemoState");
    return true;  // Skip all loading logic
  }

  // Check if world needs to be loaded
  if (!m_worldLoaded) {
    GAMESTATE_INFO("World not loaded yet - will transition to LoadingState on first update");
    m_needsLoading = true;
    m_worldLoaded = true;  // Mark as loaded to prevent loop on re-entry
    return true;  // Will transition to loading screen in update()
  }

  // World is loaded - proceed with normal initialization
  GAMESTATE_INFO("World already loaded - initializing event demo");

  try {
    // Cache GameEngine reference for better performance
    const GameEngine &gameEngine = GameEngine::Instance();
    auto& worldManager = WorldManager::Instance();

    // Update world dimensions from loaded world
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
      m_worldWidth = std::max(0.0f, maxX - minX);
      m_worldHeight = std::max(0.0f, maxY - minY);
      GAMESTATE_INFO("World dimensions: " + std::to_string(m_worldWidth) + " x " + std::to_string(m_worldHeight) + " pixels");
    } else {
      // Fallback to screen dimensions if world bounds unavailable
      m_worldWidth = gameEngine.getLogicalWidth();
      m_worldHeight = gameEngine.getLogicalHeight();
    }

    // Initialize event system
    setupEventSystem();

    // Create player
    m_player = std::make_shared<Player>();
    m_player->ensurePhysicsBodyRegistered();
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
    ui.createTitleAtTop("event_title", "Event Demo State", UIConstants::DEFAULT_TITLE_HEIGHT);  // Use standard title height (auto-repositions)

    ui.createLabel("event_phase", {UIConstants::INFO_LABEL_MARGIN_X, UIConstants::INFO_FIRST_LINE_Y, 300, UIConstants::INFO_LABEL_HEIGHT}, "Phase: Initialization");
    // Set auto-repositioning: top-aligned with calculated position (fixes fullscreen transition)
    ui.setComponentPositioning("event_phase", {UIPositionMode::TOP_ALIGNED, UIConstants::INFO_LABEL_MARGIN_X, UIConstants::INFO_FIRST_LINE_Y, 300, UIConstants::INFO_LABEL_HEIGHT});

    const int statusY = UIConstants::INFO_FIRST_LINE_Y + UIConstants::INFO_LABEL_HEIGHT + UIConstants::INFO_LINE_SPACING;
    ui.createLabel("event_status", {UIConstants::INFO_LABEL_MARGIN_X, statusY, 400, UIConstants::INFO_LABEL_HEIGHT},
                   "FPS: -- | Weather: Clear | NPCs: 0");
    // Set auto-repositioning: top-aligned with calculated position (fixes fullscreen transition)
    ui.setComponentPositioning("event_status", {UIPositionMode::TOP_ALIGNED, UIConstants::INFO_LABEL_MARGIN_X, statusY, 400, UIConstants::INFO_LABEL_HEIGHT});

    const int controlsY = statusY + UIConstants::INFO_LABEL_HEIGHT + UIConstants::INFO_LINE_SPACING + UIConstants::INFO_STATUS_SPACING;
    ui.createLabel(
        "event_controls", {UIConstants::INFO_LABEL_MARGIN_X, controlsY, ui.getLogicalWidth() - 2*UIConstants::INFO_LABEL_MARGIN_X, UIConstants::INFO_LABEL_HEIGHT},
        "[B] Exit | [SPACE] Manual | [1-6] Events | [A] Auto | [R] "
        "Reset | [F] Fire | [S] Smoke | [K] Sparks | [I] Inventory | [ ] Zoom");
    // Set auto-repositioning: top-aligned, spans full width minus margins
    ui.setComponentPositioning("event_controls", {UIPositionMode::TOP_ALIGNED, UIConstants::INFO_LABEL_MARGIN_X, controlsY,
                                                  -2*UIConstants::INFO_LABEL_MARGIN_X, UIConstants::INFO_LABEL_HEIGHT});

    // Create event log component using auto-detected dimensions
    ui.createEventLog("event_log", {10, ui.getLogicalHeight() - 200, 730, 180},
                      7);
    // Set auto-repositioning: anchored to bottom with percentage-based width for responsiveness
    UIPositioning eventLogPos;
    eventLogPos.mode = UIPositionMode::BOTTOM_ALIGNED;
    eventLogPos.offsetX = 10;
    eventLogPos.offsetY = 20;
    eventLogPos.fixedHeight = 180;
    eventLogPos.widthPercent = UIConstants::EVENT_LOG_WIDTH_PERCENT;
    ui.setComponentPositioning("event_log", eventLogPos);

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
    // Set auto-repositioning: right-aligned with fixed offsetY
    ui.setComponentPositioning("inventory_panel", {UIPositionMode::RIGHT_ALIGNED, 20, inventoryY - (ui.getLogicalHeight() - inventoryHeight) / 2, inventoryWidth, inventoryHeight});

    ui.createTitle("inventory_title",
                   {inventoryX + 10, inventoryY + 25, inventoryWidth - 20, 35},  // Increased height from 30 to 35
                   "Player Inventory");
    ui.setComponentVisible("inventory_title", false);
    // Children will be repositioned in onWindowResize based on panel position

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

    // Initialize camera for world navigation (world is already loaded by LoadingState)
    initializeCamera();

    // Mark as fully initialized to prevent re-entering loading logic
    m_initialized = true;

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
    if (m_transitioningToLoading) {
      // Transitioning to LoadingState - do cleanup but preserve m_worldLoaded flag
      // This prevents infinite loop when returning from LoadingState

      // Reset the flag after using it
      m_transitioningToLoading = false;

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

      // Remove all events from EventManager
      EventManager::Instance().clearAllEvents();

      // Clean up managers (same as full exit)
      AIManager &aiMgr = AIManager::Instance();
      aiMgr.prepareForStateTransition();

      CollisionManager &collisionMgr = CollisionManager::Instance();
      if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
        collisionMgr.prepareForStateTransition();
      }

      PathfinderManager &pathfinderMgr = PathfinderManager::Instance();
      if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
        pathfinderMgr.prepareForStateTransition();
      }

      ParticleManager &particleMgr = ParticleManager::Instance();
      if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
        particleMgr.prepareForStateTransition();
      }

      // Clean up camera
      m_camera.reset();

      // Clean up UI
      auto &ui = UIManager::Instance();
      ui.prepareForStateTransition();

      // Unload world (LoadingState will reload it)
      WorldManager &worldMgr = WorldManager::Instance();
      if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
        worldMgr.unloadWorld();
        // CRITICAL: DO NOT reset m_worldLoaded here - keep it true to prevent infinite loop
        // when LoadingState returns to this state
      }

      // Reset initialized flag so state re-initializes after loading
      m_initialized = false;

      // Keep m_worldLoaded = true to remember we've already been through loading
      GAMESTATE_INFO("EventDemoState cleanup for LoadingState transition complete");
      return true;
    }

    // Full exit (going to main menu, other states, or shutting down)

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

    // Remove all events from EventManager
    EventManager::Instance().clearAllEvents();

    // Optional: leave global handlers intact for other states; no blanket clear here

    // Use manager prepareForStateTransition methods for deterministic cleanup
    AIManager &aiMgr = AIManager::Instance();
    aiMgr.prepareForStateTransition();

    // Clean collision state before other systems
    CollisionManager &collisionMgr = CollisionManager::Instance();
    if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
      collisionMgr.prepareForStateTransition();
    }

    // Clean pathfinding state for fresh start
    PathfinderManager &pathfinderMgr = PathfinderManager::Instance();
    if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
      pathfinderMgr.prepareForStateTransition();
    }

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
      // Reset m_worldLoaded when doing full exit (going to main menu, etc.)
      m_worldLoaded = false;
    }

    // Reset initialization flag for next fresh start
    m_initialized = false;

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

void EventDemoState::onWindowResize(int newLogicalWidth,
                                     int newLogicalHeight) {
  // Recalculate UI positions based on new window dimensions
  auto &ui = UIManager::Instance();

  // Reposition inventory panel (matches initializeWorld pattern)
  const int inventoryWidth = 280;
  const int inventoryHeight = 400;
  const int inventoryX = newLogicalWidth - inventoryWidth - 20;
  const int inventoryY = 170;

  ui.setComponentBounds("inventory_panel",
                        {inventoryX, inventoryY, inventoryWidth,
                         inventoryHeight});

  ui.setComponentBounds("inventory_title",
                        {inventoryX + 10, inventoryY + 25,
                         inventoryWidth - 20, 35});

  ui.setComponentBounds("inventory_status",
                        {inventoryX + 10, inventoryY + 75,
                         inventoryWidth - 20, 25});

  ui.setComponentBounds("inventory_list",
                        {inventoryX + 10, inventoryY + 110,
                         inventoryWidth - 20, 270});

  GAMESTATE_DEBUG("EventDemoState: Repositioned UI for new window size: " +
                  std::to_string(newLogicalWidth) + "x" +
                  std::to_string(newLogicalHeight));
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
  // Check if we need to transition to loading screen (do this in update, not enter)
  if (m_needsLoading) {
    m_needsLoading = false;  // Clear flag

    GAMESTATE_INFO("Transitioning to LoadingState for world generation");

    // Create world configuration for event demo (HUGE world)
    HammerEngine::WorldGenerationConfig config;
    config.width = 500;  // Massive 500x500 world
    config.height = 500;
    config.seed = static_cast<int>(std::time(nullptr));
    config.elevationFrequency = 0.05f;
    config.humidityFrequency = 0.03f;
    config.waterLevel = 0.3f;
    config.mountainLevel = 0.7f;

    // Configure LoadingState and transition to it
    const auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    if (gameStateManager) {
      auto* loadingState = dynamic_cast<LoadingState*>(gameStateManager->getState("LoadingState").get());
      if (loadingState) {
        loadingState->configure("EventDemo", config);
        // Set flag before transitioning to preserve m_worldLoaded in exit()
        m_transitioningToLoading = true;
        // Use changeState (called from update) to properly exit and re-enter
        gameStateManager->changeState("LoadingState");
      } else {
        GAMESTATE_ERROR("LoadingState not found in GameStateManager");
      }
    }

    return;  // Don't continue with rest of update
  }

  // Update game time (advances calendar, dispatches time events)
  GameTime::Instance().update(deltaTime);

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

  // Note: NPC cleanup is handled by AIManager::prepareForStateTransition() in exit()
  // Attempting cleanup here causes undefined behavior by calling AIManager methods on null pointers

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
        addLogEntry("Weather demo started");
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

          if (m_weatherChangesShown >= m_weatherSequence.size()) {
            m_weatherDemoComplete = true;
            addLogEntry("Weather demo complete");
          }
        } else {
          m_weatherDemoComplete = true;
        }
      }
      if (m_weatherDemoComplete && m_phaseTimer >= 2.0f) {
        m_currentPhase = DemoPhase::NPCSpawnDemo;
        m_phaseTimer = 0.0f;
        addLogEntry("NPC spawn demo");
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
        addLogEntry("Scene transition demo");
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
        addLogEntry("Particle effect demo");
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
        addLogEntry("Resource demo");
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
        addLogEntry("Custom event demo");
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
        addLogEntry("Interactive mode (1-5 for events)");
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

  // Set render scale for zoom (scales all world/entity rendering automatically)
  float zoom = m_camera ? m_camera->getZoom() : 1.0f;
  SDL_SetRenderScale(renderer, zoom, zoom);

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

  // Reset render scale to 1.0 for UI rendering (UI should not be zoomed)
  SDL_SetRenderScale(renderer, 1.0f, 1.0f);

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

  // Cache EventManager reference for better performance
  // Note: EventManager is already initialized by GameEngine
  EventManager &eventMgr = EventManager::Instance();

  GAMESTATE_INFO("EventDemoState: Using pre-initialized EventManager");

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
  addLogEntry("Event system ready");
}

void EventDemoState::createTestEvents() {

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

  if (successCount == 11) {
    addLogEntry("Created 11 demo events");
  } else {
    addLogEntry("Created " + std::to_string(successCount) + "/11 events");
  }
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
    m_lastEventTriggerTime = m_totalDemoTime;
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_2) &&
      (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f &&
      m_spawnedNPCs.size() < 5000) {
    if (m_autoMode && m_currentPhase == DemoPhase::NPCSpawnDemo) {
      m_phaseTimer = 0.0f;
    }
    triggerNPCSpawnDemo();
    m_lastEventTriggerTime = m_totalDemoTime;
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_3) &&
      (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
    if (m_autoMode && m_currentPhase == DemoPhase::SceneTransitionDemo) {
      m_phaseTimer = 0.0f;
    }
    triggerSceneTransitionDemo();
    m_lastEventTriggerTime = m_totalDemoTime;
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_4) &&
      (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f &&
      m_spawnedNPCs.size() < 5000) {
    if (m_autoMode && m_currentPhase == DemoPhase::CustomEventDemo) {
      m_phaseTimer = 0.0f;
    }
    triggerCustomEventDemo();
    m_lastEventTriggerTime = m_totalDemoTime;
  }
  // Provide feedback when NPC cap reached
  else if (inputMgr.wasKeyPressed(SDL_SCANCODE_4) &&
           (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f &&
           m_spawnedNPCs.size() >= 5000) {
    addLogEntry("NPC limit (R to reset)");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_5)) {
    resetAllEvents();
    addLogEntry("Events reset");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_6) &&
      (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
    if (m_autoMode && m_currentPhase == DemoPhase::ResourceDemo) {
      m_phaseTimer = 0.0f;
    }
    triggerResourceDemo();
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
    addLogEntry("Demo reset");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_C) &&
      (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
    triggerConvenienceMethodsDemo();
    m_lastEventTriggerTime = m_totalDemoTime;
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_A)) {
    m_autoMode = !m_autoMode;
    addLogEntry(m_autoMode ? "Auto mode ON" : "Auto mode OFF");
  }

  // Cache ParticleManager reference for better performance
  ParticleManager &particleMgr = ParticleManager::Instance();

  // Fire effect toggle (F key)
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_F)) {
    particleMgr.toggleFireEffect();
    addLogEntry("Fire toggled");
  }

  // Smoke effect toggle (S key)
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_S)) {
    particleMgr.toggleSmokeEffect();
    addLogEntry("Smoke toggled");
  }

  // Sparks effect toggle (K key)
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_K)) {
    particleMgr.toggleSparksEffect();
    addLogEntry("Sparks toggled");
  }

  // Inventory toggle (I key)
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_I)) {
    toggleInventoryDisplay();
  }

  // Camera zoom controls
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_LEFTBRACKET) && m_camera) {
    m_camera->zoomIn();  // [ key = zoom in (objects larger)
  }
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_RIGHTBRACKET) && m_camera) {
    m_camera->zoomOut();  // ] key = zoom out (objects smaller)
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
    gameEngine.getGameStateManager()->changeState("MainMenuState");
  }

  // Mouse input for world interaction
    if (inputMgr.getMouseButtonState(LEFT) && m_camera) {
        Vector2D mousePos = inputMgr.getMousePosition();
        const auto& ui = UIManager::Instance();

        if (!ui.isClickOnUI(mousePos)) {
            // World interaction at mouse position
            // Currently unused - world click coordinates available via m_camera->screenToWorld(mousePos)
            (void)m_camera->screenToWorld(mousePos);
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
  addLogEntry("Weather: " + weatherName + " (auto)");
}

void EventDemoState::triggerWeatherDemoManual() {
  size_t currentIndex = m_manualWeatherIndex;
  WeatherType newWeather = m_weatherSequence[m_manualWeatherIndex];
  std::string customType = m_customWeatherTypes[m_manualWeatherIndex];

  // Suppress unused variable warning - currentIndex used for internal tracking
  (void)currentIndex;

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
  addLogEntry("Weather: " + weatherName + " (manual)");
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
  addLogEntry("Spawned: " + npcType);
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

  addLogEntry("Scene: " + sceneName + " (" + std::string(transitionName) + ")");
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
    addLogEntry("Particle: " + effectName);
  } else {
    addLogEntry("Particle: no handler");
  }

  // Advance to next effect and position
  m_particleEffectIndex = (m_particleEffectIndex + 1) % m_particleEffectNames.size();
  m_particlePositionIndex = (m_particlePositionIndex + 1) % m_particleEffectPositions.size();
}

void EventDemoState::triggerResourceDemo() {
  if (!m_player || !m_player->getInventory()) {
    addLogEntry("Resource: no inventory");
    return;
  }

  auto *inventory = m_player->getInventory();
  const auto &templateManager = ResourceTemplateManager::Instance();

  if (!templateManager.isInitialized()) {
    addLogEntry("Resource: not initialized");
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
    addLogEntry("Resource: not found");
    m_resourceDemonstrationStep++;
    return;
  }

  // Use the discovered resource
  auto handle = selectedResource->getHandle();
  std::string resourceName = selectedResource->getName();
  int currentQuantity = inventory->getResourceQuantity(handle);

  if (m_resourceIsAdding) {
    // Add resources to player inventory
    bool success = inventory->addResource(handle, quantity);
    if (success) {
      int newQuantity = inventory->getResourceQuantity(handle);
      addLogEntry("+" + std::to_string(quantity) + " " + resourceName +
                  " (" + std::to_string(newQuantity) + " total)");

      // Trigger resource change via EventManager (deferred by default)
      const EventManager &eventMgr = EventManager::Instance();
      eventMgr.triggerResourceChange(m_player, handle, currentQuantity,
                                     newQuantity, "event_demo");
    } else {
      addLogEntry("Failed: " + resourceName + " (full)");
    }
  } else {
    // Remove resources from player inventory
    int removeQuantity = std::min(quantity, currentQuantity);

    if (removeQuantity > 0) {
      bool success = inventory->removeResource(handle, removeQuantity);
      if (success) {
        int newQuantity = inventory->getResourceQuantity(handle);
        addLogEntry("-" + std::to_string(removeQuantity) + " " + resourceName +
                    " (" + std::to_string(newQuantity) + " left)");

        // Trigger resource change via EventManager (deferred by default)
        const EventManager &eventMgr = EventManager::Instance();
        eventMgr.triggerResourceChange(m_player, handle, currentQuantity,
                                       newQuantity, "event_demo");
      } else {
        addLogEntry("Failed: remove " + resourceName);
      }
    } else {
      addLogEntry("No " + resourceName + " to remove");
    }
  }

  // Progress through different discovery patterns and alternate between adding
  // and removing
  m_resourceDemonstrationStep++;
  if (m_resourceDemonstrationStep % 6 == 0) {
    m_resourceIsAdding = !m_resourceIsAdding; // Switch between adding and removing after full cycle
    std::string mode = m_resourceIsAdding ? "Adding" : "Removing";
    addLogEntry("Mode: " + mode);
  }
}

void EventDemoState::triggerCustomEventDemo() {
  addLogEntry("Custom event demo");

  triggerWeatherDemoManual();

  if (m_spawnedNPCs.size() >= 5000) {
    addLogEntry("NPC limit reached (5000)");
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
    addLogEntry(npcType1 + ": " + behaviorName1 + " queued");
  }

  if (npc2) {
    std::string behaviorName2 = determineBehaviorForNPCType(npcType2);
    aiMgr.registerEntityForUpdates(npc2, rand() % 9 + 1, behaviorName2);
    addLogEntry(npcType2 + ": " + behaviorName2 + " queued");
  }

  addLogEntry("Spawned: " + npcType1 + ", " + npcType2 +
              " (" + std::to_string(m_spawnedNPCs.size()) + " total)");
}

void EventDemoState::triggerConvenienceMethodsDemo() {
  addLogEntry("Convenience methods demo");

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
    addLogEntry("Created 6 events successfully");

    // Trigger via EventManager for demonstration
    eventMgr.changeWeather("Foggy", 2.5f, EventManager::DispatchMode::Deferred);

    m_currentWeather = WeatherType::Foggy;
    addLogEntry("Weather: Foggy (demo)");
  } else {
    addLogEntry("Created " + std::to_string(successCount) + "/6 events");
  }
}

void EventDemoState::resetAllEvents() {
  cleanupSpawnedNPCs();

  // Remove all events from EventManager
  EventManager &eventMgr = EventManager::Instance();
  eventMgr.clearAllEvents();

  // Trigger clear weather via EventManager
  eventMgr.changeWeather("Clear", 1.0f,
                         EventManager::DispatchMode::Deferred);

  m_currentWeather = WeatherType::Clear;

  m_currentWeatherIndex = 0;
  m_currentNPCTypeIndex = 0;
  m_currentSceneIndex = 0;

  m_lastEventTriggerTime = 0.0f;
  m_limitMessageShown = false;

  addLogEntry("Events cleared");
}

void EventDemoState::onWeatherChanged(const std::string &message) {
  // Weather handler triggered - message logged via GAMESTATE_DEBUG
  (void)message;
}

void EventDemoState::onNPCSpawned(const EventData &data) {
  try {
    if (!data.event) {
      GAMESTATE_ERROR("NPCSpawnEvent data is null");
      return;
    }

    auto npcEvent = std::dynamic_pointer_cast<NPCSpawnEvent>(data.event);
    if (!npcEvent) {
      GAMESTATE_ERROR("Event is not an NPCSpawnEvent");
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

    addLogEntry("Spawned: " + npcType + " x" + std::to_string(spawned));
  } catch (const std::exception &e) {
    GAMESTATE_ERROR("NPC spawn handler: " + std::string(e.what()));
  }
}

void EventDemoState::onSceneChanged(const std::string &message) {
  addLogEntry("Scene: " + message);
}

void EventDemoState::onResourceChanged(const EventData &data) {
  // Extract ResourceChangeEvent from EventData
  try {
    if (!data.event) {
      GAMESTATE_ERROR("ResourceChangeEvent data is null");
      return;
    }

    // Cast to ResourceChangeEvent to access its data
    auto resourceEvent =
        std::dynamic_pointer_cast<ResourceChangeEvent>(data.event);
    if (!resourceEvent) {
      GAMESTATE_ERROR("Event is not a ResourceChangeEvent");
      return;
    }

    // Get the resource change data
    HammerEngine::ResourceHandle handle = resourceEvent->getResourceHandle();
    int oldQty = resourceEvent->getOldQuantity();
    int newQty = resourceEvent->getNewQuantity();
    std::string source = resourceEvent->getChangeReason();

    // Process different aspects of resource changes
    processResourceAchievements(handle, oldQty, newQty);
    checkResourceWarnings(handle, newQty);
    logResourceAnalytics(handle, oldQty, newQty, source);
  } catch (const std::exception &e) {
    GAMESTATE_ERROR("Error in resource change handler: " + std::string(e.what()));
  }
}

void EventDemoState::setupAIBehaviors() {
  //TODO: need to make sure that this loigic is moved out of the gamestate. Maybe on AI Manager init. init/configure all availible behviors
  GAMESTATE_INFO("EventDemoState: Setting up AI behaviors for NPC integration...");
  // Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();

  if (!aiMgr.hasBehavior("Wander")) {
    auto wanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::MEDIUM_AREA, 60.0f);
    aiMgr.registerBehavior("Wander", std::move(wanderBehavior));
    GAMESTATE_INFO("EventDemoState: Registered Wander behavior");
  }

  if (!aiMgr.hasBehavior("SmallWander")) {
    auto smallWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::SMALL_AREA, 45.0f);
    aiMgr.registerBehavior("SmallWander", std::move(smallWanderBehavior));
    GAMESTATE_INFO("EventDemoState: Registered SmallWander behavior");
  }

  if (!aiMgr.hasBehavior("LargeWander")) {
    auto largeWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::LARGE_AREA, 75.0f);
    aiMgr.registerBehavior("LargeWander", std::move(largeWanderBehavior));
    GAMESTATE_INFO("EventDemoState: Registered LargeWander behavior");
  }

  if (!aiMgr.hasBehavior("EventWander")) {
    auto eventWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::EVENT_TARGET, 52.5f);
    aiMgr.registerBehavior("EventWander", std::move(eventWanderBehavior));
    GAMESTATE_INFO("EventDemoState: Registered EventWander behavior");
  }

  if (!aiMgr.hasBehavior("Patrol")) {
    auto patrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 56.25f, true);
    aiMgr.registerBehavior("Patrol", std::move(patrolBehavior));
    GAMESTATE_INFO("EventDemoState: Registered Patrol behavior");
  }

  if (!aiMgr.hasBehavior("RandomPatrol")) {
    auto randomPatrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::RANDOM_AREA, 63.75f, false);
    aiMgr.registerBehavior("RandomPatrol", std::move(randomPatrolBehavior));
    GAMESTATE_INFO("EventDemoState: Registered RandomPatrol behavior");
  }

  if (!aiMgr.hasBehavior("CirclePatrol")) {
    auto circlePatrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::CIRCULAR_AREA, 67.5f, false);
    aiMgr.registerBehavior("CirclePatrol", std::move(circlePatrolBehavior));
    GAMESTATE_INFO("EventDemoState: Registered CirclePatrol behavior");
  }

  if (!aiMgr.hasBehavior("EventTarget")) {
    auto eventTargetBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::EVENT_TARGET, 71.25f, false);
    aiMgr.registerBehavior("EventTarget", std::move(eventTargetBehavior));
    GAMESTATE_INFO("EventDemoState: Registered EventTarget behavior");
  }

  if (!aiMgr.hasBehavior("Chase")) {
    auto chaseBehavior = std::make_unique<ChaseBehavior>(90.0f, 500.0f, 50.0f);
    aiMgr.registerBehavior("Chase", std::move(chaseBehavior));
    GAMESTATE_INFO("EventDemoState: Chase behavior registered (will use AIManager::getPlayerReference())");
  }

  GAMESTATE_DEBUG("AI Behaviors configured for NPC integration");
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
    auto npc = NPC::create(textureID, position);
    npc->initializeInventory(); // Initialize inventory after construction

    npc->setWanderArea(0.0f, 0.0f, m_worldWidth, m_worldHeight);

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
    // Send to UI event log component (no timestamp - keeps messages concise)
    auto &ui = UIManager::Instance();
    ui.addEventLogEntry("event_log", entry);

    // Also log to console for debugging with timestamp
    GAMESTATE_DEBUG("EventDemo [" + std::to_string((int)m_totalDemoTime) + "s]: " + entry);
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
    auto npc = NPC::create(textureID, position);
    npc->initializeInventory(); // Initialize inventory after construction

    npc->setWanderArea(0.0f, 0.0f, m_worldWidth, m_worldHeight);

    std::string behaviorName = determineBehaviorForNPCType(npcType);

    // Cache AIManager reference for better performance
    AIManager &aiMgr = AIManager::Instance();
    aiMgr.registerEntityForUpdates(npc, rand() % 9 + 1, behaviorName);

    addLogEntry(npcType + ": " + behaviorName + " queued");

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
    GAMESTATE_WARN("Cannot setup achievements: ResourceTemplateManager not initialized");
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

  GAMESTATE_DEBUG("Achievement thresholds set for " +
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

    addLogEntry("Achievement: " + std::to_string(threshold) + " " + resourceName);

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

  const std::string& resourceName = resourceTemplate->getName();

  // Warning for low quantities of important resources (player-facing)
  if (resourceTemplate->getType() == ResourceType::Consumable && newQty <= 2 &&
      newQty > 0) {
    addLogEntry("Low: " + resourceName + " (" + std::to_string(newQty) + ")");
  }

  // Warning when resources are completely depleted (player-facing)
  if (newQty == 0) {
    addLogEntry("Out of " + resourceName);
  }

  // Warning for high-value resources reaching storage limits (player-facing)
  if (resourceTemplate->isStackable() &&
      resourceTemplate->getMaxStackSize() > 0) {
    int maxStack = resourceTemplate->getMaxStackSize();
    if (newQty >= maxStack * 0.9f) { // 90% of stack limit
      addLogEntry(resourceName + " nearly full (" +
                  std::to_string(newQty) + "/" + std::to_string(maxStack) + ")");
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

  const std::string& resourceName = resourceTemplate->getName();
  int change = newQty - oldQty;

  // Create detailed analytics entry (console only)
  std::string analyticsEntry =
      "ANALYTICS: [" + source + "] " + resourceName + " changed by " +
      std::to_string(change) +
      " (value: " + std::to_string(resourceTemplate->getValue() * change) +
      " coins)";

  m_resourceLog.push_back(analyticsEntry);
  GAMESTATE_DEBUG(analyticsEntry);

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
  if (m_player) {
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

    // Camera auto-synchronizes world bounds on update
  }
}

void EventDemoState::updateCamera(float deltaTime) {
  if (m_camera) {
    // Sync viewport with current window size (handles resize events)
    m_camera->syncViewportWithEngine();

    // Update camera position and following logic
    m_camera->update(deltaTime);
  }
}

// Removed setupCameraForWorld(): camera manages world bounds itself

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

  GAMESTATE_DEBUG("Inventory " + std::string(m_showInventory ? "shown" : "hidden"));
}
