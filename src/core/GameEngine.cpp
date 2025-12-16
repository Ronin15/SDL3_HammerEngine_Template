/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "core/GameEngine.hpp"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_surface.h"
#include "SDL3/SDL_video.h"
#include "core/GameLoop.hpp" // IWYU pragma: keep - Required for GameLoop weak_ptr declaration
#include "managers/GameTimeManager.hpp"
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
#include "managers/EventManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/GameStateManager.hpp"
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
#include "managers/CollisionManager.hpp"
#include <atomic>
#include <chrono>
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
    GAMEENGINE_CRITICAL(std::format("SDL initialization failed: {}", SDL_GetError()));
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

  // Texture memory management and text rendering optimization
  SDL_SetHint("SDL_HINT_RENDER_VSYNC", "1"); // Ensure VSync is properly handled
  SDL_SetHint("SDL_HINT_VIDEO_DOUBLE_BUFFER",
              "1"); // Enable double buffering for smoother rendering
  SDL_SetHint("SDL_HINT_FRAMEBUFFER_ACCELERATION",
              "1"); // Enable framebuffer acceleration

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
    GAMEENGINE_INFO(std::format("Using default window size: {}x{}", m_windowWidth, m_windowHeight));
  } else {
    // Use the provided dimensions
    m_windowWidth = width;
    m_windowHeight = height;
    GAMEENGINE_INFO(std::format("Using requested window size: {}x{}", m_windowWidth, m_windowHeight));
  }

  // Save windowed dimensions before fullscreen might override them
  m_windowedWidth = m_windowWidth;
  m_windowedHeight = m_windowHeight;

  // Query display capabilities for intelligent window sizing (all platforms)
  int displayCount = 0;
  std::unique_ptr<SDL_DisplayID[], decltype(&SDL_free)> displays(
      SDL_GetDisplays(&displayCount), SDL_free);

  const SDL_DisplayMode* displayMode = nullptr;
  if (displays && displayCount > 0) {
    displayMode = SDL_GetDesktopDisplayMode(displays[0]);
    if (displayMode) {
      GAMEENGINE_INFO(std::format("Primary display: {}x{} @ {}Hz",
                                  displayMode->w, displayMode->h, displayMode->refresh_rate));

      // If requested window size is larger than 90% of display, use fullscreen
      // This prevents awkward oversized windows on smaller displays (handhelds, laptops)
      const float displayUsageThreshold = 0.9f;
      if (!fullscreen &&
          (m_windowWidth > static_cast<int>(displayMode->w * displayUsageThreshold) ||
           m_windowHeight > static_cast<int>(displayMode->h * displayUsageThreshold))) {
        GAMEENGINE_INFO(std::format("Requested window size ({}x{}) is larger than {}% of display - enabling fullscreen for better UX",
                                    m_windowWidth, m_windowHeight, static_cast<int>(displayUsageThreshold * 100)));
        fullscreen = true;
      }
    }
  } else {
    GAMEENGINE_WARN("Could not query display capabilities - proceeding with requested dimensions");
  }
  // Window handling with platform-specific optimizations
  SDL_WindowFlags flags =
      fullscreen ? (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY) : 0;

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
      GAMEENGINE_INFO(std::format("Window size set to Full Screen: {}x{}", m_windowWidth, m_windowHeight));
    }
