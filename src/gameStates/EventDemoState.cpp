/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/EventDemoState.hpp"
#include "ai/behaviors/ChaseBehavior.hpp"
#include "ai/behaviors/FleeBehavior.hpp"
#include "ai/behaviors/FollowBehavior.hpp"
#include "ai/behaviors/IdleBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/WanderBehavior.hpp"
#include "controllers/world/DayNightController.hpp"
#include "controllers/world/WeatherController.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "events/SceneChangeEvent.hpp"
#include "events/WeatherEvent.hpp"
#include "gameStates/LoadingState.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/GameTimeManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/ParticleManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldManager.hpp"
#include "utils/Camera.hpp"
#include "utils/WorldRenderPipeline.hpp"

#ifdef USE_SDL3_GPU
#include "gpu/GPURenderer.hpp"
#include "gpu/SpriteBatch.hpp"
#include "utils/GPUSceneRenderer.hpp"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <format>

namespace {
// Static lookup table for WeatherType -> string conversion
constexpr std::array<const char*, 8> kWeatherTypeNames = {
    "Clear", "Cloudy", "Rainy", "Stormy", "Foggy", "Snowy", "Windy", "Custom"
};
} // namespace

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
    GAMESTATE_ERROR(
        std::format("Exception in EventDemoState destructor: {}", e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in EventDemoState destructor");
  }
}

