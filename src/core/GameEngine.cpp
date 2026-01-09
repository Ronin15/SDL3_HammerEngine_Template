/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "core/GameEngine.hpp"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_surface.h"
#include "SDL3/SDL_video.h"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "gameStates/AIDemoState.hpp"
#include "gameStates/AdvancedAIDemoState.hpp"
#include "gameStates/EventDemoState.hpp"
#include "gameStates/GamePlayState.hpp"
#include "gameStates/LoadingState.hpp"
#include "gameStates/LogoState.hpp"
#include "gameStates/MainMenuState.hpp"
#include "gameStates/OverlayDemoState.hpp"
#include "gameStates/SettingsMenuState.hpp"
#include "gameStates/UIDemoState.hpp"
#include "managers/AIManager.hpp"
#include "managers/BackgroundSimulationManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/GameTimeManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/ParticleManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/SaveGameManager.hpp"
#include "managers/SettingsManager.hpp"
#include "managers/SoundManager.hpp"
#include "managers/TextureManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include <cstdlib>
#include <format>
#include <future>
#include <string>
#include <string_view>
#include <vector>

#define HAMMER_GRAY 31, 32, 34, 255

bool GameEngine::init(const std::string_view title, const int width,
                      const int height, bool fullscreen) {
  GAMEENGINE_INFO("Initializing SDL Video and Gamepad");

  // Initialize video and gamepad together to ensure proper IOKit setup on macOS
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    GAMEENGINE_CRITICAL(
        std::format("SDL initialization failed: {}", SDL_GetError()));
    return false;
  }

  GAMEENGINE_INFO("SDL Video online");

  // Set SDL hints for better rendering quality
  SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD,
              "3"); // Use geometry for smoother lines
  SDL_SetHint("SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR",
              "0");                           // Don't bypass compositor
  SDL_SetHint("SDL_MOUSE_AUTO_CAPTURE", "0"); // Prevent mouse capture issues

  // Performance hints for rendering
  SDL_SetHint("SDL_RENDER_SCALE_QUALITY",
              "0"); // Use nearest pixel sampling for crisp tiles
  SDL_SetHint("SDL_RENDER_BATCHING",
              "1"); // Enable render batching for performance

// Prefer Vulkan backend for lower per-draw-call overhead on Linux/AMD
// SDL3 auto-falls back to OpenGL if Vulkan unavailable
#ifdef __linux__
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "vulkan");
#endif

  // VSync and buffering hints
  // VSync hint - preference before renderer creation (runtime API
  // SDL_SetRenderVSync() also used)
  SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
  // Double buffer hint - only supported on Raspberry Pi and Wayland (no-op on
  // Windows/macOS)
  SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
  // Framebuffer acceleration - cross-platform
  SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "1");

// macOS-specific hints for better fullscreen and DPI handling
#ifdef __APPLE__
  SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES,
              "1"); // Use Spaces for fullscreen
#endif

  GAMEENGINE_DEBUG("SDL rendering hints configured for optimal quality");

  // Use reliable window sizing approach instead of potentially corrupted
  // display bounds
  if (width <= 0 || height <= 0) {
    // Default to reasonable size if not specified
    m_windowWidth = 1280;
    m_windowHeight = 720;
    GAMEENGINE_INFO(std::format("Using default window size: {}x{}",
                                m_windowWidth, m_windowHeight));
  } else {
    // Use the provided dimensions
    m_windowWidth = width;
    m_windowHeight = height;
    GAMEENGINE_INFO(std::format("Using requested window size: {}x{}",
                                m_windowWidth, m_windowHeight));
  }

  // Save windowed dimensions before fullscreen might override them
  m_windowedWidth = m_windowWidth;
  m_windowedHeight = m_windowHeight;

  // Query display capabilities for intelligent window sizing (all platforms)
  int displayCount = 0;
  std::unique_ptr<SDL_DisplayID[], decltype(&SDL_free)> displays(
      SDL_GetDisplays(&displayCount), SDL_free);

  const SDL_DisplayMode *displayMode = nullptr;
  if (displays && displayCount > 0) {
    displayMode = SDL_GetDesktopDisplayMode(displays[0]);
    if (displayMode) {
      GAMEENGINE_INFO(std::format("Primary display: {}x{} @ {}Hz",
                                  displayMode->w, displayMode->h,
                                  displayMode->refresh_rate));

      // If requested window size is larger than 90% of display, use fullscreen
      // This prevents awkward oversized windows on smaller displays (handhelds,
      // laptops)
      const float displayUsageThreshold = 0.9f;
      if (!fullscreen &&
          (m_windowWidth >
               static_cast<int>(displayMode->w * displayUsageThreshold) ||
           m_windowHeight >
               static_cast<int>(displayMode->h * displayUsageThreshold))) {
        GAMEENGINE_INFO(
            std::format("Requested window size ({}x{}) is larger than {}% of "
                        "display - enabling fullscreen for better UX",
                        m_windowWidth, m_windowHeight,
                        static_cast<int>(displayUsageThreshold * 100)));
        fullscreen = true;
      }
    }
  } else {
    GAMEENGINE_WARN("Could not query display capabilities - proceeding with "
                    "requested dimensions");
  }
  // Window handling with platform-specific optimizations
  SDL_WindowFlags flags = 0;
#ifdef __APPLE__
  // Always use high pixel density on macOS for crisp Retina rendering in both
  // windowed and fullscreen
  flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
  if (fullscreen) {
    flags |= SDL_WINDOW_FULLSCREEN;
  }

  if (fullscreen) {
#ifdef __APPLE__
    // On macOS, keep logical dimensions for proper scaling
    // Don't override m_windowWidth and m_windowHeight for macOS
    GAMEENGINE_INFO("Window set to Fullscreen mode for macOS compatibility");
#else
    // On non-macOS platforms, use native display resolution for fullscreen
    if (displayMode) {
      m_windowWidth = displayMode->w;
      m_windowHeight = displayMode->h;
      GAMEENGINE_INFO(std::format("Window size set to Full Screen: {}x{}",
                                  m_windowWidth, m_windowHeight));
    }
#endif
  }

  // Track initial fullscreen state
  m_isFullscreen = fullscreen;

  mp_window.reset(
      SDL_CreateWindow(title.data(), m_windowWidth, m_windowHeight, flags));

  if (!mp_window) {
    GAMEENGINE_ERROR(
        std::format("Failed to create window: {}", SDL_GetError()));
    return false;
  }

  GAMEENGINE_DEBUG("Window creation system online");

#ifdef __APPLE__
  // On macOS, set fullscreen mode to null for borderless fullscreen desktop
  // mode
  if (fullscreen) {
    SDL_SetWindowFullscreenMode(mp_window.get(), nullptr);
    GAMEENGINE_INFO("Set to borderless fullscreen desktop mode on macOS");
  }