#endif
  }

  // Track initial fullscreen state
  m_isFullscreen = fullscreen;

  mp_window.reset(
      SDL_CreateWindow(title.data(), m_windowWidth, m_windowHeight, flags));

  if (!mp_window) {
    GAMEENGINE_ERROR(std::format("Failed to create window: {}", SDL_GetError()));
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
    GAMEENGINE_ERROR(std::format("Failed to get window pixel size: {}", SDL_GetError()));
  }
  if (!SDL_GetWindowSize(mp_window.get(), &logicalWidth, &logicalHeight)) {
    GAMEENGINE_ERROR(std::format("Failed to get window logical size: {}", SDL_GetError()));
  }

  // Create renderer (let SDL3 choose the best available backend)
  mp_renderer.reset(SDL_CreateRenderer(mp_window.get(), NULL));

  if (!mp_renderer) {
    GAMEENGINE_ERROR(std::format("Failed to create renderer: {}", SDL_GetError()));
    return false;
  }

  // Log which renderer backend SDL3 actually selected
  auto rendererName = SDL_GetRendererName(mp_renderer.get());
  if (rendererName) {
    GAMEENGINE_INFO(std::format("SDL3 selected renderer backend: {}", rendererName));
  } else {
    GAMEENGINE_WARN("Could not determine selected renderer backend");
  }

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

  // Log detected platform
  if (m_isWayland) {
    GAMEENGINE_INFO("Platform detected: Wayland");
  } else {
    GAMEENGINE_INFO(std::format("Platform detected: {}", videoDriver.empty() ? "Unknown" : std::string(videoDriver)));
  }

  // Load VSync preference from SettingsManager (defaults to enabled)
  auto& settings = HammerEngine::SettingsManager::Instance();
  bool vsyncRequested = settings.get<bool>("graphics", "vsync", true);
  GAMEENGINE_INFO(std::format("VSync setting from SettingsManager: {}", vsyncRequested ? "enabled" : "disabled"));

  // Attempt to set VSync based on user preference
  bool vsyncSetSuccessfully = SDL_SetRenderVSync(mp_renderer.get(), vsyncRequested ? 1 : 0);

  if (!vsyncSetSuccessfully) {
    GAMEENGINE_WARN(std::format("Failed to {} VSync: {}",
                                vsyncRequested ? "enable" : "disable", SDL_GetError()));
  }

  // Verify VSync state and update software frame limiting flag
  // verifyVSyncState() updates m_usingSoftwareFrameLimiting as a side effect
  if (vsyncSetSuccessfully) {
    verifyVSyncState(vsyncRequested);
  }

  // Log frame timing mode
  if (m_usingSoftwareFrameLimiting) {
    if (vsyncRequested) {
      GAMEENGINE_INFO("Using software frame limiting (VSync unavailable or failed verification)");
    } else {
      GAMEENGINE_INFO("Using software frame limiting (VSync disabled by user)");
    }
  } else {
    GAMEENGINE_INFO("Using hardware VSync for frame timing");
  }

  // Load buffer configuration from SettingsManager (graphics settings)
  m_bufferCount = static_cast<size_t>(settings.get<int>("graphics", "buffer_count", 2));
  // Clamp to valid range [2, 3]
  if (m_bufferCount < 2) m_bufferCount = 2;
  if (m_bufferCount > 3) m_bufferCount = 3;
  GAMEENGINE_INFO(std::format("Buffer mode: {} buffering configured",
                              m_bufferCount == 2 ? "Double(2)" : "Triple(3)"));

#ifdef DEBUG
  // Buffer telemetry is disabled by default, press F3 to enable console logging
  GAMEENGINE_INFO("Buffer telemetry: OFF (F3 to enable console logging every 5s)");
#endif

  if (!SDL_SetRenderDrawColor(
          mp_renderer.get(),
          HAMMER_GRAY)) { // Hammer Game Engine gunmetal dark grey
    GAMEENGINE_ERROR(std::format("Failed to set initial render draw color: {}", SDL_GetError()));
  }
  // Use native resolution rendering on all platforms for crisp, sharp text
  // This eliminates GPU scaling blur and provides consistent cross-platform behavior
  int actualWidth = pixelWidth;
  int actualHeight = pixelHeight;

  // Store actual dimensions for UI positioning (no scaling needed)
  m_logicalWidth = actualWidth;
  m_logicalHeight = actualHeight;

  // Disable logical presentation to render at native resolution
  SDL_RendererLogicalPresentation presentationMode =
      SDL_LOGICAL_PRESENTATION_DISABLED;
  if (!SDL_SetRenderLogicalPresentation(mp_renderer.get(), actualWidth,
                                        actualHeight, presentationMode)) {
    GAMEENGINE_ERROR(std::format("Failed to set render logical presentation: {}", SDL_GetError()));
  }

  GAMEENGINE_INFO(std::format("Using native resolution for crisp rendering: {}x{}",
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
    GAMEENGINE_INFO(std::format("macOS display content scale: {} {}",
                                dpiScale, displayScale > 0.0f ? "(detected)" : "(fallback)"));
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

  GAMEENGINE_INFO(std::format("DPI scale: {}, window: {}x{}",
                              dpiScale, m_windowWidth, m_windowHeight));

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

  // Initialize input manager in a background thread - #2
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
            GAMEENGINE_INFO(
                "Detecting and initializing gamepads and input handling");
            InputManager &inputMgr = InputManager::Instance();
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

            // Use logical dimensions to match UI coordinate system
            constexpr std::string_view fontsPath = "res/fonts";
            if (!fontMgr.loadFontsForDisplay(std::string(fontsPath),
                                             m_logicalWidth, m_logicalHeight)) {
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
  GAMEENGINE_INFO("Waiting for AIManager dependencies (PathfinderManager, CollisionManager)");
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
    GAMEENGINE_CRITICAL(std::format("AIManager dependency initialization threw exception: {}", e.what()));
    return false;
  }
  GAMEENGINE_INFO("AIManager dependencies initialized successfully");

  // Initialize AI Manager - #8
  // Dependencies satisfied: PathfinderManager and CollisionManager are now fully initialized
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
  mp_gameStateManager->addState(std::make_unique<LoadingState>());  // Shared loading screen state
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

    // Verify managers are still responding after caching
    try {
      // Test AI Manager responsiveness
      size_t entityCount = mp_aiManager->getManagedEntityCount();
      GAMEENGINE_DEBUG(std::format("AIManager cached successfully, managing {} entities", entityCount));

      // Test Event Manager responsiveness (just verify it's initialized)
      GAMEENGINE_DEBUG("EventManager cached successfully and initialized");

      // Test Particle Manager responsiveness
      size_t activeParticles = mp_particleManager->getActiveParticleCount();
      GAMEENGINE_DEBUG(std::format("ParticleManager cached successfully, {} active particles", activeParticles));
    } catch (const std::exception &e) {
      GAMEENGINE_CRITICAL(std::format("Manager validation failed after caching: {}", e.what()));
      return false;
    }

    GAMEENGINE_INFO("Manager references cached and validated successfully");
  } catch (const std::exception &e) {
    GAMEENGINE_ERROR(std::format("Error caching manager references: {}", e.what()));
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
    GAMEENGINE_ERROR(std::format("Error setting up manager cross-dependencies: {}", e.what()));
    return false;
  }

  GAMEENGINE_INFO(std::format("Game {} initialized successfully!", title));
  GAMEENGINE_INFO(std::format("Running {} <]==={{}}", title));

  // Initialize buffer system for first frame
  m_currentBufferIndex.store(0, std::memory_order_release);
  m_renderBufferIndex.store(0, std::memory_order_release);

  // Mark first buffer as ready to ensure render() has a valid buffer before the first
  // update() completes. This prevents a render stall on the very first frame where
  // hasNewFrameToRender() would otherwise return false. The initial "frame" is just
  // the cleared screen (HAMMER_GRAY background) set up above.
  m_bufferReady[0].store(true, std::memory_order_release);
  m_bufferReady[1].store(false, std::memory_order_release);

  // Initialize frame counters
  m_lastUpdateFrame.store(0, std::memory_order_release);
  m_lastRenderedFrame.store(0, std::memory_order_release);

  // NOTE: Initial state will be pushed after GameLoop setup is complete
  // This ensures the game loop is ready to handle state updates before any
  // state enters

  return true;
}

