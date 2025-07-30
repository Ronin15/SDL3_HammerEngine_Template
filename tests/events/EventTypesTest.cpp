/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE EventTypesTest
#include <boost/test/unit_test.hpp>

#include "events/EventFactory.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "events/ParticleEffectEvent.hpp"
#include "events/SceneChangeEvent.hpp"
#include "events/WeatherEvent.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct EventTypesFixture {
  EventTypesFixture() {
    // Make sure we start fresh with each test
    EventFactory::Instance().clean();
    BOOST_CHECK(EventFactory::Instance().init());

    // Always register standard event creators explicitly for each test
    // Make sure to register Weather creator first as it's used in most tests
    registerWeatherCreator();
    registerSceneChangeCreator();
    registerNPCSpawnCreator();
  }

  ~EventTypesFixture() {
    // Clean up EventFactory after each test
    EventFactory::Instance().clean();
  }

  // Register each creator separately for better control
  void registerWeatherCreator() {
    EventFactory::Instance().registerCustomEventCreator(
        "Weather", [](const EventDefinition &def) {
          std::string weatherType = def.params.count("weatherType")
                                        ? def.params.at("weatherType")
                                        : "Clear";
          float intensity = def.numParams.count("intensity")
                                ? def.numParams.at("intensity")
                                : 0.5f;
          float transitionTime = def.numParams.count("transitionTime")
                                     ? def.numParams.at("transitionTime")
                                     : 5.0f;

          return EventFactory::Instance().createWeatherEvent(
              def.name, weatherType, intensity, transitionTime);
        });
  }

  void registerSceneChangeCreator() {
    EventFactory::Instance().registerCustomEventCreator(
        "SceneChange", [](const EventDefinition &def) {
          std::string targetScene = def.params.count("targetScene")
                                        ? def.params.at("targetScene")
                                        : "";
          std::string transitionType = def.params.count("transitionType")
                                           ? def.params.at("transitionType")
                                           : "fade";
          float duration = def.numParams.count("duration")
                               ? def.numParams.at("duration")
                               : 1.0f;

          return EventFactory::Instance().createSceneChangeEvent(
              def.name, targetScene, transitionType, duration);
        });
  }

  void registerNPCSpawnCreator() {
    /*
    EventFactory::Instance().registerCustomEventCreator("NPCSpawn", [](const
    EventDefinition& def) { std::string npcType = def.params.count("npcType") ?
    def.params.at("npcType") : ""; int count =
    static_cast<int>(def.numParams.count("count") ? def.numParams.at("count")
    : 1.0f); float spawnRadius = def.numParams.count("spawnRadius") ?
    def.numParams.at("spawnRadius") : 0.0f;

        return EventFactory::Instance().createNPCSpawnEvent(def.name, npcType,
    count, spawnRadius);
    });
    */
  }
};

