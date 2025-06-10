/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "core/GameLoop.hpp" // IWYU pragma: keep - Required for GameLoop weak_ptr declaration
#include "core/WorkerBudget.hpp"
#include <vector>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include "SDL3/SDL_surface.h"
#include "managers/AIManager.hpp"
#include "gameStates/AIDemoState.hpp"
#include "gameStates/EventDemoState.hpp"
#include "managers/EventManager.hpp"
#include "managers/FontManager.hpp"
#include "gameStates/GamePlayState.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/InputManager.hpp"
#include "gameStates/LogoState.hpp"
#include "gameStates/MainMenuState.hpp"
#include "gameStates/UIExampleState.hpp"
#include "gameStates/OverlayDemoState.hpp"
#include "managers/UIManager.hpp"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"
#include "managers/SaveGameManager.hpp"
#include "managers/SoundManager.hpp"
#include "core/ThreadSystem.hpp"
#include "managers/TextureManager.hpp"

#define FORGE_GRAY 31, 32, 34, 255

bool GameEngine::init(const char* title,
                      int width,
                      int height,
                      bool fullscreen) {
  GAMEENGINE_INFO("Initializing SDL Video");

  if (SDL_Init(SDL_INIT_VIDEO)) {
    GAMEENGINE_INFO("SDL Video online");

    // Set SDL hints for better rendering quality
    SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "3");    // Use geometry for smoother lines
    SDL_SetHint("SDL_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR", "0");  // Don't bypass compositor
    SDL_SetHint("SDL_MOUSE_AUTO_CAPTURE", "0");    // Prevent mouse capture issues
    
    // macOS-specific hints for better fullscreen and DPI handling
    #ifdef __APPLE__
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1");  // Use Spaces for fullscreen
    #endif
    
    GAMEENGINE_INFO("SDL rendering hints configured for optimal quality");

    // Use reliable window sizing approach instead of potentially corrupted display bounds
    if (width <= 0 || height <= 0) {
      // Default to reasonable size if not specified
      m_windowWidth = 1280;
      m_windowHeight = 720;
      GAMEENGINE_INFO("Using default window size: " + std::to_string(m_windowWidth) + "x" + std::to_string(m_windowHeight));
    } else {
      // Use the provided dimensions
      m_windowWidth = width;
      m_windowHeight = height;
      GAMEENGINE_INFO("Using requested window size: " + std::to_string(m_windowWidth) + "x" + std::to_string(m_windowHeight));
    }

    
    // For macOS compatibility, use fullscreen for large window requests
    #ifdef __APPLE__
    if (m_windowWidth >= 1920 || m_windowHeight >= 1080) {
      fullscreen = true;
      GAMEENGINE_INFO("Large window requested on macOS, enabling fullscreen for compatibility");
    }
    #endif
    // Window handling with platform-specific optimizations
    int flags{0};
    int flag1 = SDL_WINDOW_FULLSCREEN;
    int flag2 = SDL_WINDOW_HIGH_PIXEL_DENSITY;

    if (fullscreen) {
      flags = flag1 | flag2;
      
      #ifdef __APPLE__
      // On macOS, keep logical dimensions for proper scaling
      // Don't override m_windowWidth and m_windowHeight for macOS
      GAMEENGINE_INFO("Window set to Fullscreen mode for macOS compatibility");
      #else
      //setting window width and height to fullscreen dimensions for detected monitor
      int displayCount = 0;
      std::unique_ptr<SDL_DisplayID[], decltype(&SDL_free)> displays(
        SDL_GetDisplays(&displayCount), SDL_free);
      if (displays && displayCount > 0) {
        const SDL_DisplayMode* displayMode = SDL_GetDesktopDisplayMode(displays[0]);
        if (displayMode) {
          m_windowWidth = displayMode->w;
          m_windowHeight = displayMode->h;
          GAMEENGINE_INFO("Window size set to Full Screen: " + std::to_string(m_windowWidth) + "x" + std::to_string(m_windowHeight));
        }
      }
      #endif
    }

    mp_window.reset(SDL_CreateWindow(title, m_windowWidth, m_windowHeight, flags));

    if (mp_window) {
      GAMEENGINE_INFO("Window creation system online");
      
      #ifdef __APPLE__
      // On macOS, set fullscreen mode to null for borderless fullscreen desktop mode
      if (fullscreen) {
        SDL_SetWindowFullscreenMode(mp_window.get(), nullptr);
        GAMEENGINE_INFO("Set to borderless fullscreen desktop mode on macOS");
      }
      #endif

      // Set window icon
      GAMEENGINE_INFO("Setting window icon");

      // Use SDL_image to directly load the icon
      const char* iconPath = "res/img/icon.ico";

      // Use a separate thread to load the icon
      auto iconFuture = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
          [iconPath]() -> std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> {
              return std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(IMG_Load(iconPath), SDL_DestroySurface);
          });

      // Continue with initialization while icon loads
      // account for pixel density for rendering
      int pixelWidth, pixelHeight;
      SDL_GetWindowSizeInPixels(mp_window.get(), &pixelWidth, &pixelHeight);
      // Get logical dimensions
      int logicalWidth, logicalHeight;
      SDL_GetWindowSize(mp_window.get(), &logicalWidth, &logicalHeight);

      // Create renderer - VSync will be set separately using SDL3 API
      mp_renderer.reset(SDL_CreateRenderer(mp_window.get(), NULL));

      if (mp_renderer) {
        GAMEENGINE_INFO("Rendering system online");
        
        // Enable VSync using SDL3 API
        if (SDL_SetRenderVSync(mp_renderer.get(), 1)) {
          GAMEENGINE_INFO("VSync enabled - hardware-synchronized frame presentation active");
        } else {
          GAMEENGINE_WARN("VSync not supported - using software frame rate limiting (may cause stuttering): " + std::string(SDL_GetError()));
        }
        
        SDL_SetRenderDrawColor(mp_renderer.get(), FORGE_GRAY);  // Forge Game Engine gunmetal dark grey
        // Set logical rendering size to standard resolution for consistent aspect ratio
        int targetLogicalWidth = 1920;
        int targetLogicalHeight = 1080;
        
        #ifdef __APPLE__
        // On macOS, use LETTERBOX mode to maintain aspect ratio and avoid black bars
        SDL_RendererLogicalPresentation presentationMode = SDL_LOGICAL_PRESENTATION_LETTERBOX;
        #else
        SDL_RendererLogicalPresentation presentationMode = m_logicalPresentationMode;
        #endif
        SDL_SetRenderLogicalPresentation(mp_renderer.get(), targetLogicalWidth, targetLogicalHeight, presentationMode);
        //Render Mode.
        SDL_SetRenderDrawBlendMode(mp_renderer.get(), SDL_BLENDMODE_BLEND);
      } else {
        GAMEENGINE_CRITICAL("Rendering system creation failed: " + std::string(SDL_GetError()));
        return false;  // Forge renderer fail
      }

      // Now check if the icon was loaded successfully
      try {
        auto iconSurfacePtr = iconFuture.get();
        if (iconSurfacePtr) {
          SDL_SetWindowIcon(mp_window.get(), iconSurfacePtr.get());
          // No need to manually destroy the surface, smart pointer will handle it
          GAMEENGINE_INFO("Window icon set successfully");
        } else {
          GAMEENGINE_WARN("Failed to load window icon: " + std::string(SDL_GetError()));
        }
      } catch (const std::exception& e) {
        GAMEENGINE_WARN("Error loading window icon: " + std::string(e.what()));
      }

    } else {
      GAMEENGINE_CRITICAL("Window system creation failed: " + std::string(SDL_GetError()));
      return false;
    }
  } else {
    GAMEENGINE_CRITICAL("SDL Video initialization failed! Make sure SDL3 runtime is installed. SDL error: " + std::string(SDL_GetError()));
    return false;
  }

  // INITIALIZING GAME RESOURCE LOADING AND MANAGEMENT_________________________BEGIN
  
  // Calculate DPI-aware font sizes before threading
  float dpiScale = 1.0f;
  int pixelWidth, pixelHeight;
  int logicalWidth, logicalHeight;
  SDL_GetWindowSizeInPixels(mp_window.get(), &pixelWidth, &pixelHeight);
  SDL_GetWindowSize(mp_window.get(), &logicalWidth, &logicalHeight);
  
  if (logicalWidth > 0 && logicalHeight > 0) {
    float scaleX = static_cast<float>(pixelWidth) / static_cast<float>(logicalWidth);
    float scaleY = static_cast<float>(pixelHeight) / static_cast<float>(logicalHeight);
    dpiScale = std::max(scaleX, scaleY);
    
    #ifdef __APPLE__
    // On macOS, get the display content scale for more accurate scaling
    float displayScale = SDL_GetWindowDisplayScale(mp_window.get());
    if (displayScale > 0.0f) {
      // Use the display scale if it's valid, otherwise keep calculated scale
      dpiScale = displayScale;
      GAMEENGINE_INFO("Using macOS display content scale: " + std::to_string(displayScale));
    }
    #endif
  }
  
  // Store DPI scale for use by other managers
  m_dpiScale = dpiScale;
  
  GAMEENGINE_INFO("Using display-aware font sizing - SDL3 handles DPI scaling automatically");
  
  GAMEENGINE_INFO("DPI scale: " + std::to_string(dpiScale) + 
                 ", window: " + std::to_string(m_windowWidth) + "x" + std::to_string(m_windowHeight));

  // Use multiple threads for initialization
  std::vector<std::future<bool>>
      initTasks;  // Initialization tasks vector
  initTasks.reserve(8);  // Reserve capacity for typical number of init tasks

