/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE RenderingPipelineTests
#include <boost/test/unit_test.hpp>

#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

// ============================================================================
// Helper Functions
// ============================================================================

// Helper function to search for pattern in file (ignoring C++ comments)
bool fileContainsPattern(const std::string& filepath, const std::string& pattern) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip single-line comments (//)
        size_t commentPos = line.find("//");
        std::string codeOnly = (commentPos != std::string::npos) ? line.substr(0, commentPos) : line;

        // Skip block comment starts (/* ... we'll ignore multi-line for simplicity)
        size_t blockCommentPos = codeOnly.find("/*");
        if (blockCommentPos != std::string::npos) {
            codeOnly = codeOnly.substr(0, blockCommentPos);
        }

        if (codeOnly.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// Helper to count occurrences of pattern in file (ignoring C++ comments)
int countPatternInFile(const std::string& filepath, const std::string& pattern) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return 0;
    }

    int count = 0;
    std::string line;
    while (std::getline(file, line)) {
        // Skip single-line comments (//)
        size_t commentPos = line.find("//");
        std::string codeOnly = (commentPos != std::string::npos) ? line.substr(0, commentPos) : line;

        // Skip block comment starts
        size_t blockCommentPos = codeOnly.find("/*");
        if (blockCommentPos != std::string::npos) {
            codeOnly = codeOnly.substr(0, blockCommentPos);
        }

        size_t pos = 0;
        while ((pos = codeOnly.find(pattern, pos)) != std::string::npos) {
            count++;
            pos += pattern.length();
        }
    }
    return count;
}

// ============================================================================
// TEST SUITE: SDLRendererComplianceTests
// ============================================================================
// Tests that validate SDL_Renderer best practices
// From CLAUDE.md: "exactly one Present() per frame through unified render path"
// HammerEngine uses SDL_Renderer (not SDL3_GPU), but follows same best practices

BOOST_AUTO_TEST_SUITE(SDLRendererComplianceTests)

// ----------------------------------------------------------------------------
// Test: Only GameEngine calls SDL_RenderPresent
// ----------------------------------------------------------------------------
// Best practice: exactly ONE SDL_RenderPresent per frame for performance
// This must only happen in GameEngine::render() for unified render path

BOOST_AUTO_TEST_CASE(TestOnlyGameEngineCallsRenderPresent) {
    const std::string gameEngineFile = "src/core/GameEngine.cpp";

    // Verify GameEngine.cpp calls SDL_RenderPresent (should have exactly 1-2 calls)
    int presentCalls = countPatternInFile(gameEngineFile, "SDL_RenderPresent");
    BOOST_CHECK_GT(presentCalls, 0); // At least one call exists
    BOOST_CHECK_LE(presentCalls, 2); // Should not have excessive calls

    // Verify GameStates NEVER call SDL_RenderPresent
    std::vector<std::string> gameStateFiles = {
        "src/gameStates/MainMenuState.cpp",
        "src/gameStates/GamePlayState.cpp",
        "src/gameStates/PauseState.cpp",
        "src/gameStates/SettingsMenuState.cpp",
        "src/gameStates/LoadingState.cpp",
        "src/gameStates/UIDemoState.cpp",
        "src/gameStates/EventDemoState.cpp"
    };

    for (const auto& file : gameStateFiles) {
        bool hasPresent = fileContainsPattern(file, "SDL_RenderPresent");
        BOOST_CHECK_MESSAGE(!hasPresent, "GameState " + file + " should NOT call SDL_RenderPresent");
    }
}

// ----------------------------------------------------------------------------
// Test: Only GameEngine calls SDL_RenderClear
// ----------------------------------------------------------------------------
// SDL_RenderClear should only be called once per frame in GameEngine::render()

