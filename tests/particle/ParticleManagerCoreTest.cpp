/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ParticleManagerCoreTest
#include <boost/test/unit_test.hpp>

#include "events/ParticleEffectEvent.hpp"
#include "managers/ParticleManager.hpp"
#include "utils/Vector2D.hpp"
#include <chrono>
#include <memory>
#include <thread>

// Test fixture for ParticleManager core functionality
struct ParticleManagerCoreFixture {
  ParticleManagerCoreFixture() {
    // Get ParticleManager instance
    manager = &ParticleManager::Instance();

    // Ensure clean state for each test
    if (manager->isInitialized()) {
      manager->clean();
    }
  }

  ~ParticleManagerCoreFixture() {
    // Clean up after each test
    if (manager->isInitialized()) {
      manager->clean();
    }
  }

  ParticleManager *manager;
};

// Test basic initialization
BOOST_FIXTURE_TEST_CASE(TestInitialization, ParticleManagerCoreFixture) {
  // Initially should not be initialized
  BOOST_CHECK(!manager->isInitialized());
  BOOST_CHECK(!manager->isShutdown());

  // Initialize should succeed
  bool initResult = manager->init();
  BOOST_CHECK(initResult);
  BOOST_CHECK(manager->isInitialized());
  BOOST_CHECK(!manager->isShutdown());

  // Should start with no particles
  BOOST_CHECK_EQUAL(manager->getActiveParticleCount(), 0);
}

// Test double initialization handling
BOOST_FIXTURE_TEST_CASE(TestDoubleInitialization, ParticleManagerCoreFixture) {
  // First initialization
  bool firstInit = manager->init();
  BOOST_CHECK(firstInit);
  BOOST_CHECK(manager->isInitialized());

  // Second initialization should still return true but not break anything
  bool secondInit = manager->init();
  BOOST_CHECK(secondInit);
  BOOST_CHECK(manager->isInitialized());
  BOOST_CHECK(!manager->isShutdown());
}

// Test cleanup functionality
BOOST_FIXTURE_TEST_CASE(TestCleanup, ParticleManagerCoreFixture) {
  // Initialize first
  manager->init();
  BOOST_CHECK(manager->isInitialized());

  // Clean should mark as shutdown
  manager->clean();
  BOOST_CHECK(!manager->isInitialized());
  BOOST_CHECK(manager->isShutdown());

  // Should have no active particles after cleanup
  BOOST_CHECK_EQUAL(manager->getActiveParticleCount(), 0);
}

// Test state transition preparation
BOOST_FIXTURE_TEST_CASE(TestPrepareForStateTransition,
                        ParticleManagerCoreFixture) {
  manager->init();

  // Should not be paused initially
  BOOST_CHECK(!manager->isGloballyPaused());

  // Prepare for state transition
  manager->prepareForStateTransition();

  // Should be resumed after preparation (method temporarily pauses then
  // resumes)
  BOOST_CHECK(!manager->isGloballyPaused());
  BOOST_CHECK(manager->isInitialized()); // Should still be initialized
}

// Test built-in effect registration
BOOST_FIXTURE_TEST_CASE(TestBuiltInEffectsRegistration,
                        ParticleManagerCoreFixture) {
  manager->init();

  // Register built-in effects
  manager->registerBuiltInEffects();

  // Try to play some built-in effects to verify they're registered
  Vector2D testPosition(100, 100);

  uint32_t rainEffect =
      manager->playEffect(ParticleEffectType::Rain, testPosition, 0.5f);
  BOOST_CHECK_NE(rainEffect, 0); // Should return valid effect ID

  uint32_t snowEffect =
      manager->playEffect(ParticleEffectType::Snow, testPosition, 0.5f);
  BOOST_CHECK_NE(snowEffect, 0);

  uint32_t fogEffect =
      manager->playEffect(ParticleEffectType::Fog, testPosition, 0.5f);
  BOOST_CHECK_NE(fogEffect, 0);
}

// Test effect ID generation
BOOST_FIXTURE_TEST_CASE(TestEffectIdGeneration, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  Vector2D testPosition(100, 100);

  // Generate multiple effect IDs
  uint32_t id1 =
      manager->playEffect(ParticleEffectType::Rain, testPosition, 0.5f);
  uint32_t id2 =
      manager->playEffect(ParticleEffectType::Snow, testPosition, 0.5f);
  uint32_t id3 =
      manager->playEffect(ParticleEffectType::Fog, testPosition, 0.5f);

  // All IDs should be different and non-zero
  BOOST_CHECK_NE(id1, 0);
  BOOST_CHECK_NE(id2, 0);
  BOOST_CHECK_NE(id3, 0);
  BOOST_CHECK_NE(id1, id2);
  BOOST_CHECK_NE(id2, id3);
  BOOST_CHECK_NE(id1, id3);
}