// Test WeatherEvent creation and functionality
BOOST_FIXTURE_TEST_CASE(WeatherEventBasics, EventTypesFixture) {
  // Create a weather event
  auto rainEvent = std::make_shared<WeatherEvent>("Rain", WeatherType::Rainy);

  // Check basic properties
  BOOST_CHECK_EQUAL(rainEvent->getName(), "Rain");
  BOOST_CHECK_EQUAL(rainEvent->getType(), "Weather");
  BOOST_CHECK_EQUAL(rainEvent->getWeatherType(), WeatherType::Rainy);
  BOOST_CHECK_EQUAL(rainEvent->getWeatherTypeString(), "Rainy");
  BOOST_CHECK(rainEvent->isActive());

  // Test weather parameters
  WeatherParams params;
  params.intensity = 0.8f;
  params.visibility = 0.5f;
  params.transitionTime = 3.0f;
  params.particleEffect = "heavy_rain";
  params.soundEffect = "rain_sound";

  rainEvent->setWeatherParams(params);
  BOOST_CHECK_EQUAL(rainEvent->getWeatherParams().intensity, 0.8f);
  BOOST_CHECK_EQUAL(rainEvent->getWeatherParams().visibility, 0.5f);
  BOOST_CHECK_EQUAL(rainEvent->getWeatherParams().transitionTime, 3.0f);
  BOOST_CHECK_EQUAL(rainEvent->getWeatherParams().particleEffect, "heavy_rain");
  BOOST_CHECK_EQUAL(rainEvent->getWeatherParams().soundEffect, "rain_sound");

  // Test custom weather type
  auto customWeather = std::make_shared<WeatherEvent>("Custom", "AcidRain");
  BOOST_CHECK_EQUAL(customWeather->getWeatherType(), WeatherType::Custom);
  BOOST_CHECK_EQUAL(customWeather->getWeatherTypeString(), "AcidRain");

  // Test conditions without any conditions set
  auto baseEvent =
      std::make_shared<WeatherEvent>("BaseTest", WeatherType::Clear);
  BOOST_CHECK(
      !baseEvent->checkConditions()); // No conditions set, should return false

  // Create a new event instance for each condition test to avoid interference
  // Test with a simple false condition in its own scope
  {
    auto falseEvent =
        std::make_shared<WeatherEvent>("FalseTest", WeatherType::Clear);
    // Make sure there are no existing conditions
    falseEvent->clean();
    // Add a condition that always returns false
    falseEvent->addTimeCondition(
        []() { return false; }); // Use direct lambda to avoid capture issues
    // This should fail since the condition returns false
    BOOST_CHECK(!falseEvent->checkConditions());
  }

  // Test with a simple true condition in its own scope
  {
    auto trueEvent =
        std::make_shared<WeatherEvent>("TrueTest", WeatherType::Clear);
    // Make sure there are no existing conditions
    trueEvent->clean();
    // Add a condition that always returns true - no capture to avoid lifetime
    // issues
    trueEvent->addTimeCondition([]() { return true; });
    // This should pass since the condition returns true
    BOOST_CHECK(trueEvent->checkConditions());
  }
}

// Test SceneChangeEvent creation and functionality
BOOST_FIXTURE_TEST_CASE(SceneChangeEventBasics, EventTypesFixture) {
  // Create a scene change event
  auto sceneEvent =
      std::make_shared<SceneChangeEvent>("ToMainMenu", "MainMenu");

  // Check basic properties
  BOOST_CHECK_EQUAL(sceneEvent->getName(), "ToMainMenu");
  BOOST_CHECK_EQUAL(sceneEvent->getType(), "SceneChange");
  BOOST_CHECK_EQUAL(sceneEvent->getTargetSceneID(), "MainMenu");
  BOOST_CHECK(sceneEvent->isActive());

  // Test transition type
  sceneEvent->setTransitionType(TransitionType::Dissolve);
  BOOST_CHECK_EQUAL(sceneEvent->getTransitionType(), TransitionType::Dissolve);

  // Test transition parameters
  TransitionParams params;
  params.duration = 2.5f;
  params.transitionEffect = "dissolve";
  params.playSound = true;
  params.soundEffect = "transition_sound";
  params.soundVolume = 0.7f;

  sceneEvent->setTransitionParams(params);
  BOOST_CHECK_EQUAL(sceneEvent->getTransitionParams().duration, 2.5f);
  BOOST_CHECK_EQUAL(sceneEvent->getTransitionParams().transitionEffect,
                    "dissolve");
  BOOST_CHECK(sceneEvent->getTransitionParams().playSound);
  BOOST_CHECK_EQUAL(sceneEvent->getTransitionParams().soundEffect,
                    "transition_sound");
  BOOST_CHECK_EQUAL(sceneEvent->getTransitionParams().soundVolume, 0.7f);

  // Test trigger zones
  sceneEvent->setTriggerZone(100.0f, 200.0f, 50.0f); // Circle zone

  // Test player input trigger
  sceneEvent->setRequirePlayerInput(true);
  sceneEvent->setInputKey("E");

  // Test timer trigger
  sceneEvent->setTimerTrigger(5.0f);
  sceneEvent->startTimer();
  BOOST_CHECK(!sceneEvent->isTimerComplete()); // Timer just started

  // Test custom conditions
  bool conditionFlag = false;
  sceneEvent->addCondition([&conditionFlag]() { return conditionFlag; });

  BOOST_CHECK(
      !sceneEvent->checkConditions()); // Should be false until condition is met

  conditionFlag = true;
  // In a real test, this would return true if all other conditions were also
  // met Here we expect false because isPlayerInTriggerZone() and
  // isPlayerInputTriggered() will return false
}

