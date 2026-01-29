/* Copyright (c) 2025 Hammer Forged Games
 * Licensed under the MIT License */

#ifndef GPU_TEST_FIXTURE_HPP
#define GPU_TEST_FIXTURE_HPP

#include <SDL3/SDL.h>
#include <boost/test/unit_test.hpp>

namespace HammerEngine {
namespace Test {

/**
 * Skip macro for tests requiring GPU availability.
 * Gracefully skips tests in headless CI environments.
 */
#define SKIP_IF_NO_GPU() \
    do { \
        if (!GPUTestFixture::isGPUAvailable()) { \
            BOOST_TEST_MESSAGE("Skipping test: No GPU available"); \
            return; \
        } \
    } while (0)

/**
 * Common test fixture for GPU tests.
 * Handles SDL initialization and GPU device availability detection.
 */
class GPUTestFixture {
public:
    GPUTestFixture() {
        // Initialize SDL video subsystem for GPU tests
        if (!s_sdlInitialized) {
            if (!SDL_Init(SDL_INIT_VIDEO)) {
                BOOST_TEST_MESSAGE("SDL video initialization failed: " << SDL_GetError());
                s_gpuAvailable = false;
            } else {
                s_sdlInitialized = true;
                // Check if GPU is available by trying to create a hidden window
                checkGPUAvailability();
            }
        }
    }

    virtual ~GPUTestFixture() {
        // SDL cleanup handled by global fixture
    }

    /**
     * Check if GPU is available for testing.
     * Returns false in headless environments or when GPU init fails.
     */
    static bool isGPUAvailable() {
        return s_gpuAvailable;
    }

    /**
     * Get the test window (creates if needed).
     * Window is hidden and minimal size for testing.
     */
    static SDL_Window* getTestWindow() {
        if (!s_testWindow && s_sdlInitialized) {
            s_testWindow = SDL_CreateWindow(
                "GPU Test Window",
                64, 64,
                SDL_WINDOW_HIDDEN
            );
            if (!s_testWindow) {
                BOOST_TEST_MESSAGE("Failed to create test window: " << SDL_GetError());
            }
        }
        return s_testWindow;
    }

    /**
     * Show the test window for frame cycle tests that need a visible swapchain.
     * Call this before tests that require beginFrame() to fully execute.
     */
    static void showTestWindow() {
        if (s_testWindow) {
            SDL_ShowWindow(s_testWindow);
            // Process events to ensure window is visible
            SDL_Event event;
            while (SDL_PollEvent(&event)) {}
        }
    }

    /**
     * Hide the test window after frame cycle tests.
     */
    static void hideTestWindow() {
        if (s_testWindow) {
            SDL_HideWindow(s_testWindow);
        }
    }

    /**
     * Clean up test resources.
     */
    static void cleanup() {
        if (s_testWindow) {
            SDL_DestroyWindow(s_testWindow);
            s_testWindow = nullptr;
        }
        if (s_sdlInitialized) {
            SDL_Quit();
            s_sdlInitialized = false;
        }
        s_gpuAvailable = false;
    }

protected:
    static void checkGPUAvailability() {
        // Try to create a test window to verify GPU capability
        SDL_Window* testWin = SDL_CreateWindow(
            "GPU Test",
            64, 64,
            SDL_WINDOW_HIDDEN
        );

        if (!testWin) {
            BOOST_TEST_MESSAGE("Cannot create window for GPU test: " << SDL_GetError());
            s_gpuAvailable = false;
            return;
        }

        // Try to create a GPU device
        SDL_GPUDevice* device = SDL_CreateGPUDevice(
            SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL,
            false,  // debug mode
            nullptr // name
        );

        if (!device) {
            BOOST_TEST_MESSAGE("Cannot create GPU device: " << SDL_GetError());
            SDL_DestroyWindow(testWin);
            s_gpuAvailable = false;
            return;
        }

        // GPU is available - clean up test resources
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(testWin);
        s_gpuAvailable = true;
        BOOST_TEST_MESSAGE("GPU is available for testing");
    }

    // Static state shared across fixtures
    static inline bool s_sdlInitialized = false;
    static inline bool s_gpuAvailable = false;
    static inline SDL_Window* s_testWindow = nullptr;
};

/**
 * Global fixture for GPU test cleanup.
 * Ensures SDL resources are cleaned up after all tests.
 */
struct GPUGlobalFixture {
    ~GPUGlobalFixture() {
        GPUTestFixture::cleanup();
    }
};

} // namespace Test
} // namespace HammerEngine

#endif // GPU_TEST_FIXTURE_HPP