void GameEngine::handleEvents() {
  // SDL event polling - GameEngine owns the event loop as it owns the window/renderer
  // InputManager receives input events and maintains input state
  InputManager &inputMgr = InputManager::Instance();

  // Clear previous frame's pressed keys before processing new events
  inputMgr.clearFrameInput();

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    // Convert window coordinates to logical coordinates for mouse events
    SDL_ConvertEventToRenderCoordinates(mp_renderer.get(), &event);

    switch (event.type) {
      case SDL_EVENT_QUIT:
        GAMEENGINE_INFO("Shutting down! {}===]>");
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

#ifdef DEBUG
  // Global buffer telemetry console logging toggle (F3 key) - debug builds only
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_F3)) {
    bool currentState = m_showBufferTelemetry.load(std::memory_order_relaxed);
    m_showBufferTelemetry.store(!currentState, std::memory_order_relaxed);
    GAMEENGINE_INFO(std::format("Buffer telemetry console logging {} (F3 to toggle)",
                                !currentState ? "ENABLED (every 5s)" : "DISABLED"));
  }
#endif

  // Handle game state input on main thread where SDL events are processed (SDL3
  // requirement) This prevents cross-thread input state access between main
  // thread and update worker thread
  mp_gameStateManager->handleInput();
}

void GameEngine::setRunning(bool running) {
  if (auto gameLoop = m_gameLoop.lock()) {
    if (running) {
      // Can't restart GameLoop from GameEngine - this might be an error case
      GAMEENGINE_WARN("Cannot start GameLoop from GameEngine - use "
                      "GameLoop::run() instead");
    } else {
      gameLoop->stop();
    }
  } else {
    GAMEENGINE_WARN("No GameLoop set - cannot change running state");
  }
}

bool GameEngine::getRunning() const {
  if (auto gameLoop = m_gameLoop.lock()) {
    return gameLoop->isRunning();
  }
  return false;
}

float GameEngine::getCurrentFPS() const {
  if (auto gameLoop = m_gameLoop.lock()) {
    return gameLoop->getCurrentFPS();
  }
  return 0.0f;
}