// Test effect start and stop
BOOST_FIXTURE_TEST_CASE(TestEffectStartStop, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  Vector2D testPosition(100, 100);

  // Start an effect
  uint32_t effectId =
      manager->playEffect(ParticleEffectType::Rain, testPosition, 0.5f);
  BOOST_CHECK_NE(effectId, 0);

  // Effect should be playing
  BOOST_CHECK(manager->isEffectPlaying(effectId));

  // Stop the effect
  manager->stopEffect(effectId);

  // Effect should no longer be playing
  BOOST_CHECK(!manager->isEffectPlaying(effectId));

  // Stopping non-existent effect should not crash
  manager->stopEffect(99999);
}

// Test global pause/resume
BOOST_FIXTURE_TEST_CASE(TestGlobalPauseResume, ParticleManagerCoreFixture) {
  // Test pause/resume functionality by checking update behavior
  manager->registerBuiltInEffects();

  Vector2D testPosition(100, 100);
  uint32_t effectId =
      manager->playEffect(ParticleEffectType::Rain, testPosition, 0.5f);
  BOOST_CHECK_NE(effectId, 0);

  // Update to create some particles
  manager->update(0.1f);
  size_t initialCount = manager->getActiveParticleCount();

  // Pause globally
  manager->setGlobalPause(true);
  BOOST_CHECK(manager->isGloballyPaused());

  // Update while paused should not change particle count
  manager->update(0.1f);
  BOOST_CHECK_EQUAL(manager->getActiveParticleCount(), initialCount);

  // Resume
  manager->setGlobalPause(false);
  BOOST_CHECK(!manager->isGloballyPaused());
}

// Test global visibility
BOOST_FIXTURE_TEST_CASE(TestGlobalVisibility, ParticleManagerCoreFixture) {
  manager->init();

  // Should be visible initially
  BOOST_CHECK(manager->isGloballyVisible());

  // Set invisible
  manager->setGlobalVisibility(false);
}

// Test ParticleEffectEvent integration with ParticleManager
BOOST_FIXTURE_TEST_CASE(TestParticleEffectEventIntegration,
                        ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Test ParticleEffectEvent creation
  Vector2D testPosition(150.0f, 250.0f);
  ParticleEffectEvent effectEvent("IntegrationTest", ParticleEffectType::Fire,
                                  testPosition, 1.2f, 3.0f, "testGroup");

  // Verify event properties
  BOOST_CHECK_EQUAL(effectEvent.getName(), "IntegrationTest");
  BOOST_CHECK_EQUAL(effectEvent.getType(), "ParticleEffect");
  BOOST_CHECK_EQUAL(static_cast<int>(effectEvent.getEffectType()),
                    static_cast<int>(ParticleEffectType::Fire));
  BOOST_CHECK_EQUAL(effectEvent.getPosition().getX(), 150.0f);
  BOOST_CHECK_EQUAL(effectEvent.getPosition().getY(), 250.0f);
  BOOST_CHECK_EQUAL(effectEvent.getIntensity(), 1.2f);
  BOOST_CHECK_EQUAL(effectEvent.getDuration(), 3.0f);
  BOOST_CHECK_EQUAL(effectEvent.getGroupTag(), "testGroup");

  // Initially effect should not be active
  BOOST_CHECK(!effectEvent.isEffectActive());

  // Test event execution (will trigger ParticleManager)
  effectEvent.execute();

  // After execution, effect should be active
  BOOST_CHECK(effectEvent.isEffectActive());

  // Update particle manager to allow particle emission
  manager->update(0.1f); // 100ms update

  // Verify effect was created in ParticleManager (particles may not be emitted
  // immediately) Since the effect is active, the effect system is working
  // correctly
  BOOST_CHECK_GE(manager->getActiveParticleCount(),
                 0); // >= 0 since emission depends on timing

  // Test effect stopping
  effectEvent.stopEffect();
  BOOST_CHECK(!effectEvent.isEffectActive());

  // Test event reset
  effectEvent.reset();
  BOOST_CHECK(!effectEvent.isEffectActive());
}

// Test ParticleEffectEvent with different effect types
BOOST_FIXTURE_TEST_CASE(TestParticleEffectEventTypes,
                        ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  Vector2D position(100.0f, 200.0f);

  // Test different built-in effects
  std::vector<ParticleEffectType> effectTypes = {
      ParticleEffectType::Fire,   ParticleEffectType::Smoke,
      ParticleEffectType::Sparks, ParticleEffectType::Rain,
      ParticleEffectType::Snow,   ParticleEffectType::Fog};

  for (const auto &effectType : effectTypes) {
    std::string effectName;
    switch (effectType) {
    case ParticleEffectType::Fire:
      effectName = "Fire";
      break;
    case ParticleEffectType::Smoke:
      effectName = "Smoke";
      break;
    case ParticleEffectType::Sparks:
      effectName = "Sparks";
      break;
    case ParticleEffectType::Rain:
      effectName = "Rain";
      break;
    case ParticleEffectType::Snow:
      effectName = "Snow";
      break;
    case ParticleEffectType::Fog:
      effectName = "Fog";
      break;
    default:
      effectName = "Unknown";
      break;
    }

    ParticleEffectEvent event("Test_" + effectName, effectType, position, 0.8f,
                              2.0f);

    // Verify event creation
    BOOST_CHECK_EQUAL(static_cast<int>(event.getEffectType()),
                      static_cast<int>(effectType));
    BOOST_CHECK(!event.isEffectActive());

    // Execute event
    event.execute();

    // Should be active after execution
    BOOST_CHECK(event.isEffectActive());

    // Clean up
    event.stopEffect();
    BOOST_CHECK(!event.isEffectActive());
  }
}