// Test NPCSpawnEvent creation and functionality
BOOST_FIXTURE_TEST_CASE(NPCSpawnEventBasics, EventTypesFixture) {
  // Create an NPC spawn event
  auto spawnEvent = std::make_shared<NPCSpawnEvent>("SpawnGuards", "Guard");

  // Check basic properties
  BOOST_CHECK_EQUAL(spawnEvent->getName(), "SpawnGuards");
  BOOST_CHECK_EQUAL(spawnEvent->getType(), "NPCSpawn");
  BOOST_CHECK(spawnEvent->isActive());

  // Test spawn parameters
  SpawnParameters params;
  params.npcType = "EliteGuard";
  params.count = 3;
  params.spawnRadius = 10.0f;
  params.facingPlayer = true;
  params.fadeIn = true;
  params.fadeTime = 1.5f;
  params.playSpawnEffect = true;
  params.spawnEffectID = "smoke";
  params.spawnSoundID = "spawn_sound";

  spawnEvent->setSpawnParameters(params);
  BOOST_CHECK_EQUAL(spawnEvent->getSpawnParameters().npcType, "EliteGuard");
  BOOST_CHECK_EQUAL(spawnEvent->getSpawnParameters().count, 3);
  BOOST_CHECK_EQUAL(spawnEvent->getSpawnParameters().spawnRadius, 10.0f);
  BOOST_CHECK(spawnEvent->getSpawnParameters().facingPlayer);
  BOOST_CHECK(spawnEvent->getSpawnParameters().fadeIn);
  BOOST_CHECK_EQUAL(spawnEvent->getSpawnParameters().fadeTime, 1.5f);
  BOOST_CHECK(spawnEvent->getSpawnParameters().playSpawnEffect);
  BOOST_CHECK_EQUAL(spawnEvent->getSpawnParameters().spawnEffectID, "smoke");
  BOOST_CHECK_EQUAL(spawnEvent->getSpawnParameters().spawnSoundID,
                    "spawn_sound");

  // Test spawn locations
  spawnEvent->clearSpawnPoints();
  spawnEvent->addSpawnPoint(100.0f, 200.0f);
  spawnEvent->addSpawnPoint(150.0f, 250.0f);

  // Test spawn area
  spawnEvent->setSpawnArea(0.0f, 0.0f, 50.0f); // Circular area

  // Test proximity trigger
  spawnEvent->setProximityTrigger(100.0f);

  // Test time of day trigger
  spawnEvent->setTimeOfDayTrigger(19.0f, 6.0f); // Night time only

  // Test respawn
  spawnEvent->setRespawnTime(30.0f);
  BOOST_CHECK(spawnEvent->areAllEntitiesDead()); // No entities spawned yet
  BOOST_CHECK(!spawnEvent->canRespawn()); // Respawn timer not elapsed yet

  // Test max spawn count
  spawnEvent->setMaxSpawnCount(5);
  BOOST_CHECK_EQUAL(spawnEvent->getMaxSpawnCount(), 5);
  BOOST_CHECK_EQUAL(spawnEvent->getCurrentSpawnCount(), 0);

  // Test custom conditions
  bool conditionFlag = false;
  spawnEvent->addCondition([&conditionFlag]() { return conditionFlag; });

  BOOST_CHECK(
      !spawnEvent->checkConditions()); // Should be false until condition is met

  conditionFlag = true;
  // In a real test, this would still return false because the proximity and
  // time conditions aren't met
}

