/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef UI_STRESS_TEST_HPP
#define UI_STRESS_TEST_HPP

#include <SDL3/SDL.h>
#include <vector>
#include <chrono>
#include <string>
#include <functional>
#include <random>
#include <boost/container/small_vector.hpp>
#include <boost/container/flat_map.hpp>

// Forward declarations
class UIManager;

// Test result structures
struct PerformanceMetrics {
    double averageFrameTime{0.0};
    double minFrameTime{999999.0};
    double maxFrameTime{0.0};
    double totalTestTime{0.0};
    int totalFrames{0};
    double averageFPS{0.0};
    double memoryUsageMB{0.0};
    double cpuUsagePercent{0.0};

    // UI-specific metrics
    int totalComponents{0};
    int visibleComponents{0};
    int interactiveComponents{0};
    double averageRenderTime{0.0};
    double averageUpdateTime{0.0};
    double averageInputTime{0.0};

    void calculateAverages() {
        if (totalFrames > 0) {
            averageFrameTime = totalTestTime / totalFrames;
            averageFPS = 1000.0 / averageFrameTime; // Convert ms to FPS
        }
    }

    void reset() {
        *this = PerformanceMetrics{};
    }
};

struct StressTestConfig {
    // Test duration and components
    int durationSeconds{30};
    int maxComponents{1000};
    int componentsPerSecond{50};

    // Component distribution (percentages should sum to 100)
    int buttonPercentage{30};
    int labelPercentage{25};
    int panelPercentage{15};
    int progressBarPercentage{10};
    int sliderPercentage{5};
    int checkboxPercentage{5};
    int inputFieldPercentage{5};
    int listPercentage{3};
    int imagePercentage{2};

    // Interaction simulation
    bool simulateMouseInput{true};
    bool simulateKeyboardInput{true};
    int inputEventsPerSecond{20};

    // Animation stress
    bool enableAnimations{true};
    int animationsPerSecond{10};
    double animationDuration{1.0};

    // Memory stress options
    bool enableMemoryStress{false};
    int memoryAllocationsPerFrame{100};

    // Resolution testing
    bool testResolutionChanges{false};
    boost::container::small_vector<std::pair<int, int>, 8> testResolutions{
        {1920, 1080}, {1280, 720}, {1024, 768}, {3440, 1440}, {800, 600}
    };

    // Presentation mode testing
    bool testPresentationModes{true};
    boost::container::small_vector<SDL_RendererLogicalPresentation, 4> testModes{
        SDL_LOGICAL_PRESENTATION_DISABLED,
        SDL_LOGICAL_PRESENTATION_LETTERBOX,
        SDL_LOGICAL_PRESENTATION_STRETCH,
        SDL_LOGICAL_PRESENTATION_OVERSCAN
    };

    // Performance thresholds
    double maxAcceptableFrameTime{16.67}; // 60 FPS = 16.67ms per frame
    double maxAcceptableMemoryMB{500.0};
    double maxAcceptableCPUPercent{80.0};

    void validate() {
        // Ensure percentages are valid
        int total = buttonPercentage + labelPercentage + panelPercentage +
                   progressBarPercentage + sliderPercentage + checkboxPercentage +
                   inputFieldPercentage + listPercentage + imagePercentage;
        if (total != 100) {
            // Auto-normalize percentages
            double factor = 100.0 / total;
            buttonPercentage = static_cast<int>(buttonPercentage * factor);
            labelPercentage = static_cast<int>(labelPercentage * factor);
            panelPercentage = static_cast<int>(panelPercentage * factor);
            progressBarPercentage = static_cast<int>(progressBarPercentage * factor);
            sliderPercentage = static_cast<int>(sliderPercentage * factor);
            checkboxPercentage = static_cast<int>(checkboxPercentage * factor);
            inputFieldPercentage = static_cast<int>(inputFieldPercentage * factor);
            listPercentage = static_cast<int>(listPercentage * factor);
            imagePercentage = static_cast<int>(imagePercentage * factor);
        }
    }
};

enum class StressTestType {
    BASIC_PERFORMANCE,      // Basic component creation and rendering
    MASS_COMPONENTS,        // Create thousands of components
    RAPID_CREATION,         // Rapidly create and destroy components
    ANIMATION_STRESS,       // Many simultaneous animations
    INPUT_FLOOD,           // Flood system with input events
    MEMORY_PRESSURE,       // Test under memory pressure
    RESOLUTION_SCALING,    // Test different resolutions
    PRESENTATION_MODES,    // Test all SDL3 presentation modes
    LAYOUT_STRESS,         // Complex nested layouts
    THEME_SWITCHING,       // Rapid theme changes
    COMPREHENSIVE         // All tests combined
};

class UIStressTest {
public:
    UIStressTest();
    ~UIStressTest();

    // Main test execution
    bool runStressTest(StressTestType testType, const StressTestConfig& config = StressTestConfig{});
    bool runAllTests(const StressTestConfig& config = StressTestConfig{});

    // Individual test methods
    bool testBasicPerformance(const StressTestConfig& config);
    bool testMassComponents(const StressTestConfig& config);
    bool testRapidCreation(const StressTestConfig& config);
    bool testAnimationStress(const StressTestConfig& config);
    bool testInputFlood(const StressTestConfig& config);
    bool testMemoryPressure(const StressTestConfig& config);
    bool testResolutionScaling(const StressTestConfig& config);
    bool testPresentationModes(const StressTestConfig& config);
    bool testLayoutStress(const StressTestConfig& config);
    bool testThemeSwitching(const StressTestConfig& config);

