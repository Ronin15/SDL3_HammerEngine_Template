/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "core/GameEngine.hpp"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_surface.h"
#include "SDL3/SDL_video.h"
#include "core/GameLoop.hpp" // IWYU pragma: keep - Required for GameLoop weak_ptr declaration
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "gameStates/AIDemoState.hpp"
#include "gameStates/AdvancedAIDemoState.hpp"
#include "gameStates/EventDemoState.hpp"
#include "gameStates/GamePlayState.hpp"
#include "gameStates/LogoState.hpp"
#include "gameStates/MainMenuState.hpp"
#include "gameStates/OverlayDemoState.hpp"
#include "gameStates/UIDemoState.hpp"
#include "managers/AIManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/ParticleManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/SaveGameManager.hpp"
#include "managers/SoundManager.hpp"
#include "managers/TextureManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include <chrono>
#include <cstdlib>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#define HAMMER_GRAY 31, 32, 34, 255

bool GameEngine::init(const std::string_view title, const int width,
                      const int height, bool fullscreen) {
  GAMEENGINE_INFO("Initializing SDL Video");

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    GAMEENGINE_CRITICAL("SDL Video initialization failed: " +
                        std::string(SDL_GetError()));
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

  GAMEENGINE_INFO("SDL rendering hints configured for optimal quality");

  // Use reliable window sizing approach instead of potentially corrupted
  // display bounds
  if (width <= 0 || height <= 0) {
    // Default to reasonable size if not specified
    m_windowWidth = 1280;
    m_windowHeight = 720;
    GAMEENGINE_INFO(
        "Using default window size: " + std::to_string(m_windowWidth) + "x" +
        std::to_string(m_windowHeight));
  } else {
    // Use the provided dimensions
    m_windowWidth = width;
    m_windowHeight = height;
    GAMEENGINE_INFO(
        "Using requested window size: " + std::to_string(m_windowWidth) + "x" +
        std::to_string(m_windowHeight));
  }

// For macOS compatibility, use fullscreen for large window requests
#ifdef __APPLE__
  if (m_windowWidth >= 1920 || m_windowHeight >= 1080) {
    if (!fullscreen) { // Only log if we're changing it
      GAMEENGINE_INFO(
          "Large window on macOS - enabling fullscreen for compatibility");
    }
    fullscreen = true;
  }
#endif
  // Window handling with platform-specific optimizations
  SDL_WindowFlags flags =
      fullscreen ? (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY) : 0;

  if (fullscreen) {

#ifdef __APPLE__
    // On macOS, keep logical dimensions for proper scaling
    // Don't override m_windowWidth and m_windowHeight for macOS
    GAMEENGINE_INFO("Window set to Fullscreen mode for macOS compatibility");
#else
    // setting window width and height to fullscreen dimensions for detected
    // monitor
    int displayCount = 0;
    std::unique_ptr<SDL_DisplayID[], decltype(&SDL_free)> displays(
        SDL_GetDisplays(&displayCount), SDL_free);
    if (displays && displayCount > 0) {
      const SDL_DisplayMode *displayMode =
          SDL_GetDesktopDisplayMode(displays[0]);
      if (displayMode) {
        m_windowWidth = displayMode->w;
        m_windowHeight = displayMode->h;
        GAMEENGINE_INFO(
            "Window size set to Full Screen: " + std::to_string(m_windowWidth) +
            "x" + std::to_string(m_windowHeight));
      }
    }
#endif
  }

  mp_window.reset(
      SDL_CreateWindow(title.data(), m_windowWidth, m_windowHeight, flags));

  if (!mp_window) {
    GAMEENGINE_ERROR("Failed to create window: " + std::string(SDL_GetError()));
    return false;
  }

  GAMEENGINE_INFO("Window creation system online");

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

  // Use SDL_image to directly load the icon
  constexpr std::string_view iconPath = "res/img/icon.ico";

  // Use a separate thread to load the icon
  auto iconFuture =
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          [iconPath]()
              -> std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> {
            return std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
                IMG_Load(iconPath.data()), SDL_DestroySurface);
          });

  // Continue with initialization while icon loads
  // account for pixel density for rendering
  int pixelWidth, pixelHeight;
  if (!SDL_GetWindowSizeInPixels(mp_window.get(), &pixelWidth, &pixelHeight)) {
    GAMEENGINE_ERROR("Failed to get window pixel size: " +
                     std::string(SDL_GetError()));
    // Use logical size as fallback
    pixelWidth = m_windowWidth;
    pixelHeight = m_windowHeight;
  }
  // Get logical dimensions
  int logicalWidth, logicalHeight;
  if (!SDL_GetWindowSize(mp_window.get(), &logicalWidth, &logicalHeight)) {
    GAMEENGINE_ERROR("Failed to get window logical size: " +
                     std::string(SDL_GetError()));
    // Use stored dimensions as fallback
    logicalWidth = m_windowWidth;
    logicalHeight = m_windowHeight;
  }

  // Create renderer (let SDL3 choose the best available backend)
  mp_renderer.reset(SDL_CreateRenderer(mp_window.get(), NULL));

  if (!mp_renderer) {
    GAMEENGINE_ERROR("Failed to create renderer: " +
                     std::string(SDL_GetError()));
    return false;
  }

  // Log which renderer backend SDL3 actually selected
  auto rendererName = SDL_GetRendererName(mp_renderer.get());
  if (rendererName) {
    GAMEENGINE_INFO("SDL3 selected renderer backend: " +
                    std::string(rendererName));
  } else {
    GAMEENGINE_WARN("Could not determine selected renderer backend");
  }

  GAMEENGINE_INFO("Rendering system online");

  // Platform-specific VSync handling for timing issues
  // Check if we're using Wayland (known to have VSync timing issues with some
  // drivers)
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

  if (m_isWayland) {
    GAMEENGINE_WARN("Detected Wayland session - VSync may cause timing issues, "
                    "using software limiting");
    // Disable VSync on Wayland to avoid timing problems
    if (!SDL_SetRenderVSync(mp_renderer.get(), 0)) {
      GAMEENGINE_ERROR("Failed to disable VSync: " +
                       std::string(SDL_GetError()));
    }
    GAMEENGINE_INFO(
        "Using software frame rate limiting for consistent timing on Wayland");
  } else {
    // Try to enable VSync on X11 and other stable platforms
    if (SDL_SetRenderVSync(mp_renderer.get(), 1)) {
      GAMEENGINE_INFO(
          "VSync enabled - hardware-synchronized frame presentation active");
    } else {
      GAMEENGINE_WARN("Failed to enable VSync: " + std::string(SDL_GetError()) +
                      " - falling back to software limiting");
      GAMEENGINE_INFO(
          "Using software frame rate limiting for consistent timing");
    }
  }

  if (!SDL_SetRenderDrawColor(
          mp_renderer.get(),
          HAMMER_GRAY)) { // Hammer Game Engine gunmetal dark grey
    GAMEENGINE_ERROR("Failed to set initial render draw color: " +
                     std::string(SDL_GetError()));
  }