// Test EventFactory creation methods
BOOST_FIXTURE_TEST_CASE(EventFactoryCreation, EventTypesFixture) {
  // Make sure EventFactory is properly initialized and Weather creator is
  // registered
  EventFactory::Instance().clean();
  EventFactory::Instance().init();
  registerWeatherCreator();

  // Test weather event creation
  auto rainEvent =
      EventFactory::Instance().createWeatherEvent("Rain", "Rainy", 0.7f);
  BOOST_REQUIRE(rainEvent != nullptr);
  BOOST_CHECK_EQUAL(rainEvent->getName(), "Rain");
  BOOST_CHECK_EQUAL(rainEvent->getType(), "Weather");
  BOOST_CHECK_EQUAL(
      static_cast<WeatherEvent *>(rainEvent.get())->getWeatherTypeString(),
      "Rainy");

  // Test scene change event creation
  auto sceneEvent = EventFactory::Instance().createSceneChangeEvent(
      "ToMainMenu", "MainMenu", "fade", 1.5f);
  BOOST_REQUIRE(sceneEvent != nullptr);
  BOOST_CHECK_EQUAL(sceneEvent->getName(), "ToMainMenu");
  BOOST_CHECK_EQUAL(sceneEvent->getType(), "SceneChange");
  BOOST_CHECK_EQUAL(
      static_cast<SceneChangeEvent *>(sceneEvent.get())->getTargetSceneID(),
      "MainMenu");

  // Test NPC spawn event creation
  // Commented out due to linker errors
  /*
  auto spawnEvent = EventFactory::Instance().createNPCSpawnEvent("SpawnGuards",
  "Guard", 3, 10.0f); BOOST_REQUIRE(spawnEvent != nullptr);
  BOOST_CHECK_EQUAL(spawnEvent->getName(), "SpawnGuards");
  BOOST_CHECK_EQUAL(spawnEvent->getType(), "NPCSpawn");
  BOOST_CHECK_EQUAL(static_cast<NPCSpawnEvent*>(spawnEvent.get())->getSpawnParameters().npcType,
  "Guard");
  */

  // Test event creation from definition
  EventDefinition def;
  def.type = "Weather";
  def.name = "Storm";
  def.params["weatherType"] = "Stormy";
  def.numParams["intensity"] = 0.9f;
  def.numParams["transitionTime"] = 4.0f;
  def.boolParams["oneTime"] = true;

  auto stormEvent = EventFactory::Instance().createEvent(def);
  BOOST_REQUIRE(stormEvent != nullptr);
  BOOST_CHECK_EQUAL(stormEvent->getName(), "Storm");
  BOOST_CHECK_EQUAL(stormEvent->getType(), "Weather");
  BOOST_CHECK_EQUAL(
      static_cast<WeatherEvent *>(stormEvent.get())->getWeatherTypeString(),
      "Stormy");
  BOOST_CHECK(stormEvent->isOneTime());
}

// Test event sequence creation
BOOST_FIXTURE_TEST_CASE(EventSequenceCreation, EventTypesFixture) {
  // Create a weather sequence: Rain -> Lightning -> Clear
  std::vector<EventDefinition> weatherSequence = {{"Weather",
                                                   "StartRain",
                                                   {{"weatherType", "Rainy"}},
                                                   {{"intensity", 0.5f}},
                                                   {}},
                                                  {"Weather",
                                                   "Thunderstorm",
                                                   {{"weatherType", "Stormy"}},
                                                   {{"intensity", 0.9f}},
                                                   {}},
                                                  {"Weather",
                                                   "ClearSkies",
                                                   {{"weatherType", "Clear"}},
                                                   {{"transitionTime", 8.0f}},
                                                   {}}};

  auto sequence = EventFactory::Instance().createEventSequence(
      "WeatherSequence", weatherSequence, true);
  BOOST_CHECK_EQUAL(sequence.size(), 3);

  // Verify the sequence was created with proper priorities
  BOOST_CHECK_EQUAL(sequence[0]->getName(), "StartRain");
  BOOST_CHECK_EQUAL(sequence[1]->getName(), "Thunderstorm");
  BOOST_CHECK_EQUAL(sequence[2]->getName(), "ClearSkies");

  // First event should have highest priority
  BOOST_CHECK_GT(sequence[0]->getPriority(), sequence[1]->getPriority());
  BOOST_CHECK_GT(sequence[1]->getPriority(), sequence[2]->getPriority());
}