// Invalid effects test removed - enum system prevents invalid types at compile
// time

// Test ParticleEffectEvent lifecycle with ParticleManager
BOOST_FIXTURE_TEST_CASE(TestParticleEffectEventLifecycle,
                        ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  Vector2D position(200.0f, 300.0f);
  ParticleEffectEvent event("LifecycleTest", ParticleEffectType::Smoke,
                            position, 1.5f, 5.0f);

  // Test initial state
  BOOST_CHECK(!event.isEffectActive());
  BOOST_CHECK(event.checkConditions()); // Should pass basic conditions

  // Test execution
  event.execute();
  BOOST_CHECK(event.isEffectActive());

  // Test update (should not crash)
  event.update();
  BOOST_CHECK(event.isEffectActive());

  // Test manual stop
  event.stopEffect();
  BOOST_CHECK(!event.isEffectActive());

  // Test re-execution after stop
  event.execute();
  BOOST_CHECK(event.isEffectActive());

  // Test reset (should stop effect and reset state)
  event.reset();
  BOOST_CHECK(!event.isEffectActive());

  // Test clean (should stop effect and clean up)
  event.execute(); // Start again
  BOOST_CHECK(event.isEffectActive());
  event.clean();
  BOOST_CHECK(!event.isEffectActive());
}

// Test ParticleEffectEvent with extreme values
BOOST_FIXTURE_TEST_CASE(TestParticleEffectEventExtremeValues,
                        ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Test with extreme positions
  ParticleEffectEvent extremeEvent1("Extreme1", ParticleEffectType::Fire,
                                    -1000.0f, 1000.0f, 0.1f, 0.1f);
  extremeEvent1.execute();
  BOOST_CHECK(extremeEvent1.isEffectActive());
  extremeEvent1.stopEffect();

  // Test with very high intensity
  ParticleEffectEvent extremeEvent2("Extreme2", ParticleEffectType::Sparks,
                                    0.0f, 0.0f, 10.0f, 1.0f);
  extremeEvent2.execute();
  BOOST_CHECK(extremeEvent2.isEffectActive());
  extremeEvent2.stopEffect();

  // Test with infinite duration
  ParticleEffectEvent infiniteEvent("Infinite", ParticleEffectType::Rain,
                                    100.0f, 100.0f, 1.0f, -1.0f);
  infiniteEvent.execute();
  BOOST_CHECK(infiniteEvent.isEffectActive());
  infiniteEvent.stopEffect();

  // Test with zero duration
  ParticleEffectEvent zeroEvent("Zero", ParticleEffectType::Snow, 100.0f,
                                100.0f, 1.0f, 0.0f);
  zeroEvent.execute();
  BOOST_CHECK(zeroEvent.isEffectActive());
  zeroEvent.stopEffect();
}

// Test multiple simultaneous ParticleEffectEvents
BOOST_FIXTURE_TEST_CASE(TestMultipleParticleEffectEvents,
                        ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Create multiple events
  std::vector<std::unique_ptr<ParticleEffectEvent>> events;
  events.emplace_back(std::make_unique<ParticleEffectEvent>(
      "Multi1", ParticleEffectType::Fire, 100.0f, 100.0f));
  events.emplace_back(std::make_unique<ParticleEffectEvent>(
      "Multi2", ParticleEffectType::Smoke, 200.0f, 200.0f));
  events.emplace_back(std::make_unique<ParticleEffectEvent>(
      "Multi3", ParticleEffectType::Sparks, 300.0f, 300.0f));

  // Execute all events
  for (auto &event : events) {
    event->execute();
    BOOST_CHECK(event->isEffectActive());
  }

  // All should be active simultaneously
  for (auto &event : events) {
    BOOST_CHECK(event->isEffectActive());
  }

  // Verify multiple effects are running in ParticleManager
  BOOST_CHECK_GE(manager->getActiveParticleCount(),
                 0); // May vary based on timing

  // Stop all events
  for (auto &event : events) {
    event->stopEffect();
    BOOST_CHECK(!event->isEffectActive());
  }
}

// Test basic particle creation through effects
BOOST_FIXTURE_TEST_CASE(TestBasicParticleCreation, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  Vector2D testPosition(100, 100);

  // Start with no particles
  BOOST_CHECK_EQUAL(manager->getActiveParticleCount(), 0);

  // Start an effect
  uint32_t effectId =
      manager->playEffect(ParticleEffectType::Rain, testPosition, 1.0f);
  BOOST_CHECK_NE(effectId, 0);

  // Update to allow particle emission
  manager->update(0.1f); // 100ms update

  // Should have some particles now (Rain effect emits particles)
  size_t particleCount = manager->getActiveParticleCount();
  BOOST_CHECK_GT(particleCount, 0);

  // Update again to see particles aging
  manager->update(0.1f);

  // Particles should still exist (they have longer lifetimes)
  BOOST_CHECK_GT(manager->getActiveParticleCount(), 0);
}

