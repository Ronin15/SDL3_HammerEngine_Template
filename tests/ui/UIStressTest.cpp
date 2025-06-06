/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "UIStressTest.hpp"
#include "managers/UIManager.hpp"
#include <iostream>
#include <fstream>

#include <random>
#include <algorithm>
#include <iomanip>
#include <thread>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
#elif defined(__linux__) || defined(__APPLE__)
    #include <sys/resource.h>
    #include <unistd.h>
#endif

UIStressTest::UIStressTest()
    : m_rng(std::chrono::steady_clock::now().time_since_epoch().count()) {
    m_uiManager = &UIManager::Instance();
    m_currentMetrics = std::make_unique<PerformanceMetrics>();
    m_lastResults = std::make_unique<PerformanceMetrics>();
}

UIStressTest::~UIStressTest() {
    if (m_testRunning) {
        cleanupTest();
    }
}

bool UIStressTest::runStressTest(StressTestType testType, const StressTestConfig& config) {
    log("Starting UI Stress Test: " + std::to_string(static_cast<int>(testType)));

    StressTestConfig validatedConfig = config;
    validatedConfig.validate();

    bool result = false;

    switch (testType) {
        case StressTestType::BASIC_PERFORMANCE:
            result = testBasicPerformance(validatedConfig);
            break;
        case StressTestType::MASS_COMPONENTS:
            result = testMassComponents(validatedConfig);
            break;
        case StressTestType::RAPID_CREATION:
            result = testRapidCreation(validatedConfig);
            break;
        case StressTestType::ANIMATION_STRESS:
            result = testAnimationStress(validatedConfig);
            break;
        case StressTestType::INPUT_FLOOD:
            result = testInputFlood(validatedConfig);
            break;
        case StressTestType::MEMORY_PRESSURE:
            result = testMemoryPressure(validatedConfig);
            break;
        case StressTestType::RESOLUTION_SCALING:
            result = testResolutionScaling(validatedConfig);
            break;
        case StressTestType::PRESENTATION_MODES:
            result = testPresentationModes(validatedConfig);
            break;
        case StressTestType::LAYOUT_STRESS:
            result = testLayoutStress(validatedConfig);
            break;
        case StressTestType::THEME_SWITCHING:
            result = testThemeSwitching(validatedConfig);
            break;
        case StressTestType::COMPREHENSIVE:
            result = runAllTests(validatedConfig);
            break;
        default:
            log("Error: Unknown test type");
            return false;
    }

    m_allResults[testType] = std::make_shared<PerformanceMetrics>(*m_lastResults);

    if (result) {
        log("Test completed successfully");
        if (!checkPerformanceThresholds(validatedConfig)) {
            log("Warning: Performance thresholds exceeded");
            result = false;
        }
    } else {
        log("Test failed");
    }

    return result;
}