#ifdef __APPLE__
  // On macOS, use logical presentation with stretch mode for consistent UI
  // scaling
  int actualWidth, actualHeight;
  if (!SDL_GetWindowSizeInPixels(mp_window.get(), &actualWidth,
                                 &actualHeight)) {
    GAMEENGINE_ERROR("Failed to get actual window pixel size: " +
                     std::string(SDL_GetError()));
    // Fallback to window dimensions
    actualWidth = m_windowWidth;
    actualHeight = m_windowHeight;
  }

  // Use standard logical resolution to ensure UI elements are positioned
  // correctly This prevents buttons from going off-screen while maintaining
  // good scaling
  int targetLogicalWidth = 1920;
  int targetLogicalHeight = 1080;

  // Store logical dimensions for UI positioning
  m_logicalWidth = targetLogicalWidth;
  m_logicalHeight = targetLogicalHeight;

  // Use LETTERBOX mode to preserve all UI elements (with DPI-aware scaling for
  // sharp text)
  SDL_RendererLogicalPresentation presentationMode =
      SDL_LOGICAL_PRESENTATION_LETTERBOX;
  SDL_SetRenderLogicalPresentation(mp_renderer.get(), targetLogicalWidth,
                                   targetLogicalHeight, presentationMode);

  GAMEENGINE_INFO(
      "macOS using standard logical resolution with letterbox mode: " +
      std::to_string(targetLogicalWidth) + "x" +
      std::to_string(targetLogicalHeight) + " on " +
      std::to_string(actualWidth) + "x" + std::to_string(actualHeight));
