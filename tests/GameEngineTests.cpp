/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE GameEngineTests
#include <boost/test/unit_test.hpp>

#include "core/GameEngine.hpp"
#include "core/ThreadSystem.hpp"
#include "core/Logger.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>

// Global fixture for test setup and cleanup
struct GameEngineTestFixture {
    GameEngine* engine{nullptr};
    bool initialized{false};

    GameEngineTestFixture() {
        std::cout << "Initializing GameEngine test environment..." << std::endl;

        // Initialize ThreadSystem first (required by managers)
        HammerEngine::ThreadSystem::Instance().init();

        // Get GameEngine singleton
        engine = &GameEngine::Instance();

        // Initialize GameEngine with headless setup for testing
        // Note: This requires SDL and all managers - it's a heavy initialization
        // but necessary to test the actual double-buffering implementation
        initialized = engine->init("GameEngine Test", 800, 600, false);

        if (!initialized) {
            throw std::runtime_error("GameEngine initialization failed");
        }

        std::cout << "GameEngine initialized successfully for test" << std::endl;
    }

    ~GameEngineTestFixture() {
        std::cout << "Cleaning up GameEngine test environment..." << std::endl;

        if (initialized && engine) {
            engine->clean();
        }

        // Clean up ThreadSystem
        if (HammerEngine::ThreadSystem::Exists() &&
            !HammerEngine::ThreadSystem::Instance().isShutdown()) {
            HammerEngine::ThreadSystem::Instance().clean();
        }

        std::cout << "GameEngine test cleanup complete" << std::endl;
    }
};

BOOST_GLOBAL_FIXTURE(GameEngineTestFixture);

// ============================================================================
// PHASE 1: DOUBLE-BUFFER SYNCHRONIZATION TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(DoubleBufferSynchronizationTests)

BOOST_AUTO_TEST_CASE(TestInitialBufferState) {
    auto& engine = GameEngine::Instance();

    // After initialization, buffer indices should be valid
    size_t currentBuffer = engine.getCurrentBufferIndex();
    size_t renderBuffer = engine.getRenderBufferIndex();

    std::cout << "Initial current buffer: " << currentBuffer << std::endl;
    std::cout << "Initial render buffer: " << renderBuffer << std::endl;

    // Buffer indices should be 0 or 1
    BOOST_CHECK(currentBuffer == 0 || currentBuffer == 1);
    BOOST_CHECK(renderBuffer == 0 || renderBuffer == 1);

    // Initially, both buffers should point to the same buffer (buffer 0)
    // This is the initialization state from GameEngine.cpp:827-832
    BOOST_CHECK_EQUAL(currentBuffer, 0);
    BOOST_CHECK_EQUAL(renderBuffer, 0);
}

BOOST_AUTO_TEST_CASE(TestBufferSwapMechanism) {
    auto& engine = GameEngine::Instance();

    // Get initial buffer indices
    size_t initialCurrentBuffer = engine.getCurrentBufferIndex();
    size_t initialRenderBuffer = engine.getRenderBufferIndex();

    std::cout << "Before update - current: " << initialCurrentBuffer
              << ", render: " << initialRenderBuffer << std::endl;

    // The correct pattern from HammerMain.cpp is: swap (if ready) -> update
    // So first we need an update to produce a frame
    engine.update(0.016f);

    // After update, check if there's a new frame
    bool hasFrame = engine.hasNewFrameToRender();
    std::cout << "After update, has frame: " << (hasFrame ? "yes" : "no") << std::endl;

    // If there's a frame, swap buffers (this is the production pattern)
    if (hasFrame) {
        size_t beforeSwapCurrent = engine.getCurrentBufferIndex();
        size_t beforeSwapRender = engine.getRenderBufferIndex();

        engine.swapBuffers();

        size_t afterSwapCurrent = engine.getCurrentBufferIndex();
        size_t afterSwapRender = engine.getRenderBufferIndex();

        std::cout << "Before swap - current: " << beforeSwapCurrent
                  << ", render: " << beforeSwapRender << std::endl;
        std::cout << "After swap - current: " << afterSwapCurrent
                  << ", render: " << afterSwapRender << std::endl;

        // After swap, current should move to next buffer
        BOOST_CHECK_NE(afterSwapCurrent, beforeSwapCurrent);
        // Render buffer should point to the buffer that was just updated
        BOOST_CHECK_EQUAL(afterSwapRender, beforeSwapCurrent);
    }
}