bool EventDemoState::enter() {
  // Resume all game managers (may be paused from menu states)
  GameEngine::Instance().setGlobalPause(false);

  GAMESTATE_INFO("Entering EventDemoState...");

  // Reset transition flag when entering state
  m_transitioningToLoading = false;

  // Check if already initialized (resuming after LoadingState)
  if (m_initialized) {
    GAMESTATE_INFO("Already initialized - resuming EventDemoState");
    return true; // Skip all loading logic
  }

  // Check if world needs to be loaded
  if (!m_worldLoaded) {
    GAMESTATE_INFO("World not loaded yet - will transition to LoadingState on "
                   "first update");
    m_needsLoading = true;
    m_worldLoaded = true; // Mark as loaded to prevent loop on re-entry
    return true;          // Will transition to loading screen in update()
  }

  // World is loaded - proceed with normal initialization
  GAMESTATE_INFO("World already loaded - initializing event demo");

  try {
    // Cache GameEngine reference for better performance
    const GameEngine &gameEngine = GameEngine::Instance();
    auto &worldManager = WorldManager::Instance();

    // Update world dimensions from loaded world
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
      m_worldWidth = std::max(0.0f, maxX - minX);
      m_worldHeight = std::max(0.0f, maxY - minY);
      GAMESTATE_INFO(std::format("World dimensions: {} x {} pixels",
                                 m_worldWidth, m_worldHeight));
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
    aiMgr.setPlayerHandle(m_player->getHandle());

    // Setup achievement thresholds for demonstration
    setupResourceAchievements();

    // Initialize timing
    m_totalDemoTime = 0.0f;
    m_lastEventTriggerTime = -1.0f;

    // Register controllers (following GamePlayState pattern)
    m_controllers.add<WeatherController>();
    m_controllers.add<DayNightController>();

    // Enable automatic weather changes via GameTimeManager
    auto &gameTimeMgr = GameTimeManager::Instance();
    gameTimeMgr.enableAutoWeather(true);
    gameTimeMgr.setWeatherCheckInterval(4.0f);
    gameTimeMgr.setTimeScale(60.0f);

    // Setup AI behaviors for integration demo
    setupAIBehaviors();

    // Add initial log entry
    addLogEntry("Event Demo System Initialized");

    // Create simple UI components using auto-detecting methods
    auto &ui = UIManager::Instance();
    ui.createTitleAtTop(
        "event_title", "Event Demo State",
        UIConstants::DEFAULT_TITLE_HEIGHT); // Use standard title height

    // Status label at top
    const int statusY = UIConstants::INFO_FIRST_LINE_Y;
    ui.createLabel("event_status",
                   {UIConstants::INFO_LABEL_MARGIN_X, statusY, 400,
                    UIConstants::INFO_LABEL_HEIGHT},
                   "FPS: -- | Weather: Clear | NPCs: 0");
    ui.setComponentPositioning("event_status",
                               {UIPositionMode::TOP_ALIGNED,
                                UIConstants::INFO_LABEL_MARGIN_X, statusY, 400,
                                UIConstants::INFO_LABEL_HEIGHT});

    // Controls label
    const int controlsY = statusY + UIConstants::INFO_LABEL_HEIGHT +
                          UIConstants::INFO_LINE_SPACING;
    ui.createLabel(
        "event_controls",
        {UIConstants::INFO_LABEL_MARGIN_X, controlsY,
         ui.getLogicalWidth() - 2 * UIConstants::INFO_LABEL_MARGIN_X,
         UIConstants::INFO_LABEL_HEIGHT},
        "[B] Exit | [1-6] Events | [R] Reset | [F] Fire | [G] Smoke | [K] Sparks | [I] Inventory | [ ] Zoom");
    ui.setComponentPositioning("event_controls",
                               {UIPositionMode::TOP_ALIGNED,
                                UIConstants::INFO_LABEL_MARGIN_X, controlsY,
                                -2 * UIConstants::INFO_LABEL_MARGIN_X,
                                UIConstants::INFO_LABEL_HEIGHT});

    // Create event log component using auto-detected dimensions
    ui.createEventLog("event_log", {10, ui.getLogicalHeight() - 200, 730, 180},
                      7);
    // Set auto-repositioning: anchored to bottom with percentage-based width
    // for responsiveness
    UIPositioning eventLogPos;
    eventLogPos.mode = UIPositionMode::BOTTOM_ALIGNED;
    eventLogPos.offsetX = 10;
    eventLogPos.offsetY = 20;
    eventLogPos.fixedHeight = 180;
    eventLogPos.widthPercent = UIConstants::EVENT_LOG_WIDTH_PERCENT;
    ui.setComponentPositioning("event_log", eventLogPos);

    ui.addEventLogEntry("event_log", "Event Demo System Initialized");

    // Create right-aligned inventory panel for resource demo visualization
    // Using TOP_RIGHT positioning - UIManager handles all resize repositioning
    constexpr int inventoryWidth = 280;
    constexpr int inventoryHeight = 400;
    constexpr int panelMarginRight = 20; // 20px from right edge
    constexpr int panelMarginTop = 170;  // 170px from top
    constexpr int childInset = 10;       // Children are 10px inside panel
    constexpr int childWidth = inventoryWidth - (childInset * 2); // 260px

    int const windowWidth = ui.getLogicalWidth();
    int inventoryX = windowWidth - inventoryWidth - panelMarginRight;
    int inventoryY = panelMarginTop;

    ui.createPanel("inventory_panel",
                   {inventoryX, inventoryY, inventoryWidth, inventoryHeight});
    ui.setComponentVisible("inventory_panel", false);
    ui.setComponentPositioning(
        "inventory_panel", {UIPositionMode::TOP_RIGHT, panelMarginRight,
                            panelMarginTop, inventoryWidth, inventoryHeight});

    // Title: 25px below panel top, inset by 10px
    ui.createTitle("inventory_title",
                   {inventoryX + childInset, inventoryY + 25, childWidth, 35},
                   "Player Inventory");
    ui.setComponentVisible("inventory_title", false);
    ui.setComponentPositioning("inventory_title",
                               {UIPositionMode::TOP_RIGHT,
                                panelMarginRight + childInset,
                                panelMarginTop + 25, childWidth, 35});

    // Status label: 75px below panel top
    ui.createLabel("inventory_status",
                   {inventoryX + childInset, inventoryY + 75, childWidth, 25},
                   "Capacity: 0/50");
    ui.setComponentVisible("inventory_status", false);
    ui.setComponentPositioning("inventory_status",
                               {UIPositionMode::TOP_RIGHT,
                                panelMarginRight + childInset,
                                panelMarginTop + 75, childWidth, 25});

    // Inventory list: 110px below panel top
    ui.createList("inventory_list",
                  {inventoryX + childInset, inventoryY + 110, childWidth, 270});
    ui.setComponentVisible("inventory_list", false);
    ui.setComponentPositioning("inventory_list",
                               {UIPositionMode::TOP_RIGHT,
                                panelMarginRight + childInset,
                                panelMarginTop + 110, childWidth, 270});

    // --- DATA BINDING SETUP ---
    // Bind the inventory capacity label to a function that gets the data
    ui.bindText("inventory_status", [this]() -> std::string {
      if (!m_player) {
        return "Capacity: 0/0";
      }
      uint32_t invIdx = m_player->getInventoryIndex();
      if (invIdx == INVALID_INVENTORY_INDEX) {
        return "Capacity: 0/0";
      }
      const auto& inv = EntityDataManager::Instance().getInventoryData(invIdx);
      return std::format("Capacity: {}/{}", inv.usedSlots, inv.maxSlots);
    });

    // Bind the inventory list - populates provided buffers (zero-allocation
    // pattern)
    ui.bindList(
        "inventory_list",
        [this](std::vector<std::string> &items,
               std::vector<std::pair<std::string, int>> &sortedResources) {
          if (!m_player) {
            items.push_back("(Empty)");
            return;
          }

          uint32_t invIdx = m_player->getInventoryIndex();
          if (invIdx == INVALID_INVENTORY_INDEX) {
            items.push_back("(Empty)");
            return;
          }

          auto allResources = EntityDataManager::Instance().getInventoryResources(invIdx);

          if (allResources.empty()) {
            items.push_back("(Empty)");
            return;
          }

          // Build sorted list using reusable buffer provided by UIManager
          sortedResources.reserve(allResources.size());
          for (const auto &[resourceHandle, quantity] : allResources) {
            if (quantity > 0) {
              auto resourceTemplate =
                  ResourceTemplateManager::Instance().getResourceTemplate(
                      resourceHandle);
              std::string resourceName =
                  resourceTemplate ? resourceTemplate->getName() : "Unknown";
              sortedResources.emplace_back(std::move(resourceName), quantity);
            }
          }
          std::sort(sortedResources.begin(), sortedResources.end());

          items.reserve(sortedResources.size());
          for (const auto &[resourceId, quantity] : sortedResources) {
            items.push_back(std::format("{} x{}", resourceId, quantity));
          }

          if (items.empty()) {
            items.push_back("(Empty)");
          }
        });

    // Initialize camera for world navigation (world is already loaded by
    // LoadingState)
    initializeCamera();

    // Create world render pipeline for coordinated chunk management and scene rendering
    m_renderPipeline = std::make_unique<HammerEngine::WorldRenderPipeline>();

#ifdef USE_SDL3_GPU
    // Create GPU scene renderer for coordinated GPU rendering
    m_gpuSceneRenderer = std::make_unique<HammerEngine::GPUSceneRenderer>();
#endif

    // Pre-allocate status buffer to avoid per-frame allocations
    m_statusBuffer.reserve(64);

    // Mark as fully initialized to prevent re-entering loading logic
    m_initialized = true;

    GAMESTATE_INFO("EventDemoState initialized successfully");
    return true;

  } catch (const std::exception &e) {
    GAMESTATE_ERROR(
        std::format("Exception in EventDemoState::enter(): {}", e.what()));
    return false;
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in EventDemoState::enter()");
    return false;
  }
}

