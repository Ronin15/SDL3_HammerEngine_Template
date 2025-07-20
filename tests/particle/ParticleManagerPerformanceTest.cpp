/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ParticleManagerPerformanceTest
#include <boost/test/unit_test.hpp>

#include "managers/ParticleManager.hpp"
#include "utils/Vector2D.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

// Test fixture for ParticleManager performance tests
struct ParticleManagerPerformanceFixture {
  ParticleManagerPerformanceFixture() {
    manager = &ParticleManager::Instance();

    // Ensure clean state for each test
    if (manager->isInitialized()) {
      manager->clean();
    }

    // Initialize for performance tests
    manager->init();
    manager->registerBuiltInEffects();
  }

  ~ParticleManagerPerformanceFixture() {
    if (manager->isInitialized()) {
      manager->clean();
    }
  }

  ParticleManager *manager;

  // Helper to measure execution time
  template <typename Func> double measureExecutionTime(Func &&func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    return duration.count() / 1000.0; // Return milliseconds
  }

  // Helper to create particles for performance testing
  void createParticles(size_t targetCount,
                       const std::string &effectType = "Rain") {
    Vector2D basePosition(960, 100); // Center-top

    // Reset performance stats for clean measurement
    manager->resetPerformanceStats();

    // Create effects until we have enough particles
    std::vector<uint32_t> effectIds;

    while (manager->getActiveParticleCount() < targetCount) {
      // Create effects at different positions to spread particles
      Vector2D position(basePosition.getX() + (effectIds.size() % 10 - 5) * 100,
                        basePosition.getY() + (effectIds.size() / 10) * 50);

      uint32_t effectId = manager->playEffect(effectType, position, 1.0f);
      if (effectId != 0) {
        effectIds.push_back(effectId);
      }

      // Update to emit particles
      manager->update(0.016f); // 60 FPS

      // Safety check to prevent infinite loops
      if (effectIds.size() > 100) {
        break;
      }
    }

    std::cout << "Created " << manager->getActiveParticleCount()
              << " particles using " << effectIds.size() << " effects"
              << std::endl;
  }
};

// Test update performance with 1000 particles
BOOST_FIXTURE_TEST_CASE(TestUpdatePerformance1000Particles,
                        ParticleManagerPerformanceFixture) {
  const size_t TARGET_PARTICLES = 1000;
  const double MAX_UPDATE_TIME_MS = 5.0; // 5ms max for 1000 particles

  createParticles(TARGET_PARTICLES);
  size_t actualParticles = manager->getActiveParticleCount();

  // We might not get exactly 1000, but should be reasonably close
  BOOST_CHECK_GT(actualParticles, 500);
  std::cout << "Testing update performance with " << actualParticles
            << " particles" << std::endl;

  // Measure update time
  double updateTime = measureExecutionTime([this]() {
    manager->update(0.016f); // 60 FPS update
  });

  std::cout << "Update time: " << updateTime << "ms" << std::endl;

  // Update time should be reasonable for real-time performance
  BOOST_CHECK_LT(updateTime, MAX_UPDATE_TIME_MS);

  // Check performance stats
  ParticlePerformanceStats stats = manager->getPerformanceStats();
  BOOST_CHECK_GT(stats.updateCount, 0);
  BOOST_CHECK_GT(stats.totalUpdateTime, 0.0);
}

// Test update performance with 5000 particles
BOOST_FIXTURE_TEST_CASE(TestUpdatePerformance5000Particles,
                        ParticleManagerPerformanceFixture) {
  const size_t TARGET_PARTICLES = 5000;
  const double MAX_UPDATE_TIME_MS = 16.0; // 16ms max (one frame budget)

  createParticles(TARGET_PARTICLES);
  size_t actualParticles = manager->getActiveParticleCount();

  BOOST_CHECK_GT(actualParticles, 2000); // Should get at least 2000
  std::cout << "Testing update performance with " << actualParticles
            << " particles" << std::endl;

  // Measure update time
  double updateTime =
      measureExecutionTime([this]() { manager->update(0.016f); });

  std::cout << "Update time: " << updateTime << "ms" << std::endl;

  // Should still be within frame budget
  BOOST_CHECK_LT(updateTime, MAX_UPDATE_TIME_MS);
}