#endif

  // Set window icon
  GAMEENGINE_INFO("Setting window icon");

  // Use native SDL3 PNG loading for the icon
  constexpr std::string_view iconPath = "res/img/icon.png";

  // Use a separate thread to load the icon
  auto iconFuture =
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          [iconPath]()
              -> std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> {
            return std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
                SDL_LoadPNG(iconPath.data()), SDL_DestroySurface);
          });

  // Continue with initialization while icon loads
  // Cache window sizes once for subsequent initialization steps
  int pixelWidth = m_windowWidth;
  int pixelHeight = m_windowHeight;
  int logicalWidth = m_windowWidth;
  int logicalHeight = m_windowHeight;

  if (!SDL_GetWindowSizeInPixels(mp_window.get(), &pixelWidth, &pixelHeight)) {
    GAMEENGINE_ERROR(
        std::format("Failed to get window pixel size: {}", SDL_GetError()));
  }
  if (!SDL_GetWindowSize(mp_window.get(), &logicalWidth, &logicalHeight)) {
    GAMEENGINE_ERROR(
        std::format("Failed to get window logical size: {}", SDL_GetError()));
  }

  // Create renderer (let SDL3 choose the best available backend)
  mp_renderer.reset(SDL_CreateRenderer(mp_window.get(), NULL));

  if (!mp_renderer) {
    GAMEENGINE_ERROR(
        std::format("Failed to create renderer: {}", SDL_GetError()));
    return false;
  }

  // Log which renderer backend SDL3 actually selected
#ifdef DEBUG
  auto rendererName = SDL_GetRendererName(mp_renderer.get());
  if (rendererName) {
    GAMEENGINE_INFO(
        std::format("SDL3 selected renderer backend: {}", rendererName));
  } else {
    GAMEENGINE_WARN("Could not determine selected renderer backend");
  }
#endif

  GAMEENGINE_DEBUG("Rendering system online");

  // Unified VSync initialization with automatic fallback
  // Detect platform for logging purposes only (not used to disable VSync)
  const std::string videoDriverRaw =
      SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "";
  std::string_view videoDriver = videoDriverRaw;
  m_isWayland = (videoDriver == "wayland");

  // Fallback to environment detection if driver info unavailable
  if (!m_isWayland) {
    const std::string sessionTypeRaw =
        std::getenv("XDG_SESSION_TYPE") ? std::getenv("XDG_SESSION_TYPE") : "";
    const std::string waylandDisplayRaw =
        std::getenv("WAYLAND_DISPLAY") ? std::getenv("WAYLAND_DISPLAY") : "";

    std::string_view sessionType = sessionTypeRaw;
    bool hasWaylandDisplay = !waylandDisplayRaw.empty();

    m_isWayland = (sessionType == "wayland") || hasWaylandDisplay;
  }

#ifdef DEBUG
  // Log detected platform
  if (m_isWayland) {
    GAMEENGINE_INFO("Platform detected: Wayland");
  } else {
    GAMEENGINE_INFO(std::format(
        "Platform detected: {}",
        videoDriver.empty() ? "Unknown" : std::string(videoDriver)));
  }