void GameEngine::update(float deltaTime) {
  // This method is now thread-safe and can be called from a worker thread
#ifdef DEBUG
  // TELEMETRY POINT 7: Start timing mutex wait
  auto mutexWaitStart = std::chrono::high_resolution_clock::now();
#endif

  std::lock_guard<std::mutex> lock(m_updateMutex);

#ifdef DEBUG
  // TELEMETRY POINT 8: Mutex acquired - calculate wait time
  auto mutexAcquired = std::chrono::high_resolution_clock::now();
  double mutexWaitMs = std::chrono::duration<double, std::milli>(mutexAcquired - mutexWaitStart).count();
#endif

  // Mark update as running with relaxed ordering (protected by mutex)
  m_updateRunning.store(true, std::memory_order_relaxed);

  // Get the buffer for the current update
  const size_t updateBufferIndex =
      m_currentBufferIndex.load(std::memory_order_acquire);

  // OPTIMAL MANAGER UPDATE ARCHITECTURE - CLEAN DESIGN
  // ===================================================
  // Update order optimized for both player responsiveness AND smooth NPC movement.
  // Key design: AIManager handles batch synchronization internally (implementation detail).
  //
  // UPDATE STRATEGY:
  // - Events FIRST (can trigger state changes)
  // - Player movement SECOND (before heavy AI processing)
  // - AI processes batches (parallel internally, waits for completion before returning)
  // - Collision gets guaranteed-complete updates (no timing issues)
  //
  // GLOBAL SYSTEMS (Updated by GameEngine):
  // - EventManager: Global game events (weather, scene changes), batch processing
  // - GameStateManager: Player movement and state-specific logic
  // - AIManager: Parallel batch processing with internal sync (self-contained)
  // - ParticleManager: Global particle system with weather integration
  // - PathfinderManager: Periodic pathfinding grid updates (every 300/600 frames)
  // - CollisionManager: Collision detection and resolution for all entities
  // - InputManager: Handled in handleEvents() for proper SDL event polling architecture
  //
  // STATE-MANAGED SYSTEMS (Updated by individual states):
  // - UIManager: Optional, state-specific, only updated when UI is actually used
  //   See UIExampleState::update() for proper state-managed pattern

  // 1. Event system - FIRST: process global events, state changes, weather triggers
  if (mp_eventManager) {
    mp_eventManager->update();
  } else {
    GAMEENGINE_ERROR("EventManager cache is null!");
  }

  // 2. Game states - player movement and state logic
  mp_gameStateManager->update(deltaTime);

  // 3. AI system - processes NPC behaviors with internal parallelization
  //    Batches run in parallel, waits for completion internally before returning.
  //    From GameEngine perspective: just a regular update call (sync is internal).
  if (mp_aiManager) {
    mp_aiManager->update(deltaTime);
  } else {
    GAMEENGINE_ERROR("AIManager cache is null!");
  }

  // 4. Particle system - global weather and effect particles
  if (mp_particleManager) {
    mp_particleManager->update(deltaTime);
  } else {
    GAMEENGINE_ERROR("ParticleManager cache is null!");
  }

  // 5. Pathfinding system - periodic grid updates (every 300/600 frames)
  // PathfinderManager initialized by AIManager, cached by GameEngine for performance
  if (mp_pathfinderManager) {
    mp_pathfinderManager->update();
  } else {
    GAMEENGINE_ERROR("PathfinderManager cache is null!");
  }

  // 6. Collision system - LAST: processes complete NPC updates from AIManager
  //    AIManager guarantees all batches complete before returning, so collision
  //    always receives complete, consistent updates (no partial/stale data).
  if (mp_collisionManager) {
    mp_collisionManager->update(deltaTime);
  } else {
    GAMEENGINE_ERROR("CollisionManager cache is null!");
  }

  // Increment the frame counter atomically for thread-safe render
  // synchronization
  m_lastUpdateFrame.fetch_add(1, std::memory_order_relaxed);

  // Mark this buffer as ready for rendering
  m_bufferReady[updateBufferIndex].store(true, std::memory_order_release);

#ifdef DEBUG
  // TELEMETRY POINT 9: Buffer marked ready - record timing sample
  auto bufferReadyTime = std::chrono::high_resolution_clock::now();
  double bufferReadyDelayMs = std::chrono::duration<double, std::milli>(bufferReadyTime - mutexAcquired).count();
  m_bufferTelemetry.addTimingSample(mutexWaitMs, bufferReadyDelayMs);

  // Periodic telemetry logging (every 300 frames ~5s @ 60fps)
  // Only logs when F3 toggle is enabled, otherwise silent
  uint64_t currentFrame = m_lastUpdateFrame.load(std::memory_order_relaxed);

  if (m_showBufferTelemetry.load(std::memory_order_relaxed) &&
      currentFrame - m_telemetryLogFrame >= TELEMETRY_LOG_INTERVAL) {
    m_telemetryLogFrame = currentFrame;

    GAMEENGINE_DEBUG(std::format("=== Buffer Telemetry Report (last {} frames) ===", TELEMETRY_LOG_INTERVAL));
    GAMEENGINE_DEBUG(std::format("  Buffer Mode: {}", m_bufferCount == 2 ? "Double(2)" : "Triple(3)"));
    GAMEENGINE_DEBUG(std::format("  Swap Success Rate: {}%", m_bufferTelemetry.getSwapSuccessRate()));
    GAMEENGINE_DEBUG(std::format("  Swap Stats: {} success / {} blocked / {} CAS failures",
                                 m_bufferTelemetry.swapSuccesses.load(std::memory_order_relaxed),
                                 m_bufferTelemetry.swapBlocked.load(std::memory_order_relaxed),
                                 m_bufferTelemetry.casFailures.load(std::memory_order_relaxed)));
    GAMEENGINE_DEBUG(std::format("  Render Stalls: {}", m_bufferTelemetry.renderStalls.load(std::memory_order_relaxed)));
    GAMEENGINE_DEBUG(std::format("  Frames Skipped: {}", m_bufferTelemetry.framesSkipped.load(std::memory_order_relaxed)));
    GAMEENGINE_DEBUG(std::format("  Avg Mutex Wait: {}ms", m_bufferTelemetry.avgMutexWaitMs.load(std::memory_order_relaxed)));
    GAMEENGINE_DEBUG(std::format("  Avg Buffer Ready Delay: {}ms", m_bufferTelemetry.avgBufferReadyMs.load(std::memory_order_relaxed)));
    GAMEENGINE_DEBUG(std::format("  Current Buffers: [Write:{} Read:{}]",
                                 m_currentBufferIndex.load(std::memory_order_relaxed),
                                 m_renderBufferIndex.load(std::memory_order_relaxed)));

    // Reset counters for next interval
    m_bufferTelemetry.reset();
  }
#endif

  // Mark update as completed with relaxed ordering (protected by condition
  // variable)
  m_updateCompleted.store(true, std::memory_order_relaxed);

  m_updateRunning.store(false, std::memory_order_relaxed);

  // Notify anyone waiting on this update
  m_updateCondition.notify_all();
  m_bufferCondition.notify_all();
}