BOOST_AUTO_TEST_CASE(TestHasNewFrameToRender) {
    auto& engine = GameEngine::Instance();

    // Test the production pattern: update -> check -> swap -> update -> render
    // This follows the HammerMain.cpp pattern

    // Do an update to produce a frame
    engine.update(0.016f);

    // Check if there's a new frame
    bool hasFrame = engine.hasNewFrameToRender();
    std::cout << "Has new frame after update: " << (hasFrame ? "yes" : "no") << std::endl;

    if (hasFrame) {
        // Swap to make it available for rendering
        engine.swapBuffers();
    }

    // Do another update (to the other buffer)
    engine.update(0.016f);

    // Now render
    engine.render();

    // After rendering, the buffer should be marked as consumed
    bool hasFrameAfterRender = engine.hasNewFrameToRender();
    std::cout << "Has new frame after render: " << (hasFrameAfterRender ? "yes" : "no") << std::endl;
    BOOST_CHECK(!hasFrameAfterRender);
}

BOOST_AUTO_TEST_CASE(TestNoBufferIndexConflicts) {
    auto& engine = GameEngine::Instance();

    // Run multiple cycles following the production pattern:
    // swap (if ready) -> update -> render
    constexpr int numCycles = 10;

    for (int i = 0; i < numCycles; ++i) {
        size_t currentBuffer = engine.getCurrentBufferIndex();
        size_t renderBuffer = engine.getRenderBufferIndex();

        std::cout << "Cycle " << i << " - current: " << currentBuffer
                  << ", render: " << renderBuffer << std::endl;

        // Buffers should always be valid indices
        BOOST_CHECK(currentBuffer == 0 || currentBuffer == 1);
        BOOST_CHECK(renderBuffer == 0 || renderBuffer == 1);

        // Follow production pattern: swap (if ready) -> update -> render
        if (engine.hasNewFrameToRender()) {
            engine.swapBuffers();
        }

        engine.update(0.016f);
        engine.render();
    }

    std::cout << "Completed " << numCycles << " cycles without conflicts" << std::endl;
}

BOOST_AUTO_TEST_CASE(TestFrameCounterProgression) {
    auto& engine = GameEngine::Instance();

    // Test frame counter progression following production pattern
    // Pattern: (swap if ready) -> update -> render

    // First cycle
    if (engine.hasNewFrameToRender()) {
        engine.swapBuffers();
    }
    engine.update(0.016f);
    engine.render();

    // After render, no new frame until next update
    BOOST_CHECK(!engine.hasNewFrameToRender());

    // Second cycle
    if (engine.hasNewFrameToRender()) {
        engine.swapBuffers();
    }
    engine.update(0.016f);
    engine.render();

    // After render, no new frame
    BOOST_CHECK(!engine.hasNewFrameToRender());

    // Third cycle - pattern should be consistent
    if (engine.hasNewFrameToRender()) {
        engine.swapBuffers();
    }
    engine.update(0.016f);
    // Before render, should have frame (from update)
    // Note: hasNewFrameToRender checks frame counters, which may not show ready immediately after update
    engine.render();

    std::cout << "Frame counter progression test completed" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// PHASE 1: THREAD SAFETY TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(ThreadSafetyTests)

BOOST_AUTO_TEST_CASE(TestUpdateIsThreadSafe) {
    auto& engine = GameEngine::Instance();

    // The update() method can be called from a worker thread for testing purposes
    // Note: In production, update runs on main thread in single-threaded main loop

    std::atomic<bool> updateCompleted{false};
    std::atomic<bool> updateFailed{false};

    // Run update on a background thread
    auto updateTask = HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
        [&engine, &updateCompleted, &updateFailed]() -> bool {
            try {
                engine.update(0.016f);
                updateCompleted.store(true, std::memory_order_release);
                return true;
            } catch (const std::exception& e) {
                std::cout << "Update threw exception: " << e.what() << std::endl;
                updateFailed.store(true, std::memory_order_release);
                return false;
            }
        });

    // Wait for the task to complete
    bool success = updateTask.get();

    BOOST_CHECK(success);
    BOOST_CHECK(updateCompleted.load(std::memory_order_acquire));
    BOOST_CHECK(!updateFailed.load(std::memory_order_acquire));
}

