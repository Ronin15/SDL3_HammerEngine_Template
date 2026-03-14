/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/AdvancedAIDemoState.hpp"
#include "controllers/combat/CombatController.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "gameStates/GameOverState.hpp"
#include "gameStates/LoadingState.hpp"
#include "events/EntityEvents.hpp"
#include "managers/AIManager.hpp"
#include "managers/BackgroundSimulationManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldManager.hpp"
#include "core/WorkerBudget.hpp"
#include "utils/WorldRenderPipeline.hpp"

#ifdef USE_SDL3_GPU
#include "gpu/GPURenderer.hpp"
#include "gpu/SpriteBatch.hpp"
#include "utils/GPUSceneRenderer.hpp"
#endif

#include <cmath>
#include <cstddef>
#include <format>

// Constructor/destructor defined here where GPUSceneRenderer is complete (for unique_ptr)
AdvancedAIDemoState::AdvancedAIDemoState() = default;

AdvancedAIDemoState::~AdvancedAIDemoState() {
  // Don't call virtual functions from destructors

  try {
    // Note: Proper cleanup should already have happened in exit()
    // This destructor is just a safety measure in case exit() wasn't called

    // Reset AI behaviors first to clear entity references
    AIManager &aiMgr = AIManager::Instance();
    aiMgr.resetBehaviors();

    // Clear NPCs via NPCRenderController
    m_npcRenderCtrl.clearSpawnedNPCs();

    // Clean up player
    m_player.reset();

    GAMESTATE_INFO("Exiting AdvancedAIDemoState in destructor...");
  } catch (const std::exception &e) {
    GAMESTATE_ERROR(std::format(
        "Exception in AdvancedAIDemoState destructor: {}", e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in AdvancedAIDemoState destructor");
  }
}

void AdvancedAIDemoState::handleInput() {
  // Cache manager references for better performance
  InputManager &inputMgr = InputManager::Instance();
  AIManager &aiMgr = AIManager::Instance();
  EntityDataManager &edm = EntityDataManager::Instance();

  // Use InputManager's new event-driven key press detection
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_SPACE)) {
    // Toggle pause/resume
    m_aiPaused = !m_aiPaused;

    // Set global AI pause state in AIManager
    aiMgr.setGlobalPause(m_aiPaused);

    // Simple feedback
    GAMESTATE_INFO(
        std::format("Advanced AI {}", m_aiPaused ? "PAUSED" : "RESUMED"));
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
    GAMESTATE_INFO("Preparing to exit AdvancedAIDemoState...");
    mp_stateManager->changeState("MainMenuState");
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_1)) {
    // Assign Idle behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to IDLE behavior");
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Idle");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_2)) {
    // Assign Flee behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to FLEE behavior");
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Flee");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_3)) {
    // Assign Follow behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to FOLLOW behavior");
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Follow");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_4)) {
    // Assign Guard behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to GUARD behavior");
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Guard");
      }
    }
  }

  if (inputMgr.wasKeyPressed(SDL_SCANCODE_5)) {
    // Assign Attack behavior to all NPCs
    GAMESTATE_INFO("Switching all NPCs to ATTACK behavior");
    for (size_t edmIdx : edm.getIndicesByKind(EntityKind::NPC)) {
      EntityHandle handle = edm.getHandle(edmIdx);
      if (handle.isValid()) {
        aiMgr.assignBehavior(handle, "Attack");
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

  // Combat - F key to attack (SPACE is used for AI pause)
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_F) && m_player) {
    m_controllers.get<CombatController>()->tryAttack();
  }
}

