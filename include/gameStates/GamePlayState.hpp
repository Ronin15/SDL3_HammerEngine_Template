/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef GAME_PLAY_STATE_HPP
#define GAME_PLAY_STATE_HPP

#include "controllers/ControllerRegistry.hpp"
#include "events/TimeEvent.hpp"
#include "events/WeatherEvent.hpp"
#include "gameStates/GameState.hpp"
#include "managers/EventManager.hpp"
#include "utils/ResourceHandle.hpp"
#include <memory>
#include <string>

// Forward declarations (full includes in .cpp)
class Player;
namespace HammerEngine {
class Camera;
class WorldRenderPipeline;
#ifdef USE_SDL3_GPU
class GPUSceneRenderer;
#endif
}

class GamePlayState : public GameState {
public:
  GamePlayState();  // Defined in .cpp for unique_ptr with forward-declared types
  ~GamePlayState() override;
  bool enter() override;
  void update(float deltaTime) override;
  void render(SDL_Renderer* renderer, float interpolationAlpha = 1.0f) override;
  void handleInput() override;
  bool exit() override;
  void pause() override;
  void resume() override;
  std::string getName() const override;

#ifdef USE_SDL3_GPU
  // GPU rendering support
  void recordGPUVertices(HammerEngine::GPURenderer& gpuRenderer,
                         float interpolationAlpha) override;
  void renderGPUScene(HammerEngine::GPURenderer& gpuRenderer,
                      SDL_GPURenderPass* scenePass,
                      float interpolationAlpha) override;
  void renderGPUUI(HammerEngine::GPURenderer& gpuRenderer,
                   SDL_GPURenderPass* swapchainPass) override;
  bool supportsGPURendering() const override { return true; }
#endif

private:
  bool m_transitioningToLoading{
      false}; // Flag to indicate we're transitioning to loading state
  std::shared_ptr<Player> mp_Player{nullptr}; // Player object
  bool m_inventoryVisible{false}; // Flag to control inventory UI visibility
  bool m_initialized{false}; // Flag to track if state is already initialized (for pause/resume)

  // Camera for world navigation and player following
  std::unique_ptr<HammerEngine::Camera> m_camera{nullptr};

  // World render pipeline for coordinated chunk management and scene rendering
  std::unique_ptr<HammerEngine::WorldRenderPipeline> m_renderPipeline{nullptr};

#ifdef USE_SDL3_GPU
  // GPU scene renderer for coordinated GPU rendering
  std::unique_ptr<HammerEngine::GPUSceneRenderer> m_gpuSceneRenderer{nullptr};
#endif

  // Resource handles resolved at initialization (resource handle system
  // compliance)
  HammerEngine::ResourceHandle m_goldHandle;
  HammerEngine::ResourceHandle m_healthPotionHandle;
  HammerEngine::ResourceHandle m_ironOreHandle;
  HammerEngine::ResourceHandle m_woodHandle;

  // Track whether world has been loaded (prevents re-entering LoadingState)
  bool m_worldLoaded{false};

  // Track if we need to transition to loading screen on first update
  bool m_needsLoading{false};

  // FPS counter (toggled with F2)
  bool m_fpsVisible{false};
  std::string m_fpsBuffer{};
  float m_lastDisplayedFPS{-1.0f};

  // --- Controllers (owned by ControllerRegistry) ---
  ControllerRegistry m_controllers;

  // --- Time UI display buffer ---
  std::string m_statusBuffer{};  // Reusable buffer for status text (zero allocation)
  bool m_statusBarDirty{true};   // Flag to rebuild status bar only when events fire

  // --- Combat HUD ---
  void initializeCombatHUD();
  void updateCombatHUD();

  // Inventory UI methods
  void initializeInventoryUI();
  void toggleInventoryDisplay();
  void addDemoResource(HammerEngine::ResourceHandle resourceHandle,
                       int quantity);
  void removeDemoResource(HammerEngine::ResourceHandle resourceHandle,
                          int quantity);
  void
  initializeResourceHandles(); // Resolve names to handles during initialization

  // Camera management methods
  void initializeCamera();
  void updateCamera(float deltaTime);
  // Camera auto-manages world bounds; no state-level setup needed

  // Day/night visual overlay state (updated via TimePeriodChangedEvent)
  // Current interpolated values (what's actually rendered)
  float m_dayNightOverlayR{0.0f};
  float m_dayNightOverlayG{0.0f};
  float m_dayNightOverlayB{0.0f};
  float m_dayNightOverlayA{0.0f};
  // Target values (from event, what we're interpolating toward)
  float m_dayNightTargetR{0.0f};
  float m_dayNightTargetG{0.0f};
  float m_dayNightTargetB{0.0f};
  float m_dayNightTargetA{0.0f};
  // Transition timing
  static constexpr float DAY_NIGHT_TRANSITION_DURATION{30.0f};  // seconds
  EventManager::HandlerToken m_dayNightEventToken;
  bool m_dayNightSubscribed{false};

  // Day/night event handlers and update
  void onTimePeriodChanged(const EventData& data);
  void updateDayNightOverlay(float deltaTime);
  void renderDayNightOverlay(SDL_Renderer* renderer, int width, int height);

  // Ambient particle effects (dust motes, fireflies) - managed per time period
  void updateAmbientParticles(TimePeriod period);
  void stopAmbientParticles();
  void onWeatherChanged(const EventData& data);
  uint32_t m_ambientDustEffectId{0};
  uint32_t m_ambientFireflyEffectId{0};
  TimePeriod m_lastAmbientPeriod{TimePeriod::Day};  // Track to avoid particle thrashing
  bool m_ambientParticlesActive{false};  // Whether ambient particles are currently running
  EventManager::HandlerToken m_weatherEventToken;
  bool m_weatherSubscribed{false};
  TimePeriod m_currentTimePeriod{TimePeriod::Day};  // Track current period for weather changes
  WeatherType m_lastWeatherType{WeatherType::Clear};  // Track to avoid redundant weather processing
};

#endif // GAME_PLAY_STATE_HPP