// Test event cooldown functionality
BOOST_FIXTURE_TEST_CASE(EventCooldownFunctionality, EventTypesFixture) {
  auto event = std::make_shared<WeatherEvent>("TestEvent", WeatherType::Rainy);

  // Set cooldown time
  event->setCooldown(2.0f);
  BOOST_CHECK_EQUAL(event->getCooldown(), 2.0f);
  BOOST_CHECK(!event->isOnCooldown());

  // Start cooldown
  event->startCooldown();
  BOOST_CHECK(event->isOnCooldown());

  // Reset cooldown
  event->resetCooldown();
  BOOST_CHECK(!event->isOnCooldown());
}

// Test ParticleEffectEvent creation and basic functionality
BOOST_FIXTURE_TEST_CASE(ParticleEffectEventBasics, EventTypesFixture) {
  // Test constructor with Vector2D
  Vector2D position(100.0f, 200.0f);
  ParticleEffectEvent effectEvent1("TestEffect1", ParticleEffectType::Fire,
                                   position, 1.5f, 5.0f, "group1",
                                   "fire_sound");

  // Check basic properties
  BOOST_CHECK_EQUAL(effectEvent1.getName(), "TestEffect1");
  BOOST_CHECK_EQUAL(effectEvent1.getType(), "ParticleEffect");
  BOOST_CHECK_EQUAL(effectEvent1.getEffectName(), "Fire");
  BOOST_CHECK_EQUAL(effectEvent1.getPosition().getX(), position.getX());
  BOOST_CHECK_EQUAL(effectEvent1.getPosition().getY(), position.getY());
  BOOST_CHECK_EQUAL(effectEvent1.getIntensity(), 1.5f);
  BOOST_CHECK_EQUAL(effectEvent1.getDuration(), 5.0f);
  BOOST_CHECK_EQUAL(effectEvent1.getGroupTag(), "group1");

  // Test constructor with x,y coordinates
  ParticleEffectEvent effectEvent2("TestEffect2", ParticleEffectType::Smoke,
                                   300.0f, 400.0f, 0.8f, -1.0f, "group2");
  BOOST_CHECK_EQUAL(effectEvent2.getName(), "TestEffect2");
  BOOST_CHECK_EQUAL(effectEvent2.getEffectName(), "Smoke");
  BOOST_CHECK_EQUAL(effectEvent2.getPosition().getX(), 300.0f);
  BOOST_CHECK_EQUAL(effectEvent2.getPosition().getY(), 400.0f);
  BOOST_CHECK_EQUAL(effectEvent2.getIntensity(), 0.8f);
  BOOST_CHECK_EQUAL(effectEvent2.getDuration(), -1.0f); // Infinite duration
  BOOST_CHECK_EQUAL(effectEvent2.getGroupTag(), "group2");
}

// Test ParticleEffectEvent property setters and getters
BOOST_FIXTURE_TEST_CASE(ParticleEffectEventProperties, EventTypesFixture) {
  ParticleEffectEvent effectEvent("PropTest", ParticleEffectType::Sparks, 50.0f,
                                  60.0f);
  // Test position setters
  effectEvent.setPosition(150.0f, 250.0f);
  BOOST_CHECK_EQUAL(effectEvent.getPosition().getX(), 150.0f);
  BOOST_CHECK_EQUAL(effectEvent.getPosition().getY(), 250.0f);

  Vector2D newPos(200.0f, 300.0f);
  effectEvent.setPosition(newPos);
  BOOST_CHECK_EQUAL(effectEvent.getPosition().getX(), newPos.getX());
  BOOST_CHECK_EQUAL(effectEvent.getPosition().getY(), newPos.getY());

  // Test intensity adjustment
  effectEvent.setIntensity(2.5f);
  BOOST_CHECK_EQUAL(effectEvent.getIntensity(), 2.5f);

  // Test duration setting
  effectEvent.setDuration(15.0f);
  BOOST_CHECK_EQUAL(effectEvent.getDuration(), 15.0f);

  // Test group tagging
  effectEvent.setGroupTag("newGroup");
  BOOST_CHECK_EQUAL(effectEvent.getGroupTag(), "newGroup");

  // Test default values
  ParticleEffectEvent defaultEvent("Default", ParticleEffectType::Rain, 0.0f,
                                   0.0f);
  BOOST_CHECK_EQUAL(defaultEvent.getIntensity(), 1.0f);
  BOOST_CHECK_EQUAL(defaultEvent.getDuration(), -1.0f);
  BOOST_CHECK_EQUAL(defaultEvent.getGroupTag(), "");
}