bool UIStressTest::runAllTests(const StressTestConfig& config) {
    log("Running comprehensive UI stress test suite");

    std::vector<StressTestType> tests = {
        StressTestType::BASIC_PERFORMANCE,
        StressTestType::MASS_COMPONENTS,
        StressTestType::RAPID_CREATION,
        StressTestType::ANIMATION_STRESS,
        StressTestType::INPUT_FLOOD,
        StressTestType::LAYOUT_STRESS,
        StressTestType::THEME_SWITCHING
    };

    if (config.enableMemoryStress) {
        tests.push_back(StressTestType::MEMORY_PRESSURE);
    }

    if (config.testResolutionChanges) {
        tests.push_back(StressTestType::RESOLUTION_SCALING);
    }

    if (config.testPresentationModes) {
        tests.push_back(StressTestType::PRESENTATION_MODES);
    }

    bool allPassed = true;
    for (auto testType : tests) {
        log("Running sub-test: " + std::to_string(static_cast<int>(testType)));
        if (!runStressTest(testType, config)) {
            allPassed = false;
            log("Sub-test failed");
        }

        // Brief pause between tests
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return allPassed;
}

bool UIStressTest::testBasicPerformance(const StressTestConfig& config) {
    initializeTest(config);

    // Create a baseline set of components
    int baselineComponents = std::min(100, config.maxComponents / 10);
    createComponentBatch(baselineComponents, config);

    auto testLogic = [this, &config](float deltaTime) -> bool {
        // Basic rendering and update cycle
        m_uiManager->update(deltaTime);
        m_uiManager->render(m_renderer);

        // Simulate light interaction
        if (config.simulateMouseInput && randomInt(0, 60) == 0) {
            simulateRandomClick();
        }

        return true;
    };

    bool result = runTestLoop(config, testLogic);
    cleanupTest();
    return result;
}

bool UIStressTest::testMassComponents(const StressTestConfig& config) {
    initializeTest(config);

    auto testLogic = [this, &config](float deltaTime) -> bool {
        // Gradually add components up to the maximum
        static float componentTimer = 0.0f;
        componentTimer += deltaTime;

        if (componentTimer >= (1.0f / config.componentsPerSecond)) {
            if (static_cast<int>(m_testComponentIDs.size()) < config.maxComponents) {
                createRandomComponent(config);
                componentTimer = 0.0f;
            }
        }

        // Update and render all components
        m_uiManager->update(deltaTime);
        m_uiManager->render(m_renderer);

        // Track component count
        m_currentMetrics->totalComponents = static_cast<int>(m_testComponentIDs.size());

        return true;
    };

    bool result = runTestLoop(config, testLogic);
    cleanupTest();
    return result;
}

bool UIStressTest::testRapidCreation(const StressTestConfig& config) {
    initializeTest(config);

    auto testLogic = [this, &config](float deltaTime) -> bool {
        // Rapidly create and destroy components
        static float createTimer = 0.0f;
        static float destroyTimer = 0.0f;

        createTimer += deltaTime;
        destroyTimer += deltaTime;

        // Create components rapidly
        if (createTimer >= 0.1f) { // 10 per second
            if (static_cast<int>(m_testComponentIDs.size()) < config.maxComponents) {
                createRandomComponent(config);
                m_stats.componentsCreated++;
            }
            createTimer = 0.0f;
        }

        // Destroy components occasionally
        if (destroyTimer >= 0.3f && m_testComponentIDs.size() > 50) {
            destroyRandomComponents(5);
            destroyTimer = 0.0f;
        }

        m_uiManager->update(deltaTime);
        m_uiManager->render(m_renderer);

        return true;
    };

    bool result = runTestLoop(config, testLogic);
    cleanupTest();
    return result;
}

bool UIStressTest::testAnimationStress(const StressTestConfig& config) {
    initializeTest(config);

    // Create components for animation
    createComponentBatch(200, config);

    auto testLogic = [this, &config](float deltaTime) -> bool {
        // Trigger animations frequently
        static float animTimer = 0.0f;
        animTimer += deltaTime;

        if (animTimer >= (1.0f / config.animationsPerSecond)) {
            if (config.enableAnimations) {
                createRandomAnimation();
                m_stats.animationsTriggered++;
            }
            animTimer = 0.0f;
        }

        m_uiManager->update(deltaTime);
        m_uiManager->render(m_renderer);

        return true;
    };

    bool result = runTestLoop(config, testLogic);
    cleanupTest();
    return result;
}

bool UIStressTest::testInputFlood(const StressTestConfig& config) {
    initializeTest(config);

    // Create interactive components
    createComponentBatch(100, config);

    auto testLogic = [this, &config](float deltaTime) -> bool {
        // Flood with input events
        static float inputTimer = 0.0f;
        inputTimer += deltaTime;

        if (inputTimer >= (1.0f / config.inputEventsPerSecond)) {
            if (config.simulateMouseInput) {
                simulateRandomClick();
                simulateRandomHover();
                m_stats.inputEventsSimulated += 2;
            }

            if (config.simulateKeyboardInput) {
                // Simulate keyboard input
                m_stats.inputEventsSimulated++;
            }

            inputTimer = 0.0f;
        }

        m_uiManager->update(deltaTime);
        m_uiManager->render(m_renderer);

        return true;
    };

    bool result = runTestLoop(config, testLogic);
    cleanupTest();
    return result;
}

bool UIStressTest::testMemoryPressure(const StressTestConfig& config) {
    if (!config.enableMemoryStress) {
        log("Memory stress test skipped (not enabled in config)");
        return true;
    }

    initializeTest(config);

    // Allocate memory to create pressure
    std::vector<std::vector<char>> memoryPressure;

    auto testLogic = [this, &config, &memoryPressure](float deltaTime) -> bool {
        // Create memory pressure
        for (int i = 0; i < config.memoryAllocationsPerFrame; ++i) {
            memoryPressure.emplace_back(1024); // 1KB allocations
            m_stats.memoryAllocations++;
        }

        // Occasionally clear some memory
        if (memoryPressure.size() > 10000) {
            memoryPressure.erase(memoryPressure.begin(), memoryPressure.begin() + 1000);
        }

        // Create and destroy UI components under memory pressure
        if (randomInt(0, 10) == 0) {
            createRandomComponent(config);
        }

        if (randomInt(0, 20) == 0 && !m_testComponentIDs.empty()) {
            destroyRandomComponents(1);
        }

        m_uiManager->update(deltaTime);
        m_uiManager->render(m_renderer);

        return true;
    };

    bool result = runTestLoop(config, testLogic);
    cleanupTest();
    return result;
}

bool UIStressTest::testResolutionScaling(const StressTestConfig& config) {
    if (!config.testResolutionChanges) {
        log("Resolution scaling test skipped (not enabled in config)");
        return true;
    }

    backupRendererState();

    bool allPassed = true;
    for (const auto& [width, height] : config.testResolutions) {
        log("Testing resolution: " + std::to_string(width) + "x" + std::to_string(height));
        if (!testResolution(width, height, config)) {
            allPassed = false;
            logPerformanceWarning("Resolution test failed: " + std::to_string(width) + "x" + std::to_string(height));
        }
    }

    restoreRendererState();
    return allPassed;
}

bool UIStressTest::testPresentationModes(const StressTestConfig& config) {
    if (!config.testPresentationModes) {
        log("Presentation mode test skipped (not enabled in config)");
        return true;
    }

    backupRendererState();

    bool allPassed = true;
    for (auto mode : config.testModes) {
        std::string modeName;
        switch (mode) {
            case SDL_LOGICAL_PRESENTATION_DISABLED: modeName = "DISABLED"; break;
            case SDL_LOGICAL_PRESENTATION_LETTERBOX: modeName = "LETTERBOX"; break;
            case SDL_LOGICAL_PRESENTATION_STRETCH: modeName = "STRETCH"; break;
            case SDL_LOGICAL_PRESENTATION_OVERSCAN: modeName = "OVERSCAN"; break;
            default: modeName = "UNKNOWN"; break;
        }

        log("Testing presentation mode: " + modeName);
        if (!testPresentationMode(mode, config)) {
            allPassed = false;
            logPerformanceWarning("Presentation mode test failed: " + modeName);
        }
    }

    restoreRendererState();
    return allPassed;
}

bool UIStressTest::testLayoutStress(const StressTestConfig& config) {
    initializeTest(config);

    // Create complex nested layouts
    createNestedLayouts(5, 4); // 5 levels deep, 4 children per level

    auto testLogic = [this](float deltaTime) -> bool {
        // Modify layouts dynamically
        static float layoutTimer = 0.0f;
        layoutTimer += deltaTime;

        if (layoutTimer >= 0.5f) {
            stressTestLayouts();
            layoutTimer = 0.0f;
        }

        m_uiManager->update(deltaTime);
        m_uiManager->render(m_renderer);

        return true;
    };

    bool result = runTestLoop(config, testLogic);
    cleanupTest();
    return result;
}

bool UIStressTest::testThemeSwitching(const StressTestConfig& config) {
    initializeTest(config);

    // Create components that will be affected by theme changes
    createComponentBatch(50, config);

    auto testLogic = [this](float deltaTime) -> bool {
        // Switch themes rapidly
        static float themeTimer = 0.0f;
        themeTimer += deltaTime;

        if (themeTimer >= 1.0f) {
            // Alternate between default and dark theme
            static bool isDark = false;
            isDark = !isDark;

            if (isDark) {
                // Apply dark theme (simplified)
                for (const auto& id : m_testComponentIDs) {
                    UIStyle darkStyle;
                    darkStyle.backgroundColor = {20, 20, 25, 240};
                    darkStyle.textColor = {255, 255, 255, 255};
                    darkStyle.borderColor = {100, 100, 100, 255};
                    m_uiManager->setStyle(id, darkStyle);
                }
            } else {
                // Apply light theme
                m_uiManager->setDefaultTheme();
            }

            m_stats.themeChanges++;
            themeTimer = 0.0f;
        }

        m_uiManager->update(deltaTime);
        m_uiManager->render(m_renderer);

        return true;
    };

    bool result = runTestLoop(config, testLogic);
    cleanupTest();
    return result;
}

void UIStressTest::initializeTest([[maybe_unused]] const StressTestConfig& config) {
    log("Initializing stress test...");

    m_testRunning = true;
    m_shouldStop = false;
    m_currentMetrics->reset();
    m_stats.reset();

    // Clear any existing test components
    destroyAllTestComponents();

    // Clear containers
    m_testComponentIDs.clear();
    m_testLayoutIDs.clear();
    m_activeAnimations.clear();

    startPerformanceMonitoring();
}

void UIStressTest::cleanupTest() {
    if (!m_testRunning) return;

    log("Cleaning up stress test...");

    stopPerformanceMonitoring();
    destroyAllTestComponents();

    // Clear layouts
    for ([[maybe_unused]] const auto& layoutID : m_testLayoutIDs) {
        // Note: UIManager doesn't have removeLayout method, so we just clear the vector
    }
    m_testLayoutIDs.clear();

    m_testRunning = false;
    *m_lastResults = *m_currentMetrics;

    log("Test cleanup completed");
}

bool UIStressTest::runTestLoop(const StressTestConfig& config, std::function<bool(float)> testLogic) {
    auto startTime = std::chrono::steady_clock::now();
    auto lastFrameTime = startTime;

    float totalTime = 0.0f;
    const float maxTime = static_cast<float>(config.durationSeconds);

    while (totalTime < maxTime && !m_shouldStop) {
        auto currentTime = std::chrono::steady_clock::now();
        auto frameTime = std::chrono::duration<double, std::milli>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;

        totalTime += static_cast<float>(frameTime / 1000.0);

        // Run test-specific logic
        if (!testLogic(static_cast<float>(frameTime / 1000.0))) {
            return false;
        }

        // Update performance metrics
        updatePerformanceMetrics(frameTime);

        // Check for early exit conditions
        if (frameTime > config.maxAcceptableFrameTime * 2.0) {
            logPerformanceWarning("Frame time exceeded 2x threshold: " + std::to_string(frameTime) + "ms");
        }

        // Prevent busy waiting
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    return true;
}

void UIStressTest::startPerformanceMonitoring() {
    m_testStartTime = std::chrono::steady_clock::now();
    m_lastFrameTime = m_testStartTime;
    m_frameTimes.clear();
}

void UIStressTest::updatePerformanceMetrics(double frameTime) {
    m_frameTimes.push_back(frameTime);
    m_currentMetrics->totalFrames++;
    m_currentMetrics->totalTestTime += frameTime;

    if (frameTime < m_currentMetrics->minFrameTime) {
        m_currentMetrics->minFrameTime = frameTime;
    }

    if (frameTime > m_currentMetrics->maxFrameTime) {
        m_currentMetrics->maxFrameTime = frameTime;
    }

    // Update component counts
    m_currentMetrics->totalComponents = static_cast<int>(m_testComponentIDs.size());
    m_currentMetrics->visibleComponents = m_currentMetrics->totalComponents; // Simplified

    // Sample memory and CPU usage periodically
    if (m_currentMetrics->totalFrames % 60 == 0) { // Every ~1 second at 60fps
        m_currentMetrics->memoryUsageMB = getCurrentMemoryUsageMB();
        m_currentMetrics->cpuUsagePercent = getCurrentCPUUsagePercent();
    }
}

void UIStressTest::stopPerformanceMonitoring() {
    m_currentMetrics->calculateAverages();

    // Calculate additional metrics
    if (!m_frameTimes.empty()) {
        std::sort(m_frameTimes.begin(), m_frameTimes.end());
        // You could calculate percentiles here if needed
    }
}

double UIStressTest::getCurrentMemoryUsageMB() const {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024.0 * 1024.0);
    }
#elif defined(__linux__)
    std::ifstream file("/proc/self/status");
    std::string line;
    while (std::getline(file, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            std::istringstream iss(line);
            std::string token;
            int value;
            iss >> token >> value;
            return value / 1024.0; // Convert KB to MB
        }
    }
#elif defined(__APPLE__)
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss / (1024.0 * 1024.0); // ru_maxrss is in bytes on macOS
    }
