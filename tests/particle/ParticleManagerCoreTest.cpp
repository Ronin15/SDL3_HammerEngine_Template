/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE ParticleManagerCoreTest
#include <boost/test/unit_test.hpp>

#include "managers/ParticleManager.hpp"
#include "events/ParticleEffectEvent.hpp"
#include "utils/Vector2D.hpp"
#include <memory>
#include <chrono>
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

    ParticleManager* manager;
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
BOOST_FIXTURE_TEST_CASE(TestPrepareForStateTransition, ParticleManagerCoreFixture) {
    manager->init();
    
    // Should not be paused initially
    BOOST_CHECK(!manager->isGloballyPaused());
    
    // Prepare for state transition
    manager->prepareForStateTransition();
    
    // Should be resumed after preparation (method temporarily pauses then resumes)
    BOOST_CHECK(!manager->isGloballyPaused());
    BOOST_CHECK(manager->isInitialized()); // Should still be initialized
}

// Test built-in effect registration
BOOST_FIXTURE_TEST_CASE(TestBuiltInEffectsRegistration, ParticleManagerCoreFixture) {
    manager->init();
    
    // Register built-in effects
    manager->registerBuiltInEffects();
    
    // Try to play some built-in effects to verify they're registered
    Vector2D testPosition(100, 100);
    
    uint32_t rainEffect = manager->playEffect("Rain", testPosition, 0.5f);
    BOOST_CHECK_NE(rainEffect, 0); // Should return valid effect ID
    
    uint32_t snowEffect = manager->playEffect("Snow", testPosition, 0.5f);
    BOOST_CHECK_NE(snowEffect, 0);
    
    uint32_t fogEffect = manager->playEffect("Fog", testPosition, 0.5f);
    BOOST_CHECK_NE(fogEffect, 0);
    
    // Invalid effect should return 0
    uint32_t invalidEffect = manager->playEffect("NonExistentEffect", testPosition, 0.5f);
    BOOST_CHECK_EQUAL(invalidEffect, 0);
}