#endif

  // Load VSync preference from SettingsManager (defaults to enabled)
  auto &settings = HammerEngine::SettingsManager::Instance();
  bool vsyncRequested = settings.get<bool>("graphics", "vsync", true);
  m_vsyncRequested = vsyncRequested;
  GAMEENGINE_INFO(std::format("VSync setting from SettingsManager: {}",
                              vsyncRequested ? "enabled" : "disabled"));

  // Attempt to set VSync based on user preference
  bool vsyncSetSuccessfully =
      SDL_SetRenderVSync(mp_renderer.get(), vsyncRequested ? 1 : 0);

  GAMEENGINE_WARN_IF(!vsyncSetSuccessfully,
                     std::format("Failed to {} VSync: {}",
                                 vsyncRequested ? "enable" : "disable",
                                 SDL_GetError()));

  // Create TimestepManager (uses default 60 FPS target and 1/60s fixed
  // timestep)
  m_timestepManager = std::make_unique<TimestepManager>();

  // Verify VSync state and configure software frame limiting in TimestepManager
  if (vsyncSetSuccessfully) {
    verifyVSyncState(vsyncRequested);
  } else {
    m_timestepManager->setSoftwareFrameLimiting(true);
  }

  if (m_timestepManager->isUsingSoftwareFrameLimiting()) {
    TIMESTEP_INFO(std::format("Created: {:.0f} Hz updates, {:.0f} FPS target, "
                              "software frame limiting",
                              m_timestepManager->getUpdateFrequencyHz(),
                              m_timestepManager->getTargetFPS()));
  } else {
    TIMESTEP_INFO(std::format("Created: {:.0f} Hz updates, VSync enabled",
                              m_timestepManager->getUpdateFrequencyHz()));
  }

  if (!SDL_SetRenderDrawColor(
          mp_renderer.get(),
          HAMMER_GRAY)) { // Hammer Game Engine gunmetal dark grey
    GAMEENGINE_ERROR(std::format("Failed to set initial render draw color: {}",
                                 SDL_GetError()));
  }
  // Use native resolution rendering on all platforms for crisp, sharp text
  // This eliminates GPU scaling blur and provides consistent cross-platform
  // behavior
  int const actualWidth = pixelWidth;
  int const actualHeight = pixelHeight;

  // Store actual dimensions for UI positioning (no scaling needed)
  m_logicalWidth = actualWidth;
  m_logicalHeight = actualHeight;

  // Disable logical presentation to render at native resolution
  SDL_RendererLogicalPresentation const presentationMode =
      SDL_LOGICAL_PRESENTATION_DISABLED;
  if (!SDL_SetRenderLogicalPresentation(mp_renderer.get(), actualWidth,
                                        actualHeight, presentationMode)) {
    GAMEENGINE_ERROR(std::format(
        "Failed to set render logical presentation: {}", SDL_GetError()));
  }

  GAMEENGINE_INFO(
      std::format("Using native resolution for crisp rendering: {}x{}",
                  actualWidth, actualHeight));
  // Render Mode.
  SDL_SetRenderDrawBlendMode(mp_renderer.get(), SDL_BLENDMODE_BLEND);

  // Now check if the icon was loaded successfully
  try {
    auto iconSurfacePtr = iconFuture.get();
    if (iconSurfacePtr) {
      SDL_SetWindowIcon(mp_window.get(), iconSurfacePtr.get());
      // No need to manually destroy the surface, smart pointer will handle it
      GAMEENGINE_INFO("Window icon set successfully");
    } else {
      GAMEENGINE_WARN("Failed to load window icon");
    }
  } catch (const std::exception &e) {
    GAMEENGINE_WARN(std::format("Error loading window icon: {}", e.what()));
  }

  // INITIALIZING GAME RESOURCE LOADING AND
  // MANAGEMENT_________________________BEGIN

  // Calculate DPI-aware font sizes before threading
  float dpiScale = 1.0f;

  const int dpiLogicalWidth = logicalWidth;
  const int dpiLogicalHeight = logicalHeight;

  if (dpiLogicalWidth > 0 && dpiLogicalHeight > 0) {
#ifdef __APPLE__
    // On macOS, use the native display content scale for proper text rendering
    float displayScale = SDL_GetWindowDisplayScale(mp_window.get());
    dpiScale = (displayScale > 0.0f) ? displayScale : 1.0f;
    GAMEENGINE_INFO(
        std::format("macOS display content scale: {} {}", dpiScale,
                    displayScale > 0.0f ? "(detected)" : "(fallback)"));
#else
    // On other platforms, don't apply additional DPI scaling - SDL3 logical
    // presentation handles it
    dpiScale = 1.0f;
    GAMEENGINE_INFO("Non-macOS: Using DPI scale 1.0 (SDL3 logical presentation "
                    "handles scaling)");
#endif
  }

  // Store DPI scale for use by other managers
  m_dpiScale = dpiScale;

  GAMEENGINE_INFO("Using display-aware font sizing - SDL3 handles DPI scaling "
                  "automatically");

  GAMEENGINE_INFO(std::format("DPI scale: {}, window: {}x{}", dpiScale,
                              m_windowWidth, m_windowHeight));

  // Use multiple threads for initialization
  std::vector<std::future<bool>> initTasks; // Initialization tasks vector
  initTasks.reserve(12); // Reserve capacity for typical number of init tasks

  // CRITICAL: Initialize Event Manager FIRST - #1
  // All other managers that register event handlers depend on this
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
            GAMEENGINE_INFO("Creating Event Manager");
            EventManager &eventMgr = EventManager::Instance();
            if (!eventMgr.init()) {
              GAMEENGINE_CRITICAL("Failed to initialize Event Manager");
              return false;
            }
            GAMEENGINE_INFO("Event Manager initialized successfully");
            return true;
          }));

  // Initialize EntityDataManager - #1.5
  // Central data authority for all entities (Phase 1 of Entity System Overhaul)
  // Must be initialized before any entities are created
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
            GAMEENGINE_INFO("Creating Entity Data Manager");
            EntityDataManager &edm = EntityDataManager::Instance();
            if (!edm.init()) {
              GAMEENGINE_CRITICAL("Failed to initialize Entity Data Manager");
              return false;
            }
            GAMEENGINE_INFO("Entity Data Manager initialized successfully");
            return true;
          }));

  // Initialize input manager in a background thread - #2
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
            GAMEENGINE_INFO(
                "Detecting and initializing gamepads and input handling");
            InputManager &inputMgr = InputManager::Instance();
            if (!inputMgr.init()) {
              GAMEENGINE_CRITICAL("Failed to initialize Input Manager");
              return false;
            }
            inputMgr.initializeGamePad();
            return true;
          }));

  // Create and initialize texture manager - MAIN THREAD
  GAMEENGINE_INFO("Creating Texture Manager");
  TextureManager &texMgr = TextureManager::Instance();

  // Load textures in main thread
  GAMEENGINE_INFO("Creating and loading textures");
  constexpr std::string_view textureResPath = "res/img";
  constexpr std::string_view texturePrefix = "";
  texMgr.load(std::string(textureResPath), std::string(texturePrefix),
              mp_renderer.get());

  // Initialize sound manager in a separate thread - #3
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
            GAMEENGINE_INFO("Creating Sound Manager");
            SoundManager &soundMgr = SoundManager::Instance();
            if (!soundMgr.init()) {
              GAMEENGINE_CRITICAL("Failed to initialize Sound Manager");
              return false;
            }

            GAMEENGINE_INFO("Loading sounds and music");
            constexpr std::string_view sfxPath = "res/sfx";
            constexpr std::string_view sfxPrefix = "sfx";
            constexpr std::string_view musicPath = "res/music";
            constexpr std::string_view musicPrefix = "music";
            soundMgr.loadSFX(std::string(sfxPath), std::string(sfxPrefix));
            soundMgr.loadMusic(std::string(musicPath),
                               std::string(musicPrefix));
            return true;
          }));

  // Initialize font manager in a separate thread - #4
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          [this]() -> bool {
            GAMEENGINE_INFO("Creating Font Manager");
            FontManager &fontMgr = FontManager::Instance();
            if (!fontMgr.init()) {
              GAMEENGINE_CRITICAL("Failed to initialize Font Manager");
              return false;
            }

            GAMEENGINE_INFO("Loading fonts with display-aware sizing");

            // Use logical dimensions with DPI scale for proper sizing on
            // high-DPI displays
            constexpr std::string_view fontsPath = "res/fonts";
            if (!fontMgr.loadFontsForDisplay(std::string(fontsPath),
                                             m_windowWidth, m_windowHeight,
                                             m_dpiScale)) {
              GAMEENGINE_CRITICAL("Failed to load fonts for display");
              return false;
            }
            return true;
          }));

  // Initialize save game manager in a separate thread - #5
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
            GAMEENGINE_INFO("Creating Save Game Manager");
            SaveGameManager &saveMgr = SaveGameManager::Instance();
            if (!saveMgr.init()) {
              GAMEENGINE_CRITICAL("Failed to initialize Save Game Manager");
              return false;
            }

            // Set the save directory to "res" folder
            constexpr std::string_view saveDir = "res";
            saveMgr.setSaveDirectory(std::string(saveDir));
            return true;
          }));

  // Initialize Pathfinder Manager - #6
  // CRITICAL: Must complete BEFORE AIManager (explicit dependency)
  GAMEENGINE_INFO("Creating Pathfinder Manager (AIManager dependency)");
  auto pathfinderFuture =
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
            GAMEENGINE_INFO("Initializing PathfinderManager");
            PathfinderManager &pathfinderMgr = PathfinderManager::Instance();
            if (!pathfinderMgr.init()) {
              GAMEENGINE_CRITICAL("Failed to initialize Pathfinder Manager");
              return false;
            }
            GAMEENGINE_INFO("Pathfinder Manager initialized successfully");
            return true;
          });

  // Initialize Collision Manager - #7
  // CRITICAL: Must complete BEFORE AIManager (explicit dependency)
  GAMEENGINE_INFO("Creating Collision Manager (AIManager dependency)");
  auto collisionFuture =
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
            GAMEENGINE_INFO("Initializing CollisionManager");
            CollisionManager &collisionMgr = CollisionManager::Instance();
            if (!collisionMgr.init()) {
              GAMEENGINE_CRITICAL("Failed to initialize Collision Manager");
              return false;
            }
            GAMEENGINE_INFO("Collision Manager initialized successfully");
            return true;
          });

  // Wait for AIManager dependencies to complete before proceeding
  // This enforces the initialization dependency graph explicitly
  GAMEENGINE_INFO("Waiting for AIManager dependencies (PathfinderManager, "
                  "CollisionManager)");
  try {
    if (!pathfinderFuture.get()) {
      GAMEENGINE_CRITICAL("PathfinderManager initialization failed");
      return false;
    }
    if (!collisionFuture.get()) {
      GAMEENGINE_CRITICAL("CollisionManager initialization failed");
      return false;
    }
  } catch (const std::exception &e) {
    GAMEENGINE_CRITICAL(std::format(
        "AIManager dependency initialization threw exception: {}", e.what()));
    return false;
  }
  GAMEENGINE_INFO("AIManager dependencies initialized successfully");

  // Initialize AI Manager - #8
  // Dependencies satisfied: PathfinderManager and CollisionManager are now
  // fully initialized
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
            GAMEENGINE_INFO("Creating AI Manager");
            AIManager &aiMgr = AIManager::Instance();
            if (!aiMgr.init()) {
              GAMEENGINE_CRITICAL("Failed to initialize AI Manager");
              return false;
            }
            GAMEENGINE_INFO("AI Manager initialized successfully");
            return true;
          }));

  // Initialize Particle Manager in a separate thread - #9
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult([]()
                                                                       -> bool {
        GAMEENGINE_INFO("Creating Particle Manager");
        ParticleManager &particleMgr = ParticleManager::Instance();
        if (!particleMgr.init()) {
          GAMEENGINE_CRITICAL("Failed to initialize Particle Manager");
          return false;
        }

        // Register built-in weather effect presets
        particleMgr.registerBuiltInEffects();

        GAMEENGINE_INFO(
            "Particle Manager initialized successfully with built-in effects");
        return true;
      }));

  // Initialize Resource Template Manager in a separate thread - #10
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult([]()
                                                                       -> bool {
        GAMEENGINE_INFO("Creating Resource Template Manager");
        ResourceTemplateManager &resourceMgr =
            ResourceTemplateManager::Instance();
        if (!resourceMgr.init()) {
          GAMEENGINE_CRITICAL("Failed to initialize Resource Template Manager");
          return false;
        }
        GAMEENGINE_INFO("Resource Template Manager initialized successfully");
        return true;
      }));

  // Initialize World Resource Manager for global resource tracking - #11
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
            GAMEENGINE_INFO("Creating World Resource Manager");
            WorldResourceManager &worldResourceMgr =
                WorldResourceManager::Instance();
            if (!worldResourceMgr.init()) {
              GAMEENGINE_CRITICAL(
                  "Failed to initialize World Resource Manager");
              return false;
            }
            GAMEENGINE_INFO("World Resource Manager initialized successfully");
            return true;
          }));

  // Initialize World Manager for world generation and management - #12
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
            GAMEENGINE_INFO("Creating World Manager");
            WorldManager &worldMgr = WorldManager::Instance();
            if (!worldMgr.init()) {
              GAMEENGINE_CRITICAL("Failed to initialize World Manager");
              return false;
            }
            GAMEENGINE_INFO("World Manager initialized successfully");
            return true;
          }));

  // Initialize game state manager (on main thread because it directly calls
  // rendering) - MAIN THREAD
  GAMEENGINE_INFO(
      "Creating Game State Manager and setting up initial Game States");
  mp_gameStateManager = std::make_unique<GameStateManager>();

  // Initialize UI Manager (on main thread because it uses font/text rendering)
  // - MAIN THREAD
  GAMEENGINE_INFO("Creating UI Manager");
  UIManager &uiMgr = UIManager::Instance();
  if (!uiMgr.init()) {
    GAMEENGINE_CRITICAL("Failed to initialize UI Manager");
    return false;
  }

  GAMEENGINE_DEBUG("UI Manager initialized successfully");

  // Setting Up initial game states
  mp_gameStateManager->addState(std::make_unique<LogoState>());
  mp_gameStateManager->addState(
      std::make_unique<LoadingState>()); // Shared loading screen state
  mp_gameStateManager->addState(std::make_unique<MainMenuState>());
  mp_gameStateManager->addState(std::make_unique<SettingsMenuState>());
  mp_gameStateManager->addState(std::make_unique<GamePlayState>());
  mp_gameStateManager->addState(std::make_unique<AIDemoState>());
  mp_gameStateManager->addState(std::make_unique<AdvancedAIDemoState>());
  mp_gameStateManager->addState(std::make_unique<EventDemoState>());
  mp_gameStateManager->addState(std::make_unique<UIExampleState>());
  mp_gameStateManager->addState(std::make_unique<OverlayDemoState>());

  // Wait for all initialization tasks to complete
  bool allTasksSucceeded = true;
  for (auto &task : initTasks) {
    try {
      allTasksSucceeded &= task.get();
    } catch (const std::exception &e) {
      GAMEENGINE_ERROR(std::format("Initialization task failed: {}", e.what()));
      allTasksSucceeded = false;
    }
  }

  if (!allTasksSucceeded) {
    GAMEENGINE_ERROR("One or more initialization tasks failed");
    return false;
  }

  // Initialize GameTime (fast, no threading needed)
  // Time scale: 60.0 = 1 real second equals 1 game minute
  GAMEENGINE_INFO("Initializing GameTime system");
  if (!GameTimeManager::Instance().init(12.0f, 60.0f)) {
    GAMEENGINE_ERROR("Failed to initialize GameTime");
    return false;
  }
  GAMEENGINE_INFO("GameTime initialized (starting at noon, 60x speed)");

  // Initialize BackgroundSimulationManager (depends on EntityDataManager)
  // Handles simplified simulation for off-screen entities (Phase 5 of Entity
  // System Overhaul)
  GAMEENGINE_INFO("Initializing Background Simulation Manager");
  if (!BackgroundSimulationManager::Instance().init()) {
    GAMEENGINE_CRITICAL("Failed to initialize Background Simulation Manager");
    return false;
  }
  // Configure tier radii based on logical screen size (dynamic for different
  // devices)
  BackgroundSimulationManager::Instance().configureForScreenSize(
      m_logicalWidth, m_logicalHeight);
  GAMEENGINE_INFO(std::format(
      "Background Simulation Manager initialized (activeRadius: {:.0f}, "
      "backgroundRadius: {:.0f})",
      BackgroundSimulationManager::Instance().getActiveRadius(),
      BackgroundSimulationManager::Instance().getBackgroundRadius()));

  // Step 2: Cache manager references for performance (after all background init
  // complete)
  GAMEENGINE_INFO("Caching and validating manager references");
  try {
    // Validate AI Manager before caching
    AIManager &aiMgrTest = AIManager::Instance();
    if (!aiMgrTest.isInitialized()) {
      GAMEENGINE_CRITICAL("AIManager not properly initialized before caching!");
      return false;
    }
    mp_aiManager = &aiMgrTest;

    // Validate Event Manager before caching
    EventManager &eventMgrTest = EventManager::Instance();
    if (!eventMgrTest.isInitialized()) {
      GAMEENGINE_CRITICAL(
          "EventManager not properly initialized before caching!");
      return false;
    }
    mp_eventManager = &eventMgrTest;

    // Validate Particle Manager before caching
    ParticleManager &particleMgrTest = ParticleManager::Instance();
    if (!particleMgrTest.isInitialized()) {
      GAMEENGINE_CRITICAL(
          "ParticleManager not properly initialized before caching!");
      return false;
    }
    mp_particleManager = &particleMgrTest;

    // Validate Pathfinder Manager before caching (initialized by AIManager)
    PathfinderManager &pathfinderMgrTest = PathfinderManager::Instance();
    if (!pathfinderMgrTest.isInitialized()) {
      GAMEENGINE_CRITICAL(
          "PathfinderManager not properly initialized before caching!");
      return false;
    }
    mp_pathfinderManager = &pathfinderMgrTest;

    // Validate Collision Manager before caching (initialized by AIManager)
    CollisionManager &collisionMgrTest = CollisionManager::Instance();
    if (!collisionMgrTest.isInitialized()) {
      GAMEENGINE_CRITICAL(
          "CollisionManager not properly initialized before caching!");
      return false;
    }
    mp_collisionManager = &collisionMgrTest;

    // Validate Background Simulation Manager before caching
    BackgroundSimulationManager &bgSimMgrTest =
        BackgroundSimulationManager::Instance();
    if (!bgSimMgrTest.isInitialized()) {
      GAMEENGINE_CRITICAL("BackgroundSimulationManager not properly "
                          "initialized before caching!");
      return false;
    }
    mp_backgroundSimManager = &bgSimMgrTest;

    // Validate Resource Manager before caching
    ResourceTemplateManager &resourceMgrTest =
        ResourceTemplateManager::Instance();
    if (!resourceMgrTest.isInitialized()) {
      GAMEENGINE_CRITICAL(
          "ResourceTemplateManager not properly initialized before caching!");
      return false;
    }
    mp_resourceTemplateManager = &resourceMgrTest;

    // Validate World Resource Manager before caching
    WorldResourceManager &worldResourceMgrTest =
        WorldResourceManager::Instance();
    if (!worldResourceMgrTest.isInitialized()) {
      GAMEENGINE_CRITICAL(
          "WorldResourceManager not properly initialized before caching!");
      return false;
    }
    mp_worldResourceManager = &worldResourceMgrTest;

    // Validate World Manager before caching
    WorldManager &worldMgrTest = WorldManager::Instance();
    if (!worldMgrTest.isInitialized()) {
      GAMEENGINE_CRITICAL(
          "WorldManager not properly initialized before caching!");
      return false;
    }
    mp_worldManager = &worldMgrTest;

    // InputManager not cached - handled in handleEvents() for proper SDL
    // architecture

    // Manager references are valid (assigned above)

    // Manager references cached - managers handle their own logging internally
    GAMEENGINE_INFO("Manager references cached successfully");
  } catch (const std::exception &e) {
    GAMEENGINE_ERROR(
        std::format("Error caching manager references: {}", e.what()));
    return false;
  }
  //_______________________________________________________________________________________________________________END

  // Step 3: Post-initialization setup that requires manager dependencies
  GAMEENGINE_INFO("Setting up manager cross-dependencies");
  try {
    // Setup WorldManager event handlers now that EventManager is guaranteed to
    // be ready
    WorldManager &worldMgr = WorldManager::Instance();
    if (worldMgr.isInitialized()) {
      worldMgr.setupEventHandlers();
      GAMEENGINE_INFO("WorldManager event handlers setup complete");
    } else {
      GAMEENGINE_ERROR(
          "WorldManager not initialized - cannot setup event handlers");
      return false;
    }

    GAMEENGINE_INFO("Manager cross-dependencies setup complete");
  } catch (const std::exception &e) {
    GAMEENGINE_ERROR(std::format(
        "Error setting up manager cross-dependencies: {}", e.what()));
    return false;
  }

  GAMEENGINE_INFO(std::format("Game {} initialized successfully!", title));
  GAMEENGINE_INFO(std::format("Running {}", title));

  m_running = true;
  return true;
}