bool EventDemoState::exit() {
  GAMESTATE_INFO("Exiting EventDemoState...");

  // Get manager references for cleanup
  AIManager &aiMgr = AIManager::Instance();
  EntityDataManager &edm = EntityDataManager::Instance();
  CollisionManager &collisionMgr = CollisionManager::Instance();
  PathfinderManager &pathfinderMgr = PathfinderManager::Instance();
  ParticleManager &particleMgr = ParticleManager::Instance();
  UIManager &ui = UIManager::Instance();
  WorldManager &worldMgr = WorldManager::Instance();
  EventManager &eventMgr = EventManager::Instance();

  try {
    if (m_transitioningToLoading) {
      // Transitioning to LoadingState - do cleanup but preserve m_worldLoaded
      // flag This prevents infinite loop when returning from LoadingState

      // Reset the flag after using it
      m_transitioningToLoading = false;

      // Reset player
      m_player.reset();

      // Clear spawned NPCs (data-driven via NPCRenderController)
      m_npcRenderCtrl.clearSpawnedNPCs();

      // Clear controllers
      m_controllers.clear();

      // Unregister our specific handlers via tokens
      unregisterEventHandlers();

      // Remove all events from EventManager
      eventMgr.clearAllEvents();

      // Clean up managers (same as full exit)
      // CRITICAL: PathfinderManager MUST be cleaned BEFORE EDM
      // Pending path tasks hold captured edmIndex values - they must complete
      // or see the transition flag before EDM clears its data
      if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
        pathfinderMgr.prepareForStateTransition();
      }

      aiMgr.prepareForStateTransition();
      edm.prepareForStateTransition();

      if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
        collisionMgr.prepareForStateTransition();
      }

      if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
        particleMgr.prepareForStateTransition();
      }

      // Clean up camera and scene renderer
      m_camera.reset();
      m_renderPipeline.reset();

      // Clean up UI
      ui.prepareForStateTransition();

      // Unload world (LoadingState will reload it)
      if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
        worldMgr.unloadWorld();
        // CRITICAL: DO NOT reset m_worldLoaded here - keep it true to prevent
        // infinite loop when LoadingState returns to this state
      }

      // Reset initialized flag so state re-initializes after loading
      m_initialized = false;

      // Keep m_worldLoaded = true to remember we've already been through
      // loading
      GAMESTATE_INFO(
          "EventDemoState cleanup for LoadingState transition complete");
      return true;
    }

    // Full exit (going to main menu, other states, or shutting down)

    // Reset player
    m_player.reset();

    // Clear spawned NPCs (data-driven via NPCRenderController)
    m_npcRenderCtrl.clearSpawnedNPCs();

    // Clear controllers
    m_controllers.clear();

    // Unregister our specific handlers via tokens
    unregisterEventHandlers();

    // Remove all events from EventManager
    eventMgr.clearAllEvents();

    // Use manager prepareForStateTransition methods for deterministic cleanup
    // CRITICAL: PathfinderManager MUST be cleaned BEFORE EDM
    // Pending path tasks hold captured edmIndex values - they must complete or
    // see the transition flag before EDM clears its data
    if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
      pathfinderMgr.prepareForStateTransition();
    }

    aiMgr.prepareForStateTransition();
    edm.prepareForStateTransition();

    // Clean collision state before other systems
    if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
      collisionMgr.prepareForStateTransition();
    }

    // Simple particle cleanup - let prepareForStateTransition handle everything
    if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
      particleMgr.prepareForStateTransition(); // This handles weather effects
                                               // and cleanup
    }

    // Clean up camera and scene renderer first to stop world rendering
    m_camera.reset();
    m_renderPipeline.reset();

    // Clean up UI components before world cleanup
    ui.prepareForStateTransition();

    // Unload the world when fully exiting, but only if there's actually a world
    // loaded This matches GamePlayState's safety pattern and prevents Metal
    // renderer crashes
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
    GAMESTATE_ERROR(
        std::format("Exception in EventDemoState::exit(): {}", e.what()));
    return false;
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in EventDemoState::exit()");
    return false;
  }
}

void EventDemoState::unregisterEventHandlers() {
  try {
    auto &eventMgr = EventManager::Instance();
    for (const auto &tok : m_handlerTokens) {
      (void)eventMgr.removeHandler(tok);
    }
    m_handlerTokens.clear();
  } catch (...) {
    // Swallow errors during cleanup to avoid exit() failure
  }
}

void EventDemoState::update(float deltaTime) {
  // Check if we need to transition to loading screen (do this in update, not
  // enter)
  if (m_needsLoading) {
    m_needsLoading = false; // Clear flag

    GAMESTATE_INFO("Transitioning to LoadingState for world generation");

    // Create world configuration for event demo (HUGE world)
    HammerEngine::WorldGenerationConfig config;
    config.width = 500; // Massive 500x500 world
    config.height = 500;
    config.seed = static_cast<int>(std::time(nullptr));
    config.elevationFrequency = 0.018f;  // Lower frequency = larger biome regions
    config.humidityFrequency = 0.012f;
    config.waterLevel = 0.28f;
    config.mountainLevel = 0.72f;

    // Configure LoadingState and transition to it
    auto *loadingState = dynamic_cast<LoadingState *>(
        mp_stateManager->getState("LoadingState").get());
    if (loadingState) {
      loadingState->configure("EventDemo", config);
      // Set flag before transitioning to preserve m_worldLoaded in exit()
      m_transitioningToLoading = true;
      // Use changeState (called from update) to properly exit and re-enter
      mp_stateManager->changeState("LoadingState");
    } else {
      GAMESTATE_ERROR("LoadingState not found in GameStateManager");
    }

    return; // Don't continue with rest of update
  }

  // Update timing
  m_totalDemoTime += deltaTime;

  // Update player
  if (m_player) {
    m_player->update(deltaTime);
  }

  // Update NPC animations (velocity-based, data-driven)
  m_npcRenderCtrl.update(deltaTime);

  // Cache NPC count for render() (avoids EDM query in render path)
  m_cachedNPCCount = EntityDataManager::Instance().getEntityCount(EntityKind::NPC);

  // Update camera (follows player automatically)
  updateCamera(deltaTime);

  // Prepare chunks via WorldRenderPipeline (predictive prefetching + dirty chunk updates)
  if (m_renderPipeline && m_camera) {
    m_renderPipeline->prepareChunks(*m_camera, deltaTime);
  }

  // AI Manager is updated globally by GameEngine for optimal performance
  // Entity updates are handled by AIManager::update() in GameEngine

  // Update UI (moved from render path for consistent frame timing)
  auto &uiMgr = UIManager::Instance();
  if (!uiMgr.isShutdown()) {
    uiMgr.update(deltaTime);
  }

  // Note: EventManager is updated globally by GameEngine in the main update
  // loop for optimal performance and consistency with other global systems
}