#else
  // On non-Apple platforms, use actual screen resolution to eliminate scaling
  // blur
  int actualWidth, actualHeight;
  if (!SDL_GetWindowSizeInPixels(mp_window.get(), &actualWidth,
                                 &actualHeight)) {
    GAMEENGINE_ERROR("Failed to get actual window pixel size: " +
                     std::string(SDL_GetError()));
    // Fallback to window dimensions
    actualWidth = m_windowWidth;
    actualHeight = m_windowHeight;
  }

  // Store actual dimensions for UI positioning (no scaling needed)
  m_logicalWidth = actualWidth;
  m_logicalHeight = actualHeight;

  // Disable logical presentation to render at native resolution
  SDL_RendererLogicalPresentation presentationMode =
      SDL_LOGICAL_PRESENTATION_DISABLED;
  SDL_SetRenderLogicalPresentation(mp_renderer.get(), actualWidth, actualHeight,
                                   presentationMode);

  GAMEENGINE_INFO("Using native resolution for crisp rendering: " +
                  std::to_string(actualWidth) + "x" +
                  std::to_string(actualHeight));
#endif
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
      GAMEENGINE_WARN("Failed to load window icon: " +
                      std::string(SDL_GetError()));
    }
  } catch (const std::exception &e) {
    GAMEENGINE_WARN("Error loading window icon: " + std::string(e.what()));
  }

  // INITIALIZING GAME RESOURCE LOADING AND
  // MANAGEMENT_________________________BEGIN

  // Calculate DPI-aware font sizes before threading
  float dpiScale = 1.0f;
  int dpiPixelWidth, dpiPixelHeight;
  int dpiLogicalWidth, dpiLogicalHeight;
  if (!SDL_GetWindowSizeInPixels(mp_window.get(), &dpiPixelWidth,
                                 &dpiPixelHeight)) {
    GAMEENGINE_ERROR("Failed to get window pixel size for DPI calculation: " +
                     std::string(SDL_GetError()));
    // Use window dimensions as fallback
    dpiPixelWidth = m_windowWidth;
    dpiPixelHeight = m_windowHeight;
  }
  if (!SDL_GetWindowSize(mp_window.get(), &dpiLogicalWidth,
                         &dpiLogicalHeight)) {
    GAMEENGINE_ERROR("Failed to get window logical size for DPI calculation: " +
                     std::string(SDL_GetError()));
    // Use stored dimensions as fallback
    dpiLogicalWidth = m_windowWidth;
    dpiLogicalHeight = m_windowHeight;
  }

  if (dpiLogicalWidth > 0 && dpiLogicalHeight > 0) {
#ifdef __APPLE__
    // On macOS, use the native display content scale for proper text rendering
    float displayScale = SDL_GetWindowDisplayScale(mp_window.get());
    if (displayScale > 0.0f) {
      dpiScale = displayScale;
    } else {
      dpiScale = 1.0f;
    }
    GAMEENGINE_INFO("macOS display content scale: " + std::to_string(dpiScale) +
                    (displayScale > 0.0f ? " (detected)" : " (fallback)"));
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

  GAMEENGINE_INFO("DPI scale: " + std::to_string(dpiScale) +
                  ", window: " + std::to_string(m_windowWidth) + "x" +
                  std::to_string(m_windowHeight));

  // Use multiple threads for initialization
  std::vector<std::future<bool>> initTasks; // Initialization tasks vector
  initTasks.reserve(10); // Reserve capacity for typical number of init tasks

  // Initialize input manager in a background thread - #1
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

  // Initialize sound manager in a separate thread - #2
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

  // Initialize font manager in a separate thread - #3
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

  // Initialize save game manager in a separate thread - #4
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

  // Initialize AI Manager in a separate thread - #5
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

  // Initialize Event Manager in a separate thread - #6
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

  // Initialize Particle Manager in a separate thread - #7
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

  // Initialize Resource Template Manager in a separate thread - #8
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
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

  // Initialize World Resource Manager for global resource tracking - #9
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

  // Initialize World Manager for world generation and management - #10
  initTasks.push_back(
      HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
          []() -> bool {
            GAMEENGINE_INFO("Creating World Manager");
            HammerEngine::WorldManager &worldMgr = HammerEngine::WorldManager::Instance();
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

  // Set cached renderer for performance optimization
  uiMgr.setRenderer(mp_renderer.get());
  GAMEENGINE_INFO("UI Manager initialized successfully with cached renderer");

  // Setting Up initial game states
  mp_gameStateManager->addState(std::make_unique<LogoState>());
  mp_gameStateManager->addState(std::make_unique<MainMenuState>());
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
      GAMEENGINE_ERROR("Initialization task failed: " + std::string(e.what()));
      allTasksSucceeded = false;
    }
  }

  if (!allTasksSucceeded) {
    GAMEENGINE_ERROR("One or more initialization tasks failed");
    return false;
  }

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

    // InputManager not cached - handled in handleEvents() for proper SDL
    // architecture

    // Manager references are valid (assigned above)

    // Verify managers are still responding after caching
    try {
      // Test AI Manager responsiveness
      size_t entityCount = mp_aiManager->getManagedEntityCount();
      GAMEENGINE_DEBUG("AIManager cached successfully, managing " +
                       std::to_string(entityCount) + " entities");

      // Test Event Manager responsiveness (just verify it's initialized)
      GAMEENGINE_DEBUG("EventManager cached successfully and initialized");

      // Test Particle Manager responsiveness
      size_t activeParticles = mp_particleManager->getActiveParticleCount();
      GAMEENGINE_DEBUG("ParticleManager cached successfully, " +
                       std::to_string(activeParticles) + " active particles");
    } catch (const std::exception &e) {
      GAMEENGINE_CRITICAL("Manager validation failed after caching: " +
                          std::string(e.what()));
      return false;
    }

    GAMEENGINE_INFO("Manager references cached and validated successfully");
  } catch (const std::exception &e) {
    GAMEENGINE_ERROR("Error caching manager references: " +
                     std::string(e.what()));
    return false;
  }
  //_______________________________________________________________________________________________________________END

  // Step 3: Post-initialization setup that requires manager dependencies
  GAMEENGINE_INFO("Setting up manager cross-dependencies");
  try {
    // Setup WorldManager event handlers now that EventManager is guaranteed to be ready
    HammerEngine::WorldManager &worldMgr = HammerEngine::WorldManager::Instance();
    if (worldMgr.isInitialized()) {
      worldMgr.setupEventHandlers();
      GAMEENGINE_INFO("WorldManager event handlers setup complete");
    } else {
      GAMEENGINE_ERROR("WorldManager not initialized - cannot setup event handlers");
      return false;
    }
    
    GAMEENGINE_INFO("Manager cross-dependencies setup complete");
  } catch (const std::exception &e) {
    GAMEENGINE_ERROR("Error setting up manager cross-dependencies: " + std::string(e.what()));
    return false;
  }

  GAMEENGINE_INFO("Game " + std::string(title) + " initialized successfully!");
  GAMEENGINE_INFO("Running " + std::string(title) + " <]==={}");

  // Initialize buffer system for first frame
  m_currentBufferIndex.store(0, std::memory_order_release);
  m_renderBufferIndex.store(0, std::memory_order_release);

  // Mark first buffer as ready with initial clear frame
  m_bufferReady[0].store(true, std::memory_order_release);
  m_bufferReady[1].store(false, std::memory_order_release);

  // Initialize frame counters
  m_lastUpdateFrame.store(0, std::memory_order_release);
  m_lastRenderedFrame.store(0, std::memory_order_release);

  // NOTE: Initial state will be pushed after GameLoop setup is complete
  // This ensures the game loop is ready to handle state updates before any state enters

  return true;
}