void GameEngine::handleEvents() {
  // SDL event polling - GameEngine owns the event loop as it owns the
  // window/renderer InputManager receives input events and maintains input
  // state
  InputManager &inputMgr = InputManager::Instance();

  // Clear previous frame's pressed keys before processing new events
  inputMgr.clearFrameInput();

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    // Convert window coordinates to logical coordinates for mouse events
    SDL_ConvertEventToRenderCoordinates(mp_renderer.get(), &event);

    switch (event.type) {
    case SDL_EVENT_QUIT:
      GAMEENGINE_INFO("Shutting down!");
      setRunning(false);
      break;

    // Input events -> route to InputManager
    case SDL_EVENT_KEY_DOWN:
      inputMgr.onKeyDown(event);
      break;
    case SDL_EVENT_KEY_UP:
      inputMgr.onKeyUp(event);
      break;
    case SDL_EVENT_MOUSE_MOTION:
      inputMgr.onMouseMove(event);
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      inputMgr.onMouseButtonDown(event);
      break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
      inputMgr.onMouseButtonUp(event);
      break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      inputMgr.onGamepadAxisMove(event);
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      inputMgr.onGamepadButtonDown(event);
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      inputMgr.onGamepadButtonUp(event);
      break;

    // Window/Display events -> handle directly in GameEngine
    case SDL_EVENT_WINDOW_RESIZED:
      onWindowResize(event);
      break;
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_OCCLUDED:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      onWindowEvent(event);
      break;
    case SDL_EVENT_DISPLAY_ORIENTATION:
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
    case SDL_EVENT_DISPLAY_MOVED:
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
      onDisplayChange(event);
      break;

    default:
      break;
    }
  }

  // Global fullscreen toggle (F1 key) - processed before state input
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_F1)) {
    toggleFullscreen();
  }

  // Handle game state input on main thread where SDL events are processed
  mp_gameStateManager->handleInput();
}