// Initialize input manager in a background thread - #1
initTasks.push_back(
    Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
      GAMEENGINE_INFO("Detecting and initializing gamepads and input handling");
      InputManager& inputMgr = InputManager::Instance();
      inputMgr.initializeGamePad();
      return true;
    }));

// Create and initialize texture manager - MAIN THREAD
GAMEENGINE_INFO("Creating Texture Manager");
TextureManager& texMgr = TextureManager::Instance();

// Load textures in main thread
GAMEENGINE_INFO("Creating and loading textures");
texMgr.load("res/img", "", mp_renderer.get());

  // Initialize sound manager in a separate thread - #2
  initTasks.push_back(
      Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        GAMEENGINE_INFO("Creating Sound Manager");
        SoundManager& soundMgr = SoundManager::Instance();
        if (!soundMgr.init()) {
          GAMEENGINE_CRITICAL("Failed to initialize Sound Manager");
          return false;
        }

        GAMEENGINE_INFO("Loading sounds and music");
        soundMgr.loadSFX("res/sfx", "sfx");
        soundMgr.loadMusic("res/music", "music");
        return true;
      }));

  // Initialize font manager in a separate thread - #3
  initTasks.push_back(
      Forge::ThreadSystem::Instance().enqueueTaskWithResult([this]() -> bool {
        GAMEENGINE_INFO("Creating Font Manager");
        FontManager& fontMgr = FontManager::Instance();
        if (!fontMgr.init()) {
          GAMEENGINE_CRITICAL("Failed to initialize Font Manager");
          return false;
        }

        GAMEENGINE_INFO("Loading fonts with display-aware sizing");
        
        // Let FontManager calculate optimal sizes based on display characteristics
        if (!fontMgr.loadFontsForDisplay("res/fonts", m_windowWidth, m_windowHeight)) {
          GAMEENGINE_CRITICAL("Failed to load fonts for display");
          return false;
        }
        return true;
      }));

  // Initialize save game manager in a separate thread - #4
  initTasks.push_back(
      Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        GAMEENGINE_INFO("Creating Save Game Manager");
        SaveGameManager& saveMgr = SaveGameManager::Instance();

        // Set the save directory to "res" folder
        saveMgr.setSaveDirectory("res");
        return true;
      }));

  // Initialize AI Manager in a separate thread - #5
  initTasks.push_back(
      Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        GAMEENGINE_INFO("Creating AI Manager");
        AIManager& aiMgr = AIManager::Instance();
        if (!aiMgr.init()) {
          GAMEENGINE_CRITICAL("Failed to initialize AI Manager");
          return false;
        }
        GAMEENGINE_INFO("AI Manager initialized successfully");
        return true;
      }));

  // Initialize Event Manager in a separate thread - #6
  initTasks.push_back(
      Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        GAMEENGINE_INFO("Creating Event Manager");
        EventManager& eventMgr = EventManager::Instance();
        if (!eventMgr.init()) {
          GAMEENGINE_CRITICAL("Failed to initialize Event Manager");
          return false;
        }
        GAMEENGINE_INFO("Event Manager initialized successfully");
        return true;
      }));

  // Initialize game state manager (on main thread because it directly calls rendering) - MAIN THREAD
  GAMEENGINE_INFO("Creating Game State Manager and setting up initial Game States");
  mp_gameStateManager = std::make_unique<GameStateManager>();

  // Initialize UI Manager (on main thread because it uses font/text rendering) - MAIN THREAD
  GAMEENGINE_INFO("Creating UI Manager");
  UIManager& uiMgr = UIManager::Instance();
  if (!uiMgr.init()) {
    GAMEENGINE_CRITICAL("Failed to initialize UI Manager");
    return false;
  }
  GAMEENGINE_INFO("UI Manager initialized successfully");

  // Setting Up initial game states
  mp_gameStateManager->addState(std::make_unique<LogoState>());
  mp_gameStateManager->addState(std::make_unique<MainMenuState>());
  mp_gameStateManager->addState(std::make_unique<GamePlayState>());
  mp_gameStateManager->addState(std::make_unique<AIDemoState>());
  mp_gameStateManager->addState(std::make_unique<EventDemoState>());
  mp_gameStateManager->addState(std::make_unique<UIExampleState>());
  mp_gameStateManager->addState(std::make_unique<OverlayDemoState>());

  // Wait for all initialization tasks to complete
  bool allTasksSucceeded = true;
  for (auto& task : initTasks) {
    try {
      allTasksSucceeded &= task.get();
    } catch (const std::exception& e) {
      std::cerr << "Forge Game Engine - Initialization task failed: "
                << e.what() << std::endl;
      allTasksSucceeded = false;
    }
  }

  if (!allTasksSucceeded) {
    std::cerr << "Forge Game Engine - One or more initialization tasks failed"
              << std::endl;
    return false;
  }

  // Step 2: Cache manager references for performance (after all background init complete)
  GAMEENGINE_INFO("Caching manager references");
  try {
    mp_aiManager = &AIManager::Instance();
    mp_eventManager = &EventManager::Instance();
    // InputManager not cached - handled in handleEvents() for proper SDL architecture

    // Validate that cached managers are properly initialized
    if (!mp_aiManager || !mp_eventManager) {
      std::cerr << "Forge Game Engine - Error: One or more manager references are null!" << std::endl;
      return false;
    }

    GAMEENGINE_INFO("Manager references cached and validated successfully");
  } catch (const std::exception& e) {
    GAMEENGINE_ERROR("Error caching manager references: " + std::string(e.what()));
    return false;
  }
  //_______________________________________________________________________________________________________________END

  GAMEENGINE_INFO("Game " + std::string(title) + " initialized successfully!");
  GAMEENGINE_INFO("Running " + std::string(title) + " <]==={");

  // Initialize first buffer as ready for immediate rendering
  m_bufferReady[0].store(true, std::memory_order_release);
  m_renderBufferIndex.store(0, std::memory_order_release);
  // Ensure first frame is always considered new by setting update counter to 1
  m_lastUpdateFrame.store(1, std::memory_order_release);
  
  // setting logo state for default state
  mp_gameStateManager->setState("LogoState");//set to "LogoState" for normal operation.
  // Note: GameLoop will be started by ForgeMain, not here
  return true;
}