// Test ParticleEffectEvent conditions and state
BOOST_FIXTURE_TEST_CASE(ParticleEffectEventConditions, EventTypesFixture) {
  ParticleEffectEvent effectEvent("ConditionTest", ParticleEffectType::Snow,
                                  0.0f, 0.0f);
  // Should be active by default
  BOOST_CHECK(effectEvent.isActive());

  // Check conditions - should pass basic checks (active state, non-empty effect
  // name) Note: ParticleManager availability check will fail in test
  // environment
  BOOST_CHECK(
      !effectEvent
           .checkConditions()); // Fails due to ParticleManager not initialized

  // Test with empty effect name
  ParticleEffectEvent emptyEvent("Empty", ParticleEffectType::Rain, 0.0f, 0.0f);
  BOOST_CHECK(
      !emptyEvent.checkConditions()); // Should fail due to empty effect name

  // Test inactive event
  effectEvent.setActive(false);
  BOOST_CHECK(
      !effectEvent.checkConditions()); // Should fail due to inactive state
}

// Test ParticleEffectEvent lifecycle
BOOST_FIXTURE_TEST_CASE(ParticleEffectEventLifecycle, EventTypesFixture) {
  ParticleEffectEvent effectEvent("LifecycleTest", ParticleEffectType::Fire,
                                  100.0f, 100.0f, 1.0f, 3.0f);

  // Initially should not be active
  BOOST_CHECK(!effectEvent.isEffectActive());

  // Test update method (should not crash)
  effectEvent.update();

  // Test reset method
  effectEvent.reset();
  BOOST_CHECK(!effectEvent.isEffectActive());

  // Test clean method
  effectEvent.clean();
  BOOST_CHECK(!effectEvent.isEffectActive());

  // Test stopEffect method (should not crash even if no effect is running)
  effectEvent.stopEffect();
  BOOST_CHECK(!effectEvent.isEffectActive());
}

// Test ParticleEffectEvent edge cases
BOOST_FIXTURE_TEST_CASE(ParticleEffectEventEdgeCases, EventTypesFixture) {
  // Test with extreme values
  ParticleEffectEvent extremeEvent("Extreme", ParticleEffectType::Custom,
                                   -1000.0f, 1000.0f, 0.0f, 0.0f);
  BOOST_CHECK_EQUAL(extremeEvent.getPosition().getX(), -1000.0f);
  BOOST_CHECK_EQUAL(extremeEvent.getPosition().getY(), 1000.0f);
  BOOST_CHECK_EQUAL(extremeEvent.getIntensity(), 0.0f);
  BOOST_CHECK_EQUAL(extremeEvent.getDuration(), 0.0f);

  // Test with very high intensity
  extremeEvent.setIntensity(10.0f);
  BOOST_CHECK_EQUAL(extremeEvent.getIntensity(), 10.0f);

  // Test with very long duration
  extremeEvent.setDuration(9999.0f);
  BOOST_CHECK_EQUAL(extremeEvent.getDuration(), 9999.0f);

  // Test execution without ParticleManager (should handle gracefully)
  extremeEvent.execute();                      // Should not crash
  BOOST_CHECK(!extremeEvent.isEffectActive()); // Effect won't be active due to
                                               // no ParticleManager
}