void EventDemoState::render(SDL_Renderer *renderer, float interpolationAlpha) {
  // Get manager references at function start
  auto &particleMgr = ParticleManager::Instance();
  auto &uiMgr = UIManager::Instance();

  // Use WorldRenderPipeline for coordinated world rendering
  const bool worldActive = m_camera && m_renderPipeline;

  // ========== BEGIN SCENE (to intermediate target) ==========
  HammerEngine::WorldRenderPipeline::RenderContext ctx;
  if (worldActive) {
    ctx = m_renderPipeline->beginScene(renderer, *m_camera, interpolationAlpha);
  }

  if (ctx) {
    // Render background particles (rain, snow) - behind player/NPCs
    if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
      particleMgr.renderBackground(renderer, ctx.cameraX, ctx.cameraY,
                                   interpolationAlpha);
    }

    // Render world tiles via pipeline (uses pre-computed context)
    m_renderPipeline->renderWorld(renderer, ctx);

    // Render player (sub-pixel smoothness from entity's own interpolation)
    if (m_player) {
      m_player->render(renderer, ctx.cameraX, ctx.cameraY, interpolationAlpha);
    }

    // Render NPCs (sub-pixel smoothness from entity interpolation)
    m_npcRenderCtrl.renderNPCs(renderer, ctx.cameraX, ctx.cameraY, interpolationAlpha);

    // Render world-space and foreground particles (after player)
    if (particleMgr.isInitialized() && !particleMgr.isShutdown()) {
      particleMgr.render(renderer, ctx.cameraX, ctx.cameraY, interpolationAlpha);
      particleMgr.renderForeground(renderer, ctx.cameraX, ctx.cameraY,
                                   interpolationAlpha);
    }
  }

  // ========== END SCENE (composite with zoom) ==========
  if (worldActive) {
    m_renderPipeline->endScene(renderer);
  }

  // Render UI components (update moved to update() for consistent frame timing)
  if (!uiMgr.isShutdown()) {
    // Lazy-cache weather string (only compute when enum changes)
    if (m_currentWeather != m_lastCachedWeather) {
      m_cachedWeatherStr = getCurrentWeatherString();
      m_lastCachedWeather = m_currentWeather;
    }

    float const currentFPS = mp_stateManager->getCurrentFPS();
    // Use cached NPC count from update() to avoid EDM query in render path
    size_t npcCount = m_cachedNPCCount;

    // Update if FPS changed by more than 0.05 (avoids flicker) or other values
    // changed
    if (std::abs(currentFPS - m_lastDisplayedFPS) > 0.05f ||
        m_cachedWeatherStr != m_lastDisplayedWeather ||
        npcCount != m_lastDisplayedNPCCount) {

      m_statusBuffer.clear();
      std::format_to(std::back_inserter(m_statusBuffer),
                     "FPS: {:.1f} | Weather: {} | NPCs: {}", currentFPS,
                     m_cachedWeatherStr, npcCount);
      uiMgr.setText("event_status", m_statusBuffer);

      m_lastDisplayedFPS = currentFPS;
      m_lastDisplayedWeather = m_cachedWeatherStr;
      m_lastDisplayedNPCCount = npcCount;
    }
  }
  uiMgr.render(renderer);
}

void EventDemoState::setupEventSystem() {
  GAMESTATE_INFO("EventDemoState: EventManager instance obtained");
  GAMESTATE_INFO("EventDemoState: Using pre-initialized EventManager");

  auto &eventMgr = EventManager::Instance();

  // Register event handlers using token-based API for easy removal
  m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(
      EventTypeId::Weather, [this](const EventData &data) {
        if (data.isActive())
          onWeatherChanged("weather_changed");
      }));

  m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(
      EventTypeId::NPCSpawn, [this](const EventData &data) {
        if (data.isActive())
          onNPCSpawned(data);
      }));

  m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(
      EventTypeId::SceneChange, [this](const EventData &data) {
        if (data.isActive())
          onSceneChanged("scene_changed");
      }));

  m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(
      EventTypeId::ResourceChange, [this](const EventData &data) {
        if (data.isActive())
          onResourceChanged(data);
      }));

  GAMESTATE_INFO("EventDemoState: Event handlers registered");
  addLogEntry("Event system ready");
}