void GameEngine::handleEvents() {
  // Handle input events - InputManager stays here for SDL event polling architecture
  InputManager& inputMgr = InputManager::Instance();
  inputMgr.update();
}

void GameEngine::setRunning(bool running) {
  if (auto gameLoop = m_gameLoop.lock()) {
    if (running) {
      // Can't restart GameLoop from GameEngine - this might be an error case
      std::cerr << "Warning: Cannot start GameLoop from GameEngine - use GameLoop::run() instead" << std::endl;
    } else {
      gameLoop->stop();
    }
  } else {
    std::cerr << "Warning: No GameLoop set - cannot change running state" << std::endl;
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
  if (Forge::ThreadSystem::Exists()) {
    auto& threadSystem = Forge::ThreadSystem::Instance();

    // Calculate worker budget for this frame
    size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
    Forge::WorkerBudget budget = Forge::calculateWorkerBudget(availableWorkers);

    // Submit engine coordination tasks respecting our worker budget
    // Use high priority for engine tasks to ensure timely processing
    threadSystem.enqueueTask([this, deltaTime]() {
      // Critical game engine coordination
      processEngineCoordination(deltaTime);
    }, Forge::TaskPriority::High, "GameEngine_Coordination");

    // Only submit additional tasks if we have multiple workers allocated
    if (budget.engineReserved > 1) {
      threadSystem.enqueueTask([this]() {
        // Secondary engine tasks (resource management, cleanup, etc.)
        processEngineSecondaryTasks();
      }, Forge::TaskPriority::Normal, "GameEngine_Secondary");
    }
  }

  // Mark update as running
  m_updateRunning = true;

  // Get the buffer for the current update
  const size_t updateBufferIndex = m_currentBufferIndex.load(std::memory_order_acquire);

  try {
    // HYBRID MANAGER UPDATE ARCHITECTURE
    // =====================================
    // HYBRID MANAGER UPDATE ARCHITECTURE - Step 1: Global Updates (No Caching)
    // =====================================
    // Core engine systems are updated globally for optimal performance and consistency
    // State-specific systems are updated by individual states for flexibility and efficiency

    // GLOBAL SYSTEMS (Updated by GameEngine):
    // - AIManager: World simulation with 10K+ entities, benefits from consistent global updates
    // - EventManager: Global game events (weather, scene changes), batch processing optimization
    // - InputManager: Handled in handleEvents() for proper SDL event polling architecture

    // AI system - manages world entities across all states (cached reference access)
    if (mp_aiManager) {
      try {
        mp_aiManager->update(deltaTime);
      } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - AIManager exception: " << e.what() << std::endl;
      } catch (...) {
        std::cerr << "Forge Game Engine - AIManager unknown exception" << std::endl;
      }
    } else {
      std::cerr << "Forge Game Engine - AIManager cache is null!" << std::endl;
    }

    // Event system - global game events and world simulation (cached reference access)
    if (mp_eventManager) {
      try {
        mp_eventManager->update();
      } catch (const std::exception& e) {
        std::cerr << "Forge Game Engine - EventManager exception: " << e.what() << std::endl;
      } catch (...) {
        std::cerr << "Forge Game Engine - EventManager unknown exception" << std::endl;
      }
    } else {
      std::cerr << "Forge Game Engine - EventManager cache is null!" << std::endl;
    }

    // STATE-MANAGED SYSTEMS (Updated by individual states):
    // - UIManager: Optional, state-specific, only updated when UI is actually used
    // See UIExampleState::update() for proper state-managed pattern

    // Update game states - states handle their specific system needs
    mp_gameStateManager->update(deltaTime);

    // Increment the frame counter atomically for thread-safe render synchronization
    m_lastUpdateFrame.fetch_add(1, std::memory_order_relaxed);

    // Mark this buffer as ready for rendering
    m_bufferReady[updateBufferIndex].store(true, std::memory_order_release);

    // Mark update as completed
    m_updateCompleted = true;
  } catch (const std::exception& e) {
    std::cerr << "Forge Game Engine - Exception in update: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << "Forge Game Engine - Unknown exception in update" << std::endl;
  }

  m_updateRunning = false;

  // Notify anyone waiting on this update
  m_updateCondition.notify_all();
  m_bufferCondition.notify_all();
}