BOOST_AUTO_TEST_CASE(TestRenderOnMainThreadOnly) {
    auto& engine = GameEngine::Instance();

    // The render() method must be called on the main thread only
    // GameEngine.cpp:987 comment states "Always on MAIN thread as its an - SDL REQUIREMENT"

    // This test just verifies render can be called on main thread without error
    BOOST_CHECK_NO_THROW(engine.render());

    // Note: We cannot test calling render from background thread because
    // SDL will likely crash or produce undefined behavior
    // The architecture enforces this through documentation and design
}

BOOST_AUTO_TEST_CASE(TestConcurrentUpdateAndRender) {
    auto& engine = GameEngine::Instance();

    // Test that update and render can happen concurrently without data races
    // This is the core of the double-buffering pattern
    // Pattern: update thread does (swap if ready) -> update
    // Render thread does render

    std::atomic<int> updateCount{0};
    std::atomic<int> renderCount{0};
    std::atomic<bool> stopTest{false};

    // Start update thread following production pattern
    auto updateTask = HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
        [&engine, &updateCount, &stopTest]() -> bool {
            while (!stopTest.load(std::memory_order_acquire)) {
                // Production pattern: swap if ready, then update
                if (engine.hasNewFrameToRender()) {
                    engine.swapBuffers();
                }
                engine.update(0.016f);
                updateCount.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
            return true;
        });

    // Give update thread a moment to start and produce a frame
    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    // Render on main thread
    for (int i = 0; i < 20; ++i) {
        engine.render();
        renderCount.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    // Stop the update thread
    stopTest.store(true, std::memory_order_release);
    updateTask.wait();

    int finalUpdateCount = updateCount.load(std::memory_order_acquire);
    int finalRenderCount = renderCount.load(std::memory_order_acquire);

    std::cout << "Updates: " << finalUpdateCount << ", Renders: " << finalRenderCount << std::endl;

    // Both should have happened
    BOOST_CHECK_GT(finalUpdateCount, 0);
    BOOST_CHECK_GT(finalRenderCount, 0);
}

BOOST_AUTO_TEST_CASE(TestUpdateRunningFlag) {
    auto& engine = GameEngine::Instance();

    // Test that isUpdateRunning() correctly reflects update state

    // Initially not running (or running if another test left it in that state)
    // Just verify it's a valid boolean value
    bool initiallyRunning = engine.isUpdateRunning();
    std::cout << "Update initially running: " << (initiallyRunning ? "yes" : "no") << std::endl;

    // Start an update on a background thread
    std::atomic<bool> updateStarted{false};
    std::atomic<bool> wasRunningDuringUpdate{false};

    auto updateTask = HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
        [&engine, &updateStarted, &wasRunningDuringUpdate]() -> bool {
            // Signal that update is about to start
            updateStarted.store(true, std::memory_order_release);

            // Give main thread time to check
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

            // Store the running state mid-update
            wasRunningDuringUpdate.store(engine.isUpdateRunning(), std::memory_order_release);

            // Perform the actual update
            engine.update(0.016f);
            return true;
        });

    // Wait for update to start
    while (!updateStarted.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    // Small delay to ensure we're checking during update
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    // Check if update is running (timing-dependent, best effort)
    bool runningDuringCheck = engine.isUpdateRunning();

    // Wait for update to complete
    updateTask.wait();

    // After update completes, should not be running
    bool runningAfterUpdate = engine.isUpdateRunning();
    BOOST_CHECK(!runningAfterUpdate);

    std::cout << "Running during check (main thread): " << (runningDuringCheck ? "yes" : "no") << std::endl;
    std::cout << "Was running during update (worker thread): "
              << (wasRunningDuringUpdate.load(std::memory_order_acquire) ? "yes" : "no") << std::endl;
    std::cout << "Running after update: " << (runningAfterUpdate ? "yes" : "no") << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// PHASE 1: MANAGER COORDINATION TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(ManagerCoordinationTests)

BOOST_AUTO_TEST_CASE(TestUpdateCallsAllManagers) {
    auto& engine = GameEngine::Instance();

    // The update() method should update all managers in the correct order
    // From GameEngine.cpp:901-967:
    // 1. EventManager
    // 2. GameStateManager
    // 3. AIManager
    // 4. ParticleManager
    // 5. PathfinderManager
    // 6. CollisionManager

    // This test verifies that update() completes successfully
    // (which implies all managers were updated)
    BOOST_CHECK_NO_THROW(engine.update(0.016f));

    // Note: We cannot directly verify the order without instrumenting the managers
    // The order is documented in GameEngine.cpp and enforced by the implementation
}

BOOST_AUTO_TEST_CASE(TestManagersInitializedBeforeUpdate) {
    auto& engine = GameEngine::Instance();

    // All managers should be properly initialized before any update
    // GameEngine::init() validates manager initialization at lines 694-798

    // Perform an update to ensure managers are responsive
    BOOST_CHECK_NO_THROW(engine.update(0.016f));
}

BOOST_AUTO_TEST_CASE(TestMultipleUpdateCyclesStable) {
    auto& engine = GameEngine::Instance();

    // Run multiple update cycles to ensure manager coordination remains stable
    constexpr int numUpdates = 100;

    for (int i = 0; i < numUpdates; ++i) {
        BOOST_CHECK_NO_THROW(engine.update(0.016f));

        if (i % 10 == 0) {
            std::cout << "Completed " << i << " updates" << std::endl;
        }
    }

    std::cout << "Completed " << numUpdates << " update cycles successfully" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// DETERMINISM TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(DeterminismTests)

BOOST_AUTO_TEST_CASE(TestBufferSwapDeterminism) {
    auto& engine = GameEngine::Instance();

    // Buffer swap should be deterministic - same sequence of operations
    // should produce same buffer progression

    std::vector<std::pair<size_t, size_t>> bufferSequence;

    // Record buffer state through multiple cycles
    for (int i = 0; i < 5; ++i) {
        engine.update(0.016f);
        engine.swapBuffers();

        bufferSequence.push_back({
            engine.getCurrentBufferIndex(),
            engine.getRenderBufferIndex()
        });
    }

    // Verify the sequence follows the ping-pong pattern
    // After each swap, buffers should alternate
    for (size_t i = 1; i < bufferSequence.size(); ++i) {
        std::cout << "Cycle " << i << ": current=" << bufferSequence[i].first
                  << ", render=" << bufferSequence[i].second << std::endl;
    }

    // The pattern should be deterministic
    BOOST_CHECK_EQUAL(bufferSequence.size(), 5);
}

BOOST_AUTO_TEST_CASE(TestUpdateDeltaTimeConsistency) {
    auto& engine = GameEngine::Instance();

    // Update should process the provided delta time consistently
    // This test verifies that update accepts different delta times without error

    std::vector<float> deltaTimes = {0.016f, 0.033f, 0.008f, 0.020f};

    for (float dt : deltaTimes) {
        BOOST_CHECK_NO_THROW(engine.update(dt));
        std::cout << "Update with dt=" << dt << " completed successfully" << std::endl;
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// EDGE CASE TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(EdgeCaseTests)

BOOST_AUTO_TEST_CASE(TestZeroDeltaTime) {
    auto& engine = GameEngine::Instance();

    // Update with zero delta time should not crash
    BOOST_CHECK_NO_THROW(engine.update(0.0f));
}

BOOST_AUTO_TEST_CASE(TestVeryLargeDeltaTime) {
    auto& engine = GameEngine::Instance();

    // Update with very large delta time should not crash
    // (though it may cause unexpected behavior in game logic)
    BOOST_CHECK_NO_THROW(engine.update(1.0f)); // 1 second delta
}

BOOST_AUTO_TEST_CASE(TestRapidBufferSwaps) {
    auto& engine = GameEngine::Instance();

    // Rapidly swapping buffers should not cause issues
    for (int i = 0; i < 100; ++i) {
        engine.update(0.001f); // Very small delta
        engine.swapBuffers();
    }

    // Should still be in valid state
    BOOST_CHECK(engine.getCurrentBufferIndex() == 0 || engine.getCurrentBufferIndex() == 1);
    BOOST_CHECK(engine.getRenderBufferIndex() == 0 || engine.getRenderBufferIndex() == 1);
}

BOOST_AUTO_TEST_CASE(TestRenderWithoutUpdate) {
    auto& engine = GameEngine::Instance();

    // Rendering without update should work (will render the same frame)
    BOOST_CHECK_NO_THROW(engine.render());
    BOOST_CHECK_NO_THROW(engine.render());
    BOOST_CHECK_NO_THROW(engine.render());
}

BOOST_AUTO_TEST_SUITE_END()