bool AdvancedAIDemoState::enter() {
  // Cache GameEngine reference at function start
  auto &gameEngine = GameEngine::Instance();

  // Resume all game managers (may be paused from menu states)
  gameEngine.setGlobalPause(false);

  GAMESTATE_INFO("Entering AdvancedAIDemoState...");

  // Reset transition flag when entering state
  m_transitioningToLoading = false;
  m_transitioningToGameOver = false;

  // Check if already initialized (resuming after LoadingState)
  if (m_initialized) {
    GAMESTATE_INFO("Already initialized - resuming AdvancedAIDemoState");
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
  GAMESTATE_INFO("World already loaded - initializing advanced AI demo");

  try {
    auto &worldManager = WorldManager::Instance();

    // Setup world size using actual world bounds from loaded world
    float worldMinX, worldMinY, worldMaxX, worldMaxY;
    if (worldManager.getWorldBounds(worldMinX, worldMinY, worldMaxX,
                                    worldMaxY)) {
      m_worldWidth = std::max(0.0f, worldMaxX - worldMinX);
      m_worldHeight = std::max(0.0f, worldMaxY - worldMinY);
      GAMESTATE_INFO(std::format("World dimensions: {} x {} pixels",
                                 m_worldWidth, m_worldHeight));
    } else {
      // Fallback to screen dimensions if world bounds unavailable
      m_worldWidth = gameEngine.getLogicalWidth();
      m_worldHeight = gameEngine.getLogicalHeight();
    }

    // Create player first (required for flee/follow/attack behaviors)
    m_player = std::make_shared<Player>();
    m_player->ensurePhysicsBodyRegistered();
    m_player->initializeInventory(); // Initialize inventory after construction
    m_player->setPosition(Vector2D(m_worldWidth / 2, m_worldHeight / 2));

    // Initialize camera (world is already loaded by LoadingState)
    initializeCamera();

    // Create world render pipeline for coordinated chunk management and scene rendering
    m_renderPipeline = std::make_unique<HammerEngine::WorldRenderPipeline>();

#ifdef USE_SDL3_GPU
    // Create GPU scene renderer for coordinated GPU rendering
    m_gpuSceneRenderer = std::make_unique<HammerEngine::GPUSceneRenderer>();
#endif

    // Initialize PathfinderManager for Follow behavior pathfinding
    PathfinderManager &pathfinderMgr = PathfinderManager::Instance();
    if (!pathfinderMgr.isInitialized()) {
      pathfinderMgr.init();
      GAMESTATE_INFO("PathfinderManager initialized for Follow behavior");
    }

    // Manually trigger grid rebuild now that world is loaded
    // This ensures the pathfinding grid is ready before NPCs start following
    pathfinderMgr.rebuildGrid();
    GAMESTATE_INFO("PathfinderManager grid rebuild initiated");

    // Cache AIManager reference for better performance
    AIManager &aiMgr = AIManager::Instance();

    // Set player reference in AIManager BEFORE registering behaviors
    // This ensures Follow/Flee/Attack behaviors can access the player target
    // Explicitly cast PlayerPtr to EntityPtr to ensure proper conversion
    EntityPtr playerAsEntity = std::static_pointer_cast<Entity>(m_player);
    aiMgr.setPlayerHandle(playerAsEntity->getHandle());

    // Register CombatController (follows GamePlayState pattern)
    m_controllers.add<CombatController>(m_player);
    m_controllers.subscribeAll();
    registerEventHandlers();

    // Spawn test village with merchants, guards, and villagers
    setupTestVillage();

    // Create advanced HUD UI (matches EventDemoState spacing pattern)
    auto &ui = UIManager::Instance();
    ui.createTitle("advanced_ai_title",
                   {0, UIConstants::TITLE_TOP_OFFSET,
                    gameEngine.getLogicalWidth(),
                    UIConstants::DEFAULT_TITLE_HEIGHT},
                   "Advanced AI Demo State");
    ui.setTitleAlignment("advanced_ai_title", UIAlignment::CENTER_CENTER);
    // Set auto-repositioning: top-aligned, full width
    ui.setComponentPositioning("advanced_ai_title",
                               {UIPositionMode::TOP_ALIGNED, 0,
                                UIConstants::TITLE_TOP_OFFSET, -1,
                                UIConstants::DEFAULT_TITLE_HEIGHT});

    // Create centered instruction labels
    ui.createLabel(
        "advanced_ai_instructions_line1",
        {0, UIConstants::INFO_FIRST_LINE_Y, gameEngine.getLogicalWidth(),
         UIConstants::INFO_LABEL_HEIGHT},
        "[B] Exit | [SPACE] Pause/Resume | [1] Idle | [2] Flee | [3] Follow");
    ui.setLabelAlignment("advanced_ai_instructions_line1", UIAlignment::CENTER_CENTER);
    ui.setComponentPositioning(
        "advanced_ai_instructions_line1",
        {UIPositionMode::CENTERED_H, 0, UIConstants::INFO_FIRST_LINE_Y, -1,
         UIConstants::INFO_LABEL_HEIGHT});

    const int line2Y = UIConstants::INFO_FIRST_LINE_Y +
                       UIConstants::INFO_LABEL_HEIGHT +
                       UIConstants::INFO_LINE_SPACING;
    ui.createLabel(
        "advanced_ai_instructions_line2",
        {0, line2Y, gameEngine.getLogicalWidth(), UIConstants::INFO_LABEL_HEIGHT},
        "[F] Player Attack | [4] Guard | [5] Attack NPCs");
    ui.setLabelAlignment("advanced_ai_instructions_line2", UIAlignment::CENTER_CENTER);
    ui.setComponentPositioning("advanced_ai_instructions_line2",
                               {UIPositionMode::CENTERED_H, 0, line2Y, -1,
                                UIConstants::INFO_LABEL_HEIGHT});

    const int statusY = line2Y + UIConstants::INFO_LABEL_HEIGHT +
                        UIConstants::INFO_LINE_SPACING +
                        UIConstants::INFO_STATUS_SPACING;
    ui.createLabel("advanced_ai_status",
                   {0, statusY, gameEngine.getLogicalWidth(),
                    UIConstants::INFO_LABEL_HEIGHT},
                   "FPS: -- | NPCs: -- | AI: RUNNING | Combat: ON");
    ui.setLabelAlignment("advanced_ai_status", UIAlignment::CENTER_CENTER);
    ui.setComponentPositioning("advanced_ai_status",
                               {UIPositionMode::CENTERED_H, 0, statusY, -1,
                                UIConstants::INFO_LABEL_HEIGHT});

    // Initialize combat HUD (health/stamina bars, target frame)
    UIManager::Instance().createCombatHUD();

    // Log status
    GAMESTATE_INFO(std::format("Created {} NPCs with advanced AI behaviors",
                               EntityDataManager::Instance().getEntityCount(EntityKind::NPC)));
    GAMESTATE_INFO("CombatController registered");

    // Pre-allocate status buffer to avoid per-frame allocations
    m_statusBuffer.reserve(64);

    // Mark as fully initialized to prevent re-entering loading logic
    m_initialized = true;

    return true;
  } catch (const std::exception &e) {
    GAMESTATE_ERROR(
        std::format("Exception in AdvancedAIDemoState::enter(): {}", e.what()));
    return false;
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in AdvancedAIDemoState::enter()");
    return false;
  }
}

bool AdvancedAIDemoState::exit() {
  GAMESTATE_INFO("Exiting AdvancedAIDemoState...");

  // Cache manager references for better performance
  AIManager &aiMgr = AIManager::Instance();
  BackgroundSimulationManager &bgSimMgr = BackgroundSimulationManager::Instance();
  EntityDataManager &edm = EntityDataManager::Instance();
  CollisionManager &collisionMgr = CollisionManager::Instance();
  PathfinderManager &pathfinderMgr = PathfinderManager::Instance();
  UIManager &ui = UIManager::Instance();
  WorldManager &worldMgr = WorldManager::Instance();
  EventManager &eventMgr = EventManager::Instance();

  if (m_transitioningToLoading) {
    // Transitioning to LoadingState - do cleanup but preserve m_worldLoaded
    // flag This prevents infinite loop when returning from LoadingState

    // Reset the flag after using it
    m_transitioningToLoading = false;
    m_transitioningToGameOver = false;

    // CRITICAL: Clear NPCs and player BEFORE prepareForStateTransition()
    // NPCs hold EDM indices - must be destroyed while EDM data is still valid
    m_npcRenderCtrl.clearSpawnedNPCs();
    if (m_player) {
      m_player.reset();
    }

    unregisterEventHandlers();
    aiMgr.prepareForStateTransition();
    bgSimMgr.prepareForStateTransition();

    eventMgr.prepareForStateTransition();

    if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
      collisionMgr.prepareForStateTransition();
    }

    if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
      pathfinderMgr.prepareForStateTransition();
    }

    edm.prepareForStateTransition();
    HammerEngine::WorkerBudgetManager::Instance().prepareForStateTransition();

    // Clean up camera and scene renderer
    m_camera.reset();
    m_renderPipeline.reset();

    // Clean up UI
    ui.prepareForStateTransition();

    // Destroy all controllers so re-entry creates fresh instances with valid refs
    m_controllers.clear();

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
    GAMESTATE_INFO(
        "AdvancedAIDemoState cleanup for LoadingState transition complete");
    return true;
  }

  // Full exit (going to main menu, other states, or shutting down)
  m_transitioningToGameOver = false;

  // CRITICAL: Clear NPCs and player BEFORE prepareForStateTransition()
  // NPCs hold EDM indices - must be destroyed while EDM data is still valid
  m_npcRenderCtrl.clearSpawnedNPCs();
  if (m_player) {
    m_player.reset();
  }

  unregisterEventHandlers();
  aiMgr.prepareForStateTransition();
  bgSimMgr.prepareForStateTransition();

  eventMgr.prepareForStateTransition();

  // Clean collision state
  if (collisionMgr.isInitialized() && !collisionMgr.isShutdown()) {
    collisionMgr.prepareForStateTransition();
  }

  if (pathfinderMgr.isInitialized() && !pathfinderMgr.isShutdown()) {
    pathfinderMgr.prepareForStateTransition();
  }

  edm.prepareForStateTransition();
  HammerEngine::WorkerBudgetManager::Instance().prepareForStateTransition();

  // Clean up camera and scene renderer first to stop world rendering
  m_camera.reset();
  m_renderPipeline.reset();

  // Clean up UI components using simplified method
  ui.prepareForStateTransition();

  // Destroy all controllers so re-entry creates fresh instances with valid refs
  m_controllers.clear();

  // Unload the world when fully exiting, but only if there's actually a world
  // loaded This matches EventDemoState's safety pattern and prevents crashes
  if (worldMgr.isInitialized() && worldMgr.hasActiveWorld()) {
    worldMgr.unloadWorld();
    // Reset m_worldLoaded when doing full exit (going to main menu, etc.)
    m_worldLoaded = false;
  }

  // Always restore AI to unpaused state when exiting the demo state
  aiMgr.setGlobalPause(false);
  m_aiPaused = false;

  // Reset initialization flag for next fresh start
  m_initialized = false;

  GAMESTATE_INFO("AdvancedAIDemoState exit complete");
  return true;
}