void GameEngine::render([[maybe_unused]] float interpolation) {
  // Always on MAIN thread as its an - SDL REQUIREMENT
  std::lock_guard<std::mutex> lock(m_renderMutex);

  // Always render - optimized buffer management ensures render buffer is always valid
  {
    try {
      SDL_SetRenderDrawColor(mp_renderer.get(), FORGE_GRAY);  // Forge Game Engine gunmetal dark grey
      SDL_RenderClear(mp_renderer.get());

      // Make sure GameStateManager knows which buffer to render from
      mp_gameStateManager->render();

      SDL_RenderPresent(mp_renderer.get());

      // Increment rendered frame counter for fast synchronization
      m_lastRenderedFrame.fetch_add(1, std::memory_order_relaxed);
    } catch (const std::exception& e) {
      std::cerr << "Forge Game Engine - Exception in render: " << e.what() << std::endl;
    } catch (...) {
      std::cerr << "Forge Game Engine - Unknown exception in render" << std::endl;
    }
  }
}

void GameEngine::waitForUpdate() {
  std::unique_lock<std::mutex> lock(m_updateMutex);
  // Set a timeout to avoid deadlocks with extremely high entity counts
  if (!m_updateCondition.wait_for(lock, std::chrono::milliseconds(100),
      [this] { return m_updateCompleted.load(std::memory_order_acquire); })) {
    // If timeout occurs, log a warning but continue
    std::cerr << "Forge Game Engine - Warning: Update wait timeout with high entity count" << std::endl;
    // Force the update completed flag to true to avoid deadlock
    m_updateCompleted.store(true, std::memory_order_release);
  }
}

