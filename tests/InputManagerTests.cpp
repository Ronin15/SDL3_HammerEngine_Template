/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE InputManagerTests
#include <boost/test/unit_test.hpp>

#include <SDL3/SDL.h>
#include "managers/InputManager.hpp"
#include "utils/Vector2D.hpp"

// Global fixture for SDL and InputManager initialization
struct InputManagerTestFixture {
    InputManagerTestFixture() {
        // Initialize SDL Video subsystem (needed for event processing)
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error("Failed to initialize SDL: " + std::string(SDL_GetError()));
        }

        // Initialize InputManager (singleton)
        // Note: InputManager will be in a clean state for first test
    }

    ~InputManagerTestFixture() {
        // Clean up InputManager
        if (!InputManager::Instance().isShutdown()) {
            InputManager::Instance().clean();
        }

        // Quit SDL
        SDL_Quit();
    }

    // Helper to inject a keyboard event into SDL's event queue
    void injectKeyEvent(SDL_Scancode scancode, bool isDown) {
        SDL_Event event;
        SDL_zero(event);
        event.type = isDown ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.scancode = scancode;
        event.key.mod = SDL_KMOD_NONE;
        event.key.repeat = false;

        SDL_PushEvent(&event);
    }

    // Helper to inject a mouse button event
    void injectMouseButtonEvent(int button, bool isDown, float x, float y) {
        SDL_Event event;
        SDL_zero(event);
        event.type = isDown ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = button;
        event.button.x = x;
        event.button.y = y;
        event.button.clicks = 1;

        SDL_PushEvent(&event);
    }

    // Helper to inject a mouse motion event
    void injectMouseMotionEvent(float x, float y) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.x = x;
        event.motion.y = y;
        event.motion.xrel = 0.0f;
        event.motion.yrel = 0.0f;

        SDL_PushEvent(&event);
    }

    // Helper to clear all pending events
    void clearEventQueue() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Discard all events
        }
    }
};

BOOST_GLOBAL_FIXTURE(InputManagerTestFixture);

// Static helper functions for use in tests
namespace TestHelpers {
    void injectKeyEvent(SDL_Scancode scancode, bool isDown) {
        SDL_Event event;
        SDL_zero(event);
        event.type = isDown ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.scancode = scancode;
        event.key.mod = SDL_KMOD_NONE;
        event.key.repeat = false;
        SDL_PushEvent(&event);
    }

    void injectMouseButtonEvent(int button, bool isDown, float x, float y) {
        SDL_Event event;
        SDL_zero(event);
        event.type = isDown ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = button;
        event.button.x = x;
        event.button.y = y;
        event.button.clicks = 1;
        SDL_PushEvent(&event);
    }

    void injectMouseMotionEvent(float x, float y) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.x = x;
        event.motion.y = y;
        event.motion.xrel = 0.0f;
        event.motion.yrel = 0.0f;
        SDL_PushEvent(&event);
    }

    void clearEventQueue() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Discard all events
        }
    }
}

// ============================================================================
// KEYBOARD STATE TRACKING TESTS
// Note: isKeyDown() relies on SDL_GetKeyboardState() which only tracks real
// hardware input and cannot be faked with injected events. We test
// wasKeyPressed() which uses InputManager's own m_pressedThisFrame tracking.
// ============================================================================

BOOST_AUTO_TEST_SUITE(KeyboardStateTests)