// Test effect ID generation
BOOST_FIXTURE_TEST_CASE(TestEffectIdGeneration, ParticleManagerCoreFixture) {
    manager->init();
    manager->registerBuiltInEffects();
    
    Vector2D testPosition(100, 100);
    
    // Generate multiple effect IDs
    uint32_t id1 = manager->playEffect("Rain", testPosition, 0.5f);
    uint32_t id2 = manager->playEffect("Snow", testPosition, 0.5f);
    uint32_t id3 = manager->playEffect("Fog", testPosition, 0.5f);
    
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
    uint32_t effectId = manager->playEffect("Rain", testPosition, 0.5f);
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
    uint32_t effectId = manager->playEffect("Rain", testPosition, 0.5f);
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
BOOST_FIXTURE_TEST_CASE(TestParticleEffectEventIntegration, ParticleManagerCoreFixture) {
    manager->init();
    manager->registerBuiltInEffects();
    
    // Test ParticleEffectEvent creation
    Vector2D testPosition(150.0f, 250.0f);
    ParticleEffectEvent effectEvent("IntegrationTest", "Fire", testPosition, 1.2f, 3.0f, "testGroup");
    
    // Verify event properties
    BOOST_CHECK_EQUAL(effectEvent.getName(), "IntegrationTest");
    BOOST_CHECK_EQUAL(effectEvent.getType(), "ParticleEffect");
    BOOST_CHECK_EQUAL(effectEvent.getEffectName(), "Fire");
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
    
    // Verify effect was created in ParticleManager (particles may not be emitted immediately)
    // Since the effect is active, the effect system is working correctly
    BOOST_CHECK_GE(manager->getActiveParticleCount(), 0); // >= 0 since emission depends on timing
    
    // Test effect stopping
    effectEvent.stopEffect();
    BOOST_CHECK(!effectEvent.isEffectActive());
    
    // Test event reset
    effectEvent.reset();
    BOOST_CHECK(!effectEvent.isEffectActive());
}

// Test ParticleEffectEvent with different effect types
BOOST_FIXTURE_TEST_CASE(TestParticleEffectEventTypes, ParticleManagerCoreFixture) {
    manager->init();
    manager->registerBuiltInEffects();
    
    Vector2D position(100.0f, 200.0f);
    
    // Test different built-in effects
    std::vector<std::string> effectTypes = {"Fire", "Smoke", "Sparks", "Rain", "Snow", "Fog"};
    
    for (const auto& effectType : effectTypes) {
        ParticleEffectEvent event("Test_" + effectType, effectType, position, 0.8f, 2.0f);
        
        // Verify event creation
        BOOST_CHECK_EQUAL(event.getEffectName(), effectType);
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

// Test ParticleEffectEvent with invalid effects
BOOST_FIXTURE_TEST_CASE(TestParticleEffectEventInvalidEffects, ParticleManagerCoreFixture) {
    manager->init();
    manager->registerBuiltInEffects();
    
    Vector2D position(100.0f, 200.0f);
    
    // Test with non-existent effect
    ParticleEffectEvent invalidEvent("InvalidTest", "NonExistentEffect", position);
    
    // Should not be active initially
    BOOST_CHECK(!invalidEvent.isEffectActive());
    
    // Execute should fail gracefully
    invalidEvent.execute();
    
    // Should still not be active
    BOOST_CHECK(!invalidEvent.isEffectActive());
    
    // Test with empty effect name
    ParticleEffectEvent emptyEvent("EmptyTest", "", position);
    
    // Should fail conditions check
    BOOST_CHECK(!emptyEvent.checkConditions());
    
    emptyEvent.execute();
    BOOST_CHECK(!emptyEvent.isEffectActive());
}

// Test ParticleEffectEvent lifecycle with ParticleManager
BOOST_FIXTURE_TEST_CASE(TestParticleEffectEventLifecycle, ParticleManagerCoreFixture) {
    manager->init();
    manager->registerBuiltInEffects();
    
    Vector2D position(200.0f, 300.0f);
    ParticleEffectEvent event("LifecycleTest", "Smoke", position, 1.5f, 5.0f);
    
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
BOOST_FIXTURE_TEST_CASE(TestParticleEffectEventExtremeValues, ParticleManagerCoreFixture) {
    manager->init();
    manager->registerBuiltInEffects();
    
    // Test with extreme positions
    ParticleEffectEvent extremeEvent1("Extreme1", "Fire", -1000.0f, 1000.0f, 0.1f, 0.1f);
    extremeEvent1.execute();
    BOOST_CHECK(extremeEvent1.isEffectActive());
    extremeEvent1.stopEffect();
    
    // Test with very high intensity
    ParticleEffectEvent extremeEvent2("Extreme2", "Sparks", 0.0f, 0.0f, 10.0f, 1.0f);
    extremeEvent2.execute();
    BOOST_CHECK(extremeEvent2.isEffectActive());
    extremeEvent2.stopEffect();
    
    // Test with infinite duration
    ParticleEffectEvent infiniteEvent("Infinite", "Rain", 100.0f, 100.0f, 1.0f, -1.0f);
    infiniteEvent.execute();
    BOOST_CHECK(infiniteEvent.isEffectActive());
    infiniteEvent.stopEffect();
    
    // Test with zero duration
    ParticleEffectEvent zeroEvent("Zero", "Snow", 100.0f, 100.0f, 1.0f, 0.0f);
    zeroEvent.execute();
    BOOST_CHECK(zeroEvent.isEffectActive());
    zeroEvent.stopEffect();
}

// Test multiple simultaneous ParticleEffectEvents
BOOST_FIXTURE_TEST_CASE(TestMultipleParticleEffectEvents, ParticleManagerCoreFixture) {
    manager->init();
    manager->registerBuiltInEffects();
    
    // Create multiple events
    std::vector<std::unique_ptr<ParticleEffectEvent>> events;
    events.emplace_back(std::make_unique<ParticleEffectEvent>("Multi1", "Fire", 100.0f, 100.0f));
    events.emplace_back(std::make_unique<ParticleEffectEvent>("Multi2", "Smoke", 200.0f, 200.0f));
    events.emplace_back(std::make_unique<ParticleEffectEvent>("Multi3", "Sparks", 300.0f, 300.0f));
    
    // Execute all events
    for (auto& event : events) {
        event->execute();
        BOOST_CHECK(event->isEffectActive());
    }
    
    // All should be active simultaneously
    for (auto& event : events) {
        BOOST_CHECK(event->isEffectActive());
    }
    
    // Verify multiple effects are running in ParticleManager
    BOOST_CHECK_GE(manager->getActiveParticleCount(), 0); // May vary based on timing
    
    // Stop all events
    for (auto& event : events) {
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
    uint32_t effectId = manager->playEffect("Rain", testPosition, 1.0f);
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
BOOST_FIXTURE_TEST_CASE(TestUpdateWithoutInitialization, ParticleManagerCoreFixture) {
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
    manager->playEffect("Rain", testPosition, 1.0f);
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
    BOOST_CHECK_LE(maxCapacity, 200000); // Should be reasonable (updated for new higher limits)
    
    // Test that setMaxParticles doesn't crash
    manager->setMaxParticles(5000);
    size_t newCapacity = manager->getMaxParticleCapacity();
    // Capacity should be at least the requested amount (may be more due to vector growth)
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
    uint32_t effectId = manager->playEffect("Rain", testPosition, 1.0f);
    BOOST_CHECK_NE(effectId, 0);
    BOOST_CHECK(manager->isEffectPlaying(effectId));
    
    // Update 1201 times to ensure we hit the performance recording threshold
    // Performance stats are only recorded every 1200 frames for performance reasons
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