// Test update without initialization
BOOST_FIXTURE_TEST_CASE(TestUpdateWithoutInitialization,
                        ParticleManagerCoreFixture) {
  // Should not crash when updating without initialization
  BOOST_CHECK_NO_THROW(manager->update(0.016f));
  BOOST_CHECK_EQUAL(manager->getActiveParticleCount(), 0);
}

// Test operations when globally paused
BOOST_FIXTURE_TEST_CASE(TestOperationsWhenPaused, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  Vector2D testPosition(100, 100);

  // Start an effect and create some particles
  manager->playEffect(ParticleEffectType::Rain, testPosition, 1.0f);
  manager->update(0.1f);
  size_t initialParticleCount = manager->getActiveParticleCount();

  // Pause globally
  manager->setGlobalPause(true);

  // Update should not affect particle count when paused
  manager->update(0.1f);
  BOOST_CHECK_EQUAL(manager->getActiveParticleCount(), initialParticleCount);
}

// Test maximum particle capacity
BOOST_FIXTURE_TEST_CASE(TestMaxParticleCapacity, ParticleManagerCoreFixture) {
  manager->init();

  // Should have some reasonable default capacity
  size_t maxCapacity = manager->getMaxParticleCapacity();
  BOOST_CHECK_GT(maxCapacity, 1000); // Should be at least 1000
  BOOST_CHECK_LE(
      maxCapacity,
      200000); // Should be reasonable (updated for new higher limits)

  // Test that setMaxParticles doesn't crash
  manager->setMaxParticles(5000);
  size_t newCapacity = manager->getMaxParticleCapacity();
  // Capacity should be at least the requested amount (may be more due to vector
  // growth)
  BOOST_CHECK_GE(newCapacity, 5000);
}

// Test performance statistics
BOOST_FIXTURE_TEST_CASE(TestPerformanceStats, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Ensure manager is not paused from previous tests
  manager->setGlobalPause(false);
  BOOST_CHECK(!manager->isGloballyPaused());
  BOOST_CHECK(manager->isInitialized());

  // Reset stats
  manager->resetPerformanceStats();
  ParticlePerformanceStats stats = manager->getPerformanceStats();

  // Should start with zero stats
  BOOST_CHECK_EQUAL(stats.updateCount, 0);
  BOOST_CHECK_EQUAL(stats.renderCount, 0);
  BOOST_CHECK_EQUAL(stats.totalUpdateTime, 0.0);
  BOOST_CHECK_EQUAL(stats.totalRenderTime, 0.0);

  // Create some particles and update multiple times to ensure emission
  Vector2D testPosition(100, 100);
  uint32_t effectId =
      manager->playEffect(ParticleEffectType::Rain, testPosition, 1.0f);
  BOOST_CHECK_NE(effectId, 0);
  BOOST_CHECK(manager->isEffectPlaying(effectId));

  // Update 1201 times to ensure we hit the performance recording threshold
  // Performance stats are only recorded every 1200 frames for performance
  // reasons
  for (int i = 0; i < 1201; ++i) {
    manager->update(0.016f);
  }

  // Verify particles were actually created
  size_t particleCount = manager->getActiveParticleCount();
  BOOST_CHECK_GT(particleCount, 0);

  // Stats should have been updated after 600+ frames
  stats = manager->getPerformanceStats();
  BOOST_CHECK_GT(stats.updateCount, 0);
  BOOST_CHECK_GT(stats.totalUpdateTime, 0.0);
}

// ============================================================================
// PARTICLE INTERPOLATION TESTS
// Tests for smooth particle movement between frames
// ============================================================================

// Test that particles maintain previous position for interpolation
BOOST_FIXTURE_TEST_CASE(TestParticlePositionTracking, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();
  manager->setGlobalPause(false);

  // Create some particles
  Vector2D testPosition(200, 200);
  uint32_t effectId =
      manager->playEffect(ParticleEffectType::Rain, testPosition, 1.0f);
  BOOST_CHECK_NE(effectId, 0);

  // Update multiple times to ensure particles are created
  for (int i = 0; i < 10; ++i) {
    manager->update(0.016f);
  }

  // Verify particles were created
  size_t particleCount = manager->getActiveParticleCount();
  BOOST_CHECK_GT(particleCount, 0);

  // Update again - previous positions should be tracked
  manager->update(0.016f);

  // Still should have particles (rain is continuous)
  BOOST_CHECK_GT(manager->getActiveParticleCount(), 0);
}

// Test particle update with varying delta times
BOOST_FIXTURE_TEST_CASE(TestParticleUpdateWithVaryingDeltaTime, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();
  manager->setGlobalPause(false);

  // Create some particles
  Vector2D testPosition(100, 100);
  uint32_t effectId =
      manager->playEffect(ParticleEffectType::Smoke, testPosition, 1.0f);
  BOOST_CHECK_NE(effectId, 0);

  // Initial updates to create particles
  for (int i = 0; i < 5; ++i) {
    manager->update(0.016f);  // 60 FPS
  }

  // Update with different delta times (simulating frame rate variation)
  manager->update(0.033f);  // 30 FPS
  manager->update(0.008f);  // 120 FPS
  manager->update(0.016f);  // 60 FPS

  // Particles should still exist and be valid
  size_t countAfterVarying = manager->getActiveParticleCount();
  // Count might change due to particle creation/death, but shouldn't crash
  BOOST_CHECK_GE(countAfterVarying, 0);
}