void GameEngine::render() {
  // Always on MAIN thread as its an - SDL REQUIREMENT
  std::lock_guard<std::mutex> lock(m_renderMutex);

  // Calculate interpolation alpha from GameLoop's TimestepManager
  // This enables smooth rendering at any refresh rate with fixed 60Hz updates
  float interpolationAlpha = 1.0f;
  if (auto gameLoop = m_gameLoop.lock()) {
    interpolationAlpha = static_cast<float>(
        gameLoop->getTimestepManager().getInterpolationAlpha());
  }

  // Always render - optimized buffer management ensures render buffer is always
  // valid
  if (!SDL_SetRenderDrawColor(
          mp_renderer.get(),
          HAMMER_GRAY)) { // Hammer Game Engine gunmetal dark grey
    GAMEENGINE_ERROR(std::format("Failed to set render draw color: {}", SDL_GetError()));
  }
  if (!SDL_RenderClear(mp_renderer.get())) {
    GAMEENGINE_ERROR(std::format("Failed to clear renderer: {}", SDL_GetError()));
  }

  // Pass renderer and interpolation alpha to GameStateManager for state rendering
  mp_gameStateManager->render(mp_renderer.get(), interpolationAlpha);

  if (!SDL_RenderPresent(mp_renderer.get())) {
    GAMEENGINE_ERROR(std::format("Failed to present renderer: {}", SDL_GetError()));
  }

  // After presenting, mark the render buffer as consumed to avoid stale
  // re-renders
  size_t renderIndex = m_renderBufferIndex.load(std::memory_order_acquire);
  m_bufferReady[renderIndex].store(false, std::memory_order_release);

  // Increment rendered frame counter for fast synchronization
  m_lastRenderedFrame.fetch_add(1, std::memory_order_relaxed);
}

void GameEngine::swapBuffers() {
  // Thread-safe buffer swap with proper synchronization
  // Supports both double (2) and triple (3) buffering modes
  size_t currentIndex = m_currentBufferIndex.load(std::memory_order_acquire);
  size_t nextUpdateIndex = (currentIndex + 1) % m_bufferCount;  // Use runtime buffer count

#ifdef DEBUG
  // TELEMETRY POINT 1: Track swap attempt
  m_bufferTelemetry.swapAttempts.fetch_add(1, std::memory_order_relaxed);
#endif

  // TRIPLE BUFFERING OPTIMIZATION:
  // With 3 buffers, there's always a free buffer available (one for update, one for render, one free).
  // With 2 buffers, we must check that next buffer isn't being rendered to prevent overwriting.

  bool canSwap = m_bufferReady[currentIndex].load(std::memory_order_acquire);

  if (m_bufferCount == 2) {
    // Double buffering: also check that next buffer isn't being rendered
    size_t currentRenderIndex = m_renderBufferIndex.load(std::memory_order_acquire);
    canSwap = canSwap && (nextUpdateIndex != currentRenderIndex);
  }
  // Triple buffering: always has a free buffer, no additional check needed

  if (canSwap) {
    // Atomic compare-exchange to ensure no race condition
    size_t expected = currentIndex;
    if (m_currentBufferIndex.compare_exchange_strong(
            expected, nextUpdateIndex, std::memory_order_acq_rel)) {
      // Successfully swapped update buffer
      // Make previous update buffer available for rendering
      m_renderBufferIndex.store(currentIndex, std::memory_order_release);

      // Clear the next buffer's ready state for the next update cycle
      m_bufferReady[nextUpdateIndex].store(false, std::memory_order_release);

      // Signal buffer swap completion
      m_bufferCondition.notify_one();

#ifdef DEBUG
      // TELEMETRY POINT 2: Successful swap
      m_bufferTelemetry.swapSuccesses.fetch_add(1, std::memory_order_relaxed);
#endif
    } else {
#ifdef DEBUG
      // TELEMETRY POINT 3: CAS failure (atomic contention)
      m_bufferTelemetry.casFailures.fetch_add(1, std::memory_order_relaxed);
#endif
    }
  } else {
#ifdef DEBUG
    // TELEMETRY POINT 4: Swap blocked (buffer not ready or rendering conflict)
    m_bufferTelemetry.swapBlocked.fetch_add(1, std::memory_order_relaxed);
#endif
  }
}