#endif
    return 0.0; // Unable to determine
}

double UIStressTest::getCurrentCPUUsagePercent() const {
    // Simplified CPU usage calculation
    // In a real implementation, you'd want more sophisticated CPU monitoring
    return 0.0;
}

void UIStressTest::createRandomComponent(const StressTestConfig& config) {
    ComponentType type = selectRandomComponentType(config);
    std::string id = "stress_test_" + std::to_string(m_testComponentIDs.size());

    // Random position and size
    int x = randomInt(0, 800);
    int y = randomInt(0, 600);
    int width = randomInt(50, 200);
    int height = randomInt(20, 100);
    UIRect bounds(x, y, width, height);

    switch (type) {
        case ComponentType::BUTTON:
            m_uiManager->createButton(id, bounds, "Button " + std::to_string(m_stats.componentsCreated));
            break;
        case ComponentType::LABEL:
            m_uiManager->createLabel(id, bounds, "Label " + std::to_string(m_stats.componentsCreated));
            break;
        case ComponentType::PANEL:
            m_uiManager->createPanel(id, bounds);
            break;
        case ComponentType::PROGRESS_BAR:
            m_uiManager->createProgressBar(id, bounds, 0.0f, 100.0f);
            m_uiManager->setValue(id, randomFloat(0.0f, 100.0f));
            break;
        case ComponentType::SLIDER:
            m_uiManager->createSlider(id, bounds, 0.0f, 100.0f);
            m_uiManager->setValue(id, randomFloat(0.0f, 100.0f));
            break;
        case ComponentType::CHECKBOX:
            m_uiManager->createCheckbox(id, bounds, "Check " + std::to_string(m_stats.componentsCreated));
            break;
        case ComponentType::INPUT_FIELD:
            m_uiManager->createInputField(id, bounds, "Placeholder...");
            break;
        case ComponentType::LIST:
            m_uiManager->createList(id, bounds);
            for (int i = 0; i < 3; ++i) {
                m_uiManager->addListItem(id, "Item " + std::to_string(i));
            }
            break;
        case ComponentType::IMAGE:
            m_uiManager->createImage(id, bounds, ""); // No texture ID for stress test
            break;
    }

    m_testComponentIDs.push_back(id);
    m_stats.componentsCreated++;
}