void AdvancedAIDemoState::registerEventHandlers() {
  (void)EventManager::Instance();
}

void AdvancedAIDemoState::unregisterEventHandlers() {
  (void)EventManager::Instance();
}

void AdvancedAIDemoState::update(float deltaTime) {
  try {
    // Check if we need to transition to loading screen (do this in update, not
    // enter)
    if (m_needsLoading) {
      m_needsLoading = false; // Clear flag

      GAMESTATE_INFO("Transitioning to LoadingState for world generation");

      // Create world configuration for advanced AI demo
      HammerEngine::WorldGenerationConfig config;
      config.width = 350; // Medium-sized world for advanced AI showcase
      config.height = 350;
      config.seed = static_cast<int>(std::time(nullptr));
      config.elevationFrequency = 0.025f;  // Lower frequency = larger biome regions
      config.humidityFrequency = 0.018f;
      config.waterLevel = 0.3f;
      config.mountainLevel = 0.7f;

      // Configure LoadingState and transition to it
      auto *loadingState = dynamic_cast<LoadingState *>(
          mp_stateManager->getState("LoadingState").get());
      if (loadingState) {
        loadingState->configure("AdvancedAIDemoState", config);
        // Set flag before transitioning to preserve m_worldLoaded in exit()
        m_transitioningToLoading = true;
        // Use changeState (called from update) to properly exit and re-enter
        mp_stateManager->changeState("LoadingState");
      } else {
        GAMESTATE_ERROR("LoadingState not found in GameStateManager");
      }

      return; // Don't continue with rest of update
    }

    // Update player
    if (m_player) {
      m_player->update(deltaTime);

      if (!m_player->isAlive() && !m_transitioningToGameOver) {
        m_transitioningToGameOver = true;
        mp_stateManager->changeState("GameOverState");
        return;
      }
    }

    // Cache manager references for better performance
    UIManager &ui = UIManager::Instance();

    // Update data-driven NPCs (animations handled by NPCRenderController)
    m_npcRenderCtrl.update(deltaTime);

    // Cache NPC count for render() (avoids EDM query in render path)
    m_cachedNPCCount = EntityDataManager::Instance().getEntityCount(EntityKind::NPC);

    // Update camera (follows player automatically)
    updateCamera(deltaTime);

#ifndef USE_SDL3_GPU
    // Prepare chunks via WorldRenderPipeline (predictive prefetching + dirty chunk updates)
    // GPU path renders tiles directly from atlas coords each frame — no chunk textures needed
    if (m_renderPipeline && m_camera) {
      m_renderPipeline->prepareChunks(*m_camera, deltaTime);
    }
#endif

    // Update controllers (CombatController handles cooldowns, stamina regen)
    m_controllers.updateAll(deltaTime);

    // Update combat HUD (health/stamina bars, target frame)
    auto& combatCtrl = *m_controllers.get<CombatController>();
    UIManager::Instance().updateCombatHUD(
        m_player->getHealth(),
        m_player->getStamina(),
        combatCtrl.hasActiveTarget(),
        "Target",
        combatCtrl.getTargetHealth());

    // Update UI (moved from render path for consistent frame timing)
    if (!ui.isShutdown()) {
      ui.update(deltaTime);
    }

  } catch (const std::exception &e) {
    GAMESTATE_ERROR(std::format(
        "Exception in AdvancedAIDemoState::update(): {}", e.what()));
  } catch (...) {
    GAMESTATE_ERROR("Unknown exception in AdvancedAIDemoState::update()");
  }
}