void GameEngine::setRunning(bool running) { m_running = running; }

bool GameEngine::getRunning() const { return m_running; }

float GameEngine::getCurrentFPS() const {
  if (m_timestepManager) {
    return m_timestepManager->getCurrentFPS();
  }
  return 0.0f;
}

void GameEngine::update(float deltaTime) {
  // OPTIMAL MANAGER UPDATE ARCHITECTURE - CLEAN DESIGN
  // ===================================================
  // Update order optimized for both player responsiveness AND smooth NPC
  // movement. Key design: AIManager handles batch synchronization internally
  // (implementation detail).
  //
  // UPDATE STRATEGY:
  // - Events FIRST (can trigger state changes)
  // - Player movement SECOND (before heavy AI processing)
  // - AI processes batches (parallel internally, waits for completion before
  // returning)
  // - Collision gets guaranteed-complete updates (no timing issues)
  //
  // GLOBAL SYSTEMS (Updated by GameEngine):
  // - EventManager: Global game events (weather, scene changes), batch
  // processing
  // - GameStateManager: Player movement and state-specific logic
  // - AIManager: Parallel batch processing with internal sync (self-contained)
  // - ParticleManager: Global particle system with weather integration
  // - PathfinderManager: Periodic pathfinding grid updates (every 300/600
  // frames)
  // - CollisionManager: Collision detection and resolution for all entities
  // - InputManager: Handled in handleEvents() for proper SDL event polling
  // architecture
  //
  // STATE-MANAGED SYSTEMS (Updated by individual states):
  // - UIManager: Optional, state-specific, only updated when UI is actually
  // used
  //   See UIExampleState::update() for proper state-managed pattern

  // 1. Event system - FIRST: process global events, state changes, weather
  // triggers
  mp_eventManager->update();

  // 2. Game states - player movement and state logic
  // MUST update BEFORE AIManager so NPCs react to current player position, not
  // stale data Push FPS to GameStateManager so states don't need to call
  // GameEngine::Instance()
  mp_gameStateManager->setCurrentFPS(m_timestepManager->getCurrentFPS());
  mp_gameStateManager->update(deltaTime);

  // 3. AI system - processes NPC behaviors with internal parallelization
  //    Batches run in parallel, waits for completion internally before
  //    returning. From GameEngine perspective: just a regular update call (sync
  //    is internal).
  mp_aiManager->update(deltaTime);

  // 4. Particle system - global weather and effect particles
  mp_particleManager->update(deltaTime);

  // 5. Pathfinding system - periodic grid updates (every 300/600 frames)
  // PathfinderManager initialized by AIManager, cached by GameEngine for
  // performance
  mp_pathfinderManager->update();

  // 6. Collision system - processes complete NPC updates from AIManager
  //    AIManager guarantees all batches complete before returning, so collision
  //    always receives complete, consistent updates (no partial/stale data).
  mp_collisionManager->update(deltaTime);

  // 7. Background simulation (tier updates + entity processing)
  // Single call handles everything: tier recalc every 60 frames,
  // background entity processing at 10Hz when entities exist.
  // Power-efficient: immediate return when paused or no work needed.
  mp_backgroundSimManager->update(mp_aiManager->getPlayerPosition(), deltaTime);
}