void GameEngine::handleEvents() {
  // Handle input events - InputManager stays here for SDL event polling
  // architecture
  InputManager &inputMgr = InputManager::Instance();
  inputMgr.update();

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

void GameEngine::update([[maybe_unused]] float deltaTime) {
  // This method is now thread-safe and can be called from a worker thread
  std::lock_guard<std::mutex> lock(m_updateMutex);

  // Use WorkerBudget system for coordinated task submission
  if (HammerEngine::ThreadSystem::Exists()) {
    auto &threadSystem = HammerEngine::ThreadSystem::Instance();

    // Calculate worker budget for this frame
    size_t availableWorkers =
        static_cast<size_t>(threadSystem.getThreadCount());
    HammerEngine::WorkerBudget budget =
        HammerEngine::calculateWorkerBudget(availableWorkers);

    // Submit engine coordination tasks respecting our worker budget
    // Use high priority for engine tasks to ensure timely processing
    threadSystem.enqueueTask(
        [this, deltaTime]() {
          // Critical game engine coordination
          processEngineCoordination(deltaTime);
        },
        HammerEngine::TaskPriority::High, "GameEngine_Coordination");

    // Only submit additional tasks if we have multiple workers allocated
    if (budget.engineReserved > 1) {
      threadSystem.enqueueTask(
          [this]() {
            // Secondary engine tasks (resource management, cleanup, etc.)
            processEngineSecondaryTasks();
          },
          HammerEngine::TaskPriority::Normal, "GameEngine_Secondary");
    }
  }

  // Mark update as running with relaxed ordering (protected by mutex)
  m_updateRunning.store(true, std::memory_order_relaxed);

  // Get the buffer for the current update
  const size_t updateBufferIndex =
      m_currentBufferIndex.load(std::memory_order_acquire);

  // HYBRID MANAGER UPDATE ARCHITECTURE
  // =====================================
  // HYBRID MANAGER UPDATE ARCHITECTURE - Step 1: Global Updates (No Caching)
  // =====================================
  // Core engine systems are updated globally for optimal performance and
  // consistency State-specific systems are updated by individual states for
  // flexibility and efficiency

  // GLOBAL SYSTEMS (Updated by GameEngine):
  // - AIManager: World simulation with 10K+ entities, benefits from
  // consistent global updates
  // - EventManager: Global game events (weather, scene changes), batch
  // processing optimization
  // - ParticleManager: Global particle system with weather integration,
  // cache-optimized SoA updates
  // - InputManager: Handled in handleEvents() for proper SDL event polling
  // architecture

  // AI system - manages world entities across all states (cached reference
  // access)
  if (mp_aiManager) {
    mp_aiManager->update(deltaTime);
  } else {
    GAMEENGINE_ERROR("AIManager cache is null!");
  }

  // Event system - global game events and world simulation (cached reference
  // access)
  if (mp_eventManager) {
    mp_eventManager->update();
  } else {
    GAMEENGINE_ERROR("EventManager cache is null!");
  }

  // Particle system - global weather and effect particles (cached reference
  // access)
  if (mp_particleManager) {
    mp_particleManager->update(deltaTime);
  } else {
    GAMEENGINE_ERROR("ParticleManager cache is null!");
  }

  // STATE-MANAGED SYSTEMS (Updated by individual states):
  // - UIManager: Optional, state-specific, only updated when UI is actually
  // used See UIExampleState::update() for proper state-managed pattern

  // Update game states - states handle their specific system needs
  mp_gameStateManager->update(deltaTime);

  // Increment the frame counter atomically for thread-safe render
  // synchronization
  m_lastUpdateFrame.fetch_add(1, std::memory_order_relaxed);

  // Mark this buffer as ready for rendering
  m_bufferReady[updateBufferIndex].store(true, std::memory_order_release);

  // Mark update as completed with relaxed ordering (protected by condition
  // variable)
  m_updateCompleted.store(true, std::memory_order_relaxed);

  m_updateRunning.store(false, std::memory_order_relaxed);

  // Notify anyone waiting on this update
  m_updateCondition.notify_all();
  m_bufferCondition.notify_all();
}

void GameEngine::render(double alpha) {
  // Always on MAIN thread as its an - SDL REQUIREMENT
  std::lock_guard<std::mutex> lock(m_renderMutex);

  // Always render - optimized buffer management ensures render buffer is always
  // valid
  if (!SDL_SetRenderDrawColor(
          mp_renderer.get(),
          HAMMER_GRAY)) { // Hammer Game Engine gunmetal dark grey
    GAMEENGINE_ERROR("Failed to set render draw color: " +
                     std::string(SDL_GetError()));
  }
  if (!SDL_RenderClear(mp_renderer.get())) {
    GAMEENGINE_ERROR("Failed to clear renderer: " +
                     std::string(SDL_GetError()));
  }

  // Make sure GameStateManager knows which buffer to render from
  mp_gameStateManager->render(alpha);

  if (!SDL_RenderPresent(mp_renderer.get())) {
    GAMEENGINE_ERROR("Failed to present renderer: " +
                     std::string(SDL_GetError()));
  }

  // Increment rendered frame counter for fast synchronization
  m_lastRenderedFrame.fetch_add(1, std::memory_order_relaxed);
}

void GameEngine::waitForUpdate() {
  std::unique_lock<std::mutex> lock(m_updateMutex);

  // Calculate an adaptive timeout based on the target FPS.
  auto timeout = std::chrono::milliseconds(100); // Default timeout
  if (auto gameLoop = m_gameLoop.lock()) {
    const float targetFPS = gameLoop->getTargetFPS();
    if (targetFPS > 0) {
      // Set timeout to 2x the frame time as a responsive safety net
      const auto frameTimeMs =
          static_cast<long long>(1000.0 / targetFPS);
      timeout = std::chrono::milliseconds(frameTimeMs * 2);
    }
  }

  // Wait for update completion with the adaptive timeout
  m_updateCondition.wait_for(lock, timeout, [this] {
    return m_updateCompleted.load(std::memory_order_acquire) ||
           m_stopRequested.load(std::memory_order_acquire);
  });
}

void GameEngine::signalUpdateComplete() {
  {
    std::lock_guard<std::mutex> lock(m_updateMutex);
    m_updateCompleted.store(false,
                            std::memory_order_release); // Reset for next frame
  }
}

void GameEngine::swapBuffers() {
  // Thread-safe buffer swap with proper synchronization
  size_t currentIndex = m_currentBufferIndex.load(std::memory_order_acquire);
  size_t nextUpdateIndex = (currentIndex + 1) % BUFFER_COUNT;

  // Check if we have a valid render buffer before attempting swap
  size_t currentRenderIndex =
      m_renderBufferIndex.load(std::memory_order_acquire);

  // Only swap if current buffer is ready AND next buffer isn't being rendered
  if (m_bufferReady[currentIndex].load(std::memory_order_acquire) &&
      nextUpdateIndex != currentRenderIndex) {

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
    }
  }
}

