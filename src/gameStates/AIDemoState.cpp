/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/AIDemoState.hpp"
#include "ai/behaviors/ChaseBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/WanderBehavior.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "gameStates/LoadingState.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/ParticleManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldManager.hpp"
#include "managers/EventManager.hpp"
#include "utils/Camera.hpp"
#include "world/WorldData.hpp"
#include <cmath>
#include <cstddef>
#include <ctime>
#include <format>
#include <memory>

AIDemoState::~AIDemoState() {
  // Don't call virtual functions from destructors

  try {
    // Note: Proper cleanup should already have happened in exit()
    // This destructor is just a safety measure in case exit() wasn't called
    // Reset AI behaviors first to clear entity references
    // Don't call unassignBehaviorFromEntity here - it uses shared_from_this()
    // Clear NPCs without calling clean() on them
    m_npcRenderCtrl.clearSpawnedNPCs();

    // Clean up player
    m_player.reset();

    GAMESTATE_INFO("Exiting AIDemoState in destructor...");
  } catch (const std::exception &e) {
    GAMESTATE_ERROR(
        std::format("Exception in AIDemoState destructor: {}", e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in AIDemoState destructor");
  }
}

void AIDemoState::handleInput() {
  // Cache manager references for better performance
  InputManager const &inputMgr = InputManager::Instance();
  AIManager &aiMgr = AIManager::Instance();

  // Use InputManager's new event-driven key press detection
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_SPACE)) {
    // Toggle pause/resume
    m_aiPaused = !m_aiPaused;

    // Set global AI pause state in AIManager
    aiMgr.setGlobalPause(m_aiPaused);

    // Also send messages for behaviors that need them
    std::string message = m_aiPaused ? "pause" : "resume";
    aiMgr.broadcastMessage(message, true);

    // Simple feedback
    GAMESTATE_INFO(std::format("AI {}", m_aiPaused ? "PAUSED" : "RESUMED"));
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
    GAMESTATE_INFO("Preparing to exit AIDemoState...");
    mp_stateManager->changeState("MainMenuState");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_1)) {
    // Assign Wander behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to WANDER behavior");
    auto& edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Wander");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_2)) {
    // Assign Patrol behavior to all NPCs
    auto& edm = EntityDataManager::Instance();
    GAMESTATE_INFO(std::format(
        "Switching {} NPCs to PATROL behavior (batched processing)...",
        edm.getEntityCount(EntityKind::NPC)));
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Patrol");
      }
    }
    GAMESTATE_INFO("Patrol assignments queued. Processing "
                   "instantly in parallel for optimal performance.");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_3)) {
    // Assign Chase behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to CHASE behavior");

    // Chase behavior target is automatically maintained by AIManager
    // No manual target updates needed
    auto& edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Chase");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_4)) {
    // Assign SmallWander behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to SMALL WANDER behavior");
    auto& edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "SmallWander");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_5)) {
    // Assign LargeWander behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to LARGE WANDER behavior");
    auto& edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "LargeWander");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_6)) {
    // Assign EventWander behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to EVENT WANDER behavior");
    auto& edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "EventWander");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_7)) {
    // Assign RandomPatrol behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to RANDOM PATROL behavior");
    auto& edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "RandomPatrol");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_8)) {
    // Assign CirclePatrol behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to CIRCLE PATROL behavior");
    auto& edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "CirclePatrol");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_9)) {
    // Assign EventTarget behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to EVENT TARGET behavior");
    auto& edm = EntityDataManager::Instance();
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "EventTarget");
      }
    }
  }

  // Camera zoom controls
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_LEFTBRACKET) && m_camera) {
    m_camera->zoomIn(); // [ key = zoom in (objects larger)
  }
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_RIGHTBRACKET) && m_camera) {
    m_camera->zoomOut(); // ] key = zoom out (objects smaller)
  }

  // NPC spawning controls - use EventManager for unified spawning
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_N)) {
    // Spawn 2000 Villagers across entire world via events
    GAMESTATE_INFO("Spawning 2000 Villagers across world...");
    EventManager::Instance().spawnNPC("Villager", 0, 0, 2000, 0, true);
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_M)) {
    // Spawn 2000 random NPCs across entire world via events (random race/class)
    GAMESTATE_INFO("Spawning 2000 random NPCs across world...");
    EventManager::Instance().spawnNPC("Random", 0, 0, 2000, 0, true);
  }
}