void GameEngine::render() {
  // Calculate interpolation alpha for smooth rendering between fixed updates
  float interpolationAlpha =
      static_cast<float>(m_timestepManager->getInterpolationAlpha());

  // Clear and render
  SDL_SetRenderDrawColor(mp_renderer.get(), HAMMER_GRAY);
  SDL_RenderClear(mp_renderer.get());

  mp_gameStateManager->render(mp_renderer.get(), interpolationAlpha);

  SDL_RenderPresent(mp_renderer.get());
}

void GameEngine::processBackgroundTasks() {
  // Background task processing hook for truly-async, non-critical work.
  //
  // Background simulation moved to update() for power efficiency.
  // This hook remains for future truly-async background work like:
  //   - Asset pre-loading for upcoming game states
  //   - Background save game serialization
  //   - Analytics/telemetry data collection
  //   - Periodic cache cleanup or memory defragmentation
  //   - Network polling for non-latency-critical updates
  //
  // THREAD SAFETY:
  //   Any work added here must be thread-safe and not require main-thread
  //   resources (SDL rendering, UI state, etc.).
}

void GameEngine::setLogicalPresentationMode(
    SDL_RendererLogicalPresentation mode) {
  m_logicalPresentationMode = mode;
  if (mp_renderer) {
    int width = m_windowWidth;
    int height = m_windowHeight;
    if (SDL_GetWindowSize(mp_window.get(), &width, &height)) {
      // width/height updated
    } else {
      GAMEENGINE_ERROR(
          std::format("Failed to get window size for logical presentation: {}",
                      SDL_GetError()));
    }
    if (!SDL_SetRenderLogicalPresentation(mp_renderer.get(), width, height,
                                          mode)) {
      GAMEENGINE_ERROR(std::format(
          "Failed to set render logical presentation: {}", SDL_GetError()));
    }
  }
}

bool GameEngine::isVSyncEnabled() const noexcept {
  if (!mp_renderer) {
    return false;
  }

  // Check current VSync setting using SDL3 API
  int vsync = 0;
  if (SDL_GetRenderVSync(mp_renderer.get(), &vsync)) {
    return (vsync > 0); // Any positive value means VSync is enabled
  }

  return false;
}

SDL_RendererLogicalPresentation
GameEngine::getLogicalPresentationMode() const noexcept {
  return m_logicalPresentationMode;
}

void GameEngine::clean() {
  GAMEENGINE_INFO("Starting shutdown sequence...");

  // Cache manager references for better performance
  HammerEngine::ThreadSystem &threadSystem =
      HammerEngine::ThreadSystem::Instance();

  // Clean up the thread system FIRST to ensure all threads are joined
  // before we destroy the managers they might be using.
  GAMEENGINE_INFO("Cleaning up Thread System...");
  if (HammerEngine::ThreadSystem::Exists() && !threadSystem.isShutdown()) {
    threadSystem.clean();
  }

  // Clean up engine managers (non-singletons)
  GAMEENGINE_INFO("Cleaning up GameState manager...");
  mp_gameStateManager.reset();

  // Save copies of the smart pointers to resources we'll clean up at the very
  // end
  auto window_to_destroy = std::move(mp_window);
  auto renderer_to_destroy = std::move(mp_renderer);

  // Clean up Managers in the correct order, respecting dependencies.
  // Systems that are used by other systems must be cleaned up last.
  GAMEENGINE_INFO("Cleaning up Particle Manager...");
  ParticleManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Font Manager...");
  FontManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Sound Manager...");
  SoundManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up UI Manager...");
  UIManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Event Manager...");
  EventManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Pathfinder Manager...");
  PathfinderManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Collision Manager...");
  CollisionManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Background Simulation Manager...");
  BackgroundSimulationManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up AI Manager...");
  AIManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Entity Data Manager...");
  EntityDataManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Save Game Manager...");
  SaveGameManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Input Manager...");
  InputManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up World Manager...");
  WorldManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up World Resource Manager...");
  WorldResourceManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Texture Manager...");
  TextureManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Resource Template Manager...");
  ResourceTemplateManager::Instance().clean();

  // Clear manager cache references
  GAMEENGINE_INFO("Clearing manager caches...");
  mp_aiManager = nullptr;
  mp_eventManager = nullptr;
  mp_particleManager = nullptr;
  mp_pathfinderManager = nullptr;
  mp_collisionManager = nullptr;
  mp_resourceTemplateManager = nullptr;
  mp_worldResourceManager = nullptr;
  mp_worldManager = nullptr;

  // InputManager not cached
  GAMEENGINE_INFO("Manager caches cleared");

  // Finally clean up SDL resources
  GAMEENGINE_INFO("Cleaning up SDL resources...");

  // Explicitly reset smart pointers at the end, after all subsystems
  // are done using them - this will trigger their custom deleters
  GAMEENGINE_INFO("Destroying renderer...");
  renderer_to_destroy.reset();
  GAMEENGINE_INFO("Renderer destroyed successfully");

  GAMEENGINE_INFO("Destroying window...");
  window_to_destroy.reset();
  GAMEENGINE_INFO("Window destroyed successfully");

  GAMEENGINE_INFO("Calling SDL_Quit...");
  SDL_Quit();

  GAMEENGINE_INFO("SDL resources cleaned!");
  GAMEENGINE_INFO("Shutdown complete!");
}