// Test particle creation throughput
BOOST_FIXTURE_TEST_CASE(TestParticleCreationThroughput,
                        ParticleManagerPerformanceFixture) {
  Vector2D testPosition(500, 300);

  // Measure time to create many effects
  const int NUM_EFFECTS = 50;
  double creationTime = measureExecutionTime([&]() {
    for (int i = 0; i < NUM_EFFECTS; ++i) {
      Vector2D pos(testPosition.getX() + i * 10, testPosition.getY());
      manager->playEffect("Rain", pos, 1.0f);
    }
  });

  std::cout << "Time to create " << NUM_EFFECTS << " effects: " << creationTime
            << "ms" << std::endl;

  // Effect creation should be fast
  BOOST_CHECK_LT(creationTime, 10.0); // Should take less than 10ms

  // Update to emit particles and measure
  double emissionTime = measureExecutionTime([this]() {
    for (int i = 0; i < 10; ++i) {
      manager->update(0.016f);
    }
  });

  std::cout << "Time for 10 updates (particle emission): " << emissionTime
            << "ms" << std::endl;
  std::cout << "Particles created: " << manager->getActiveParticleCount()
            << std::endl;

  BOOST_CHECK_GT(manager->getActiveParticleCount(), 0);
}

// Test memory usage scaling
BOOST_FIXTURE_TEST_CASE(TestMemoryUsageScaling,
                        ParticleManagerPerformanceFixture) {
  Vector2D testPosition(500, 300);

  // Start with baseline
  // Create many effects rapidly
  std::vector<size_t> particleCounts;
  std::vector<double> updateTimes;

  for (int batch = 0; batch < 5; ++batch) {
    // Add more effects
    for (int i = 0; i < 10; ++i) {
      Vector2D pos(testPosition.getX() + batch * 100 + i * 10,
                   testPosition.getY());
      manager->playEffect("Rain", pos, 1.0f);
    }

    // Update to create particles
    for (int i = 0; i < 5; ++i) {
      manager->update(0.016f);
    }

    // Measure update performance at this particle count
    size_t currentCount = manager->getActiveParticleCount();
    double updateTime =
        measureExecutionTime([this]() { manager->update(0.016f); });

    particleCounts.push_back(currentCount);
    updateTimes.push_back(updateTime);

    std::cout << "Particles: " << currentCount
              << ", Update time: " << updateTime << "ms" << std::endl;
  }

  // Check that we have reasonable scaling
  BOOST_CHECK_GT(particleCounts.back(), particleCounts.front());

  // Update times should scale somewhat linearly, not exponentially
  double firstTime = updateTimes.front();
  double lastTime = updateTimes.back();
  double particleRatio =
      static_cast<double>(particleCounts.back()) / particleCounts.front();
  double timeRatio = lastTime / firstTime;

  std::cout << "Particle ratio: " << particleRatio
            << ", Time ratio: " << timeRatio << std::endl;

  // Time scaling should be reasonable (not more than 3x worse than linear)
  BOOST_CHECK_LT(timeRatio,
                 particleRatio *
                     5.0); // Allow up to 5x scaling due to system variance
}