bool AIDemoState::enter() {
  // Resume all game managers (may be paused from menu states)
  GameEngine::Instance().setGlobalPause(false);

  GAMESTATE_INFO("Entering AIDemoState...");

  // Reset transition flag when entering state
  m_transitioningToLoading = false;

  // Check if already initialized (resuming after LoadingState)
  if (m_initialized) {
    GAMESTATE_INFO("Already initialized - resuming AIDemoState");
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
  GAMESTATE_INFO("World already loaded - initializing AI demo");

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

    // Texture has to be loaded by NPC or Player can't be loaded here
    setupAIBehaviors();

    // Create player first (the chase behavior will need it)
    m_player = std::make_shared<Player>();
    m_player->ensurePhysicsBodyRegistered();
    m_player->initializeInventory(); // Initialize inventory after construction
    m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

    // Cache AIManager reference for better performance
    AIManager &aiMgr = AIManager::Instance();

    // Set player handle in AIManager for distance optimization
    // (EntityHandle-based API)
    aiMgr.setPlayerHandle(m_player->getHandle());

    // Create and register chase behavior - behaviors can get player via
    // getPlayerHandle() or getPlayerPosition()
    auto chaseBehavior = std::make_unique<ChaseBehavior>(90.0f, 500.0f, 50.0f);
    aiMgr.registerBehavior("Chase", std::move(chaseBehavior));
    GAMESTATE_INFO(
        "Chase behavior registered (will use AIManager::getPlayerHandle())");

    // Create simple HUD UI (matches EventDemoState spacing pattern)
    auto &ui = UIManager::Instance();
    ui.createTitle("ai_title",
                   {0, UIConstants::TITLE_TOP_OFFSET,
                    gameEngine.getLogicalWidth(),
                    UIConstants::DEFAULT_TITLE_HEIGHT},
                   "AI Demo State");
    ui.setTitleAlignment("ai_title", UIAlignment::CENTER_CENTER);
    // Set auto-repositioning: top-aligned, full width
    ui.setComponentPositioning("ai_title", {UIPositionMode::TOP_ALIGNED, 0,
                                            UIConstants::TITLE_TOP_OFFSET, -1,
                                            UIConstants::DEFAULT_TITLE_HEIGHT});

    ui.createLabel(
        "ai_instructions_line1",
        {UIConstants::INFO_LABEL_MARGIN_X, UIConstants::INFO_FIRST_LINE_Y,
         gameEngine.getLogicalWidth() - 2 * UIConstants::INFO_LABEL_MARGIN_X,
         UIConstants::INFO_LABEL_HEIGHT},
        "Controls: [B] Exit | [SPACE] Pause/Resume | [N] Spawn 2K Standard | "
        "[M] Spawn 2K Random | [ ] Zoom");
    // Set auto-repositioning: top-aligned, full width minus margins
    ui.setComponentPositioning(
        "ai_instructions_line1",
        {UIPositionMode::TOP_ALIGNED, UIConstants::INFO_LABEL_MARGIN_X,
         UIConstants::INFO_FIRST_LINE_Y, -2 * UIConstants::INFO_LABEL_MARGIN_X,
         UIConstants::INFO_LABEL_HEIGHT});

    const int line2Y = UIConstants::INFO_FIRST_LINE_Y +
                       UIConstants::INFO_LABEL_HEIGHT +
                       UIConstants::INFO_LINE_SPACING;
    ui.createLabel(
        "ai_instructions_line2",
        {UIConstants::INFO_LABEL_MARGIN_X, line2Y,
         gameEngine.getLogicalWidth() - 2 * UIConstants::INFO_LABEL_MARGIN_X,
         UIConstants::INFO_LABEL_HEIGHT},
        "Behaviors: [1] Wander | [2] Patrol | [3] Chase | [4] Small | [5] "
        "Large | "
        "[6] Event | [7] Random | [8] Circle | [9] Target");
    // Set auto-repositioning: top-aligned, full width minus margins
    ui.setComponentPositioning("ai_instructions_line2",
                               {UIPositionMode::TOP_ALIGNED,
                                UIConstants::INFO_LABEL_MARGIN_X, line2Y,
                                -2 * UIConstants::INFO_LABEL_MARGIN_X,
                                UIConstants::INFO_LABEL_HEIGHT});

    const int statusY = line2Y + UIConstants::INFO_LABEL_HEIGHT +
                        UIConstants::INFO_LINE_SPACING +
                        UIConstants::INFO_STATUS_SPACING;
    ui.createLabel("ai_status",
                   {UIConstants::INFO_LABEL_MARGIN_X, statusY, 400,
                    UIConstants::INFO_LABEL_HEIGHT},
                   "FPS: -- | Entities: -- | AI: RUNNING");
    // Set auto-repositioning: top-aligned with calculated position (fixes
    // fullscreen transition)
    ui.setComponentPositioning("ai_status",
                               {UIPositionMode::TOP_ALIGNED,
                                UIConstants::INFO_LABEL_MARGIN_X, statusY, 400,
                                UIConstants::INFO_LABEL_HEIGHT});

    // Initialize camera (world is already loaded by LoadingState)
    initializeCamera();

    // Pre-allocate status buffer to avoid per-frame allocations
    m_statusBuffer.reserve(64);

    // Mark as fully initialized to prevent re-entering loading logic
    m_initialized = true;

    return true;
  } catch (const std::exception &e) {
    GAMESTATE_ERROR(
        std::format("Exception in AIDemoState::enter(): {}", e.what()));
    return false;
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in AIDemoState::enter()");
    return false;
  }
}