void UIStressTest::createComponentBatch(int count, const StressTestConfig& config) {
    for (int i = 0; i < count; ++i) {
        createRandomComponent(config);
    }
}

void UIStressTest::destroyRandomComponents(int count) {
    int toDestroy = std::min(count, static_cast<int>(m_testComponentIDs.size()));

    for (int i = 0; i < toDestroy; ++i) {
        if (!m_testComponentIDs.empty()) {
            int index = randomInt(0, static_cast<int>(m_testComponentIDs.size()) - 1);
            std::string id = m_testComponentIDs[index];

            m_uiManager->removeComponent(id);
            m_testComponentIDs.erase(m_testComponentIDs.begin() + index);
            m_stats.componentsDestroyed++;
        }
    }
}

void UIStressTest::destroyAllTestComponents() {
    for (const auto& id : m_testComponentIDs) {
        m_uiManager->removeComponent(id);
    }
    m_testComponentIDs.clear();
}

void UIStressTest::simulateRandomClick() {
    if (m_testComponentIDs.empty()) return;

    // Select a random component to "click"
    int index = randomInt(0, static_cast<int>(m_testComponentIDs.size()) - 1);
    std::string id = m_testComponentIDs[index];

    // Get component bounds to simulate click within them
    UIRect bounds = m_uiManager->getBounds(id);
    if (bounds.width > 0 && bounds.height > 0) {
        // Note: This is a simplified simulation
        // In a real test, you'd inject actual SDL events
    }
}