    // Results and reporting
    const PerformanceMetrics& getLastResults() const { return *m_lastResults; }
    const boost::container::flat_map<StressTestType, std::shared_ptr<PerformanceMetrics>>& getAllResults() const { return m_allResults; }

    void printResults(StressTestType testType) const;
    void printAllResults() const;
    void saveResultsToFile(const std::string& filename) const;

    // Configuration
    void setRenderer(SDL_Renderer* renderer) { m_renderer = renderer; }
    void setVerbose(bool verbose) { m_verbose = verbose; }
    void setLogCallback(std::function<void(const std::string&)> callback) { m_logCallback = std::move(callback); }

    // Utility methods
    static StressTestConfig createLightConfig();
    static StressTestConfig createMediumConfig();
    static StressTestConfig createHeavyConfig();
    static StressTestConfig createExtremeConfig();

private:
    // Core test infrastructure
    void initializeTest(const StressTestConfig& config);
    void cleanupTest();
    bool runTestLoop(const StressTestConfig& config, std::function<bool(float)> testLogic);

    // Performance monitoring
    void startPerformanceMonitoring();
    void updatePerformanceMetrics(double frameTime);
    void stopPerformanceMonitoring();
    double getCurrentMemoryUsageMB() const;
    double getCurrentCPUUsagePercent() const;

    // Component generation
    void createRandomComponent(const StressTestConfig& config);
    void createComponentBatch(int count, const StressTestConfig& config);
    void destroyRandomComponents(int count);
    void destroyAllTestComponents();

    // Input simulation
    void simulateMouseInput();
    void simulateKeyboardInput();
    void simulateRandomClick();
    void simulateRandomHover();

    // Animation helpers
    void createRandomAnimation();
    void triggerMassAnimations(int count);

    // Layout testing
    void createNestedLayouts(int depth, int childrenPerLevel);
    void stressTestLayouts();

    // Resolution and presentation testing
    bool testResolution(int width, int height, const StressTestConfig& config);
    bool testPresentationMode(SDL_RendererLogicalPresentation mode, const StressTestConfig& config);

    // Logging and reporting
    void log(const std::string& message) const;
    void logPerformanceWarning(const std::string& warning) const;
    bool checkPerformanceThresholds(const StressTestConfig& config) const;

    // Random number generation
    int randomInt(int min, int max);
    float randomFloat(float min, float max);
    std::string generateRandomString(int length);

    // Component type selection
    enum class ComponentType {
        BUTTON, LABEL, PANEL, PROGRESS_BAR, SLIDER,
        CHECKBOX, INPUT_FIELD, LIST, IMAGE
    };
    ComponentType selectRandomComponentType(const StressTestConfig& config);

    // Data members - matching main project patterns
    SDL_Renderer* m_renderer{nullptr};
    UIManager* m_uiManager{nullptr}; // Use raw pointer like main project singleton pattern

    // Test state - using standard containers like main project for accurate performance testing
    boost::container::small_vector<std::string, 2048> m_testComponentIDs;
    boost::container::small_vector<std::string, 256> m_testLayoutIDs;
    boost::container::small_vector<std::string, 256> m_activeAnimations;

    // Performance tracking - using smart pointers only where main project does
    std::unique_ptr<PerformanceMetrics> m_currentMetrics;
    std::unique_ptr<PerformanceMetrics> m_lastResults;
    boost::container::flat_map<StressTestType, std::shared_ptr<PerformanceMetrics>> m_allResults;

    // Timing - using standard containers for performance accuracy
    std::chrono::steady_clock::time_point m_testStartTime;
    std::chrono::steady_clock::time_point m_lastFrameTime;
    boost::container::small_vector<double, 1000> m_frameTimes;

    // Configuration
    bool m_verbose{false};
    std::function<void(const std::string&)> m_logCallback;

    // Random number generator
    std::mt19937 m_rng;

    // Test control
    bool m_testRunning{false};
    bool m_shouldStop{false};

    // Window/renderer state backup
    struct RendererState {
        int windowWidth{0};
        int windowHeight{0};
        int logicalWidth{0};
        int logicalHeight{0};
        SDL_RendererLogicalPresentation presentation{SDL_LOGICAL_PRESENTATION_DISABLED};
        bool wasLogicalPresentation{false};
    } m_originalState;

    void backupRendererState();
    void restoreRendererState();

    // Statistics tracking
    struct TestStatistics {
        int componentsCreated{0};
        int componentsDestroyed{0};
        int animationsTriggered{0};
        int inputEventsSimulated{0};
        int layoutsCreated{0};
        int themeChanges{0};
        int memoryAllocations{0};

        void reset() { *this = TestStatistics{}; }
    } m_stats;
};

// Utility functions for easy test execution
namespace UIStressTesting {
    // Quick test functions
    bool quickPerformanceTest(SDL_Renderer* renderer, int durationSeconds = 10);
    bool quickComponentTest(SDL_Renderer* renderer, int maxComponents = 500);
    bool quickAnimationTest(SDL_Renderer* renderer, int animationCount = 100);

    // Benchmark comparison
    struct BenchmarkResult {
        std::string testName;
        double averageFPS;
        double averageFrameTime;
        double memoryUsage;
        bool passed;
        std::string notes;
    };

    std::vector<BenchmarkResult> runBenchmarkSuite(SDL_Renderer* renderer);
    void printBenchmarkResults(const std::vector<BenchmarkResult>& results);

    // Configuration presets
    StressTestConfig getLightweightConfig();
    StressTestConfig getStandardConfig();
    StressTestConfig getHeavyConfig();
    StressTestConfig getContinuousIntegrationConfig(); // For automated testing
}

#endif // UI_STRESS_TEST_HPP
