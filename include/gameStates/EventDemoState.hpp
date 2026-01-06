/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef EVENT_DEMO_STATE_HPP
#define EVENT_DEMO_STATE_HPP

#include "events/WeatherEvent.hpp"
#include "gameStates/GameState.hpp"
#include "managers/EventManager.hpp" // For EventData
#include "managers/ParticleManager.hpp"

#include "entities/NPC.hpp"
#include "entities/Player.hpp"
#include "utils/Camera.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations with smart pointer types
class NPC;
using NPCPtr = std::shared_ptr<NPC>;

class Player;
using PlayerPtr = std::shared_ptr<Player>;

// Forward declarations for cached manager pointers
class EntityDataManager;
class WorldManager;
class UIManager;

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
  void createTestEvents();
  void unregisterEventHandlers();
  void updateDemoTimer(float deltaTime);

  void renderUI();

  // Event demonstration methods
  void triggerWeatherDemo();
  void triggerWeatherDemoAuto();   // Auto demo progression version
  void triggerWeatherDemoManual(); // Manual trigger version
  void triggerNPCSpawnDemo();
  void triggerSceneTransitionDemo();
  void triggerParticleEffectDemo(); // NEW: Demonstrate particle effects via
                                    // EventManager
  void triggerResourceDemo();       // NEW: Demonstrate resource management via
                                    // EventManager
  void triggerCustomEventDemo();
  void triggerConvenienceMethodsDemo(); // NEW: Demonstrate convenience methods
  void resetAllEvents();

  // Event handler methods
  void onWeatherChanged(const std::string &message);
  void onNPCSpawned(const EventData &data);
  void onSceneChanged(const std::string &message);
  void onResourceChanged(
      const EventData &data); // NEW: Resource change event handler

  // Demo state tracking
  enum class DemoPhase {
    Initialization,
    WeatherDemo,
    NPCSpawnDemo,
    SceneTransitionDemo,
    ParticleEffectDemo,
    ResourceDemo,
    CustomEventDemo,
    InteractiveMode,
    Complete
  };

  DemoPhase m_currentPhase{DemoPhase::Initialization};
  DemoPhase m_lastInstructionsPhase{DemoPhase::Complete};  // Track to avoid per-frame string allocations
  float m_phaseTimer{0.0f};
  float m_phaseDuration{8.0f}; // 8 seconds per phase for better pacing
  bool m_autoMode{true}; // Auto-advance through demos - enabled by default

  // Entities - stored by handle ID for O(1) lookup
  std::unordered_map<uint32_t, NPCPtr> m_npcsById{};
  std::vector<NPCPtr> m_npcsByEdmIndex{};
  PlayerPtr m_player{};
  
  // Camera for world navigation
  std::unique_ptr<HammerEngine::Camera> m_camera{nullptr};

  // Event tracking
  std::unordered_map<std::string, bool> m_eventStates{};
  std::vector<std::string> m_eventLog{};
  size_t m_maxLogEntries{10};

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

  // Weather demo variables
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
  float m_weatherChangeInterval{4.0f}; // Time between weather changes
  size_t m_weatherChangesShown{0};     // Track how many weather types shown
  bool m_weatherDemoComplete{false}; // Flag to prevent infinite weather cycling

  // NPC spawn demo variables
  std::vector<std::string> m_npcTypes{"Guard", "Villager", "Merchant",
                                      "Warrior"};
  size_t m_currentNPCTypeIndex{0};

  // Scene transition demo variables
  std::vector<std::string> m_sceneNames{"Forest", "Village", "Castle",
                                        "Dungeon"};
  size_t m_currentSceneIndex{0};

  // Independent particle effects tracking
  uint32_t m_fireEffectId{0};
  uint32_t m_smokeEffectId{0};
  uint32_t m_sparksEffectId{0};
  bool m_fireActive{false};
  bool m_smokeActive{false};
  bool m_sparksActive{false};

  // UI and display
  std::string m_statusText{};
  std::vector<std::string> m_instructions{};

  // Demo timing using accumulated deltaTime
  float m_totalDemoTime{0.0f};
  float m_lastEventTriggerTime{0.0f};
  float m_eventFireInterval{4.0f}; // Minimum seconds between event triggers -
                                   // slower for better visibility
  bool m_limitMessageShown{false}; // Track if limit message has been shown

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
  std::string getCurrentPhaseString() const;
  std::string getCurrentWeatherString() const;
  void updateInstructions();
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

  // Unified NPC creation - always assigns behavior before making NPC available
  std::shared_ptr<NPC>
  createNPC(const std::string &npcType, float x, float y,
            const std::string &behaviorOverride = "");
  std::string determineBehaviorForNPCType(const std::string &npcType);

  // AI behavior integration methods
  void setupAIBehaviors();
  
  // Camera management methods
  void initializeCamera();
  void updateCamera(float deltaTime);
  // Camera auto-manages world bounds; no state-level setup needed
  // Camera render offset computed in render() via unified single-read pattern
  
  // Thread-safe replacements for static variables
  size_t m_manualWeatherIndex{0};
  size_t m_particleEffectIndex{0};
  size_t m_particlePositionIndex{0};
  size_t m_resourceDemonstrationStep{0};
  bool m_resourceIsAdding{true};
  int m_convenienceDemoCounter{0};
  
  // Particle effect demo data (moved from static)
  std::vector<std::string> m_particleEffectNames{"Fire", "Smoke", "Sparks"};
  std::vector<Vector2D> m_particleEffectPositions{
      Vector2D(200, 150), // Top-left area
      Vector2D(600, 150), // Top-right area
      Vector2D(400, 300), // Center
      Vector2D(300, 450), // Bottom-left
      Vector2D(500, 450), // Bottom-right
  };

  // Registered handler tokens for cleanup
  std::vector<EventManager::HandlerToken> m_handlerTokens{};

  // Cached manager pointers for render hot path (resolved in enter())
  EntityDataManager* mp_edm{nullptr};
  EventManager* mp_eventMgr{nullptr};
  ParticleManager* mp_particleMgr{nullptr};
  WorldManager* mp_worldMgr{nullptr};
  UIManager* mp_uiMgr{nullptr};

  // Status display optimization - zero per-frame allocations (C++20 type-safe)
  std::string m_phaseBuffer{};
  std::string m_statusBuffer2{};  // Named to avoid conflict with existing m_statusText
  float m_lastDisplayedFPS{-1.0f};  // Float for decimal precision
  size_t m_lastDisplayedNPCCount{0};
  std::string m_lastDisplayedWeather{};
  std::string m_lastDisplayedPhase{};

  // Lazy-cached strings (computed only when underlying enum changes)
  DemoPhase m_lastCachedPhase{DemoPhase::Complete};  // Initialize to invalid to force first compute
  WeatherType m_lastCachedWeather{WeatherType::Custom};  // Initialize to invalid to force first compute
  std::string m_cachedPhaseStr{};
  std::string m_cachedWeatherStr{};

  // Render scale caching - avoid GPU state changes when zoom unchanged
  float m_lastRenderedZoom{1.0f};
};

#endif // EVENT_DEMO_STATE_HPP