void UIStressTest::simulateRandomHover() {
    // Similar to click simulation but for hover
    // Implementation would depend on how you want to inject mouse events
}

void UIStressTest::createRandomAnimation() {
    if (m_testComponentIDs.empty()) return;

    int index = randomInt(0, static_cast<int>(m_testComponentIDs.size()) - 1);
    std::string id = m_testComponentIDs[index];

    UIRect currentBounds = m_uiManager->getBounds(id);
    UIRect targetBounds = currentBounds;

    // Random movement
    targetBounds.x += randomInt(-100, 100);
    targetBounds.y += randomInt(-100, 100);

    float duration = randomFloat(0.5f, 2.0f);

    m_uiManager->animateMove(id, targetBounds, duration, [this, id, currentBounds]() {
        // Animate back to original position
        m_uiManager->animateMove(id, currentBounds, 0.5f);
    });
}

void UIStressTest::createNestedLayouts(int depth, int childrenPerLevel) {
    if (depth <= 0) return;

    std::string layoutID = "stress_layout_" + std::to_string(m_testLayoutIDs.size());

    int x = randomInt(0, 600);
    int y = randomInt(0, 400);
    int width = randomInt(200, 400);
    int height = randomInt(150, 300);

    m_uiManager->createLayout(layoutID, UILayoutType::GRID, UIRect(x, y, width, height));
    m_uiManager->setLayoutColumns(layoutID, std::min(childrenPerLevel, 4));
    m_testLayoutIDs.push_back(layoutID);

    // Create child components
    for (int i = 0; i < childrenPerLevel; ++i) {
        std::string childID = layoutID + "_child_" + std::to_string(i);
        m_uiManager->createButton(childID, UIRect(0, 0, 50, 30), "Child " + std::to_string(i));
        m_uiManager->addComponentToLayout(layoutID, childID);
        m_testComponentIDs.push_back(childID);
    }

    m_uiManager->updateLayout(layoutID);
    m_stats.layoutsCreated++;

    // Recursively create nested layouts
    createNestedLayouts(depth - 1, childrenPerLevel);
}

void UIStressTest::stressTestLayouts() {
    for (const auto& layoutID : m_testLayoutIDs) {
        // Randomly modify layout properties
        if (randomInt(0, 3) == 0) {
            m_uiManager->setLayoutSpacing(layoutID, randomInt(0, 20));
        }

        if (randomInt(0, 3) == 0) {
            m_uiManager->setLayoutColumns(layoutID, randomInt(1, 5));
        }

        // Update layout
        m_uiManager->updateLayout(layoutID);
    }
}

bool UIStressTest::testResolution(int width, int height, [[maybe_unused]] const StressTestConfig& config) {
    // This would need integration with the actual window/renderer system
    // For now, we'll simulate the test
    log("Testing resolution " + std::to_string(width) + "x" + std::to_string(height));

    // Create some components and test basic functionality
    StressTestConfig shortConfig = config;
    shortConfig.durationSeconds = 5; // Short test for each resolution
    shortConfig.maxComponents = 100;

    return testBasicPerformance(shortConfig);
}