bool GameEngine::hasNewFrameToRender() const noexcept {
  // Optimized render check with minimal atomic operations
  size_t renderIndex = m_renderBufferIndex.load(std::memory_order_acquire);

  // Single check for buffer readiness
  if (!m_bufferReady[renderIndex].load(std::memory_order_acquire)) {
#ifdef DEBUG
    // TELEMETRY POINT 5: Render stall (no buffer ready)
    m_bufferTelemetry.renderStalls.fetch_add(1, std::memory_order_relaxed);
#endif
    return false;
  }

  // Compare frame counters only if buffer is ready - use relaxed ordering for
  // counters
  uint64_t lastUpdate = m_lastUpdateFrame.load(std::memory_order_relaxed);
  uint64_t lastRendered = m_lastRenderedFrame.load(std::memory_order_relaxed);

#ifdef DEBUG
  // TELEMETRY POINT 6: Frame skipped (update hasn't advanced)
  if (lastUpdate == lastRendered) {
    m_bufferTelemetry.framesSkipped.fetch_add(1, std::memory_order_relaxed);
  }
#endif

  return lastUpdate > lastRendered;
}

bool GameEngine::isUpdateRunning() const noexcept {
  return m_updateRunning.load(std::memory_order_relaxed);
}

size_t GameEngine::getCurrentBufferIndex() const noexcept {
  return m_currentBufferIndex.load(std::memory_order_relaxed);
}

size_t GameEngine::getRenderBufferIndex() const noexcept {
  return m_renderBufferIndex.load(std::memory_order_relaxed);
}