void AdvancedAIDemoState::render(SDL_Renderer *renderer,
                                 float interpolationAlpha) {
  // Cache UI manager reference
  UIManager &ui = UIManager::Instance();

  // Use WorldRenderPipeline for coordinated world rendering
  const bool worldActive = m_camera && m_renderPipeline;

  // ========== BEGIN SCENE (to intermediate target) ==========
  HammerEngine::WorldRenderPipeline::RenderContext ctx;
  if (worldActive) {
    ctx = m_renderPipeline->beginScene(renderer, *m_camera, interpolationAlpha);
  }

  if (ctx) {
    // Render world tiles via pipeline (uses pre-computed context)
    m_renderPipeline->renderWorld(renderer, ctx);

    // Render NPCs (sub-pixel smoothness from entity interpolation)
    m_npcRenderCtrl.renderNPCs(renderer, ctx.cameraX, ctx.cameraY, interpolationAlpha);

    // Render player (sub-pixel smoothness from entity's own interpolation)
    if (m_player) {
      m_player->render(renderer, ctx.cameraX, ctx.cameraY, interpolationAlpha);
    }
  }

  // ========== END SCENE (composite with zoom) ==========
  if (worldActive) {
    m_renderPipeline->endScene(renderer);
  }

  // Render UI components (update moved to update() for consistent frame timing)
  if (!ui.isShutdown()) {
    // Update status only when values change (C++20 type-safe, zero allocations)
    // Use m_aiPaused member instead of polling AIManager::isGloballyPaused()
    // every frame
    float currentFPS = mp_stateManager->getCurrentFPS();
    // Use cached NPC count from update() to avoid EDM query in render path
    size_t npcCount = m_cachedNPCCount;

    if (std::abs(currentFPS - m_lastDisplayedFPS) > 0.05f ||
        npcCount != m_lastDisplayedNPCCount ||
        m_aiPaused != m_lastDisplayedPauseState) {

      m_statusBuffer.clear();
      std::format_to(std::back_inserter(m_statusBuffer),
                     "FPS: {:.1f} | NPCs: {} | AI: {} | Combat: ON", currentFPS,
                     npcCount, m_aiPaused ? "PAUSED" : "RUNNING");
      ui.setText("advanced_ai_status", m_statusBuffer);

      m_lastDisplayedFPS = currentFPS;
      m_lastDisplayedNPCCount = npcCount;
      m_lastDisplayedPauseState = m_aiPaused;
    }
  }
  ui.render(renderer);
}