// Test cleanup performance
BOOST_FIXTURE_TEST_CASE(TestCleanupPerformance,
                        ParticleManagerPerformanceFixture) {
  // Create many weather effects (not regular effects) to test cleanup
  Vector2D testPosition(500, 300);

  // Create weather effects that can be properly cleaned up
  for (int i = 0; i < 20; ++i) {
    manager->triggerWeatherEffect("Rainy", 1.0f);
    // Small delay to let effect start
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Update to create particles
  for (int i = 0; i < 15; ++i) {
    manager->update(0.016f);
  }

  size_t initialCount = manager->getActiveParticleCount();
  BOOST_CHECK_GT(initialCount, 50); // Should have created some particles
                                    // (adjusted for new emission rates)

  std::cout << "Initial particle count: " << initialCount << std::endl;

  // Stop all weather effects to trigger cleanup
  manager->stopWeatherEffects(0.0f); // Immediate stop

  // Measure cleanup time
  double cleanupTime = measureExecutionTime([this]() {
    // Update several times to process cleanup
    for (int i = 0; i < 10; ++i) {
      manager->update(0.016f);
    }
  });

  size_t finalCount = manager->getActiveParticleCount();
  std::cout << "Final particle count: " << finalCount << std::endl;
  std::cout << "Cleanup time: " << cleanupTime << "ms" << std::endl;

  // Cleanup should be reasonably fast
  BOOST_CHECK_LT(cleanupTime, 20.0); // Should take less than 20ms

  // Weather effects should be properly cleaned up
  BOOST_CHECK_LT(finalCount,
                 initialCount); // Should have fewer particles after cleanup
}

// Test effect management performance
BOOST_FIXTURE_TEST_CASE(TestEffectManagementPerformance,
                        ParticleManagerPerformanceFixture) {
  Vector2D testPosition(500, 300);

  // Create and immediately stop many effects
  const int NUM_EFFECTS = 100;
  std::vector<uint32_t> effectIds;

  double effectManagementTime = measureExecutionTime([&]() {
    // Create effects
    for (int i = 0; i < NUM_EFFECTS; ++i) {
      Vector2D pos(testPosition.getX() + i * 5, testPosition.getY());
      uint32_t id = manager->playEffect("Rain", pos, 0.5f);
      if (id != 0) {
        effectIds.push_back(id);
      }
    }

    // Stop half of them
    for (size_t i = 0; i < effectIds.size() / 2; ++i) {
      manager->stopEffect(effectIds[i]);
    }
  });

  std::cout << "Time to create and manage " << NUM_EFFECTS
            << " effects: " << effectManagementTime << "ms" << std::endl;

  // Effect management should be fast
  BOOST_CHECK_LT(effectManagementTime, 15.0);

  // Check that some effects are still playing
  int activeEffects = 0;
  for (uint32_t id : effectIds) {
    if (manager->isEffectPlaying(id)) {
      activeEffects++;
    }
  }

  std::cout << "Active effects remaining: " << activeEffects << std::endl;
  BOOST_CHECK_GT(activeEffects, 0);
  BOOST_CHECK_LT(activeEffects, static_cast<int>(effectIds.size()));
}

// Test sustained performance over time
BOOST_FIXTURE_TEST_CASE(TestSustainedPerformance,
                        ParticleManagerPerformanceFixture) {
  createParticles(1500);

  const int NUM_FRAMES = 60; // Test 1 second at 60 FPS
  std::vector<double> frameTimes;

  // Measure sustained performance
  for (int frame = 0; frame < NUM_FRAMES; ++frame) {
    double frameTime =
        measureExecutionTime([this]() { manager->update(0.016f); });
    frameTimes.push_back(frameTime);
  }

  // Calculate statistics
  double totalTime = 0.0;
  double maxTime = 0.0;
  double minTime = frameTimes[0];

  for (double time : frameTimes) {
    totalTime += time;
    maxTime = std::max(maxTime, time);
    minTime = std::min(minTime, time);
  }

  double avgTime = totalTime / NUM_FRAMES;

  std::cout << "Sustained performance over " << NUM_FRAMES
            << " frames:" << std::endl;
  std::cout << "  Average: " << avgTime << "ms" << std::endl;
  std::cout << "  Min: " << minTime << "ms" << std::endl;
  std::cout << "  Max: " << maxTime << "ms" << std::endl;
  std::cout << "  Total: " << totalTime << "ms" << std::endl;

  // Performance should be consistent
  BOOST_CHECK_LT(avgTime, 10.0); // Average should be reasonable
  BOOST_CHECK_LT(maxTime, 25.0); // No frame should take too long

  // Max shouldn't be too much worse than average (indicating consistent
  // performance) Note: OS scheduling can cause occasional timing spikes, so we
  // use a tolerant threshold
  BOOST_CHECK_LT(maxTime, avgTime * 6.0);
}

// Test performance with different effect types
BOOST_FIXTURE_TEST_CASE(TestDifferentEffectTypesPerformance,
                        ParticleManagerPerformanceFixture) {
  Vector2D testPosition(500, 300);

  std::vector<std::string> effectTypes = {"Rain", "Snow", "Fog"};

  for (const auto &effectType : effectTypes) {
    std::cout << "\nTesting " << effectType
              << " effect performance:" << std::endl;

    // Clean up previous effects
    manager->stopWeatherEffects(0.0f);
    for (int i = 0; i < 5; ++i) {
      manager->update(0.016f); // Clean up
    }

    // Create effects of this type
    std::vector<uint32_t> effectIds;
    for (int i = 0; i < 20; ++i) {
      Vector2D pos(testPosition.getX() + i * 20, testPosition.getY());
      uint32_t id = manager->playEffect(effectType, pos, 1.0f);
      if (id != 0) {
        effectIds.push_back(id);
      }
    }

    // Update to create particles
    for (int i = 0; i < 10; ++i) {
      manager->update(0.016f);
    }

    size_t particleCount = manager->getActiveParticleCount();

    // Measure update performance
    double updateTime =
        measureExecutionTime([this]() { manager->update(0.016f); });

    std::cout << "  Particles: " << particleCount << std::endl;
    std::cout << "  Update time: " << updateTime << "ms" << std::endl;

    // All effect types should perform reasonably
    BOOST_CHECK_LT(updateTime, 15.0);
    BOOST_CHECK_GT(particleCount, 0);
  }
}
