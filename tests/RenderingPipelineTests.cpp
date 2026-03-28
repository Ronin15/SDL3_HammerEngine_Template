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
// TEST SUITE: GPURenderPipelineComplianceTests
// ============================================================================
// Tests that validate GPU render-pass best practices
// From AGENTS.md: one present/end-frame path through GameEngine
BOOST_AUTO_TEST_SUITE(GPURenderPipelineComplianceTests)

// ----------------------------------------------------------------------------
// Test: Only GameEngine ends the GPU frame
// ----------------------------------------------------------------------------
// Best practice: exactly one end-frame path per frame for performance
// This must only happen in GameEngine::present() for unified render path

BOOST_AUTO_TEST_CASE(TestOnlyGameEngineCallsEndFrame) {
    const std::string gameEngineFile = "src/core/GameEngine.cpp";
    const std::string gpuRendererFile = "src/gpu/GPURenderer.cpp";

    bool hasPresentMethod = fileContainsPattern(gameEngineFile, "void GameEngine::present()");
    bool callsEndFrame = fileContainsPattern(gameEngineFile, "GPURenderer::Instance().endFrame()");
    BOOST_CHECK(hasPresentMethod);
    BOOST_CHECK(callsEndFrame);

    // Verify game states never end the frame directly
    std::vector<std::string> gameStateFiles = {
        "src/gameStates/GameOverState.cpp",
        "src/gameStates/MainMenuState.cpp",
        "src/gameStates/GamePlayState.cpp",
        "src/gameStates/PauseState.cpp",
        "src/gameStates/SettingsMenuState.cpp",
        "src/gameStates/LoadingState.cpp",
        "src/gameStates/UIDemoState.cpp",
        "src/gameStates/EventDemoState.cpp"
    };

    for (const auto& file : gameStateFiles) {
        bool hasEndFrame = fileContainsPattern(file, "endFrame(");
        BOOST_CHECK_MESSAGE(!hasEndFrame, "GameState " + file + " should NOT end the GPU frame");
    }

    BOOST_CHECK(fileContainsPattern(gpuRendererFile, "void GPURenderer::endFrame()"));
}

// ----------------------------------------------------------------------------
// Test: Only GameEngine begins the scene pass
// ----------------------------------------------------------------------------
// Scene-pass acquisition should be centralized in GameEngine::render()