void AdvancedAIDemoState::setupTestVillage() {
  if (!m_player) {
    GAMESTATE_WARN("Cannot setup test village: no player");
    return;
  }

  auto& edm = EntityDataManager::Instance();
  auto& aiMgr = AIManager::Instance();

  Vector2D playerPos = m_player->getPosition();

  // Village center is offset from player to give them room to approach
  Vector2D villageCenter = playerPos + Vector2D(200.0f, 0.0f);

  GAMESTATE_INFO(std::format("Setting up test village at ({:.0f}, {:.0f})",
                            villageCenter.getX(), villageCenter.getY()));

  // Random race selection for village diversity
  static thread_local std::mt19937 rng{std::random_device{}()};
  static constexpr const char* friendlyRaces[] = {"Human", "Elf", "Dwarf"};
  std::uniform_int_distribution<size_t> raceDist(0, 2);
  auto getRandomFriendlyRace = [&]() { return friendlyRaces[raceDist(rng)]; };

  // ========================================================================
  // MERCHANTS - Arranged in a semi-circle for easy access
  // ========================================================================
  struct MerchantSpawn {
    const char* npcClass;
    Vector2D offset;
  };

  std::vector<MerchantSpawn> merchants = {
      {"Blacksmith",      Vector2D(-80.0f, -60.0f)},   // Top-left
      {"Armourer",        Vector2D(80.0f, -60.0f)},    // Top-right
      {"Alchemist",       Vector2D(-120.0f, 20.0f)},   // Left
      {"GeneralMerchant", Vector2D(120.0f, 20.0f)},    // Right
      {"Innkeeper",       Vector2D(-60.0f, 80.0f)},    // Bottom-left
      {"TavernKeeper",    Vector2D(60.0f, 80.0f)},     // Bottom-right
      {"Jeweler",         Vector2D(0.0f, -100.0f)},    // Top-center
  };

  int merchantCount = 0;
  for (const auto& spawn : merchants) {
    Vector2D pos = villageCenter + spawn.offset;
    const char* race = getRandomFriendlyRace();
    EntityHandle handle = edm.createNPCWithRaceClass(pos, race, spawn.npcClass);
    if (handle.isValid()) {
      // Merchants stay idle at their posts
      aiMgr.assignBehavior(handle, "Idle");
      merchantCount++;
      GAMESTATE_DEBUG(std::format("Spawned {} {} at ({:.0f}, {:.0f})",
                                 race, spawn.npcClass, pos.getX(), pos.getY()));
    }
  }

  // ========================================================================
  // GUARDS - Patrolling the village perimeter
  // ========================================================================
  std::vector<Vector2D> guardOffsets = {
      Vector2D(-180.0f, -120.0f),  // NW corner
      Vector2D(180.0f, -120.0f),   // NE corner
      Vector2D(-180.0f, 140.0f),   // SW corner
      Vector2D(180.0f, 140.0f),    // SE corner
      Vector2D(0.0f, -160.0f),     // North entrance
      Vector2D(0.0f, 180.0f),      // South entrance
  };

  int guardCount = 0;
  for (const auto& offset : guardOffsets) {
    Vector2D pos = villageCenter + offset;
    const char* race = getRandomFriendlyRace();
    EntityHandle handle = edm.createNPCWithRaceClass(pos, race, "Guard");
    if (handle.isValid()) {
      // Guards use Guard behavior (stationary but alert)
      aiMgr.assignBehavior(handle, "Guard");
      guardCount++;
    }
  }

  // ========================================================================
  // WANDERING VILLAGERS - Farmer, Miner, Woodcutter around the edges
  // ========================================================================
  struct VillagerSpawn {
    const char* npcClass;
    Vector2D offset;
    const char* behavior;
  };

  std::vector<VillagerSpawn> villagers = {
      {"Farmer",     Vector2D(-250.0f, 50.0f),  "Wander"},
      {"Farmer",     Vector2D(250.0f, 30.0f),   "Wander"},
      {"Miner",      Vector2D(-200.0f, -180.0f), "Wander"},
      {"Woodcutter", Vector2D(220.0f, -150.0f),  "Wander"},
      {"Woodcutter", Vector2D(-180.0f, 200.0f),  "Wander"},
  };

  int villagerCount = 0;
  for (const auto& spawn : villagers) {
    Vector2D pos = villageCenter + spawn.offset;
    const char* race = getRandomFriendlyRace();
    EntityHandle handle = edm.createNPCWithRaceClass(pos, race, spawn.npcClass);
    if (handle.isValid()) {
      aiMgr.assignBehavior(handle, spawn.behavior);
      villagerCount++;
    }
  }

  // ========================================================================
  // COMBAT NPCs - A few hostile NPCs for testing combat
  // ========================================================================
  std::vector<Vector2D> hostileOffsets = {
      Vector2D(-350.0f, 0.0f),     // West of village
      Vector2D(350.0f, -50.0f),    // East of village
      Vector2D(0.0f, -300.0f),     // North of village
  };

  int hostileCount = 0;
  const char* hostileClasses[] = {"Warrior", "Rogue", "Ranger"};
  for (size_t i = 0; i < hostileOffsets.size(); ++i) {
    Vector2D pos = villageCenter + hostileOffsets[i];
    // Orcs spawn neutral (faction 2) — Attack behavior scans for nearest target dynamically
    EntityHandle handle = edm.createNPCWithRaceClass(
        pos, "Orc", hostileClasses[i % 3], Sex::Unknown, 2);
    if (handle.isValid()) {
      aiMgr.assignBehavior(handle, "Attack");
      hostileCount++;
    }
  }

  // Add an Orc Mage for ranged combat testing
  {
    Vector2D pos = villageCenter + Vector2D(300.0f, 200.0f);
    EntityHandle handle = edm.createNPCWithRaceClass(pos, "Orc", "Mage", Sex::Unknown, 2);
    if (handle.isValid()) {
      aiMgr.assignBehavior(handle, "Attack");
      hostileCount++;
    }
  }

  // ========================================================================
  // SUMMARY
  // ========================================================================
  GAMESTATE_INFO(std::format("Test village spawned: {} merchants, {} guards, "
                            "{} villagers, {} hostile NPCs",
                            merchantCount, guardCount, villagerCount, hostileCount));
}