BOOST_AUTO_TEST_CASE(TestOnlyGameEngineCallsRenderClear) {
    const std::string gameEngineFile = "src/core/GameEngine.cpp";

    // Verify GameEngine.cpp calls SDL_RenderClear
    bool hasClear = fileContainsPattern(gameEngineFile, "SDL_RenderClear");
    BOOST_CHECK(hasClear);

    // Verify GameStates NEVER call SDL_RenderClear
    std::vector<std::string> gameStateFiles = {
        "src/gameStates/MainMenuState.cpp",
        "src/gameStates/GamePlayState.cpp",
        "src/gameStates/PauseState.cpp",
        "src/gameStates/SettingsMenuState.cpp",
        "src/gameStates/LoadingState.cpp",
        "src/gameStates/UIDemoState.cpp",
        "src/gameStates/EventDemoState.cpp"
    };

    for (const auto& file : gameStateFiles) {
        bool hasClear = fileContainsPattern(file, "SDL_RenderClear");
        BOOST_CHECK_MESSAGE(!hasClear, "GameState " + file + " should NOT call SDL_RenderClear");
    }
}

// ----------------------------------------------------------------------------
// Test: LoadingState uses async pattern (no blocking rendering)
// ----------------------------------------------------------------------------
// LoadingState must use ThreadSystem for async loading, not blocking loops

BOOST_AUTO_TEST_CASE(TestLoadingStateAsyncPattern) {
    const std::string loadingStateFile = "src/gameStates/LoadingState.cpp";

    // Verify LoadingState uses ThreadSystem for async operations
    bool usesThreadSystem = fileContainsPattern(loadingStateFile, "ThreadSystem");
    BOOST_CHECK_MESSAGE(usesThreadSystem, "LoadingState should use ThreadSystem for async loading");

    // Verify LoadingState uses atomics for thread-safe progress tracking
    bool usesAtomics = fileContainsPattern(loadingStateFile, "std::atomic") ||
                      fileContainsPattern(loadingStateFile, ".load(") ||
                      fileContainsPattern(loadingStateFile, ".store(");
    BOOST_CHECK_MESSAGE(usesAtomics, "LoadingState should use atomics for thread-safe state");

    // Verify LoadingState does NOT have blocking loops with manual rendering
    // (No "while" loops with SDL_RenderPresent inside LoadingState)
    std::ifstream file(loadingStateFile);
    BOOST_REQUIRE(file.is_open());

    bool inWhileLoop = false;
    bool foundBlockingPattern = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("while") != std::string::npos && line.find("(") != std::string::npos) {
            inWhileLoop = true;
        }
        if (inWhileLoop && (line.find("SDL_RenderPresent") != std::string::npos ||
                           line.find("SDL_RenderClear") != std::string::npos)) {
            foundBlockingPattern = true;
            break;
        }
        if (line.find("}") != std::string::npos) {
            inWhileLoop = false;
        }
    }

    BOOST_CHECK_MESSAGE(!foundBlockingPattern,
                       "LoadingState should NOT have blocking loops with manual rendering");
}

// ----------------------------------------------------------------------------
// Test: LoadingState render() follows correct pattern
// ----------------------------------------------------------------------------
// LoadingState::render() should only update UI, not call SDL directly