bool GameEngine::setVSyncEnabled(bool enable) {
  if (!mp_renderer) {
    GAMEENGINE_ERROR("Cannot set VSync - renderer not initialized");
    return false;
  }

  m_vsyncRequested = enable;
  GAMEENGINE_INFO(
      std::format("{} VSync...", enable ? "Enabling" : "Disabling"));

  // Attempt to set VSync
  bool vsyncSetSuccessfully =
      SDL_SetRenderVSync(mp_renderer.get(), enable ? 1 : 0);

  if (!vsyncSetSuccessfully) {
    GAMEENGINE_ERROR(std::format("Failed to {} VSync: {}",
                                 enable ? "enable" : "disable",
                                 SDL_GetError()));
    // Ensure software frame limiting is enabled if VSync enable failed
    if (enable) {
      m_timestepManager->setSoftwareFrameLimiting(true);
      GAMEENGINE_INFO("Falling back to software frame limiting");
    }
    return false;
  }

  // Verify VSync state - this sets TimestepManager's software limiting flag
  bool const vsyncVerified = verifyVSyncState(enable);

  // Save VSync setting to SettingsManager for persistence
  auto &settings = HammerEngine::SettingsManager::Instance();
  settings.set("graphics", "vsync", enable && vsyncVerified);
  settings.saveToFile("res/settings.json");

  return vsyncVerified;
}

void GameEngine::toggleFullscreen() {
  if (!mp_window) {
    GAMEENGINE_ERROR("Cannot toggle fullscreen - window not initialized");
    return;
  }

  // Toggle fullscreen state
  m_isFullscreen = !m_isFullscreen;

  GAMEENGINE_INFO(std::format(
      "Toggling fullscreen mode: {} (windowed size: {}x{})",
      m_isFullscreen ? "ON" : "OFF", m_windowedWidth, m_windowedHeight));

#ifdef __APPLE__
  // macOS: Use borderless fullscreen desktop mode for better compatibility
  if (m_isFullscreen) {
    if (!SDL_SetWindowFullscreen(mp_window.get(), true)) {
      GAMEENGINE_ERROR(
          std::format("Failed to enable fullscreen: {}", SDL_GetError()));
      m_isFullscreen = false; // Revert state on failure
      return;
    }
    // Set to borderless fullscreen desktop mode (nullptr = use desktop mode)
    if (!SDL_SetWindowFullscreenMode(mp_window.get(), nullptr)) {
      GAMEENGINE_WARN(std::format(
          "Failed to set borderless fullscreen mode: {}", SDL_GetError()));
    }
    GAMEENGINE_INFO("macOS: Enabled borderless fullscreen desktop mode");
  } else {
    if (!SDL_SetWindowFullscreen(mp_window.get(), false)) {
      GAMEENGINE_ERROR(
          std::format("Failed to disable fullscreen: {}", SDL_GetError()));
      m_isFullscreen = true; // Revert state on failure
      return;
    }
    // Restore windowed size
    if (!SDL_SetWindowSize(mp_window.get(), m_windowedWidth,
                           m_windowedHeight)) {
      GAMEENGINE_ERROR(
          std::format("Failed to restore window size: {}", SDL_GetError()));
    } else {
      GAMEENGINE_INFO(std::format("macOS: Restored window size to {}x{}",
                                  m_windowedWidth, m_windowedHeight));
    }
  }
#else
  // Other platforms: Use standard fullscreen toggle
  if (!SDL_SetWindowFullscreen(mp_window.get(), m_isFullscreen)) {
    GAMEENGINE_ERROR(
        std::format("Failed to toggle fullscreen: {}", SDL_GetError()));
    m_isFullscreen = !m_isFullscreen; // Revert state on failure
    return;
  }

  // Restore windowed size when exiting fullscreen
  if (!m_isFullscreen) {
    if (!SDL_SetWindowSize(mp_window.get(), m_windowedWidth,
                           m_windowedHeight)) {
      GAMEENGINE_ERROR(
          std::format("Failed to restore window size: {}", SDL_GetError()));
    } else {
      GAMEENGINE_INFO(std::format("Restored window size to {}x{}",
                                  m_windowedWidth, m_windowedHeight));
    }
  }

  GAMEENGINE_INFO(std::format("Fullscreen mode {}",
                              m_isFullscreen ? "enabled" : "disabled"));
#endif

  // Note: SDL will automatically trigger SDL_EVENT_WINDOW_RESIZED
  // which will be handled by InputManager::onWindowResize()
  // This ensures fonts and UI are properly updated for the new display mode
}

void GameEngine::setFullscreen(bool enabled) {
  if (!mp_window) {
    GAMEENGINE_ERROR("Cannot set fullscreen - window not initialized");
    return;
  }

  // Only change if the state is different
  if (m_isFullscreen == enabled) {
    return;
  }

  // Use the existing toggle function since the state needs to change
  toggleFullscreen();
}

void GameEngine::setGlobalPause(bool paused) {
  m_globallyPaused = paused;

  // Pause all managers (cached pointers guaranteed valid after init)
  mp_aiManager->setGlobalPause(paused);
  mp_particleManager->setGlobalPause(paused);
  mp_collisionManager->setGlobalPause(paused);
  mp_pathfinderManager->setGlobalPause(paused);
  mp_backgroundSimManager->setGlobalPause(paused);
  GameTimeManager::Instance().setGlobalPause(paused);
  EventManager::Instance().setGlobalPause(paused);

#ifdef DEBUG
  if (paused) {
    GAMEENGINE_INFO("Game globally paused - all managers idle");
  } else {
    GAMEENGINE_INFO("Game globally resumed");
  }
#endif
}

bool GameEngine::isGloballyPaused() const { return m_globallyPaused; }

int GameEngine::getOptimalDisplayIndex() const {
#ifdef __APPLE__
  // On macOS, prioritize the primary display (0) for MacBook built-in screens
  return 0;
#else
  // On other platforms, try secondary display first if available
  int displayCount = 0;
  std::unique_ptr<SDL_DisplayID[], decltype(&SDL_free)> displays(
      SDL_GetDisplays(&displayCount), SDL_free);
  return (displayCount > 1) ? 1 : 0;
#endif
}

bool GameEngine::verifyVSyncState(bool requested) {
  // Verify VSync state matches requested setting
  int vsyncState = 0;
  bool vsyncVerified = false;

  if (SDL_GetRenderVSync(mp_renderer.get(), &vsyncState)) {
    if (requested) {
      // When enabling, verify it's actually on
      vsyncVerified = (vsyncState > 0);
#ifdef DEBUG
      if (vsyncVerified) {
        GAMEENGINE_INFO(
            std::format("VSync enabled and verified (mode: {})", vsyncState));
      } else {
        GAMEENGINE_WARN(
            std::format("VSync set but verification failed (reported mode: {})",
                        vsyncState));
      }
#endif
    } else {
      // When disabling, verify it's actually off
      vsyncVerified = (vsyncState == 0);
#ifdef DEBUG
      if (vsyncVerified) {
        GAMEENGINE_INFO("VSync disabled and verified");
      } else {
        GAMEENGINE_WARN(
            std::format("VSync disable verification failed (reported mode: {})",
                        vsyncState));
      }
#endif
    }
  } else {
    GAMEENGINE_WARN(
        std::format("Could not verify VSync state: {}", SDL_GetError()));
    vsyncVerified = false;
  }

  // Set software frame limiting based on verification result
  // Use software limiting when: VSync should be on but verification failed, or
  // VSync is intentionally disabled
  bool useSoftwareLimiting = requested ? !vsyncVerified : true;
  if (m_timestepManager) {
    m_timestepManager->setSoftwareFrameLimiting(useSoftwareLimiting);
  }

  return vsyncVerified;
}

