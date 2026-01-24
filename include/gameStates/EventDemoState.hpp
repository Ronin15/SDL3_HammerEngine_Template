/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef EVENT_DEMO_STATE_HPP
#define EVENT_DEMO_STATE_HPP

#include "controllers/ControllerRegistry.hpp"
#include "events/WeatherEvent.hpp"
#include "gameStates/GameState.hpp"
#include "managers/EventManager.hpp" // For EventData
#include "managers/ParticleManager.hpp"

#include "controllers/render/NPCRenderController.hpp"
#include "entities/EntityHandle.hpp"
#include "entities/Player.hpp"
#include "utils/Camera.hpp"
#include "utils/SceneRenderer.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations with smart pointer types
class Player;
using PlayerPtr = std::shared_ptr<Player>;

class EventDemoState : public GameState {
public:
  EventDemoState();
  ~EventDemoState() override;

  void update(float deltaTime) override;
  void render(SDL_Renderer* renderer, float interpolationAlpha = 1.0f) override;
  void handleInput() override;

  bool enter() override;
  bool exit() override;

  std::string getName() const override { return "EventDemo"; }

private:
  // Demo management methods
  void setupEventSystem();
  void unregisterEventHandlers();

  // Event demonstration methods (manual triggers only)
  void triggerWeatherDemo();
  void triggerNPCSpawnDemo();
  void triggerSceneTransitionDemo();
  void triggerResourceDemo();
  void triggerMassNPCSpawnDemo();
  void triggerConvenienceMethodsDemo();
  void resetAllEvents();

  // Event handler methods
  void onWeatherChanged(const std::string &message);
  void onNPCSpawned(const EventData &data);
  void onSceneChanged(const std::string &message);
  void onResourceChanged(const EventData &data);

  // Controllers (owned by ControllerRegistry, following GamePlayState pattern)
  ControllerRegistry m_controllers{};

  // Data-driven NPC rendering (velocity-based animation)
  NPCRenderController m_npcRenderCtrl{};

  // Player entity
  PlayerPtr m_player{};
  
  // Camera for world navigation
  std::unique_ptr<HammerEngine::Camera> m_camera{nullptr};

  // Scene renderer for pixel-perfect zoomed world rendering
  std::unique_ptr<HammerEngine::SceneRenderer> m_sceneRenderer{nullptr};

  // Demo settings
  float m_worldWidth{800.0f};
  float m_worldHeight{600.0f};

  // Track whether world has been loaded (prevents re-entering LoadingState)
  bool m_worldLoaded{false};

  // Track if we need to transition to loading screen on first update
  bool m_needsLoading{false};

  // Track if we're transitioning to LoadingState (prevents infinite loop)
  bool m_transitioningToLoading{false};

  // Track if state is fully initialized (after returning from LoadingState)
  bool m_initialized{false};

  // Weather demo variables (for manual cycling through weather types)
  WeatherType m_currentWeather{WeatherType::Clear};
  float m_weatherTransitionTime{3.0f};
  std::vector<WeatherType> m_weatherSequence{
      WeatherType::Clear,  WeatherType::Cloudy, WeatherType::Rainy,
      WeatherType::Stormy, WeatherType::Foggy,  WeatherType::Snowy,
      WeatherType::Windy,  WeatherType::Custom, WeatherType::Custom,
      WeatherType::Custom, WeatherType::Custom}; // Custom for HeavyRain,
                                                 // HeavySnow, WindyDust, WindyStorm
  std::vector<std::string> m_customWeatherTypes{
      "", "", "", "", "", "", "", "HeavyRain", "HeavySnow", "WindyDust", "WindyStorm"};
  size_t m_currentWeatherIndex{0};

  // NPC spawn demo variables
  std::vector<std::string> m_npcTypes{"Guard", "Villager", "Merchant",
                                      "Warrior"};
  size_t m_currentNPCTypeIndex{0};

  // Scene transition demo variables
  std::vector<std::string> m_sceneNames{"Forest", "Village", "Castle",
                                        "Dungeon"};
  size_t m_currentSceneIndex{0};


  // Event trigger debouncing
  float m_totalDemoTime{0.0f};
  float m_lastEventTriggerTime{0.0f};

  // Inventory UI
  bool m_showInventory{false}; // Hide inventory panel by default like GamePlayState

  // Resource change tracking for demonstrations
  std::unordered_map<HammerEngine::ResourceHandle, int>
      m_achievementThresholds{};
  std::unordered_map<HammerEngine::ResourceHandle, bool>
      m_achievementsUnlocked{};
  std::vector<std::string> m_resourceLog{}; // Detailed resource change log

  // Event manager accessed via singleton - no raw pointer needed

  // Helper methods
  void addLogEntry(const std::string &entry);
  std::string getCurrentWeatherString() const;
  void cleanupSpawnedNPCs();
  void setupResourceAchievements(); // Setup achievement demonstration

  // Inventory UI methods
  void toggleInventoryDisplay(); // Toggle inventory visibility like GamePlayState

  // Resource change event demonstration methods
  void processResourceAchievements(HammerEngine::ResourceHandle handle,
                                   int oldQty, int newQty);
  void checkResourceWarnings(HammerEngine::ResourceHandle handle, int newQty);
  void logResourceAnalytics(HammerEngine::ResourceHandle handle, int oldQty,
                            int newQty, const std::string &source);

  // AI behavior integration methods
  void setupAIBehaviors();
  
  // Camera management methods
  void initializeCamera();
  void updateCamera(float deltaTime);
  // Camera auto-manages world bounds; no state-level setup needed
  // Camera render offset computed in render() via unified single-read pattern
  
  // Resource demo state
  size_t m_resourceDemonstrationStep{0};
  bool m_resourceIsAdding{true};
  int m_convenienceDemoCounter{0};

  // Registered handler tokens for cleanup
  std::vector<EventManager::HandlerToken> m_handlerTokens{};

  // Status display optimization - zero per-frame allocations (C++20 type-safe)
  std::string m_statusBuffer{};
  float m_lastDisplayedFPS{-1.0f};  // Float for decimal precision
  size_t m_lastDisplayedNPCCount{0};
  std::string m_lastDisplayedWeather{};

  // Cached NPC count (updated in update(), used in render())
  size_t m_cachedNPCCount{0};

  // Lazy-cached weather string (computed only when underlying enum changes)
  WeatherType m_lastCachedWeather{WeatherType::Custom};  // Initialize to invalid to force first compute
  std::string m_cachedWeatherStr{};
};

#endif // EVENT_DEMO_STATE_HPP