void GameEngine::signalUpdateComplete() {
  {
    std::lock_guard<std::mutex> lock(m_updateMutex);
    m_updateCompleted.store(false, std::memory_order_release);  // Reset for next frame
  }
}

void GameEngine::swapBuffers() {
  // Ultra-fast lock-free buffer swap - optimized for render thread performance
  size_t currentIndex = m_currentBufferIndex.load(std::memory_order_acquire);
  size_t nextUpdateIndex = (currentIndex + 1) % BUFFER_COUNT;

  // Only swap if current buffer is ready
  if (m_bufferReady[currentIndex].load(std::memory_order_acquire)) {
    // Make current buffer available for rendering immediately
    m_renderBufferIndex.store(currentIndex, std::memory_order_release);
    
    // Switch to next buffer for updates
    m_currentBufferIndex.store(nextUpdateIndex, std::memory_order_release);
    
    // Clear the next buffer's ready state for the next update cycle
    m_bufferReady[nextUpdateIndex].store(false, std::memory_order_release);
  }
}

bool GameEngine::hasNewFrameToRender() const {
  // Ultra-fast render check - single atomic read with relaxed ordering for speed
  uint64_t lastUpdate = m_lastUpdateFrame.load(std::memory_order_relaxed);
  uint64_t lastRendered = m_lastRenderedFrame.load(std::memory_order_relaxed);
  // Always true for first frame, then compare frame counters
  return lastUpdate > lastRendered || (lastUpdate == 1 && lastRendered == 0);
}