void GameEngine::onWindowResize(const SDL_Event &event) {
  // Centralized resize pipeline:
  // 1) Update GameEngine window size (authoritative source)
  // 2) Update SDL logical presentation (macOS: 1920x1080 letterbox; others:
  // native) 3) Reload fonts via FontManager for new display characteristics 4)
  // UI scales from logical size; UIManager layout recalculates on next render

  // Update GameEngine with new window dimensions
  int const newWidth = event.window.data1;
  int const newHeight = event.window.data2;

  GAMEENGINE_INFO(std::format("Window resized to: {}x{}", newWidth, newHeight));

  // Update GameEngine window dimensions
  setWindowSize(newWidth, newHeight);

  // Use native resolution rendering (all platforms) for crisp, sharp text
  // This matches the initialization approach in GameEngine and ensures
  // consistent rendering whether in windowed or fullscreen mode
  int actualWidth, actualHeight;
  if (!SDL_GetWindowSizeInPixels(mp_window.get(), &actualWidth,
                                 &actualHeight)) {
    GAMEENGINE_ERROR(std::format("Failed to get actual window pixel size: {}",
                                 SDL_GetError()));
    actualWidth = newWidth;
    actualHeight = newHeight;
  }

  // Update renderer to native resolution (no scaling)
  SDL_SetRenderLogicalPresentation(mp_renderer.get(), actualWidth, actualHeight,
                                   SDL_LOGICAL_PRESENTATION_DISABLED);

  // Update GameEngine's cached logical dimensions
  setLogicalSize(actualWidth, actualHeight);

  GAMEENGINE_INFO(std::format("Updated to native resolution: {}x{}",
                              actualWidth, actualHeight));

  // Reconfigure tier radii for new screen size
  mp_backgroundSimManager->configureForScreenSize(actualWidth, actualHeight);
  GAMEENGINE_INFO(std::format(
      "Tier radii reconfigured (active: {:.0f}, background: {:.0f})",
      mp_backgroundSimManager->getActiveRadius(),
      mp_backgroundSimManager->getBackgroundRadius()));

  // Reload fonts for new display configuration with DPI scale
  GAMEENGINE_INFO("Reloading fonts for display configuration change...");
  FontManager &fontManager = FontManager::Instance();
  if (!fontManager.reloadFontsForDisplay("res/fonts", m_windowWidth,
                                         m_windowHeight, m_dpiScale)) {
    GAMEENGINE_ERROR("Failed to reinitialize font system after window resize");
  } else {
    GAMEENGINE_INFO(
        "Font system reinitialized successfully after window resize");
  }

  // UIManager owns all UI positioning - directly call its resize handler
  UIManager::Instance().onWindowResize(getLogicalWidth(), getLogicalHeight());
  GAMEENGINE_INFO("UIManager notified for UI component repositioning");
}

void GameEngine::onWindowEvent(const SDL_Event &event) {
  switch (event.type) {
  case SDL_EVENT_WINDOW_MINIMIZED:
  case SDL_EVENT_WINDOW_OCCLUDED:
  case SDL_EVENT_WINDOW_HIDDEN:
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    if (!m_windowOccluded) {
      m_windowOccluded = true;
      if (m_timestepManager) {
        m_timestepManager->setSoftwareFrameLimiting(true);
      }
      GAMEENGINE_DEBUG("Window occluded - enabling software frame limiting");
    }
    break;
  case SDL_EVENT_WINDOW_RESTORED:
  case SDL_EVENT_WINDOW_SHOWN:
  case SDL_EVENT_WINDOW_EXPOSED:
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
    if (m_windowOccluded) {
      m_windowOccluded = false;
      bool vsyncVerified = false;
      if (m_timestepManager) {
        vsyncVerified = verifyVSyncState(m_vsyncRequested);
      }
      GAMEENGINE_DEBUG(std::format("Window visible - VSync {} (requested: {})",
                                   vsyncVerified ? "verified" : "not verified",
                                   m_vsyncRequested ? "enabled" : "disabled"));
    }
    break;
  default:
    break;
  }
}

void GameEngine::onDisplayChange(const SDL_Event &event) {
  // Centralized display-change pipeline:
  // - Log event and, on Apple, refresh fonts due to DPI/content-scale changes
  // - Normalize UI scale (UIManager::setGlobalScale(1.0f))
  // - Force UI layout refresh and reload fonts using GameEngine logical size

  const char *eventName = "Unknown";
  switch (event.type) {
  case SDL_EVENT_DISPLAY_ORIENTATION:
    eventName = "Orientation Change";
    break;
  case SDL_EVENT_DISPLAY_ADDED:
    eventName = "Display Added";
    break;
  case SDL_EVENT_DISPLAY_REMOVED:
    eventName = "Display Removed";
    break;
  case SDL_EVENT_DISPLAY_MOVED:
    eventName = "Display Moved";
    break;
  case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
    eventName = "Content Scale Changed";
    break;
  default:
    break;
  }

  GAMEENGINE_INFO(std::format("Display event detected: {}", eventName));

// On Apple platforms, display changes often invalidate font textures
// due to different DPI scaling or context changes
#ifdef __APPLE__
  GAMEENGINE_INFO(
      "Apple platform: Reinitializing font system due to display change...");
#else
  GAMEENGINE_INFO("Non-Apple platform: Display change handled by existing "
                  "window resize logic");
#endif

  // Update UI systems with consistent scaling and reload fonts ONCE using
  // logical dimensions
  try {
    UIManager &uiManager = UIManager::Instance();
    uiManager.setGlobalScale(1.0f);
    GAMEENGINE_INFO("Updated UIManager with consistent 1.0 scale");

    uiManager.cleanupForStateTransition();

    FontManager &fontManager = FontManager::Instance();
    if (!fontManager.reloadFontsForDisplay("res/fonts", m_windowWidth,
                                           m_windowHeight, m_dpiScale)) {
      GAMEENGINE_WARN("Failed to reload fonts for new display size");
    } else {
      GAMEENGINE_INFO("Successfully reloaded fonts for new display size");
    }

    // UIManager owns all UI positioning - trigger repositioning for display
    // change
    uiManager.onWindowResize(getLogicalWidth(), getLogicalHeight());
    GAMEENGINE_INFO("UIManager notified for display change repositioning");
  } catch (const std::exception &e) {
    GAMEENGINE_ERROR(std::format(
        "Error updating UI scaling after window resize: {}", e.what()));
  }
}