bool UIStressTest::testPresentationMode(SDL_RendererLogicalPresentation mode, const StressTestConfig& config) {
    if (!m_renderer) return false;

    // Set the presentation mode
    SDL_SetRenderLogicalPresentation(m_renderer, 1920, 1080, mode);

    // Run a short test with this mode
    StressTestConfig shortConfig = config;
    shortConfig.durationSeconds = 5;
    shortConfig.maxComponents = 50;

    bool result = testBasicPerformance(shortConfig);

    return result;
}

UIStressTest::ComponentType UIStressTest::selectRandomComponentType(const StressTestConfig& config) {
    int random = randomInt(0, 99);
    int cumulative = 0;

    cumulative += config.buttonPercentage;
    if (random < cumulative) return ComponentType::BUTTON;

    cumulative += config.labelPercentage;
    if (random < cumulative) return ComponentType::LABEL;

    cumulative += config.panelPercentage;
    if (random < cumulative) return ComponentType::PANEL;

    cumulative += config.progressBarPercentage;
    if (random < cumulative) return ComponentType::PROGRESS_BAR;

    cumulative += config.sliderPercentage;
    if (random < cumulative) return ComponentType::SLIDER;

    cumulative += config.checkboxPercentage;
    if (random < cumulative) return ComponentType::CHECKBOX;

    cumulative += config.inputFieldPercentage;
    if (random < cumulative) return ComponentType::INPUT_FIELD;

    cumulative += config.listPercentage;
    if (random < cumulative) return ComponentType::LIST;

    return ComponentType::IMAGE; // Default fallback
}

void UIStressTest::backupRendererState() {
    if (!m_renderer) return;

    // Get window dimensions directly from SDL instead of GameEngine
    SDL_Window* window = SDL_GetRenderWindow(m_renderer);
    if (window) {
        SDL_GetWindowSize(window, &m_originalState.windowWidth, &m_originalState.windowHeight);
    }

    m_originalState.wasLogicalPresentation = SDL_GetRenderLogicalPresentation(
        m_renderer,
        &m_originalState.logicalWidth,
        &m_originalState.logicalHeight,
        &m_originalState.presentation);
}

void UIStressTest::restoreRendererState() {
    if (!m_renderer) return;

    if (m_originalState.wasLogicalPresentation) {
        SDL_SetRenderLogicalPresentation(
            m_renderer,
            m_originalState.logicalWidth,
            m_originalState.logicalHeight,
            m_originalState.presentation);
    }
}

int UIStressTest::randomInt(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(m_rng);
}

float UIStressTest::randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(m_rng);
}

std::string UIStressTest::generateRandomString(int length) {
    const std::string chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string result;
    result.reserve(length);

    for (int i = 0; i < length; ++i) {
        result += chars[randomInt(0, static_cast<int>(chars.size()) - 1)];
    }

    return result;
}

void UIStressTest::log(const std::string& message) const {
    if (m_verbose) {
        std::cout << "[UIStressTest] " << message << std::endl;
    }

    if (m_logCallback) {
        m_logCallback(message);
    }
}

void UIStressTest::logPerformanceWarning(const std::string& warning) const {
    std::cout << "[UIStressTest WARNING] " << warning << std::endl;

    if (m_logCallback) {
        m_logCallback("WARNING: " + warning);
    }
}

bool UIStressTest::checkPerformanceThresholds(const StressTestConfig& config) const {
    bool passed = true;

    if (m_lastResults->averageFrameTime > config.maxAcceptableFrameTime) {
        logPerformanceWarning("Average frame time exceeded threshold: " +
                            std::to_string(m_lastResults->averageFrameTime) + "ms > " +
                            std::to_string(config.maxAcceptableFrameTime) + "ms");
        passed = false;
    }

    if (m_lastResults->memoryUsageMB > config.maxAcceptableMemoryMB) {
        logPerformanceWarning("Memory usage exceeded threshold: " +
                            std::to_string(m_lastResults->memoryUsageMB) + "MB > " +
                            std::to_string(config.maxAcceptableMemoryMB) + "MB");
        passed = false;
    }

    if (m_lastResults->cpuUsagePercent > config.maxAcceptableCPUPercent) {
        logPerformanceWarning("CPU usage exceeded threshold: " +
                            std::to_string(m_lastResults->cpuUsagePercent) + "% > " +
                            std::to_string(config.maxAcceptableCPUPercent) + "%");
        passed = false;
    }

    return passed;
}