bool AIDemoState::exit() {
  GAMESTATE_INFO("Exiting AIDemoState...");

  // Cache manager references for better performance
  AIManager &aiMgr = AIManager::Instance();
  EntityDataManager &edm = EntityDataManager::Instance();
  CollisionManager &collisionMgr = CollisionManager::Instance();
  PathfinderManager &pathfinderMgr = PathfinderManager::Instance();
  UIManager &ui = UIManager::Instance();
  WorldManager &worldMgr = WorldManager::Instance();

  if (m_transitioningToLoading) {
    // Transitioning to LoadingState - do cleanup but preserve m_worldLoaded
    // flag This prevents infinite loop when returning from LoadingState

    // Reset the flag after using it
    m_transitioningToLoading = false;

    // CRITICAL: Clear NPCs and player BEFORE prepareForStateTransition()
    // NPCs hold EDM indices - must be destroyed while EDM data is still valid
    m_npcRenderCtrl.clearSpawnedNPCs();
    if (m_player) {
      m_player.reset();
    }

    // Now safe to clear manager state
    // CRITICAL: PathfinderManager MUST be cleaned BEFORE EDM
    // Pending path tasks hold captured edmIndex values - they must complete or
    // see the transition flag before EDM clears its data
    if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
      pathfinderMgr.prepareForStateTransition();
    }

    aiMgr.prepareForStateTransition();
    edm.prepareForStateTransition();

    if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
      collisionMgr.prepareForStateTransition();
    }

    // Clean up camera
    m_camera.reset();

    // Clean up UI
    ui.prepareForStateTransition();

    // Unload world (LoadingState will reload it)
    if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
      worldMgr.unloadWorld();
      // CRITICAL: DO NOT reset m_worldLoaded here - keep it true to prevent
      // infinite loop when LoadingState returns to this state
    }

    // Restore AI to unpaused state
    aiMgr.setGlobalPause(false);
    m_aiPaused = false;

    // Reset initialized flag so state re-initializes after loading
    m_initialized = false;

    // Keep m_worldLoaded = true to remember we've already been through loading
    GAMESTATE_INFO("AIDemoState cleanup for LoadingState transition complete");
    return true;
  }

  // Full exit (going to main menu, other states, or shutting down)

  // CRITICAL: Clear NPCs and player BEFORE prepareForStateTransition()
  // NPCs hold EDM indices - must be destroyed while EDM data is still valid
  m_npcRenderCtrl.clearSpawnedNPCs();
  if (m_player) {
    m_player.reset();
  }

  // Now safe to clear manager state
  // CRITICAL: PathfinderManager MUST be cleaned BEFORE EDM
  // Pending path tasks hold captured edmIndex values - they must complete or
  // see the transition flag before EDM clears its data
  if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
    pathfinderMgr.prepareForStateTransition();
  }

  aiMgr.prepareForStateTransition();
  edm.prepareForStateTransition();

  // Clean collision state
  if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
    collisionMgr.prepareForStateTransition();
  }

  // Clean up camera first to stop world rendering
  m_camera.reset();

  // Clean up UI components using simplified method
  ui.prepareForStateTransition();

  // Unload the world when fully exiting, but only if there's actually a world
  // loaded This matches EventDemoState's safety pattern and prevents crashes
  if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
    worldMgr.unloadWorld();
    // Reset m_worldLoaded when doing full exit (going to main menu, etc.)
    m_worldLoaded = false;
  }

  // Always restore AI to unpaused state when exiting the demo state
  // This prevents the global pause from affecting other states
  aiMgr.setGlobalPause(false);
  m_aiPaused = false;

  // Reset initialization flag for next fresh start
  m_initialized = false;

  GAMESTATE_INFO("AIDemoState exit complete");
  return true;
}