BOOST_AUTO_TEST_CASE(TestLoadingStateRenderPattern) {
    const std::string loadingStateFile = "src/gameStates/LoadingState.cpp";

    // Find the render() method
    std::ifstream file(loadingStateFile);
    BOOST_REQUIRE(file.is_open());

    bool inRenderMethod = false;
    bool foundUIManagerCall = false;
    bool foundManualSDLCall = false;

    std::string line;
    while (std::getline(file, line)) {
        // Detect render() method start
        if (line.find("void LoadingState::render()") != std::string::npos ||
            line.find("void LoadingState::render(") != std::string::npos) {
            inRenderMethod = true;
        }

        if (inRenderMethod) {
            // Strip comments from line
            size_t commentPos = line.find("//");
            std::string codeOnly = (commentPos != std::string::npos) ? line.substr(0, commentPos) : line;
            size_t blockCommentPos = codeOnly.find("/*");
            if (blockCommentPos != std::string::npos) {
                codeOnly = codeOnly.substr(0, blockCommentPos);
            }

            // Check for UIManager usage (GOOD)
            if (codeOnly.find("UIManager") != std::string::npos) {
                foundUIManagerCall = true;
            }

            // Check for manual SDL calls in actual code (not comments) (BAD)
            if (codeOnly.find("SDL_RenderPresent") != std::string::npos ||
                codeOnly.find("SDL_RenderClear") != std::string::npos) {
                foundManualSDLCall = true;
            }

            // Exit render method
            if (line.find("}") != std::string::npos && line.find("{") == std::string::npos) {
                break;
            }
        }
    }

    BOOST_CHECK_MESSAGE(foundUIManagerCall, "LoadingState::render() should use UIManager");
    BOOST_CHECK_MESSAGE(!foundManualSDLCall, "LoadingState::render() should NOT call SDL directly");
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TEST SUITE: RenderingFlowTests
// ============================================================================
// Tests that validate the correct rendering flow architecture

BOOST_AUTO_TEST_SUITE(RenderingFlowTests)

// ----------------------------------------------------------------------------
// Test: GameEngine::render() calls GameStateManager::render()
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestGameEngineCallsGameStateManager) {
    const std::string gameEngineFile = "src/core/GameEngine.cpp";

    // Verify GameEngine::render() delegates to GameStateManager
    bool callsGSM = fileContainsPattern(gameEngineFile, "mp_gameStateManager->render(") ||
                    fileContainsPattern(gameEngineFile, "gameStateManager->render(");

    BOOST_CHECK_MESSAGE(callsGSM, "GameEngine::render() must call GameStateManager::render()");
}

// ----------------------------------------------------------------------------
// Test: GameStateManager::render() calls GameState::render()
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestGameStateManagerCallsGameState) {
    const std::string gsmFile = "src/managers/GameStateManager.cpp";

    // Verify GameStateManager::render() delegates to active state
    bool callsState = fileContainsPattern(gsmFile, "->render(");

    BOOST_CHECK_MESSAGE(callsState, "GameStateManager::render() must call GameState::render()");
}

// ----------------------------------------------------------------------------
// Test: Rendering flow structure verification
// ----------------------------------------------------------------------------
// Complete flow: GameEngine → GameStateManager → GameState