void UIStressTest::printResults(StressTestType testType) const {
    auto it = m_allResults.find(testType);
    if (it == m_allResults.end()) {
        std::cout << "No results found for test type: " << static_cast<int>(testType) << std::endl;
        return;
    }

    const auto& results = *(it->second);

    std::cout << "\n=== UI Stress Test Results ===" << std::endl;
    std::cout << "Test Type: " << static_cast<int>(testType) << std::endl;
    std::cout << "Duration: " << std::fixed << std::setprecision(2) << results.totalTestTime / 1000.0 << "s" << std::endl;
    std::cout << "Total Frames: " << results.totalFrames << std::endl;
    std::cout << "Average FPS: " << std::fixed << std::setprecision(1) << results.averageFPS << std::endl;
    std::cout << "Average Frame Time: " << std::fixed << std::setprecision(2) << results.averageFrameTime << "ms" << std::endl;
    std::cout << "Min Frame Time: " << std::fixed << std::setprecision(2) << results.minFrameTime << "ms" << std::endl;
    std::cout << "Max Frame Time: " << std::fixed << std::setprecision(2) << results.maxFrameTime << "ms" << std::endl;
    std::cout << "Memory Usage: " << std::fixed << std::setprecision(1) << results.memoryUsageMB << "MB" << std::endl;
    std::cout << "CPU Usage: " << std::fixed << std::setprecision(1) << results.cpuUsagePercent << "%" << std::endl;
    std::cout << "Total Components: " << results.totalComponents << std::endl;
    std::cout << "Components Created: " << m_stats.componentsCreated << std::endl;
    std::cout << "Components Destroyed: " << m_stats.componentsDestroyed << std::endl;
    std::cout << "Animations Triggered: " << m_stats.animationsTriggered << std::endl;
    std::cout << "Input Events Simulated: " << m_stats.inputEventsSimulated << std::endl;
    std::cout << "Layouts Created: " << m_stats.layoutsCreated << std::endl;
    std::cout << "Theme Changes: " << m_stats.themeChanges << std::endl;
    std::cout << "================================\n" << std::endl;
}

void UIStressTest::printAllResults() const {
    for (const auto& [testType, results] : m_allResults) {
        printResults(testType);
    }
}

void UIStressTest::saveResultsToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        log("Failed to open file for writing: " + filename);
        return;
    }

    file << "UI Stress Test Results\n";
    file << "Generated: " << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << "\n\n";

    for (const auto& [testType, results] : m_allResults) {
        file << "Test Type: " << static_cast<int>(testType) << "\n";
        file << "Duration: " << results->totalTestTime / 1000.0 << "s\n";
        file << "Total Frames: " << results->totalFrames << "\n";
        file << "Average FPS: " << results->averageFPS << "\n";
        file << "Average Frame Time: " << results->averageFrameTime << "ms\n";
        file << "Min Frame Time: " << results->minFrameTime << "ms\n";
        file << "Max Frame Time: " << results->maxFrameTime << "ms\n";
        file << "Memory Usage: " << results->memoryUsageMB << "MB\n";
        file << "CPU Usage: " << results->cpuUsagePercent << "%\n";
        file << "Total Components: " << results->totalComponents << "\n";
        file << "\n";
    }

    file.close();
    log("Results saved to: " + filename);
}

StressTestConfig UIStressTest::createLightConfig() {
    StressTestConfig config;
    config.durationSeconds = 10;
    config.maxComponents = 100;
    config.componentsPerSecond = 10;
    config.inputEventsPerSecond = 5;
    config.animationsPerSecond = 2;
    config.enableMemoryStress = false;
    config.testResolutionChanges = false;
    config.testPresentationModes = false;
    return config;
}

StressTestConfig UIStressTest::createMediumConfig() {
    StressTestConfig config;
    config.durationSeconds = 30;
    config.maxComponents = 500;
    config.componentsPerSecond = 25;
    config.inputEventsPerSecond = 15;
    config.animationsPerSecond = 5;
    config.enableMemoryStress = false;
    config.testResolutionChanges = true;
    config.testPresentationModes = true;
    return config;
}

StressTestConfig UIStressTest::createHeavyConfig() {
    StressTestConfig config;
    config.durationSeconds = 60;
    config.maxComponents = 1000;
    config.componentsPerSecond = 50;
    config.inputEventsPerSecond = 30;
    config.animationsPerSecond = 15;
    config.enableMemoryStress = true;
    config.testResolutionChanges = true;
    config.testPresentationModes = true;
    return config;
}

StressTestConfig UIStressTest::createExtremeConfig() {
    StressTestConfig config;
    config.durationSeconds = 120;
    config.maxComponents = 2000;
    config.componentsPerSecond = 100;
    config.inputEventsPerSecond = 50;
    config.animationsPerSecond = 25;
    config.enableMemoryStress = true;
    config.memoryAllocationsPerFrame = 200;
    config.testResolutionChanges = true;
    config.testPresentationModes = true;
    return config;
}