void AdvancedAIDemoState::initializeCamera() {
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
    // Disable camera event firing for consistency
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

  // Register camera with WorldManager for chunk texture updates
  WorldManager::Instance().setActiveCamera(m_camera.get());
}

void AdvancedAIDemoState::updateCamera(float deltaTime) {
  if (m_camera) {
    // Sync viewport with current window size (handles resize events)
    m_camera->syncViewportWithEngine();

    // Update camera position and following logic
    m_camera->update(deltaTime);
  }
}

#ifdef USE_SDL3_GPU
void AdvancedAIDemoState::recordGPUVertices(HammerEngine::GPURenderer &gpuRenderer,
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

  // Update status text before recording UI vertices
  auto &ui = UIManager::Instance();
  {
    float currentFPS = mp_stateManager->getCurrentFPS();
    size_t npcCount = m_cachedNPCCount;

    if (std::abs(currentFPS - m_lastDisplayedFPS) > 0.05f ||
        npcCount != m_lastDisplayedNPCCount ||
        m_aiPaused != m_lastDisplayedPauseState) {

      m_statusBuffer.clear();
      std::format_to(std::back_inserter(m_statusBuffer),
                     "FPS: {:.1f} | NPCs: {} | AI: {} | Combat: ON", currentFPS,
                     npcCount, m_aiPaused ? "PAUSED" : "RUNNING");
      ui.setText("advanced_ai_status", m_statusBuffer);

      m_lastDisplayedFPS = currentFPS;
      m_lastDisplayedNPCCount = npcCount;
      m_lastDisplayedPauseState = m_aiPaused;
    }
  }

  // Record UI vertices
  ui.recordGPUVertices(gpuRenderer);

  m_gpuSceneRenderer->endScene();
}

void AdvancedAIDemoState::renderGPUScene(HammerEngine::GPURenderer &gpuRenderer,
                                         SDL_GPURenderPass *scenePass,
                                         [[maybe_unused]] float interpolationAlpha) {
  if (!m_camera || !m_gpuSceneRenderer) { return; }

  // Render world tiles (sprite batch)
  m_gpuSceneRenderer->renderScene(gpuRenderer, scenePass);

  // Render player (entity batch)
  if (m_player) {
    m_player->renderGPU(gpuRenderer, scenePass);
  }
}

void AdvancedAIDemoState::renderGPUUI(HammerEngine::GPURenderer &gpuRenderer,
                                      SDL_GPURenderPass *swapchainPass) {
  UIManager::Instance().renderGPU(gpuRenderer, swapchainPass);
}
#endif
