/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef EVENT_DEMO_STATE_HPP
#define EVENT_DEMO_STATE_HPP

#include "gameStates/GameState.hpp"
#include "events/WeatherEvent.hpp"

#include "entities/NPC.hpp"
#include "entities/Player.hpp"
#include <chrono>
#include <deque>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

// Forward declarations with smart pointer types
class NPC;
using NPCPtr = std::shared_ptr<NPC>;

class Player;
using PlayerPtr = std::shared_ptr<Player>;

class EventDemoState : public GameState {
public:
    EventDemoState();
    ~EventDemoState() override;

    void update() override;
    void render() override;

    bool enter() override;
    bool exit() override;

    std::string getName() const override { return "EventDemo"; }

private:
    // Demo management methods
    void setupEventSystem();
    void createTestEvents();
    void handleInput();
    void updateDemoTimer();
    void updateFrameRate();
    void renderUI();
    void renderEventStatus();
    void renderControls();

    // Event demonstration methods
    void triggerWeatherDemo();
    void triggerWeatherDemoAuto();     // Auto demo progression version
    void triggerWeatherDemoManual();   // Manual trigger version
    void triggerNPCSpawnDemo();
    void triggerSceneTransitionDemo();
    void triggerCustomEventDemo();
    void resetAllEvents();

    // Event handler methods
    void onWeatherChanged(const std::string& message);
    void onNPCSpawned(const std::string& message);
    void onSceneChanged(const std::string& message);

    // Demo state tracking
    enum class DemoPhase {
        Initialization,
        WeatherDemo,
        NPCSpawnDemo,
        SceneTransitionDemo,
        CustomEventDemo,
        InteractiveMode,
        Complete
    };

    DemoPhase m_currentPhase{DemoPhase::Initialization};
    float m_phaseTimer{0.0f};
    float m_phaseDuration{8.0f}; // 8 seconds per phase for better pacing
    bool m_autoMode{true}; // Auto-advance through demos - enabled by default

    // Entities
    std::vector<NPCPtr> m_spawnedNPCs{};
    PlayerPtr m_player{};

    // Event tracking
    std::unordered_map<std::string, bool> m_eventStates{};
    std::vector<std::string> m_eventLog{};
    size_t m_maxLogEntries{10};

    // Demo settings
    float m_worldWidth{800.0f};
    float m_worldHeight{600.0f};

    // Weather demo variables
    WeatherType m_currentWeather{WeatherType::Clear};
    float m_weatherTransitionTime{3.0f};
    std::vector<WeatherType> m_weatherSequence{
        WeatherType::Clear,
        WeatherType::Cloudy,
        WeatherType::Rainy,
        WeatherType::Stormy,
        WeatherType::Foggy,
        WeatherType::Snowy
    };
    size_t m_currentWeatherIndex{0};
    float m_weatherChangeInterval{4.0f}; // Time between weather changes
    size_t m_weatherChangesShown{0}; // Track how many weather types shown
    bool m_weatherDemoComplete{false}; // Flag to prevent infinite weather cycling

    // NPC spawn demo variables
    std::vector<std::string> m_npcTypes{"Guard", "Villager", "Merchant", "Warrior"};
    size_t m_currentNPCTypeIndex{0};

    // Scene transition demo variables
    std::vector<std::string> m_sceneNames{"Forest", "Village", "Castle", "Dungeon"};
    size_t m_currentSceneIndex{0};

    // Input handling
    struct InputState {
        bool space{false};
        bool enter{false};
        bool tab{false};
        bool num1{false};
        bool num2{false};
        bool num3{false};
        bool num4{false};
        bool num5{false};
        bool escape{false};
        bool r{false}; // Reset
        bool a{false}; // Auto mode toggle
    };
    InputState m_input{};
    InputState m_lastInput{};

    // UI and display
    std::string m_statusText{};
    std::vector<std::string> m_instructions{};

    // Frame rate counter
    std::chrono::steady_clock::time_point m_lastFrameTime{};
    std::deque<float> m_frameTimes{};
    int m_frameCount{0};
    float m_currentFPS{0.0f};
    float m_averageFPS{0.0f};
    static constexpr int MAX_FRAME_SAMPLES{60};

    // Demo timing (separate from FPS timing)
    std::chrono::steady_clock::time_point m_demoStartTime{};
    std::chrono::steady_clock::time_point m_demoLastTime{};
    float m_totalDemoTime{0.0f};
    float m_lastEventTriggerTime{0.0f};
    float m_eventFireInterval{4.0f}; // Minimum seconds between event triggers - slower for better visibility
    bool m_limitMessageShown{false}; // Track if limit message has been shown
    
    // Batch behavior assignment
    std::vector<std::pair<std::shared_ptr<NPC>, std::string>> m_pendingBehaviorAssignments{};

    // Event manager accessed via singleton - no raw pointer needed

    // Helper methods
    bool isKeyPressed(bool current, bool previous) const;
    void addLogEntry(const std::string& entry);
    std::string getCurrentPhaseString() const;
    std::string getCurrentWeatherString() const;
    void updateInstructions();
    void cleanupSpawnedNPCs();
    void createNPCAtPosition(const std::string& npcType, float x, float y);
    
    // Batch behavior assignment methods
    std::shared_ptr<NPC> createNPCAtPositionWithoutBehavior(const std::string& npcType, float x, float y);
    std::string determineBehaviorForNPCType(const std::string& npcType);
    void batchAssignBehaviors(const std::vector<std::pair<std::shared_ptr<NPC>, std::string>>& assignments);

    // AI behavior integration methods
    void setupAIBehaviors();
    void assignAIBehaviorToNPC(std::shared_ptr<NPC> npc, const std::string& npcType);
};

#endif // EVENT_DEMO_STATE_HPP