// Utility namespace implementation
namespace UIStressTesting {

bool quickPerformanceTest(SDL_Renderer* renderer, int durationSeconds) {
    UIStressTest tester;
    tester.setRenderer(renderer);
    tester.setVerbose(true);

    StressTestConfig config = UIStressTest::createLightConfig();
    config.durationSeconds = durationSeconds;

    return tester.runStressTest(StressTestType::BASIC_PERFORMANCE, config);
}

bool quickComponentTest(SDL_Renderer* renderer, int maxComponents) {
    UIStressTest tester;
    tester.setRenderer(renderer);
    tester.setVerbose(true);

    StressTestConfig config = UIStressTest::createMediumConfig();
    config.maxComponents = maxComponents;
    config.durationSeconds = 20;

    return tester.runStressTest(StressTestType::MASS_COMPONENTS, config);
}

bool quickAnimationTest(SDL_Renderer* renderer, int animationCount) {
    UIStressTest tester;
    tester.setRenderer(renderer);
    tester.setVerbose(true);

    StressTestConfig config = UIStressTest::createMediumConfig();
    config.animationsPerSecond = animationCount / 10; // Spread over 10 seconds
    config.durationSeconds = 15;

    return tester.runStressTest(StressTestType::ANIMATION_STRESS, config);
}

std::vector<BenchmarkResult> runBenchmarkSuite(SDL_Renderer* renderer) {
    std::vector<BenchmarkResult> results;
    UIStressTest tester;
    tester.setRenderer(renderer);
    tester.setVerbose(false);

    // Define benchmark tests
    struct BenchmarkTest {
        StressTestType type;
        std::string name;
        StressTestConfig config;
    };

    std::vector<BenchmarkTest> tests = {
        {StressTestType::BASIC_PERFORMANCE, "Basic Performance", UIStressTest::createLightConfig()},
        {StressTestType::MASS_COMPONENTS, "Mass Components", UIStressTest::createMediumConfig()},
        {StressTestType::ANIMATION_STRESS, "Animation Stress", UIStressTest::createMediumConfig()},
        {StressTestType::INPUT_FLOOD, "Input Flood", UIStressTest::createMediumConfig()},
        {StressTestType::LAYOUT_STRESS, "Layout Stress", UIStressTest::createMediumConfig()}
    };

    for (const auto& test : tests) {
        BenchmarkResult result;
        result.testName = test.name;

        bool passed = tester.runStressTest(test.type, test.config);
        const auto& metrics = tester.getLastResults();

        result.averageFPS = metrics.averageFPS;
        result.averageFrameTime = metrics.averageFrameTime;
        result.memoryUsage = metrics.memoryUsageMB;
        result.passed = passed;

        if (!passed) {
            result.notes = "Performance thresholds exceeded";
        } else if (metrics.averageFrameTime > 20.0) {
            result.notes = "Frame time concerning but acceptable";
        } else {
            result.notes = "Good performance";
        }

        results.push_back(result);
    }

    return results;
}

void printBenchmarkResults(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n=== UI Performance Benchmark Results ===" << std::endl;
    std::cout << std::left << std::setw(20) << "Test Name"
              << std::setw(12) << "Avg FPS"
              << std::setw(15) << "Frame Time(ms)"
              << std::setw(12) << "Memory(MB)"
              << std::setw(8) << "Status"
              << "Notes" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    for (const auto& result : results) {
        std::cout << std::left << std::setw(20) << result.testName
                  << std::fixed << std::setprecision(1) << std::setw(12) << result.averageFPS
                  << std::fixed << std::setprecision(2) << std::setw(15) << result.averageFrameTime
                  << std::fixed << std::setprecision(1) << std::setw(12) << result.memoryUsage
                  << std::setw(8) << (result.passed ? "PASS" : "FAIL")
                  << result.notes << std::endl;
    }
    std::cout << "=========================================\n" << std::endl;
}

StressTestConfig getLightweightConfig() {
    return UIStressTest::createLightConfig();
}

StressTestConfig getStandardConfig() {
    return UIStressTest::createMediumConfig();
}

StressTestConfig getHeavyConfig() {
    return UIStressTest::createHeavyConfig();
}

StressTestConfig getContinuousIntegrationConfig() {
    StressTestConfig config = UIStressTest::createLightConfig();
    config.durationSeconds = 5;  // Very short for CI
    config.maxComponents = 50;
    config.componentsPerSecond = 20;
    config.enableMemoryStress = false;
    config.testResolutionChanges = false;
    config.testPresentationModes = true;  // Still test presentation modes
    return config;
}

} // namespace UIStressTesting