void EventDemoState::handleInput() {
  // Get manager references at function start
  const InputManager &inputMgr = InputManager::Instance();
  ParticleManager &particleMgr = ParticleManager::Instance();
  const UIManager &ui = UIManager::Instance();
  EntityDataManager &edm = EntityDataManager::Instance();

  // Manual event triggers (keys 1-6)
  // [1] Weather - cycle through weather types
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_1) &&
      (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
    triggerWeatherDemo();
    m_lastEventTriggerTime = m_totalDemoTime;
  }

  // Cache NPC count once for all limit checks
  const size_t npcCount = edm.getEntityCount(EntityKind::NPC);
  constexpr size_t NPC_LIMIT = 5000;

  // [2] NPC Spawn
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_2) &&
      (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
    if (npcCount < NPC_LIMIT) {
      triggerNPCSpawnDemo();
      m_lastEventTriggerTime = m_totalDemoTime;
    } else {
      addLogEntry("NPC limit (R to reset)");
    }
  }

  // [3] Scene Transition
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_3) &&
      (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
    triggerSceneTransitionDemo();
    m_lastEventTriggerTime = m_totalDemoTime;
  }

  // [4] Mass NPC Spawn with varied behaviors
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_4) &&
      (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
    if (npcCount < NPC_LIMIT) {
      triggerMassNPCSpawnDemo();
      m_lastEventTriggerTime = m_totalDemoTime;
    } else {
      addLogEntry("NPC limit (R to reset)");
    }
  }

  // [5] Events Reset
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_5)) {
    resetAllEvents();
    addLogEntry("Events reset");
  }

  // [6] Resource Demo
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_6) &&
      (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
    triggerResourceDemo();
    m_lastEventTriggerTime = m_totalDemoTime;
  }

  // [R] Full Reset
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_R)) {
    resetAllEvents();
    m_totalDemoTime = 0.0f;
    m_lastEventTriggerTime = 0.0f;
    addLogEntry("Demo reset");
  }

  // [C] Convenience Methods Demo
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_C) &&
      (m_totalDemoTime - m_lastEventTriggerTime) >= 0.2f) {
    triggerConvenienceMethodsDemo();
    m_lastEventTriggerTime = m_totalDemoTime;
  }

  // Particle effect toggles
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_F)) {
    particleMgr.toggleFireEffect();
    addLogEntry("Fire toggled");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_G)) {
    particleMgr.toggleSmokeEffect();
    addLogEntry("Smoke toggled");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_K)) {
    particleMgr.toggleSparksEffect();
    addLogEntry("Sparks toggled");
  }

  // Inventory toggle
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_I)) {
    toggleInventoryDisplay();
  }

  // Camera zoom controls
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_LEFTBRACKET) && m_camera) {
    m_camera->zoomIn();
  }
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_RIGHTBRACKET) && m_camera) {
    m_camera->zoomOut();
  }

  // Back to main menu
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
    mp_stateManager->changeState("MainMenuState");
  }

  // Mouse input for world interaction
  if (inputMgr.getMouseButtonState(LEFT) && m_camera) {
    Vector2D const mousePos = inputMgr.getMousePosition();
    if (!ui.isClickOnUI(mousePos)) {
      (void)m_camera->screenToWorld(mousePos);
    }
  }
}

void EventDemoState::triggerWeatherDemo() {
  // Cycle through weather types
  WeatherType newWeather = m_weatherSequence[m_currentWeatherIndex];
  std::string customType = m_customWeatherTypes[m_currentWeatherIndex];
  m_currentWeatherIndex = (m_currentWeatherIndex + 1) % m_weatherSequence.size();

  // Use EventManager hub to change weather
  auto &eventMgr = EventManager::Instance();
  if (newWeather == WeatherType::Custom && !customType.empty()) {
    eventMgr.changeWeather(customType, m_weatherTransitionTime,
                           EventManager::DispatchMode::Deferred);
  } else {
    const char* wt = kWeatherTypeNames[static_cast<size_t>(newWeather)];
    eventMgr.changeWeather(wt, m_weatherTransitionTime,
                           EventManager::DispatchMode::Deferred);
  }

  m_currentWeather = newWeather;
  std::string weatherName =
      customType.empty() ? getCurrentWeatherString() : customType;
  addLogEntry(std::format("Weather: {}", weatherName));
}

void EventDemoState::triggerNPCSpawnDemo() {
  std::string npcType = m_npcTypes[m_currentNPCTypeIndex];
  m_currentNPCTypeIndex = (m_currentNPCTypeIndex + 1) % m_npcTypes.size();

  Vector2D playerPos = m_player->getPosition();

  size_t npcCount = EntityDataManager::Instance().getEntityCount(EntityKind::NPC);
  float offsetX = 200.0f + ((npcCount % 8) * 120.0f);
  float offsetY = 100.0f + ((npcCount % 5) * 80.0f);

  float spawnX = playerPos.getX() + offsetX;
  float spawnY = playerPos.getY() + offsetY;

  spawnX = std::max(100.0f, std::min(spawnX, m_worldWidth - 100.0f));
  spawnY = std::max(100.0f, std::min(spawnY, m_worldHeight - 100.0f));

  // Use EventManager to spawn NPC via the unified event hub
  EventManager::Instance().spawnNPC(npcType, spawnX, spawnY);
  addLogEntry(std::format("Spawned: {}", npcType));
}

void EventDemoState::triggerSceneTransitionDemo() {
  std::string sceneName = m_sceneNames[m_currentSceneIndex];
  m_currentSceneIndex = (m_currentSceneIndex + 1) % m_sceneNames.size();

  // Use EventManager hub to change scenes
  std::vector<TransitionType> transitions = {
      TransitionType::Fade, TransitionType::Slide, TransitionType::Dissolve,
      TransitionType::Wipe};
  TransitionType t = transitions[m_currentSceneIndex % transitions.size()];
  const char *transitionName = (t == TransitionType::Fade)       ? "fade"
                               : (t == TransitionType::Slide)    ? "slide"
                               : (t == TransitionType::Dissolve) ? "dissolve"
                                                                 : "wipe";

  EventManager::Instance().changeScene(sceneName, transitionName, 2.0f,
                           EventManager::DispatchMode::Deferred);

  addLogEntry(std::format("Scene: {} ({})", sceneName, transitionName));
}