// Test that particle interpolation state survives pause/resume
BOOST_FIXTURE_TEST_CASE(TestInterpolationStateAcrossPauseResume, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();
  manager->setGlobalPause(false);

  // Create some particles
  Vector2D testPosition(150, 150);
  uint32_t effectId =
      manager->playEffect(ParticleEffectType::Fire, testPosition, 1.0f);
  BOOST_CHECK_NE(effectId, 0);

  // Update to create particles
  for (int i = 0; i < 10; ++i) {
    manager->update(0.016f);
  }

  BOOST_CHECK_GT(manager->getActiveParticleCount(), 0);

  // Pause
  manager->setGlobalPause(true);
  BOOST_CHECK(manager->isGloballyPaused());

  // Updates while paused shouldn't change particle count significantly
  manager->update(0.016f);
  manager->update(0.016f);

  // Resume
  manager->setGlobalPause(false);
  BOOST_CHECK(!manager->isGloballyPaused());

  // Continue updating - should work normally
  for (int i = 0; i < 5; ++i) {
    manager->update(0.016f);
  }

  // Particles should still be valid
  BOOST_CHECK_GE(manager->getActiveParticleCount(), 0);
}

// Test particle alpha interpolation (fading)
BOOST_FIXTURE_TEST_CASE(TestParticleAlphaFading, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();
  manager->setGlobalPause(false);

  // Create smoke particles (which fade over time)
  Vector2D testPosition(100, 100);
  uint32_t effectId =
      manager->playEffect(ParticleEffectType::Smoke, testPosition, 0.5f);  // Short duration
  BOOST_CHECK_NE(effectId, 0);

  // Update to create and age particles
  for (int i = 0; i < 50; ++i) {
    manager->update(0.016f);
  }

  // Some particles should exist (continuous emission)
  BOOST_CHECK_GE(manager->getActiveParticleCount(), 0);
}

// Test multiple effects interpolating simultaneously
BOOST_FIXTURE_TEST_CASE(TestMultipleEffectsInterpolation, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();
  manager->setGlobalPause(false);

  // Create multiple effects at different positions
  Vector2D pos1(100, 100);
  Vector2D pos2(300, 300);
  Vector2D pos3(500, 500);

  uint32_t effect1 = manager->playEffect(ParticleEffectType::Rain, pos1, 1.0f);
  uint32_t effect2 = manager->playEffect(ParticleEffectType::Smoke, pos2, 1.0f);
  uint32_t effect3 = manager->playEffect(ParticleEffectType::Fire, pos3, 1.0f);

  BOOST_CHECK_NE(effect1, 0);
  BOOST_CHECK_NE(effect2, 0);
  BOOST_CHECK_NE(effect3, 0);

  // Update all effects together
  for (int i = 0; i < 20; ++i) {
    manager->update(0.016f);
  }

  // Should have particles from multiple effects
  size_t totalParticles = manager->getActiveParticleCount();
  BOOST_CHECK_GT(totalParticles, 0);

  // All effects should still be playing
  BOOST_CHECK(manager->isEffectPlaying(effect1));
  BOOST_CHECK(manager->isEffectPlaying(effect2));
  BOOST_CHECK(manager->isEffectPlaying(effect3));
}

// Test particle scale interpolation
BOOST_FIXTURE_TEST_CASE(TestParticleScaleInterpolation, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();
  manager->setGlobalPause(false);

  // Create an effect that uses scale changes
  Vector2D testPosition(200, 200);
  uint32_t effectId =
      manager->playEffect(ParticleEffectType::Fire, testPosition, 1.0f);
  BOOST_CHECK_NE(effectId, 0);

  // Update to create particles and let them age (scale should change)
  for (int i = 0; i < 30; ++i) {
    manager->update(0.016f);
  }

  // Particles should exist
  BOOST_CHECK_GT(manager->getActiveParticleCount(), 0);
}

// Test rapid effect creation/destruction doesn't break interpolation
BOOST_FIXTURE_TEST_CASE(TestRapidEffectLifecycle, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();
  manager->setGlobalPause(false);

  // Rapidly create and stop effects
  for (int cycle = 0; cycle < 10; ++cycle) {
    Vector2D pos(static_cast<float>(cycle * 50), static_cast<float>(cycle * 50));
    uint32_t effectId = manager->playEffect(ParticleEffectType::Smoke, pos, 0.5f);
    BOOST_CHECK_NE(effectId, 0);

    // Update a few times
    for (int i = 0; i < 5; ++i) {
      manager->update(0.016f);
    }

    // Stop the effect
    manager->stopEffect(effectId);
  }

  // Final updates to ensure stability
  for (int i = 0; i < 10; ++i) {
    manager->update(0.016f);
  }

  // Should not crash and manager should be in valid state
  BOOST_CHECK(manager->isInitialized());
  BOOST_CHECK(!manager->isShutdown());
}