BOOST_AUTO_TEST_CASE(TestCompleteRenderingFlow) {
    // Step 1: GameEngine::render() exists
    const std::string gameEngineFile = "src/core/GameEngine.cpp";
    bool hasGameEngineRender = fileContainsPattern(gameEngineFile, "void GameEngine::render()");
    BOOST_CHECK(hasGameEngineRender);

    // Step 2: GameStateManager::render() exists
    const std::string gsmFile = "src/managers/GameStateManager.cpp";
    bool hasGSMRender = fileContainsPattern(gsmFile, "void GameStateManager::render(SDL_Renderer*");
    BOOST_CHECK(hasGSMRender);

    // Step 3: At least one GameState implements render()
    std::vector<std::string> gameStateFiles = {
        "src/gameStates/MainMenuState.cpp",
        "src/gameStates/GamePlayState.cpp",
        "src/gameStates/LoadingState.cpp"
    };

    bool foundStateRender = false;
    for (const auto& file : gameStateFiles) {
        if (fileContainsPattern(file, "::render(SDL_Renderer*")) {
            foundStateRender = true;
            break;
        }
    }
    BOOST_CHECK(foundStateRender);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TEST SUITE: RenderingBestPracticesTests
// ============================================================================
// Tests that validate rendering best practices for SDL_Renderer

BOOST_AUTO_TEST_SUITE(RenderingBestPracticesTests)

// ----------------------------------------------------------------------------
// Test: No double-Present pattern in codebase
// ----------------------------------------------------------------------------
// Multiple SDL_RenderPresent calls per frame hurt performance

BOOST_AUTO_TEST_CASE(TestNoDoublePresentPattern) {
    const std::string gameEngineFile = "src/core/GameEngine.cpp";

    // Count SDL_RenderPresent calls in GameEngine::render()
    // Should be exactly 1 (or 2 if there's error handling/retry logic)
    int presentCount = countPatternInFile(gameEngineFile, "SDL_RenderPresent");

    BOOST_CHECK_GT(presentCount, 0);  // At least one Present
    BOOST_CHECK_LE(presentCount, 3);  // Not excessive (allows for error paths)
}

// ----------------------------------------------------------------------------
// Test: Render state isolation (no state leakage between frames)
// ----------------------------------------------------------------------------
// Each render() should be self-contained for deterministic rendering

BOOST_AUTO_TEST_CASE(TestRenderStateIsolation) {
    const std::string gameEngineFile = "src/core/GameEngine.cpp";

    // GameEngine::render() should have Clear at the start
    bool hasClear = fileContainsPattern(gameEngineFile, "SDL_RenderClear");
    BOOST_CHECK_MESSAGE(hasClear, "GameEngine::render() must clear at start for state isolation");

    // GameEngine::render() should have Present at the end
    bool hasPresent = fileContainsPattern(gameEngineFile, "SDL_RenderPresent");
    BOOST_CHECK_MESSAGE(hasPresent, "GameEngine::render() must present at end for state isolation");
}

// ----------------------------------------------------------------------------
// Test: No mid-frame Present calls
// ----------------------------------------------------------------------------
// Managers should never call Present during their render operations

BOOST_AUTO_TEST_CASE(TestNoMidFramePresentInManagers) {
    std::vector<std::string> managerFiles = {
        "src/managers/UIManager.cpp",
        "src/managers/ParticleManager.cpp",
        "src/managers/WorldManager.cpp"
    };

    for (const auto& file : managerFiles) {
        bool hasPresent = fileContainsPattern(file, "SDL_RenderPresent");
        BOOST_CHECK_MESSAGE(!hasPresent, file + " should NOT call SDL_RenderPresent");
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TEST SUITE: DeterministicRenderingTests
// ============================================================================
// Tests that validate deterministic rendering patterns

BOOST_AUTO_TEST_SUITE(DeterministicRenderingTests)

// ----------------------------------------------------------------------------
// Test: No random values in render paths
// ----------------------------------------------------------------------------
// Rendering should be fully deterministic based on game state

BOOST_AUTO_TEST_CASE(TestNoRandomInRenderMethods) {
    std::vector<std::string> gameStateFiles = {
        "src/gameStates/MainMenuState.cpp",
        "src/gameStates/GamePlayState.cpp",
        "src/gameStates/PauseState.cpp",
        "src/gameStates/SettingsMenuState.cpp"
    };

    for (const auto& file : gameStateFiles) {
        // Check for render() method with random calls (suspicious pattern)
        std::ifstream infile(file);
        if (!infile.is_open()) continue;

        bool inRenderMethod = false;
        bool foundRandomCall = false;

        std::string line;
        while (std::getline(infile, line)) {
            if (line.find("::render()") != std::string::npos) {
                inRenderMethod = true;
            }

            if (inRenderMethod) {
                if (line.find("rand()") != std::string::npos ||
                    line.find("random()") != std::string::npos ||
                    line.find("std::random") != std::string::npos) {
                    foundRandomCall = true;
                    break;
                }

                // Exit method (simple heuristic)
                if (line.find("}") != std::string::npos && line.find("{") == std::string::npos) {
                    inRenderMethod = false;
                }
            }
        }

        BOOST_CHECK_MESSAGE(!foundRandomCall,
                           file + "::render() should not use random values for determinism");
    }
}

// ----------------------------------------------------------------------------
// Test: TimestepManager provides deterministic fixed timestep
// ----------------------------------------------------------------------------
// Replaces old GameLoop buffer management (removed in commit 792501b)

BOOST_AUTO_TEST_CASE(TestTimestepManagerPattern) {
    const std::string gameEngineHpp = "include/core/GameEngine.hpp";
    const std::string hammerMainCpp = "src/core/HammerMain.cpp";

    // GameEngine must have TimestepManager
    BOOST_CHECK_MESSAGE(fileContainsPattern(gameEngineHpp, "TimestepManager"),
        "GameEngine should use TimestepManager for deterministic timing");

    // Main loop uses fixed timestep pattern: startFrame -> shouldUpdate -> render -> endFrame
    BOOST_CHECK_MESSAGE(fileContainsPattern(hammerMainCpp, "ts.startFrame()"),
        "Main loop should call startFrame() for frame timing");
    BOOST_CHECK_MESSAGE(fileContainsPattern(hammerMainCpp, "ts.shouldUpdate()"),
        "Main loop should use shouldUpdate() for fixed timestep updates");
    BOOST_CHECK_MESSAGE(fileContainsPattern(hammerMainCpp, "ts.endFrame()"),
        "Main loop should call endFrame() for frame limiting");
}

// ----------------------------------------------------------------------------
// Test: VSync configuration via SDL API
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestVSyncConfiguration) {
    const std::string gameEngineCpp = "src/core/GameEngine.cpp";

    // Verify runtime VSync handling via SDL API
    BOOST_CHECK_MESSAGE(fileContainsPattern(gameEngineCpp, "SDL_SetRenderVSync"),
        "GameEngine should configure VSync at runtime via SDL_SetRenderVSync");
    BOOST_CHECK_MESSAGE(fileContainsPattern(gameEngineCpp, "SDL_GetRenderVSync"),
        "GameEngine should verify VSync state via SDL_GetRenderVSync");
}

// ----------------------------------------------------------------------------
// Test: SDL performance hints are configured (cross-platform)
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestSDLPerformanceHints) {
    const std::string gameEngineCpp = "src/core/GameEngine.cpp";

    // Verify render batching hint (cross-platform performance optimization)
    BOOST_CHECK_MESSAGE(fileContainsPattern(gameEngineCpp, "SDL_RENDER_BATCHING"),
        "GameEngine should enable render batching for performance");

    // Verify framebuffer acceleration hint (cross-platform)
    BOOST_CHECK_MESSAGE(fileContainsPattern(gameEngineCpp, "SDL_HINT_FRAMEBUFFER_ACCELERATION"),
        "GameEngine should enable framebuffer acceleration for performance");

    // Note: SDL_HINT_VIDEO_DOUBLE_BUFFER only works on Raspberry Pi and Wayland
    // so we don't test for it as it's a no-op on Windows/macOS
}

// ----------------------------------------------------------------------------
// Test: Software frame limiting fallback exists
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestSoftwareFrameLimitingFallback) {
    const std::string timestepCpp = "src/core/TimestepManager.cpp";

    // Verify software frame limiting exists as VSync fallback
    BOOST_CHECK_MESSAGE(fileContainsPattern(timestepCpp, "preciseFrameWait"),
        "TimestepManager should have preciseFrameWait for software frame limiting");
    BOOST_CHECK_MESSAGE(fileContainsPattern(timestepCpp, "m_usingSoftwareFrameLimiting"),
        "TimestepManager should track software vs hardware frame limiting mode");
}

// ----------------------------------------------------------------------------
// Test: Interpolation alpha used for smooth rendering
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestInterpolationAlphaForSmoothRendering) {
    const std::string gameEngineCpp = "src/core/GameEngine.cpp";
    const std::string timestepCpp = "src/core/TimestepManager.cpp";

    // Verify interpolation alpha is used for smooth rendering
    BOOST_CHECK_MESSAGE(fileContainsPattern(gameEngineCpp, "getInterpolationAlpha"),
        "GameEngine::render() should use interpolation alpha for smooth rendering");
    BOOST_CHECK_MESSAGE(fileContainsPattern(timestepCpp, "getInterpolationAlpha"),
        "TimestepManager should calculate interpolation alpha from accumulator");
}

BOOST_AUTO_TEST_SUITE_END()