void AIDemoState::update(float deltaTime) {
  try {
    // Check if we need to transition to loading screen (do this in update, not
    // enter)
    if (m_needsLoading) {
      m_needsLoading = false; // Clear flag

      GAMESTATE_INFO("Transitioning to LoadingState for world generation");

      // Create world configuration for AI demo
      HammerEngine::WorldGenerationConfig config;
      config.width = 500; // Massive world matching EventDemoState
      config.height = 500;
      config.seed = static_cast<int>(std::time(nullptr));
      config.elevationFrequency = 0.1f;
      config.humidityFrequency = 0.1f;
      config.waterLevel = 0.25f;
      config.mountainLevel = 0.75f;

      // Configure LoadingState and transition to it
      auto *loadingState = dynamic_cast<LoadingState *>(
          mp_stateManager->getState("LoadingState").get());
      if (loadingState) {
        loadingState->configure("AIDemoState", config);
        // Set flag before transitioning to preserve m_worldLoaded in exit()
        m_transitioningToLoading = true;
        // Use changeState (called from update) to properly exit and re-enter
        mp_stateManager->changeState("LoadingState");
      } else {
        GAMESTATE_ERROR("LoadingState not found in GameStateManager");
      }

      return; // Don't continue with rest of update
    }

    // Auto-spawning disabled - use keyboard triggers instead (N key for
    // standard spawn, M key for random behaviors) Collision bounds are set on
    // first spawn via keyboard trigger

    // Update player
    if (m_player) {
      m_player->update(deltaTime);
    }

    // Cache manager references for better performance
    UIManager &ui = UIManager::Instance();

    // Update Active tier NPCs only (animations and state machine)
    // AIManager handles behavior logic, BackgroundSimulationManager handles
    // non-Active Data-driven NPCs are updated via NPCRenderController
    m_npcRenderCtrl.update(deltaTime);

    // Update camera (follows player automatically)
    updateCamera(deltaTime);

    // Update UI (moved from render path for consistent frame timing)
    if (!ui.isShutdown()) {
      ui.update(deltaTime);
    }

  } catch (const std::exception &e) {
    GAMESTATE_ERROR(
        std::format("Exception in AIDemoState::update(): {}", e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in AIDemoState::update()");
  }
}

void AIDemoState::render(SDL_Renderer *renderer, float interpolationAlpha) {
  // Cache manager references for better performance
  WorldManager &worldMgr = WorldManager::Instance();
  UIManager &ui = UIManager::Instance();

  // Camera offset with unified interpolation (single atomic read for sync)
  float renderCamX = 0.0f;
  float renderCamY = 0.0f;
  float zoom = 1.0f;
  float viewWidth = 0.0f;
  float viewHeight = 0.0f;
  Vector2D playerInterpPos; // Position synced with camera

  if (m_camera) {
    zoom = m_camera->getZoom();
    // Returns the position used for offset - use it for player rendering
    playerInterpPos =
        m_camera->getRenderOffset(renderCamX, renderCamY, interpolationAlpha);

    // Derive view dimensions from viewport/zoom (no per-frame GameEngine calls)
    viewWidth = m_camera->getViewport().width / zoom;
    viewHeight = m_camera->getViewport().height / zoom;
  }

  // Set render scale for zoom only when changed (avoids GPU state change
  // overhead)
  if (zoom != m_lastRenderedZoom) {
    SDL_SetRenderScale(renderer, zoom, zoom);
    m_lastRenderedZoom = zoom;
  }

  // Render world first (background layer) using pixel-snapped camera
  if (m_camera && worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
    worldMgr.render(renderer, renderCamX, renderCamY, viewWidth, viewHeight);
  }

  // Render Active tier NPCs only using data-driven rendering
  // NPCRenderController handles iteration and rendering for visible NPCs
  m_npcRenderCtrl.renderNPCs(renderer, renderCamX, renderCamY, interpolationAlpha);

  // Render player at the position camera used for offset calculation
  if (m_player) {
    // Use position camera returned - no separate atomic read
    m_player->renderAtPosition(renderer, playerInterpPos, renderCamX,
                               renderCamY);
  }

  // Reset render scale to 1.0 for UI rendering only when needed (UI should not
  // be zoomed)
  if (m_lastRenderedZoom != 1.0f) {
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    m_lastRenderedZoom = 1.0f;
  }

  // Render UI components (update moved to update() for consistent frame timing)
  if (!ui.isShutdown()) {
    // Update status only when values change (C++20 type-safe, zero allocations)
    // Use m_aiPaused member instead of polling AIManager::isGloballyPaused()
    // every frame
    int currentFPS =
        static_cast<int>(std::lround(mp_stateManager->getCurrentFPS()));
    size_t entityCount = EntityDataManager::Instance().getEntityCount(EntityKind::NPC);

    if (currentFPS != m_lastDisplayedFPS ||
        entityCount != m_lastDisplayedEntityCount ||
        m_aiPaused != m_lastDisplayedPauseState) {

      m_statusBuffer.clear(); // Keeps reserved capacity
      std::format_to(std::back_inserter(m_statusBuffer),
                     "FPS: {} | Entities: {} | AI: {}", currentFPS, entityCount,
                     m_aiPaused ? "PAUSED" : "RUNNING");
      ui.setText("ai_status", m_statusBuffer);

      m_lastDisplayedFPS = currentFPS;
      m_lastDisplayedEntityCount = entityCount;
      m_lastDisplayedPauseState = m_aiPaused;
    }
  }
  ui.render(renderer);
}

void AIDemoState::setupAIBehaviors() {
  GAMESTATE_INFO("AIDemoState: Setting up AI behaviors using EventDemoState "
                 "implementation...");
  // TODO: need to move all availible behaviors into the AIManager and Event
  // Manager NPC creation with behavior
  //  Cache AIManager reference for better performance
  AIManager &aiMgr = AIManager::Instance();

  if (!aiMgr.hasBehavior("Wander")) {
    auto wanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::MEDIUM_AREA, 60.0f);
    aiMgr.registerBehavior("Wander", std::move(wanderBehavior));
    GAMESTATE_INFO("AIDemoState: Registered Wander behavior");
  }

  if (!aiMgr.hasBehavior("SmallWander")) {
    auto smallWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::SMALL_AREA, 45.0f);
    aiMgr.registerBehavior("SmallWander", std::move(smallWanderBehavior));
    GAMESTATE_INFO("AIDemoState: Registered SmallWander behavior");
  }

  if (!aiMgr.hasBehavior("LargeWander")) {
    auto largeWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::LARGE_AREA, 75.0f);
    aiMgr.registerBehavior("LargeWander", std::move(largeWanderBehavior));
    GAMESTATE_INFO("AIDemoState: Registered LargeWander behavior");
  }

  if (!aiMgr.hasBehavior("EventWander")) {
    auto eventWanderBehavior = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::EVENT_TARGET, 52.5f);
    aiMgr.registerBehavior("EventWander", std::move(eventWanderBehavior));
    GAMESTATE_INFO("AIDemoState: Registered EventWander behavior");
  }

  if (!aiMgr.hasBehavior("Patrol")) {
    auto patrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::RANDOM_AREA, 75.0f, false);
    aiMgr.registerBehavior("Patrol", std::move(patrolBehavior));
    GAMESTATE_INFO("AIDemoState: Registered Patrol behavior (random area)");
  }

  if (!aiMgr.hasBehavior("RandomPatrol")) {
    auto randomPatrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::RANDOM_AREA, 85.0f, false);
    aiMgr.registerBehavior("RandomPatrol", std::move(randomPatrolBehavior));
    GAMESTATE_INFO("AIDemoState: Registered RandomPatrol behavior");
  }

  if (!aiMgr.hasBehavior("CirclePatrol")) {
    auto circlePatrolBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::CIRCULAR_AREA, 90.0f, false);
    aiMgr.registerBehavior("CirclePatrol", std::move(circlePatrolBehavior));
    GAMESTATE_INFO("AIDemoState: Registered CirclePatrol behavior");
  }

  if (!aiMgr.hasBehavior("EventTarget")) {
    auto eventTargetBehavior = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::EVENT_TARGET, 95.0f, false);
    aiMgr.registerBehavior("EventTarget", std::move(eventTargetBehavior));
    GAMESTATE_INFO("AIDemoState: Registered EventTarget behavior");
  }

  // Chase behavior will be set up after player is created in enter() method
  // This ensures the player reference is available for behaviors to use

  GAMESTATE_INFO("AIDemoState: AI behaviors setup complete.");
}

void AIDemoState::initializeCamera() {
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
    // Disable camera event firing for consistency with other demo states
    m_camera->setEventFiringEnabled(false);

    // Set target and enable follow mode
    std::weak_ptr<Entity> playerAsEntity =
        std::static_pointer_cast<Entity>(m_player);
    m_camera->setTarget(playerAsEntity);
    m_camera->setMode(HammerEngine::Camera::Mode::Follow);

    // Set up camera configuration for fast, smooth following
    // Using exponential smoothing for smooth, responsive follow
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
}

void AIDemoState::updateCamera(float deltaTime) {
  if (m_camera) {
    // Sync viewport with current window size (handles resize events)
    m_camera->syncViewportWithEngine();

    // Update camera position and following logic
    m_camera->update(deltaTime);
  }
}