BOOST_AUTO_TEST_CASE(TestOnlyGameEngineBeginsScenePass) {
    const std::string gameEngineFile = "src/core/GameEngine.cpp";

    bool hasScenePass = fileContainsPattern(gameEngineFile, "gpuRenderer.beginScenePass()");
    BOOST_CHECK(hasScenePass);

    // Verify GameStates NEVER begin the pass directly
    std::vector<std::string> gameStateFiles = {
        "src/gameStates/GameOverState.cpp",
        "src/gameStates/MainMenuState.cpp",
        "src/gameStates/GamePlayState.cpp",
        "src/gameStates/PauseState.cpp",
        "src/gameStates/SettingsMenuState.cpp",
        "src/gameStates/LoadingState.cpp",
        "src/gameStates/UIDemoState.cpp",
        "src/gameStates/EventDemoState.cpp"
    };

    for (const auto& file : gameStateFiles) {
        bool hasScenePass = fileContainsPattern(file, "beginScenePass(");
        BOOST_CHECK_MESSAGE(!hasScenePass, "GameState " + file + " should NOT begin GPU scene passes");
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
// Test: LoadingState GPU hooks follow correct pattern
// ----------------------------------------------------------------------------
// LoadingState should use GPU hooks and avoid manual present/clear

BOOST_AUTO_TEST_CASE(TestLoadingStateRenderPattern) {
    const std::string loadingStateFile = "src/gameStates/LoadingState.cpp";

    BOOST_CHECK(fileContainsPattern(loadingStateFile, "LoadingState::recordGPUVertices"));
    BOOST_CHECK(fileContainsPattern(loadingStateFile, "LoadingState::renderGPUUI"));
    BOOST_CHECK(fileContainsPattern(loadingStateFile, "UIManager::Instance()"));
    BOOST_CHECK(!fileContainsPattern(loadingStateFile, "SDL_RenderPresent"));
    BOOST_CHECK(!fileContainsPattern(loadingStateFile, "SDL_RenderClear"));
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
    bool callsRecord = fileContainsPattern(gameEngineFile, "mp_gameStateManager->recordGPUVertices(");
    bool callsScene = fileContainsPattern(gameEngineFile, "mp_gameStateManager->renderGPUScene(");
    bool callsUI = fileContainsPattern(gameEngineFile, "mp_gameStateManager->renderGPUUI(");

    BOOST_CHECK_MESSAGE(callsRecord, "GameEngine::render() must call GameStateManager::recordGPUVertices()");
    BOOST_CHECK_MESSAGE(callsScene, "GameEngine::render() must call GameStateManager::renderGPUScene()");
    BOOST_CHECK_MESSAGE(callsUI, "GameEngine::render() must call GameStateManager::renderGPUUI()");
}

// ----------------------------------------------------------------------------
// Test: GameStateManager GPU methods call GameState GPU methods
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestGameStateManagerCallsGameState) {
    const std::string gsmFile = "src/managers/GameStateManager.cpp";

    bool callsRecord = fileContainsPattern(gsmFile, "->recordGPUVertices(");
    bool callsScene = fileContainsPattern(gsmFile, "->renderGPUScene(");
    bool callsUI = fileContainsPattern(gsmFile, "->renderGPUUI(");

    BOOST_CHECK_MESSAGE(callsRecord, "GameStateManager must call GameState::recordGPUVertices()");
    BOOST_CHECK_MESSAGE(callsScene, "GameStateManager must call GameState::renderGPUScene()");
    BOOST_CHECK_MESSAGE(callsUI, "GameStateManager must call GameState::renderGPUUI()");
}

// ----------------------------------------------------------------------------
// Test: Rendering flow structure verification
// ----------------------------------------------------------------------------
// Complete flow: GameEngine → GameStateManager → GameState GPU hooks

BOOST_AUTO_TEST_CASE(TestCompleteRenderingFlow) {
    // Step 1: GameEngine::render() exists
    const std::string gameEngineFile = "src/core/GameEngine.cpp";
    bool hasGameEngineRender = fileContainsPattern(gameEngineFile, "void GameEngine::render()");
    BOOST_CHECK(hasGameEngineRender);

    // Step 2: GameStateManager GPU entrypoints exist
    const std::string gsmFile = "src/managers/GameStateManager.cpp";
    bool hasGSMRecord = fileContainsPattern(gsmFile, "void GameStateManager::recordGPUVertices(");
    bool hasGSMScene = fileContainsPattern(gsmFile, "void GameStateManager::renderGPUScene(");
    bool hasGSMUI = fileContainsPattern(gsmFile, "void GameStateManager::renderGPUUI(");
    BOOST_CHECK(hasGSMRecord);
    BOOST_CHECK(hasGSMScene);
    BOOST_CHECK(hasGSMUI);

    // Step 3: At least one GameState implements GPU rendering hooks
    std::vector<std::string> gameStateFiles = {
        "src/gameStates/MainMenuState.cpp",
        "src/gameStates/GamePlayState.cpp",
        "src/gameStates/LoadingState.cpp"
    };

    bool foundStateRender = false;
    for (const auto& file : gameStateFiles) {
        if (fileContainsPattern(file, "::recordGPUVertices(") ||
            fileContainsPattern(file, "::renderGPUScene(") ||
            fileContainsPattern(file, "::renderGPUUI(")) {
            foundStateRender = true;
            break;
        }
    }
    BOOST_CHECK(foundStateRender);
}

BOOST_AUTO_TEST_CASE(TestGPUVertexRecordingPrecedesScenePassAndSwapchainIsAcquiredInScenePass) {
    const std::string gameEngineFile = "src/core/GameEngine.cpp";
    const std::string gpuRendererFile = "src/gpu/GPURenderer.cpp";

    std::ifstream gameEngineStream(gameEngineFile);
    BOOST_REQUIRE(gameEngineStream.is_open());

    std::string gameEngineContent((std::istreambuf_iterator<char>(gameEngineStream)),
                                  std::istreambuf_iterator<char>());

    const auto recordPos = gameEngineContent.find("mp_gameStateManager->recordGPUVertices");
    const auto beginScenePassPos = gameEngineContent.find("gpuRenderer.beginScenePass()");

    BOOST_REQUIRE(recordPos != std::string::npos);
    BOOST_REQUIRE(beginScenePassPos != std::string::npos);
    BOOST_CHECK_LT(recordPos, beginScenePassPos);

    std::ifstream gpuRendererStream(gpuRendererFile);
    BOOST_REQUIRE(gpuRendererStream.is_open());

    std::string gpuRendererContent((std::istreambuf_iterator<char>(gpuRendererStream)),
                                   std::istreambuf_iterator<char>());

    const auto scenePassFuncPos = gpuRendererContent.find("SDL_GPURenderPass* GPURenderer::beginScenePass()");
    BOOST_REQUIRE(scenePassFuncPos != std::string::npos);

    const auto acquirePos = gpuRendererContent.find("if (!acquireSwapchainTexture())", scenePassFuncPos);
    const auto beginRenderPassPos = gpuRendererContent.find("SDL_BeginGPURenderPass", scenePassFuncPos);

    BOOST_REQUIRE(acquirePos != std::string::npos);
    BOOST_REQUIRE(beginRenderPassPos != std::string::npos);
    BOOST_CHECK_LT(acquirePos, beginRenderPassPos);
}

BOOST_AUTO_TEST_CASE(TestGamePlayStateGPUResourceDrawOrderMatchesSDLPath) {
    const std::string gamePlayFile = "src/gameStates/GamePlayState.cpp";

    std::ifstream file(gamePlayFile);
    BOOST_REQUIRE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    const auto droppedItemsPos = content.find("resourceCtrl->recordGPUDroppedItems");
    const auto npcPos = content.find("m_npcRenderCtrl.recordGPU(ctx)");

    BOOST_REQUIRE(droppedItemsPos != std::string::npos);
    BOOST_REQUIRE(npcPos != std::string::npos);
    BOOST_CHECK_LT(droppedItemsPos, npcPos);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TEST SUITE: RenderingBestPracticesTests
// ============================================================================
// Tests that validate rendering best practices for the GPU frame flow

BOOST_AUTO_TEST_SUITE(RenderingBestPracticesTests)

// ----------------------------------------------------------------------------
// Test: No duplicate end-frame pattern in codebase
// ----------------------------------------------------------------------------
// Multiple end-frame paths per frame hurt performance

BOOST_AUTO_TEST_CASE(TestNoDoublePresentPattern) {
    const std::string gameEngineFile = "src/core/GameEngine.cpp";

    int presentCount = countPatternInFile(gameEngineFile, "endFrame(");

    BOOST_CHECK_GT(presentCount, 0);
    BOOST_CHECK_LE(presentCount, 2);
}

// ----------------------------------------------------------------------------
// Test: Render state isolation (no state leakage between frames)
// ----------------------------------------------------------------------------
// Each frame should be self-contained for deterministic rendering

BOOST_AUTO_TEST_CASE(TestRenderStateIsolation) {
    const std::string gameEngineFile = "src/core/GameEngine.cpp";

    bool hasScenePass = fileContainsPattern(gameEngineFile, "gpuRenderer.beginScenePass()");
    BOOST_CHECK_MESSAGE(hasScenePass, "GameEngine::render() must begin the GPU scene pass");

    bool hasPresent = fileContainsPattern(gameEngineFile, "GPURenderer::Instance().endFrame()");
    BOOST_CHECK_MESSAGE(hasPresent, "GameEngine::present() must end the GPU frame");
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
        "src/gameStates/GameOverState.cpp",
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
// Test: VSync configuration via GPU swapchain API
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(TestVSyncConfiguration) {
    const std::string gameEngineCpp = "src/core/GameEngine.cpp";

    BOOST_CHECK_MESSAGE(fileContainsPattern(gameEngineCpp, "SDL_SetGPUSwapchainParameters"),
        "GameEngine should configure VSync at runtime via SDL_SetGPUSwapchainParameters");
    BOOST_CHECK_MESSAGE(fileContainsPattern(gameEngineCpp, "verifyVSyncState"),
        "GameEngine should retain VSync verification through verifyVSyncState()");
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