void EventDemoState::triggerResourceDemo() {
  if (!m_player) {
    addLogEntry("Resource: no player");
    return;
  }

  uint32_t invIdx = m_player->getInventoryIndex();
  if (invIdx == INVALID_INVENTORY_INDEX) {
    addLogEntry("Resource: no inventory");
    return;
  }

  // Cache manager references for better performance
  auto &edm = EntityDataManager::Instance();
  auto &eventMgr = EventManager::Instance();
  const auto &templateManager = ResourceTemplateManager::Instance();

  if (!templateManager.isInitialized()) {
    addLogEntry("Resource: not initialized");
    return;
  }

  // Realistic resource discovery: Query system for different categories/types
  ResourcePtr selectedResource = nullptr;
  int quantity = 1;

  switch (m_resourceDemonstrationStep % 6) {
  case 0: {
    // Discovery pattern: Find currency resources
    auto currencyResources =
        templateManager.getResourcesByCategory(ResourceCategory::Currency);
    if (!currencyResources.empty()) {
      // Real game logic: pick the first currency resource found
      selectedResource = currencyResources[0];
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
  int currentQuantity = edm.getInventoryQuantity(invIdx, handle);

  // Get UIManager reference for marking bindings dirty after inventory changes
  auto &ui = UIManager::Instance();

  if (m_resourceIsAdding) {
    // Add resources to player inventory
    bool success = edm.addToInventory(invIdx, handle, quantity);
    if (success) {
      int newQuantity = edm.getInventoryQuantity(invIdx, handle);
      addLogEntry(std::format("+{} {} ({} total)", quantity, resourceName,
                              newQuantity));

      // Trigger resource change via EventManager (deferred by default)
      eventMgr.triggerResourceChange(m_player->getHandle(), handle,
                                     currentQuantity, newQuantity,
                                     "event_demo");

      // Mark inventory UI bindings dirty so they refresh
      ui.markBindingDirty("inventory_status");
      ui.markBindingDirty("inventory_list");
    } else {
      addLogEntry(std::format("Failed: {} (full)", resourceName));
    }
  } else {
    // Remove resources from player inventory
    int removeQuantity = std::min(quantity, currentQuantity);

    if (removeQuantity > 0) {
      bool success = edm.removeFromInventory(invIdx, handle, removeQuantity);
      if (success) {
        int newQuantity = edm.getInventoryQuantity(invIdx, handle);
        addLogEntry(std::format("-{} {} ({} left)", removeQuantity,
                                resourceName, newQuantity));

        // Trigger resource change via EventManager (deferred by default)
        eventMgr.triggerResourceChange(m_player->getHandle(), handle,
                                       currentQuantity, newQuantity,
                                       "event_demo");

        // Mark inventory UI bindings dirty so they refresh
        ui.markBindingDirty("inventory_status");
        ui.markBindingDirty("inventory_list");
      } else {
        addLogEntry(std::format("Failed: remove {}", resourceName));
      }
    } else {
      addLogEntry(std::format("No {} to remove", resourceName));
    }
  }

  // Progress through different discovery patterns and alternate between adding
  // and removing
  m_resourceDemonstrationStep++;
  if (m_resourceDemonstrationStep % 6 == 0) {
    m_resourceIsAdding = !m_resourceIsAdding; // Switch between adding and
                                              // removing after full cycle
    addLogEntry(std::format("Mode: {}", m_resourceIsAdding ? "Adding" : "Removing"));
  }
}

void EventDemoState::triggerMassNPCSpawnDemo() {
  // Check NPC limit
  if (EntityDataManager::Instance().getEntityCount(EntityKind::NPC) >= 5000) {
    addLogEntry("NPC limit reached (5000)");
    return;
  }

  Vector2D playerPos = m_player->getPosition();

  // Spawn 200 NPCs via event system - random races, rotating through 6 behaviors
  EventManager::Instance().spawnNPC(
      "Villager",                                           // npcType
      playerPos.getX(), playerPos.getY(),                   // center position
      200,                                                  // count
      1500.0f,                                              // spawnRadius
      "Random",                                             // npcRace
      {"Idle", "Wander", "Patrol", "Chase", "Flee", "Follow"} // aiBehaviors
  );

  addLogEntry("Spawned 200 NPCs (random races, 6 behaviors)");
}

void EventDemoState::triggerConvenienceMethodsDemo() {
  addLogEntry("Convenience methods demo");

  m_convenienceDemoCounter++;

  // Cache EventManager reference for multiple calls
  auto &eventMgr = EventManager::Instance();

  bool success1 = eventMgr.createWeatherEvent(
      std::format("conv_fog_{}", m_convenienceDemoCounter), "Foggy", 0.7f,
      2.5f);
  bool success2 = eventMgr.createWeatherEvent(
      std::format("conv_storm_{}", m_convenienceDemoCounter), "Stormy", 0.9f,
      1.5f);
  bool success3 = eventMgr.createSceneChangeEvent(
      std::format("conv_dungeon_{}", m_convenienceDemoCounter), "DungeonDemo",
      "dissolve", 2.0f);
  bool success4 = eventMgr.createSceneChangeEvent(
      std::format("conv_town_{}", m_convenienceDemoCounter), "TownDemo",
      "slide", 1.0f);
  bool success5 = eventMgr.createNPCSpawnEvent(
      std::format("conv_guards_{}", m_convenienceDemoCounter), "Guard", 2,
      30.0f);
  bool success6 = eventMgr.createNPCSpawnEvent(
      std::format("conv_merchants_{}", m_convenienceDemoCounter), "Merchant", 1,
      15.0f);

  int const successCount =
      success1 + success2 + success3 + success4 + success5 + success6;
  if (successCount == 6) {
    addLogEntry("Created 6 events successfully");

    // Trigger via EventManager for demonstration
    eventMgr.changeWeather("Foggy", 2.5f,
                           EventManager::DispatchMode::Deferred);

    m_currentWeather = WeatherType::Foggy;
    addLogEntry("Weather: Foggy (demo)");
  } else {
    addLogEntry(std::format("Created {}/6 events", successCount));
  }
}

void EventDemoState::resetAllEvents() {
  cleanupSpawnedNPCs();

  // Cache EventManager reference for multiple calls
  auto &eventMgr = EventManager::Instance();

  // Remove all events from EventManager
  eventMgr.clearAllEvents();

  // Trigger clear weather via EventManager
  eventMgr.changeWeather("Clear", 1.0f, EventManager::DispatchMode::Deferred);

  m_currentWeather = WeatherType::Clear;

  m_currentWeatherIndex = 0;
  m_currentNPCTypeIndex = 0;
  m_currentSceneIndex = 0;

  m_lastEventTriggerTime = 0.0f;

  addLogEntry("Events cleared");
}

void EventDemoState::onWeatherChanged(const std::string &message) {
  // Weather handler triggered - message logged via GAMESTATE_DEBUG
  (void)message;
}

void EventDemoState::onNPCSpawned(const EventData &data) {
  // NPCSpawnEvent::execute() handles the actual spawning
  // This handler just logs the event for the demo UI
  if (!data.event) return;

  auto npcEvent = std::dynamic_pointer_cast<NPCSpawnEvent>(data.event);
  if (!npcEvent) return;

  const auto &params = npcEvent->getSpawnParameters();
  std::string typeLabel = (params.npcType.empty() || params.npcType == "Random")
                              ? "Random" : params.npcType;
  addLogEntry(std::format("NPC Spawn Event: {} x{}", typeLabel, params.count));
}

void EventDemoState::onSceneChanged(const std::string &message) {
  addLogEntry(std::format("Scene: {}", message));
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
    GAMESTATE_ERROR(
        std::format("Error in resource change handler: {}", e.what()));
  }
}

void EventDemoState::setupAIBehaviors() {
  // TODO: need to make sure that this logic is moved out of the gamestate.
  // Maybe on AI Manager init. init/configure all available behaviors
  GAMESTATE_INFO(
      "EventDemoState: Setting up AI behaviors for NPC integration...");
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
    GAMESTATE_INFO("EventDemoState: Chase behavior registered (will use "
                   "AIManager::getPlayerReference())");
  }

  if (!aiMgr.hasBehavior("Idle")) {
    auto idleBehavior = std::make_unique<IdleBehavior>(
        IdleBehavior::IdleMode::SUBTLE_SWAY, 20.0f);
    aiMgr.registerBehavior("Idle", std::move(idleBehavior));
    GAMESTATE_INFO("EventDemoState: Registered Idle behavior");
  }

  if (!aiMgr.hasBehavior("Flee")) {
    auto fleeBehavior = std::make_unique<FleeBehavior>(
        100.0f,   // fleeSpeed
        400.0f,   // detectionRange
        600.0f);  // safeDistance
    aiMgr.registerBehavior("Flee", std::move(fleeBehavior));
    GAMESTATE_INFO("EventDemoState: Registered Flee behavior");
  }

  if (!aiMgr.hasBehavior("Follow")) {
    auto followBehavior = std::make_unique<FollowBehavior>(
        FollowBehavior::FollowMode::LOOSE_FOLLOW, 60.0f);
    aiMgr.registerBehavior("Follow", std::move(followBehavior));
    GAMESTATE_INFO("EventDemoState: Registered Follow behavior");
  }

  GAMESTATE_DEBUG("AI Behaviors configured for NPC integration");
}

void EventDemoState::addLogEntry(const std::string &entry) {
  if (entry.empty())
    return;

  try {
    // Send to UI event log component (no timestamp - keeps messages concise)
    auto &ui = UIManager::Instance();
    ui.addEventLogEntry("event_log", entry);

    // Also log to console for debugging with timestamp
    GAMESTATE_DEBUG(std::format("EventDemo [{}s]: {}",
                                static_cast<int>(m_totalDemoTime), entry));
  } catch (const std::exception &e) {
    GAMESTATE_ERROR(std::format("Error adding log entry: {}", e.what()));
  }
}

std::string EventDemoState::getCurrentWeatherString() const {
  size_t index = static_cast<size_t>(m_currentWeather);
  if (index < kWeatherTypeNames.size()) {
    return kWeatherTypeNames[index];
  }
  return "Unknown";
}

void EventDemoState::cleanupSpawnedNPCs() {
  // Data-driven cleanup via NPCRenderController (handles AI unregistration and EDM destruction)
  m_npcRenderCtrl.clearSpawnedNPCs();
}

void EventDemoState::setupResourceAchievements() {
  // Set up achievement thresholds for different resource types for
  // demonstration
  const auto &templateManager = ResourceTemplateManager::Instance();

  if (!templateManager.isInitialized()) {
    GAMESTATE_WARN(
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

  GAMESTATE_DEBUG(
      std::format("Achievement thresholds set for {} resource types",
                  m_achievementThresholds.size()));
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

    addLogEntry(std::format("Achievement: {} {}", threshold, resourceName));

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

  const std::string &resourceName = resourceTemplate->getName();

  // Warning for low quantities of important resources (player-facing)
  if (resourceTemplate->getType() == ResourceType::Consumable && newQty <= 2 &&
      newQty > 0) {
    addLogEntry(std::format("Low: {} ({})", resourceName, newQty));
  }

  // Warning when resources are completely depleted (player-facing)
  if (newQty == 0) {
    addLogEntry(std::format("Out of {}", resourceName));
  }

  // Warning for high-value resources reaching storage limits (player-facing)
  if (resourceTemplate->isStackable() &&
      resourceTemplate->getMaxStackSize() > 0) {
    int maxStack = resourceTemplate->getMaxStackSize();
    if (newQty >= maxStack * 0.9f) { // 90% of stack limit
      addLogEntry(std::format("{} nearly full ({}/{})", resourceName, newQty,
                              maxStack));
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

  const std::string &resourceName = resourceTemplate->getName();
  int const change = newQty - oldQty;

  // Create detailed analytics entry (console only)
  std::string analyticsEntry =
      std::format("ANALYTICS: [{}] {} changed by {} (value: {} coins)", source,
                  resourceName, change, resourceTemplate->getValue() * change);

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
      static_cast<float>(gameEngine.getLogicalHeight()));

  // Configure camera to follow player
  if (m_player) {
    // Match GamePlayState: disable camera event firing for consistency
    m_camera->setEventFiringEnabled(false);

    // Set target and enable follow mode
    m_camera->setTarget(std::static_pointer_cast<Entity>(m_player));
    m_camera->setMode(HammerEngine::Camera::Mode::Follow);

    // Set up camera configuration for fast, smooth following (match
    // GamePlayState) Using exponential smoothing for smooth, responsive follow
    HammerEngine::Camera::Config config;
    config.followSpeed = 5.0f;      // Speed of camera interpolation
    config.deadZoneRadius = 0.0f;   // No dead zone - always follow
    config.smoothingFactor = 0.85f; // Smoothing factor (0-1, higher = smoother)
    config.clampToWorldBounds = true; // Keep camera within world
    m_camera->setConfig(config);

    // Provide camera to player for screen-to-world coordinate conversion
    m_player->setCamera(m_camera.get());

    // Camera auto-synchronizes world bounds on update
  }

  // Register camera with WorldManager for chunk texture updates
  WorldManager::Instance().setActiveCamera(m_camera.get());
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
// Removed applyCameraTransformation(): unified single-read pattern used in
// render()

void EventDemoState::toggleInventoryDisplay() {
  auto &ui = UIManager::Instance();
  m_showInventory = !m_showInventory;

  ui.setComponentVisible("inventory_panel", m_showInventory);
  ui.setComponentVisible("inventory_title", m_showInventory);
  ui.setComponentVisible("inventory_status", m_showInventory);
  ui.setComponentVisible("inventory_list", m_showInventory);

  GAMESTATE_DEBUG(
      std::format("Inventory {}", m_showInventory ? "shown" : "hidden"));
}

#ifdef USE_SDL3_GPU
void EventDemoState::recordGPUVertices(HammerEngine::GPURenderer &gpuRenderer,
                                       float interpolationAlpha) {
  if (!m_camera || !m_gpuSceneRenderer) { return; }

  // Begin scene - sets up sprite batch with atlas texture and calculates camera params
  auto ctx = m_gpuSceneRenderer->beginScene(gpuRenderer, *m_camera, interpolationAlpha);
  if (!ctx) { return; }

  // Record world tiles to sprite batch
  auto &worldMgr = WorldManager::Instance();
  worldMgr.recordGPU(*ctx.spriteBatch, ctx.cameraX, ctx.cameraY,
                     ctx.viewWidth, ctx.viewHeight, ctx.zoom);

  // Record NPCs to sprite batch (atlas-based)
  m_npcRenderCtrl.recordGPU(ctx);

  // End sprite batch recording (finalizes atlas-based sprites)
  m_gpuSceneRenderer->endSpriteBatch();

  // Record player (entity batch - separate texture)
  if (m_player) {
    m_player->recordGPUVertices(gpuRenderer, ctx.cameraX, ctx.cameraY, interpolationAlpha);
  }

  // Record particles
  auto &particleMgr = ParticleManager::Instance();
  particleMgr.recordGPUVertices(gpuRenderer, ctx.cameraX, ctx.cameraY, interpolationAlpha);

  // Update status text before recording UI vertices
  auto &uiMgr = UIManager::Instance();
  {
    float currentFPS = mp_stateManager->getCurrentFPS();
    size_t npcCount = m_cachedNPCCount;

    if (std::abs(currentFPS - m_lastDisplayedFPS) > 0.05f ||
        m_cachedWeatherStr != m_lastDisplayedWeather ||
        npcCount != m_lastDisplayedNPCCount) {

      m_statusBuffer.clear();
      std::format_to(std::back_inserter(m_statusBuffer),
                     "FPS: {:.1f} | Weather: {} | NPCs: {}", currentFPS,
                     m_cachedWeatherStr, npcCount);
      uiMgr.setText("event_status", m_statusBuffer);

      m_lastDisplayedFPS = currentFPS;
      m_lastDisplayedWeather = m_cachedWeatherStr;
      m_lastDisplayedNPCCount = npcCount;
    }
  }

  // Record UI vertices
  uiMgr.recordGPUVertices(gpuRenderer);

  m_gpuSceneRenderer->endScene();
}

void EventDemoState::renderGPUScene(HammerEngine::GPURenderer &gpuRenderer,
                                    SDL_GPURenderPass *scenePass,
                                    [[maybe_unused]] float interpolationAlpha) {
  if (!m_camera || !m_gpuSceneRenderer) { return; }

  // Render world tiles (sprite batch)
  m_gpuSceneRenderer->renderScene(gpuRenderer, scenePass);

  // Render player (entity batch)
  if (m_player) {
    m_player->renderGPU(gpuRenderer, scenePass);
  }

  // Render particles
  auto &particleMgr = ParticleManager::Instance();
  particleMgr.renderGPU(gpuRenderer, scenePass);
}

void EventDemoState::renderGPUUI(HammerEngine::GPURenderer &gpuRenderer,
                                 SDL_GPURenderPass *swapchainPass) {
  UIManager::Instance().renderGPU(gpuRenderer, swapchainPass);
}
#endif