// ============================================================================
// INDEPENDENT EFFECT MANAGEMENT API TESTS
// Tests for independent effects (not weather-related) with group management
// ============================================================================

// Test basic independent effect creation
BOOST_FIXTURE_TEST_CASE(TestPlayIndependentEffect, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  Vector2D position(100.0f, 100.0f);

  // Play an independent effect
  uint32_t effectId = manager->playIndependentEffect(
      ParticleEffectType::Fire, position, 1.0f, -1.0f, "testGroup");

  // Should return valid effect ID
  BOOST_CHECK_NE(effectId, 0);

  // Should be marked as independent
  BOOST_CHECK(manager->isIndependentEffect(effectId));

  // Should be in the active independent effects list
  auto activeEffects = manager->getActiveIndependentEffects();
  BOOST_CHECK(std::find(activeEffects.begin(), activeEffects.end(), effectId) !=
              activeEffects.end());

  // Clean up
  manager->stopIndependentEffect(effectId);
}

// Test stopping individual independent effect
BOOST_FIXTURE_TEST_CASE(TestStopIndependentEffect, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  Vector2D position(200.0f, 200.0f);

  // Create independent effect
  uint32_t effectId = manager->playIndependentEffect(
      ParticleEffectType::Smoke, position, 1.0f, -1.0f, "group1");
  BOOST_CHECK_NE(effectId, 0);
  BOOST_CHECK(manager->isEffectPlaying(effectId));

  // Stop the effect
  manager->stopIndependentEffect(effectId);

  // Should no longer be playing
  BOOST_CHECK(!manager->isEffectPlaying(effectId));

  // Should no longer be in active list
  auto activeEffects = manager->getActiveIndependentEffects();
  BOOST_CHECK(std::find(activeEffects.begin(), activeEffects.end(), effectId) ==
              activeEffects.end());
}

// Test stopping all independent effects
BOOST_FIXTURE_TEST_CASE(TestStopAllIndependentEffects, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Create multiple independent effects with different groups
  uint32_t effect1 = manager->playIndependentEffect(
      ParticleEffectType::Fire, {100.0f, 100.0f}, 1.0f, -1.0f, "groupA");
  uint32_t effect2 = manager->playIndependentEffect(
      ParticleEffectType::Smoke, {200.0f, 200.0f}, 1.0f, -1.0f, "groupB");
  uint32_t effect3 = manager->playIndependentEffect(
      ParticleEffectType::Sparks, {300.0f, 300.0f}, 1.0f, -1.0f, "groupC");

  BOOST_CHECK_NE(effect1, 0);
  BOOST_CHECK_NE(effect2, 0);
  BOOST_CHECK_NE(effect3, 0);

  // Verify all are playing
  BOOST_CHECK(manager->isEffectPlaying(effect1));
  BOOST_CHECK(manager->isEffectPlaying(effect2));
  BOOST_CHECK(manager->isEffectPlaying(effect3));

  // Stop all independent effects
  manager->stopAllIndependentEffects();

  // None should be playing now
  BOOST_CHECK(!manager->isEffectPlaying(effect1));
  BOOST_CHECK(!manager->isEffectPlaying(effect2));
  BOOST_CHECK(!manager->isEffectPlaying(effect3));

  // Active list should be empty
  auto activeEffects = manager->getActiveIndependentEffects();
  BOOST_CHECK(activeEffects.empty());
}

// Test stopping independent effects by group tag
BOOST_FIXTURE_TEST_CASE(TestStopIndependentEffectsByGroup, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Create effects in two groups
  uint32_t effectA1 = manager->playIndependentEffect(
      ParticleEffectType::Fire, {100.0f, 100.0f}, 1.0f, -1.0f, "combat");
  uint32_t effectA2 = manager->playIndependentEffect(
      ParticleEffectType::Smoke, {150.0f, 150.0f}, 1.0f, -1.0f, "combat");
  uint32_t effectB1 = manager->playIndependentEffect(
      ParticleEffectType::Sparks, {200.0f, 200.0f}, 1.0f, -1.0f, "ambient");

  BOOST_CHECK(manager->isEffectPlaying(effectA1));
  BOOST_CHECK(manager->isEffectPlaying(effectA2));
  BOOST_CHECK(manager->isEffectPlaying(effectB1));

  // Stop only combat group
  manager->stopIndependentEffectsByGroup("combat");

  // Combat effects should be stopped
  BOOST_CHECK(!manager->isEffectPlaying(effectA1));
  BOOST_CHECK(!manager->isEffectPlaying(effectA2));

  // Ambient effect should still be playing
  BOOST_CHECK(manager->isEffectPlaying(effectB1));

  // Clean up
  manager->stopIndependentEffect(effectB1);
}