BOOST_AUTO_TEST_CASE(TestKeyPressedDetection) {
    TestHelpers::clearEventQueue();

    // Initially, no keys should be pressed this frame
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_A));

    // Inject key down event
    TestHelpers::injectKeyEvent(SDL_SCANCODE_A, true);
    InputManager::Instance().update();

    // Key should be detected as pressed this frame
    BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_A));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestKeyPressedOnlyOncePerPress) {
    TestHelpers::clearEventQueue();

    // Inject key down event
    TestHelpers::injectKeyEvent(SDL_SCANCODE_B, true);
    InputManager::Instance().update();

    // Key should be detected as pressed this frame
    BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_B));

    // On next frame, wasKeyPressed should return false (only true on press frame)
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_B));

    // Still false on subsequent frames
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_B));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestKeyPressAfterRelease) {
    TestHelpers::clearEventQueue();

    // Press key
    TestHelpers::injectKeyEvent(SDL_SCANCODE_C, true);
    InputManager::Instance().update();
    BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_C));

    // Release key and update
    TestHelpers::injectKeyEvent(SDL_SCANCODE_C, false);
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_C));

    // Press again - should be detected as new press
    TestHelpers::injectKeyEvent(SDL_SCANCODE_C, true);
    InputManager::Instance().update();
    BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_C));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestMultipleKeysSimultaneous) {
    TestHelpers::clearEventQueue();

    // Press multiple keys in same frame
    TestHelpers::injectKeyEvent(SDL_SCANCODE_W, true);
    TestHelpers::injectKeyEvent(SDL_SCANCODE_A, true);
    TestHelpers::injectKeyEvent(SDL_SCANCODE_S, true);
    InputManager::Instance().update();

    // All keys should be detected as pressed this frame
    BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_W));
    BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_A));
    BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_S));

    // Next frame, none should be "pressed this frame"
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_W));
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_A));
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_S));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestPressedClearedAcrossFrames) {
    TestHelpers::clearEventQueue();

    // Press key
    TestHelpers::injectKeyEvent(SDL_SCANCODE_SPACE, true);
    InputManager::Instance().update();

    // Key pressed this frame
    BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_SPACE));

    // Frame 2: Not "pressed this frame" anymore
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_SPACE));

    // Frame 3: Still not pressed this frame
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_SPACE));

    // Release
    TestHelpers::injectKeyEvent(SDL_SCANCODE_SPACE, false);
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_SPACE));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// MOUSE STATE TRACKING TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(MouseStateTests)