bool GameEngine::isUpdateRunning() const {
  return m_updateRunning.load(std::memory_order_acquire);
}

size_t GameEngine::getCurrentBufferIndex() const {
  return m_currentBufferIndex.load(std::memory_order_acquire);
}

size_t GameEngine::getRenderBufferIndex() const {
  return m_renderBufferIndex.load(std::memory_order_acquire);
}

void GameEngine::processBackgroundTasks() {
  // This method can be used to perform background processing
  // It should be safe to run on worker threads

  try {
    // Background processing tasks can be added here
    // Note: EventManager is now updated in the main update loop for optimal performance
    // and consistency with other global systems (AI, Input)

    // Example: Process non-critical background tasks
    // These tasks can run while the main thread is handling rendering
  } catch (const std::exception& e) {
    std::cerr << "Forge Game Engine - Exception in background tasks: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << "Forge Game Engine - Unknown exception in background tasks" << std::endl;
  }
}

void GameEngine::setLogicalPresentationMode(SDL_RendererLogicalPresentation mode) {
  m_logicalPresentationMode = mode;
  if (mp_renderer) {
    int width, height;
    SDL_GetWindowSize(mp_window.get(), &width, &height);
    SDL_SetRenderLogicalPresentation(mp_renderer.get(), width, height, mode);
  }
}