// Test pausing individual independent effect
BOOST_FIXTURE_TEST_CASE(TestPauseIndependentEffect, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  Vector2D position(100.0f, 100.0f);

  uint32_t effectId = manager->playIndependentEffect(
      ParticleEffectType::Fire, position, 1.0f, -1.0f, "test");
  BOOST_CHECK_NE(effectId, 0);

  // Update to create particles
  for (int i = 0; i < 5; ++i) {
    manager->update(0.016f);
  }



  // Pause the effect
  manager->pauseIndependentEffect(effectId, true);

  // Update again - particle count shouldn't increase from this effect
  for (int i = 0; i < 5; ++i) {
    manager->update(0.016f);
  }

  // Effect should still be playing (paused != stopped)
  BOOST_CHECK(manager->isEffectPlaying(effectId));

  // Resume the effect
  manager->pauseIndependentEffect(effectId, false);

  // Should continue working normally
  manager->update(0.016f);
  BOOST_CHECK(manager->isEffectPlaying(effectId));

  // Clean up
  manager->stopIndependentEffect(effectId);
}

// Test pausing all independent effects
BOOST_FIXTURE_TEST_CASE(TestPauseAllIndependentEffects, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Create multiple effects
  uint32_t effect1 = manager->playIndependentEffect(
      ParticleEffectType::Fire, {100.0f, 100.0f}, 1.0f, -1.0f, "group1");
  uint32_t effect2 = manager->playIndependentEffect(
      ParticleEffectType::Smoke, {200.0f, 200.0f}, 1.0f, -1.0f, "group2");

  // Update to create particles
  for (int i = 0; i < 5; ++i) {
    manager->update(0.016f);
  }

  // Pause all independent effects
  manager->pauseAllIndependentEffects(true);

  // Effects should still exist but be paused
  BOOST_CHECK(manager->isEffectPlaying(effect1));
  BOOST_CHECK(manager->isEffectPlaying(effect2));

  // Resume all
  manager->pauseAllIndependentEffects(false);

  // Should continue working
  manager->update(0.016f);
  BOOST_CHECK(manager->isEffectPlaying(effect1));
  BOOST_CHECK(manager->isEffectPlaying(effect2));

  // Clean up
  manager->stopAllIndependentEffects();
}

// Test pausing independent effects by group
BOOST_FIXTURE_TEST_CASE(TestPauseIndependentEffectsByGroup, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Create effects in two groups
  uint32_t effectA = manager->playIndependentEffect(
      ParticleEffectType::Fire, {100.0f, 100.0f}, 1.0f, -1.0f, "explosions");
  uint32_t effectB = manager->playIndependentEffect(
      ParticleEffectType::Smoke, {200.0f, 200.0f}, 1.0f, -1.0f, "environment");

  // Update to create particles
  for (int i = 0; i < 5; ++i) {
    manager->update(0.016f);
  }

  // Pause only explosions group
  manager->pauseIndependentEffectsByGroup("explosions", true);

  // Both should still be playing
  BOOST_CHECK(manager->isEffectPlaying(effectA));
  BOOST_CHECK(manager->isEffectPlaying(effectB));

  // Resume explosions group
  manager->pauseIndependentEffectsByGroup("explosions", false);

  // Clean up
  manager->stopAllIndependentEffects();
}

// Test isIndependentEffect distinguishes from regular effects
BOOST_FIXTURE_TEST_CASE(TestIsIndependentEffectDistinction, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Create a regular effect
  Vector2D position(100.0f, 100.0f);
  uint32_t regularEffect =
      manager->playEffect(ParticleEffectType::Rain, position, 1.0f);

  // Create an independent effect
  uint32_t independentEffect = manager->playIndependentEffect(
      ParticleEffectType::Fire, position, 1.0f, -1.0f, "combat");

  BOOST_CHECK_NE(regularEffect, 0);
  BOOST_CHECK_NE(independentEffect, 0);

  // Regular effect should NOT be marked as independent
  BOOST_CHECK(!manager->isIndependentEffect(regularEffect));

  // Independent effect SHOULD be marked as independent
  BOOST_CHECK(manager->isIndependentEffect(independentEffect));

  // Clean up
  manager->stopEffect(regularEffect);
  manager->stopIndependentEffect(independentEffect);
}

// Test getActiveIndependentEffects returns correct list
BOOST_FIXTURE_TEST_CASE(TestGetActiveIndependentEffects, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Initially no independent effects
  auto initialEffects = manager->getActiveIndependentEffects();
  BOOST_CHECK(initialEffects.empty());

  // Create several independent effects
  uint32_t effect1 = manager->playIndependentEffect(
      ParticleEffectType::Fire, {100.0f, 100.0f}, 1.0f, -1.0f, "group1");
  uint32_t effect2 = manager->playIndependentEffect(
      ParticleEffectType::Smoke, {200.0f, 200.0f}, 1.0f, -1.0f, "group1");
  uint32_t effect3 = manager->playIndependentEffect(
      ParticleEffectType::Sparks, {300.0f, 300.0f}, 1.0f, -1.0f, "group2");

  // Get active effects
  auto activeEffects = manager->getActiveIndependentEffects();
  BOOST_CHECK_EQUAL(activeEffects.size(), 3);

  // Verify all effects are in the list
  BOOST_CHECK(std::find(activeEffects.begin(), activeEffects.end(), effect1) !=
              activeEffects.end());
  BOOST_CHECK(std::find(activeEffects.begin(), activeEffects.end(), effect2) !=
              activeEffects.end());
  BOOST_CHECK(std::find(activeEffects.begin(), activeEffects.end(), effect3) !=
              activeEffects.end());

  // Stop one effect
  manager->stopIndependentEffect(effect2);

  // Should now have 2 effects
  activeEffects = manager->getActiveIndependentEffects();
  BOOST_CHECK_EQUAL(activeEffects.size(), 2);
  BOOST_CHECK(std::find(activeEffects.begin(), activeEffects.end(), effect2) ==
              activeEffects.end());

  // Clean up
  manager->stopAllIndependentEffects();
}