BOOST_AUTO_TEST_CASE(TestMouseButtonDown) {
    TestHelpers::clearEventQueue();

    // Initially, mouse button should not be down
    BOOST_CHECK(!InputManager::Instance().getMouseButtonState(LEFT));

    // Inject left mouse button down
    TestHelpers::injectMouseButtonEvent(SDL_BUTTON_LEFT, true, 100.0f, 200.0f);
    InputManager::Instance().update();

    // Button should be detected as down
    BOOST_CHECK(InputManager::Instance().getMouseButtonState(LEFT));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestMouseButtonRelease) {
    TestHelpers::clearEventQueue();

    // Press button
    TestHelpers::injectMouseButtonEvent(SDL_BUTTON_LEFT, true, 100.0f, 200.0f);
    InputManager::Instance().update();
    BOOST_CHECK(InputManager::Instance().getMouseButtonState(LEFT));

    // Release button
    TestHelpers::injectMouseButtonEvent(SDL_BUTTON_LEFT, false, 100.0f, 200.0f);
    InputManager::Instance().update();

    // Button should no longer be down
    BOOST_CHECK(!InputManager::Instance().getMouseButtonState(LEFT));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestMultipleMouseButtons) {
    TestHelpers::clearEventQueue();

    // Press left and right buttons
    TestHelpers::injectMouseButtonEvent(SDL_BUTTON_LEFT, true, 100.0f, 200.0f);
    TestHelpers::injectMouseButtonEvent(SDL_BUTTON_RIGHT, true, 100.0f, 200.0f);
    InputManager::Instance().update();

    // Both should be detected
    BOOST_CHECK(InputManager::Instance().getMouseButtonState(LEFT));
    BOOST_CHECK(InputManager::Instance().getMouseButtonState(RIGHT));

    // Middle should not be down
    BOOST_CHECK(!InputManager::Instance().getMouseButtonState(MIDDLE));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestMousePositionTracking) {
    TestHelpers::clearEventQueue();

    // Inject mouse motion event
    TestHelpers::injectMouseMotionEvent(150.0f, 250.0f);
    InputManager::Instance().update();

    // Check position
    const Vector2D& pos = InputManager::Instance().getMousePosition();
    BOOST_CHECK_EQUAL(pos.getX(), 150.0f);
    BOOST_CHECK_EQUAL(pos.getY(), 250.0f);

    // Move mouse again
    TestHelpers::injectMouseMotionEvent(300.0f, 400.0f);
    InputManager::Instance().update();

    const Vector2D& pos2 = InputManager::Instance().getMousePosition();
    BOOST_CHECK_EQUAL(pos2.getX(), 300.0f);
    BOOST_CHECK_EQUAL(pos2.getY(), 400.0f);

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestMouseButtonWithPosition) {
    TestHelpers::clearEventQueue();

    // Press button at specific position
    TestHelpers::injectMouseButtonEvent(SDL_BUTTON_LEFT, true, 123.0f, 456.0f);
    InputManager::Instance().update();

    // Verify button state
    BOOST_CHECK(InputManager::Instance().getMouseButtonState(LEFT));

    // Note: Mouse position from button event may not update mouse position
    // depending on implementation. This tests that button events are processed.

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// STATE TRANSITION TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(StateTransitionTests)

BOOST_AUTO_TEST_CASE(TestPressedHeldReleasedCycle) {
    TestHelpers::clearEventQueue();

    // Frame 1: Press
    TestHelpers::injectKeyEvent(SDL_SCANCODE_E, true);
    InputManager::Instance().update();
    BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_E));

    // Frame 2: Held (not pressed this frame)
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_E));

    // Frame 3: Still held (still not pressed)
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_E));

    // Frame 4: Released (not pressed on release frame)
    TestHelpers::injectKeyEvent(SDL_SCANCODE_E, false);
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_E));

    // Frame 5: Still released
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_E));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestRapidPressRelease) {
    TestHelpers::clearEventQueue();

    // Press and release in same frame
    TestHelpers::injectKeyEvent(SDL_SCANCODE_F, true);
    TestHelpers::injectKeyEvent(SDL_SCANCODE_F, false);
    InputManager::Instance().update();

    // wasKeyPressed should still be true (detected the press)
    BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_F));

    // Next frame, should not be pressed
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_F));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestMultipleUpdatesEmptyQueue) {
    TestHelpers::clearEventQueue();

    // Press key
    TestHelpers::injectKeyEvent(SDL_SCANCODE_G, true);
    InputManager::Instance().update();
    BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_G));

    // Multiple updates with no events should keep wasKeyPressed false
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_G));

    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_G));

    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_G));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestResetMouseButtons) {
    TestHelpers::clearEventQueue();

    // Press mouse buttons
    TestHelpers::injectMouseButtonEvent(SDL_BUTTON_LEFT, true, 100.0f, 100.0f);
    TestHelpers::injectMouseButtonEvent(SDL_BUTTON_RIGHT, true, 100.0f, 100.0f);
    InputManager::Instance().update();

    BOOST_CHECK(InputManager::Instance().getMouseButtonState(LEFT));
    BOOST_CHECK(InputManager::Instance().getMouseButtonState(RIGHT));

    // Reset should clear mouse button states
    InputManager::Instance().reset();

    BOOST_CHECK(!InputManager::Instance().getMouseButtonState(LEFT));
    BOOST_CHECK(!InputManager::Instance().getMouseButtonState(RIGHT));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// EDGE CASE TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(EdgeCaseTests)

BOOST_AUTO_TEST_CASE(TestSameKeyPressedMultipleTimes) {
    TestHelpers::clearEventQueue();

    // Press same key multiple times in one frame
    TestHelpers::injectKeyEvent(SDL_SCANCODE_H, true);
    TestHelpers::injectKeyEvent(SDL_SCANCODE_H, true);
    TestHelpers::injectKeyEvent(SDL_SCANCODE_H, true);
    InputManager::Instance().update();

    // Should be detected as pressed this frame (deduplicated)
    BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_H));

    // Next frame should not be pressed
    InputManager::Instance().update();
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_H));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestAlternatingKeyStates) {
    TestHelpers::clearEventQueue();

    // Alternate press/release over multiple frames
    for (int i = 0; i < 5; ++i) {
        // Press
        TestHelpers::injectKeyEvent(SDL_SCANCODE_I, true);
        InputManager::Instance().update();
        BOOST_CHECK(InputManager::Instance().wasKeyPressed(SDL_SCANCODE_I));

        // Release
        TestHelpers::injectKeyEvent(SDL_SCANCODE_I, false);
        InputManager::Instance().update();
        BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_I));
    }

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestNoEventsProcessing) {
    TestHelpers::clearEventQueue();

    // Call update with no events
    InputManager::Instance().update();

    // Should not crash, no keys should be pressed this frame
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_A));
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_B));
    BOOST_CHECK(!InputManager::Instance().wasKeyPressed(SDL_SCANCODE_C));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_CASE(TestMousePositionWithoutMotionEvent) {
    TestHelpers::clearEventQueue();

    // Get position without any motion events
    const Vector2D& pos = InputManager::Instance().getMousePosition();

    // Should return some position (default or last known)
    // Just verify it doesn't crash and returns finite values
    BOOST_CHECK(std::isfinite(pos.getX()));
    BOOST_CHECK(std::isfinite(pos.getY()));

    TestHelpers::clearEventQueue();
}

BOOST_AUTO_TEST_SUITE_END()