bool GameEngine::isVSyncEnabled() const {
  if (!mp_renderer) {
    return false;
  }
  
  // Check current VSync setting using SDL3 API
  int vsync = 0;
  if (SDL_GetRenderVSync(mp_renderer.get(), &vsync)) {
    return (vsync > 0);  // Any positive value means VSync is enabled
  }
  
  return false;
}

SDL_RendererLogicalPresentation GameEngine::getLogicalPresentationMode() const {
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
  // Secondary engine tasks that only run when we have multiple workers allocated
  // Examples: performance monitoring, resource cleanup, etc.
}

void GameEngine::clean() {
  GAMEENGINE_INFO("Starting shutdown sequence...");

  // Cache manager references for better performance
  Forge::ThreadSystem& threadSystem = Forge::ThreadSystem::Instance();
  FontManager& fontMgr = FontManager::Instance();
  SoundManager& soundMgr = SoundManager::Instance();
  EventManager& eventMgr = EventManager::Instance();
  AIManager& aiMgr = AIManager::Instance();
  SaveGameManager& saveMgr = SaveGameManager::Instance();
  InputManager& inputMgr = InputManager::Instance();
  TextureManager& texMgr = TextureManager::Instance();

  // Wait for any pending background tasks to complete
  if (!threadSystem.isShutdown()) {
    std::cout
        << "Forge Game Engine - Waiting for background tasks to complete...\n";
    while (threadSystem.isBusy()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Short delay to avoid busy waiting
    }
  }

  // Clean up engine managers (non-singletons)
  GAMEENGINE_INFO("Cleaning up GameState manager...");
  mp_gameStateManager.reset();

  // Save copies of the smart pointers to resources we'll clean up at the very end
  auto window_to_destroy = std::move(mp_window);
  auto renderer_to_destroy = std::move(mp_renderer);

  // Clean up Managers in reverse order of their initialization
  GAMEENGINE_INFO("Cleaning up Font Manager...");
  fontMgr.clean();

  GAMEENGINE_INFO("Cleaning up Sound Manager...");
  soundMgr.clean();

  GAMEENGINE_INFO("Cleaning up UI Manager...");
  UIManager& uiMgr = UIManager::Instance();
  uiMgr.clean();

  GAMEENGINE_INFO("Cleaning up Event Manager...");
  eventMgr.clean();

  GAMEENGINE_INFO("Cleaning up AI Manager...");
  aiMgr.clean();

  GAMEENGINE_INFO("Cleaning up Save Game Manager...");
  saveMgr.clean();

  GAMEENGINE_INFO("Cleaning up Input Manager...");
  inputMgr.clean();

  GAMEENGINE_INFO("Cleaning up Texture Manager...");
  texMgr.clean();

  // Clean up the thread system
  GAMEENGINE_INFO("Cleaning up Thread System...");
  if (!threadSystem.isShutdown()) {
    threadSystem.clean();
  }

  // Clear manager cache references
  GAMEENGINE_INFO("Clearing manager caches...");
  mp_aiManager = nullptr;
  mp_eventManager = nullptr;
  // InputManager not cached
  GAMEENGINE_INFO("Manager caches cleared");

  // Finally clean up SDL resources
  GAMEENGINE_INFO("Cleaning up SDL resources...");

  // Explicitly reset smart pointers at the end, after all subsystems
  // are done using them - this will trigger their custom deleters
  renderer_to_destroy.reset();
  window_to_destroy.reset();
  SDL_Quit();
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