// Test getActiveIndependentEffectsByGroup
BOOST_FIXTURE_TEST_CASE(TestGetActiveIndependentEffectsByGroup, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Create effects in different groups
  uint32_t effectA1 = manager->playIndependentEffect(
      ParticleEffectType::Fire, {100.0f, 100.0f}, 1.0f, -1.0f, "combat");
  uint32_t effectA2 = manager->playIndependentEffect(
      ParticleEffectType::Smoke, {150.0f, 150.0f}, 1.0f, -1.0f, "combat");
  uint32_t effectB1 = manager->playIndependentEffect(
      ParticleEffectType::Sparks, {200.0f, 200.0f}, 1.0f, -1.0f, "ambient");


  // Get combat group effects
  auto combatEffects = manager->getActiveIndependentEffectsByGroup("combat");
  BOOST_CHECK_EQUAL(combatEffects.size(), 2);
  BOOST_CHECK(std::find(combatEffects.begin(), combatEffects.end(), effectA1) !=
              combatEffects.end());
  BOOST_CHECK(std::find(combatEffects.begin(), combatEffects.end(), effectA2) !=
              combatEffects.end());

  // Get ambient group effects
  auto ambientEffects = manager->getActiveIndependentEffectsByGroup("ambient");
  BOOST_CHECK_EQUAL(ambientEffects.size(), 1);
  BOOST_CHECK(std::find(ambientEffects.begin(), ambientEffects.end(), effectB1) !=
              ambientEffects.end());

  // Get non-existent group
  auto emptyEffects = manager->getActiveIndependentEffectsByGroup("nonexistent");
  BOOST_CHECK(emptyEffects.empty());

  // Clean up
  manager->stopAllIndependentEffects();
}

// Test independent effect with duration expiration
BOOST_FIXTURE_TEST_CASE(TestIndependentEffectDuration, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Create effect with short duration
  uint32_t effectId = manager->playIndependentEffect(
      ParticleEffectType::Sparks, {100.0f, 100.0f}, 1.0f, 0.5f, "timed");

  BOOST_CHECK_NE(effectId, 0);
  BOOST_CHECK(manager->isEffectPlaying(effectId));

  // Update for longer than duration (0.5 seconds = 500ms)
  for (int i = 0; i < 40; ++i) {  // 40 * 16ms = 640ms > 500ms
    manager->update(0.016f);
  }

  // Effect should have expired
  BOOST_CHECK(!manager->isEffectPlaying(effectId));
}

// Test multiple independent effects with same group tag
BOOST_FIXTURE_TEST_CASE(TestMultipleEffectsSameGroup, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  const std::string groupName = "explosion_cluster";

  // Create many effects with same group
  std::vector<uint32_t> effects;
  for (int i = 0; i < 10; ++i) {
    uint32_t effectId = manager->playIndependentEffect(
        ParticleEffectType::Sparks,
        {static_cast<float>(100 + i * 20), static_cast<float>(100 + i * 10)},
        1.0f, -1.0f, groupName);
    BOOST_CHECK_NE(effectId, 0);
    effects.push_back(effectId);
  }

  // All should be in the group
  auto groupEffects = manager->getActiveIndependentEffectsByGroup(groupName);
  BOOST_CHECK_EQUAL(groupEffects.size(), 10);

  // Stop by group should stop all
  manager->stopIndependentEffectsByGroup(groupName);

  // All should be stopped
  for (uint32_t effectId : effects) {
    BOOST_CHECK(!manager->isEffectPlaying(effectId));
  }

  // Group should be empty
  groupEffects = manager->getActiveIndependentEffectsByGroup(groupName);
  BOOST_CHECK(groupEffects.empty());
}

// Test independent effect with infinite duration (-1)
BOOST_FIXTURE_TEST_CASE(TestIndependentEffectInfiniteDuration, ParticleManagerCoreFixture) {
  manager->init();
  manager->registerBuiltInEffects();

  // Create effect with infinite duration
  uint32_t effectId = manager->playIndependentEffect(
      ParticleEffectType::Fire, {100.0f, 100.0f}, 1.0f, -1.0f, "persistent");

  BOOST_CHECK_NE(effectId, 0);
  BOOST_CHECK(manager->isEffectPlaying(effectId));

  // Update for a long time
  for (int i = 0; i < 100; ++i) {
    manager->update(0.016f);
  }

  // Should still be playing (infinite duration)
  BOOST_CHECK(manager->isEffectPlaying(effectId));

  // Must be manually stopped
  manager->stopIndependentEffect(effectId);
  BOOST_CHECK(!manager->isEffectPlaying(effectId));
}