void GameEngine::processBackgroundTasks() {
  // Background task processing hook for non-critical work
  //
  // PURPOSE: Provides a designated entry point for background processing that:
  //   - Runs on worker threads via ThreadSystem (NOT the main thread)
  //   - Executes asynchronously while main thread handles events/rendering
  //   - Is separate from the main update loop to avoid impacting frame timing
  //
  // USE CASES:
  //   - Asset pre-loading for upcoming game states
  //   - Background save game serialization
  //   - Analytics/telemetry data collection
  //   - Periodic cache cleanup or memory defragmentation
  //   - Network polling for non-latency-critical updates
  //
  // ARCHITECTURE NOTE:
  //   Global systems (EventManager, AIManager, etc.) are updated in the main
  //   update loop for deterministic ordering and proper synchronization.
  //   This method is for truly asynchronous, non-critical tasks only.
  //
  // THREAD SAFETY:
  //   Any work added here must be thread-safe and not require main-thread
  //   resources (SDL rendering, UI state, etc.).

  try {
    // Background processing tasks can be added here
    // Currently a placeholder - extend as needed for specific use cases
  } catch (const std::exception &e) {
    GAMEENGINE_ERROR(std::format("Exception in background tasks: {}", e.what()));
  } catch (...) {
    GAMEENGINE_ERROR("Unknown exception in background tasks");
  }
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
      GAMEENGINE_ERROR(std::format("Failed to get window size for logical presentation: {}", SDL_GetError()));
    }
    if (!SDL_SetRenderLogicalPresentation(mp_renderer.get(), width, height,
                                          mode)) {
      GAMEENGINE_ERROR(std::format("Failed to set render logical presentation: {}", SDL_GetError()));
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

  // Signal all threads to stop
  m_stopRequested.store(true, std::memory_order_release);
  m_updateCondition.notify_all();
  m_bufferCondition.notify_all();

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

  GAMEENGINE_INFO("Cleaning up AI Manager...");
  AIManager::Instance().clean();

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

  // Close gamepad handles right before SDL_Quit to ensure proper cleanup order
  GAMEENGINE_INFO("Closing gamepad handles...");
  InputManager::Instance().closeGamepads();

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

  GAMEENGINE_INFO(std::format("{} VSync...", enable ? "Enabling" : "Disabling"));

  // Attempt to set VSync
  bool vsyncSetSuccessfully = SDL_SetRenderVSync(mp_renderer.get(), enable ? 1 : 0);

  if (!vsyncSetSuccessfully) {
    GAMEENGINE_ERROR(std::format("Failed to {} VSync: {}", enable ? "enable" : "disable", SDL_GetError()));
    // Ensure software frame limiting is enabled if VSync enable failed
    if (enable) {
      m_usingSoftwareFrameLimiting = true;
      GAMEENGINE_INFO("Falling back to software frame limiting");

      // Update TimestepManager
      if (auto gameLoop = m_gameLoop.lock()) {
        gameLoop->getTimestepManager().setSoftwareFrameLimiting(true);
      }
    }
    return false;
  }

  // Verify VSync state and update software frame limiting flag
  bool vsyncVerified = verifyVSyncState(enable);

  // Update TimestepManager
  if (auto gameLoop = m_gameLoop.lock()) {
    gameLoop->getTimestepManager().setSoftwareFrameLimiting(m_usingSoftwareFrameLimiting);
    GAMEENGINE_DEBUG(std::format("TimestepManager updated: software frame limiting {}",
                                 m_usingSoftwareFrameLimiting ? "enabled" : "disabled"));
  }

  // Log frame timing mode
  if (m_usingSoftwareFrameLimiting && enable) {
    GAMEENGINE_INFO("Using software frame limiting (VSync verification failed)");
  } else if (!m_usingSoftwareFrameLimiting) {
    GAMEENGINE_INFO("Using hardware VSync for frame timing");
  } else {
    GAMEENGINE_INFO("VSync disabled, using software frame limiting");
  }

  // Save VSync setting to SettingsManager for persistence
  auto& settings = HammerEngine::SettingsManager::Instance();
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

  GAMEENGINE_INFO(std::format("Toggling fullscreen mode: {} (windowed size: {}x{})",
                              m_isFullscreen ? "ON" : "OFF", m_windowedWidth, m_windowedHeight));

#ifdef __APPLE__
  // macOS: Use borderless fullscreen desktop mode for better compatibility
  if (m_isFullscreen) {
    if (!SDL_SetWindowFullscreen(mp_window.get(), true)) {
      GAMEENGINE_ERROR(std::format("Failed to enable fullscreen: {}", SDL_GetError()));
      m_isFullscreen = false; // Revert state on failure
      return;
    }
    // Set to borderless fullscreen desktop mode (nullptr = use desktop mode)
    if (!SDL_SetWindowFullscreenMode(mp_window.get(), nullptr)) {
      GAMEENGINE_WARN(std::format("Failed to set borderless fullscreen mode: {}", SDL_GetError()));
    }
    GAMEENGINE_INFO("macOS: Enabled borderless fullscreen desktop mode");
  } else {
    if (!SDL_SetWindowFullscreen(mp_window.get(), false)) {
      GAMEENGINE_ERROR(std::format("Failed to disable fullscreen: {}", SDL_GetError()));
      m_isFullscreen = true; // Revert state on failure
      return;
    }
    // Restore windowed size
    if (!SDL_SetWindowSize(mp_window.get(), m_windowedWidth, m_windowedHeight)) {
      GAMEENGINE_ERROR(std::format("Failed to restore window size: {}", SDL_GetError()));
    } else {
      GAMEENGINE_INFO(std::format("macOS: Restored window size to {}x{}", m_windowedWidth, m_windowedHeight));
    }
  }
#else
  // Other platforms: Use standard fullscreen toggle
  if (!SDL_SetWindowFullscreen(mp_window.get(), m_isFullscreen)) {
    GAMEENGINE_ERROR(std::format("Failed to toggle fullscreen: {}", SDL_GetError()));
    m_isFullscreen = !m_isFullscreen; // Revert state on failure
    return;
  }

  // Restore windowed size when exiting fullscreen
  if (!m_isFullscreen) {
    if (!SDL_SetWindowSize(mp_window.get(), m_windowedWidth, m_windowedHeight)) {
      GAMEENGINE_ERROR(std::format("Failed to restore window size: {}", SDL_GetError()));
    } else {
      GAMEENGINE_INFO(std::format("Restored window size to {}x{}", m_windowedWidth, m_windowedHeight));
    }
  }

  GAMEENGINE_INFO(std::format("Fullscreen mode {}", m_isFullscreen ? "enabled" : "disabled"));
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
  m_globallyPaused.store(paused, std::memory_order_release);

  // Pause AI Manager (already has setGlobalPause support)
  if (mp_aiManager) {
    mp_aiManager->setGlobalPause(paused);
  }

  // Pause Particle Manager
  if (mp_particleManager) {
    mp_particleManager->setGlobalPause(paused);
  }

  // Pause Collision Manager
  if (mp_collisionManager) {
    mp_collisionManager->setGlobalPause(paused);
  }

  // Pause Pathfinder Manager
  if (mp_pathfinderManager) {
    mp_pathfinderManager->setGlobalPause(paused);
  }

  // Pause GameTime Manager
  GameTimeManager::Instance().setGlobalPause(paused);

  if (paused) {
    GAMEENGINE_INFO("Game globally paused - all managers idle");
  } else {
    GAMEENGINE_INFO("Game globally resumed");
  }
}