bool GameEngine::hasNewFrameToRender() const noexcept {
  // Optimized render check with minimal atomic operations
  size_t renderIndex = m_renderBufferIndex.load(std::memory_order_acquire);

  // Single check for buffer readiness
  if (!m_bufferReady[renderIndex].load(std::memory_order_acquire)) {
    return false;
  }

  // Compare frame counters only if buffer is ready - use relaxed ordering for
  // counters
  uint64_t lastUpdate = m_lastUpdateFrame.load(std::memory_order_relaxed);
  uint64_t lastRendered = m_lastRenderedFrame.load(std::memory_order_relaxed);

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
  // This method can be used to perform background processing
  // It should be safe to run on worker threads

  try {
    // Background processing tasks can be added here
    // Note: EventManager is now updated in the main update loop for optimal
    // performance and consistency with other global systems (AI, Input)

    // Example: Process non-critical background tasks
    // These tasks can run while the main thread is handling rendering
  } catch (const std::exception &e) {
    GAMEENGINE_ERROR("Exception in background tasks: " + std::string(e.what()));
  } catch (...) {
    GAMEENGINE_ERROR("Unknown exception in background tasks");
  }
}

void GameEngine::setLogicalPresentationMode(
    SDL_RendererLogicalPresentation mode) {
  m_logicalPresentationMode = mode;
  if (mp_renderer) {
    int width, height;
    if (!SDL_GetWindowSize(mp_window.get(), &width, &height)) {
      GAMEENGINE_ERROR("Failed to get window size for logical presentation: " +
                       std::string(SDL_GetError()));
      // Use stored dimensions as fallback
      width = m_windowWidth;
      height = m_windowHeight;
    }
    if (!SDL_SetRenderLogicalPresentation(mp_renderer.get(), width, height,
                                          mode)) {
      GAMEENGINE_ERROR("Failed to set render logical presentation: " +
                       std::string(SDL_GetError()));
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

void GameEngine::processEngineCoordination(float deltaTime) {
  // Critical engine coordination tasks
  // This runs with high priority in the WorkerBudget system
  (void)deltaTime; // Avoid unused parameter warning

  // Engine-specific coordination logic can be added here
  // Examples: state synchronization, resource coordination, etc.
}

void GameEngine::processEngineSecondaryTasks() {
  // Secondary engine tasks that only run when we have multiple workers
  // allocated Examples: performance monitoring, resource cleanup, etc.
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

  GAMEENGINE_INFO("Cleaning up AI Manager...");
  AIManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Save Game Manager...");
  SaveGameManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up Input Manager...");
  InputManager::Instance().clean();

  GAMEENGINE_INFO("Cleaning up World Manager...");
  HammerEngine::WorldManager::Instance().clean();

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
  mp_resourceTemplateManager = nullptr;
  mp_worldResourceManager = nullptr;
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
  
#ifdef __APPLE__
  // SDL_Quit() causes crashes on macOS with SDL3 due to Metal backend cleanup issues
  // The OS will clean up SDL resources when the process exits
  GAMEENGINE_INFO("macOS: Skipping SDL_Quit() due to platform-specific crash with Metal backend");
#else
  SDL_Quit();
#endif
  
  GAMEENGINE_INFO("SDL resources cleaned!");
  GAMEENGINE_INFO("Shutdown complete!");
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