bool GameEngine::isGloballyPaused() const {
  return m_globallyPaused.load(std::memory_order_acquire);
}

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
      if (vsyncVerified) {
        GAMEENGINE_INFO(std::format("VSync enabled and verified (mode: {})", vsyncState));
      } else {
        GAMEENGINE_WARN(std::format("VSync set but verification failed (reported mode: {})", vsyncState));
      }
    } else {
      // When disabling, verify it's actually off
      vsyncVerified = (vsyncState == 0);
      if (vsyncVerified) {
        GAMEENGINE_INFO("VSync disabled and verified");
      } else {
        GAMEENGINE_WARN(std::format("VSync disable verification failed (reported mode: {})", vsyncState));
      }
    }
  } else {
    GAMEENGINE_WARN(std::format("Could not verify VSync state: {}", SDL_GetError()));
    vsyncVerified = false;
  }

  // Update software frame limiting flag based on verification result
  // Use software limiting when: VSync should be on but verification failed, or VSync is intentionally disabled
  m_usingSoftwareFrameLimiting = requested ? !vsyncVerified : true;

  return vsyncVerified;
}

void GameEngine::onWindowResize(const SDL_Event& event) {
  // Centralized resize pipeline:
  // 1) Update GameEngine window size (authoritative source)
  // 2) Update SDL logical presentation (macOS: 1920x1080 letterbox; others: native)
  // 3) Reload fonts via FontManager for new display characteristics
  // 4) UI scales from logical size; UIManager layout recalculates on next render

  // Update GameEngine with new window dimensions
  int newWidth = event.window.data1;
  int newHeight = event.window.data2;

  GAMEENGINE_INFO(std::format("Window resized to: {}x{}", newWidth, newHeight));

  // Update GameEngine window dimensions
  setWindowSize(newWidth, newHeight);

  // Use native resolution rendering (all platforms) for crisp, sharp text
  // This matches the initialization approach in GameEngine and ensures
  // consistent rendering whether in windowed or fullscreen mode
  int actualWidth, actualHeight;
  if (!SDL_GetWindowSizeInPixels(mp_window.get(), &actualWidth, &actualHeight)) {
    GAMEENGINE_ERROR(std::format("Failed to get actual window pixel size: {}", SDL_GetError()));
    actualWidth = newWidth;
    actualHeight = newHeight;
  }

  // Update renderer to native resolution (no scaling)
  SDL_SetRenderLogicalPresentation(mp_renderer.get(), actualWidth, actualHeight, SDL_LOGICAL_PRESENTATION_DISABLED);

  // Update GameEngine's cached logical dimensions
  setLogicalSize(actualWidth, actualHeight);

  GAMEENGINE_INFO(std::format("Updated to native resolution: {}x{}", actualWidth, actualHeight));

  // Reload fonts for new display configuration
  GAMEENGINE_INFO("Reloading fonts for display configuration change...");
  FontManager& fontManager = FontManager::Instance();
  if (!fontManager.reloadFontsForDisplay("res/fonts", getLogicalWidth(), getLogicalHeight())) {
    GAMEENGINE_ERROR("Failed to reinitialize font system after window resize");
  } else {
    GAMEENGINE_INFO("Font system reinitialized successfully after window resize");
  }

  // UIManager owns all UI positioning - directly call its resize handler
  UIManager::Instance().onWindowResize(getLogicalWidth(), getLogicalHeight());
  GAMEENGINE_INFO("UIManager notified for UI component repositioning");
}

void GameEngine::onDisplayChange(const SDL_Event& event) {
  // Centralized display-change pipeline:
  // - Log event and, on Apple, refresh fonts due to DPI/content-scale changes
  // - Normalize UI scale (UIManager::setGlobalScale(1.0f))
  // - Force UI layout refresh and reload fonts using GameEngine logical size

  const char* eventName = "Unknown";
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
  }

  GAMEENGINE_INFO(std::format("Display event detected: {}", eventName));

  // On Apple platforms, display changes often invalidate font textures
  // due to different DPI scaling or context changes
  #ifdef __APPLE__
  GAMEENGINE_INFO("Apple platform: Reinitializing font system due to display change...");
  #else
  GAMEENGINE_INFO("Non-Apple platform: Display change handled by existing window resize logic");
  #endif

  // Update UI systems with consistent scaling and reload fonts ONCE using logical dimensions
  try {
    UIManager& uiManager = UIManager::Instance();
    uiManager.setGlobalScale(1.0f);
    GAMEENGINE_INFO("Updated UIManager with consistent 1.0 scale");

    uiManager.cleanupForStateTransition();

    FontManager& fontManager = FontManager::Instance();
    if (!fontManager.reloadFontsForDisplay("res/fonts", getLogicalWidth(), getLogicalHeight())) {
      GAMEENGINE_WARN("Failed to reload fonts for new display size");
    } else {
      GAMEENGINE_INFO("Successfully reloaded fonts for new display size");
    }

    // UIManager owns all UI positioning - trigger repositioning for display change
    uiManager.onWindowResize(getLogicalWidth(), getLogicalHeight());
    GAMEENGINE_INFO("UIManager notified for display change repositioning");
  } catch (const std::exception& e) {
    GAMEENGINE_ERROR(std::format("Error updating UI scaling after window resize: {}", e.what()));
  }
}
