/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef UI_STRESS_TEST_STATE_HPP
#define UI_STRESS_TEST_STATE_HPP

#include "gameStates/GameState.hpp"
#include "../../tests/ui/UIStressTest.hpp"
#include <memory>
#include <chrono>
#include <string>
#include <vector>
#include <future>

// Forward declarations
class UIStressTest;

class UIStressTestState : public GameState {
public:
    UIStressTestState();
    virtual ~UIStressTestState();

    // GameState interface
    bool enter() override;
    void update(float deltaTime) override;
    void render() override;
    bool exit() override;

    // Test configuration
    void setStressLevel(const std::string& level) { m_stressLevel = level; }
    void setTestDuration(int seconds) { m_testDuration = seconds; }
    void setMaxComponents(int count) { m_maxComponents = count; }
    void enableMemoryStress(bool enable) { m_enableMemoryStress = enable; }
    void enableResolutionTesting(bool enable) { m_testResolutions = enable; }
    void enablePresentationModeTesting(bool enable) { m_testPresentationModes = enable; }
    void enableVerboseOutput(bool enable) { m_verbose = enable; }
    void setSaveResults(bool save) { m_saveResults = save; }
    void setResultsPath(const std::string& path) { m_resultsPath = path; }
    void setBenchmarkMode(bool benchmark) { m_benchmarkMode = benchmark; }

    // Test control
    void startTests();
    void stopTests();
    bool isTestRunning() const { return m_testRunning; }
    bool areTestsComplete() const { return m_testsComplete; }
    bool didTestsPass() const { return m_testsPassed; }

    // Results access
    const PerformanceMetrics& getLastResults() const;
    void printResults() const;
    void saveResultsToFile() const;

private:
    // Test execution
    void initializeTestUI();
    void updateTestProgress(float deltaTime);
    void renderTestUI();
    void cleanupTestUI();
    
    // Test management
    void runStressTestSuite();
    void runBenchmarkSuite();
    StressTestConfig createConfigFromSettings();
    
    // UI components for test display
    void createTestStatusPanel();
    void createProgressIndicators();
    void createResultsDisplay();
    void updateProgressBars();
    void updateStatusText();
    void showTestResults();
    
    // Event handling
    void handleTestComplete(bool success);
    void handleTestError(const std::string& error);
    
    // Logging
    void logMessage(const std::string& message);
    void logTestProgress(const std::string& progress);
    
    // Test instance and configuration
    std::unique_ptr<UIStressTest> m_stressTest;
    std::unique_ptr<StressTestConfig> m_testConfig;
    
    // Configuration options
    std::string m_stressLevel{"medium"};
    int m_testDuration{30};
    int m_maxComponents{500};
    bool m_enableMemoryStress{false};
    bool m_testResolutions{true};
    bool m_testPresentationModes{true};
    bool m_verbose{false};
    bool m_saveResults{true};
    std::string m_resultsPath{};
    bool m_benchmarkMode{false};
    
    // Test state
    bool m_testRunning{false};
    bool m_testsComplete{false};
    bool m_testsPassed{false};
    bool m_testInitialized{false};
    
    // Test execution
    std::chrono::steady_clock::time_point m_testStartTime;
    float m_testElapsedTime{0.0f};
    float m_testProgress{0.0f};
    std::string m_currentTestName{};
    std::string m_testStatusMessage{};
    
    // Test results
    std::unique_ptr<PerformanceMetrics> m_lastTestResults;
    std::vector<std::pair<StressTestType, std::shared_ptr<PerformanceMetrics>>> m_allTestResults;
    
    // UI components for test display
    struct TestUI {
        // Main panels
        std::string mainPanel{"stress_test_main_panel"};
        std::string headerPanel{"stress_test_header"};
        std::string progressPanel{"stress_test_progress"};
        std::string resultsPanel{"stress_test_results"};
        std::string controlPanel{"stress_test_controls"};
        
        // Labels and text
        std::string titleLabel{"stress_test_title"};
        std::string statusLabel{"stress_test_status"};
        std::string progressLabel{"stress_test_progress_label"};
        std::string testNameLabel{"stress_test_name"};
        std::string timeLabel{"stress_test_time"};
        std::string fpsLabel{"stress_test_fps"};
        std::string memoryLabel{"stress_test_memory"};
        std::string componentLabel{"stress_test_components"};
        
        // Progress bars
        std::string overallProgress{"stress_test_overall_progress"};
        std::string testProgress{"stress_test_current_progress"};
        std::string fpsProgress{"stress_test_fps_progress"};
        std::string memoryProgress{"stress_test_memory_progress"};
        
        // Control buttons
        std::string startButton{"stress_test_start"};
        std::string stopButton{"stress_test_stop"};
        std::string backButton{"stress_test_back"};
        std::string saveButton{"stress_test_save"};
        
        // Results display
        std::string resultsList{"stress_test_results_list"};
        std::string summaryLabel{"stress_test_summary"};
        
        // Configuration display
        std::string configLabel{"stress_test_config"};
        
        bool created{false};
    } m_testUI;
    
    // Test execution thread (if using async execution)
    std::shared_ptr<std::future<bool>> m_testFuture;
    bool m_asyncExecution{false};
    
    // Performance monitoring during UI display
    struct UIPerformance {
        double averageFrameTime{0.0};
        double currentFPS{0.0};
        double memoryUsage{0.0};
        int componentCount{0};
        
        void update(double frameTime, double fps, double memory, int components) {
            averageFrameTime = averageFrameTime * 0.95 + frameTime * 0.05; // Moving average
            currentFPS = fps;
            memoryUsage = memory;
            componentCount = components;
        }
    };
    std::unique_ptr<UIPerformance> m_uiPerformance;
    
    // Test sequence management
    enum class TestSequence {
        NONE,
        BASIC_PERFORMANCE,
        MASS_COMPONENTS,
        RAPID_CREATION,
        ANIMATION_STRESS,
        INPUT_FLOOD,
        LAYOUT_STRESS,
        THEME_SWITCHING,
        MEMORY_PRESSURE,
        RESOLUTION_SCALING,
        PRESENTATION_MODES,
        COMPLETE
    };
    
    TestSequence m_currentSequence{TestSequence::NONE};
    std::shared_ptr<std::vector<TestSequence>> m_testSequences;
    size_t m_currentSequenceIndex{0};
    
    // Helper methods
    void setupTestSequence();
    void advanceToNextTest();
    std::string getSequenceName(TestSequence sequence) const;
    StressTestType getTestTypeForSequence(TestSequence sequence) const;
    
    // Constants
    static constexpr float UPDATE_INTERVAL = 0.1f; // Update UI 10 times per second
    float m_updateTimer{0.0f};
    
    static constexpr int WINDOW_WIDTH = 1024;
    static constexpr int WINDOW_HEIGHT = 768;
    static constexpr int PANEL_MARGIN = 20;
    static constexpr int CONTROL_HEIGHT = 40;
    static constexpr int PROGRESS_HEIGHT = 20;
};

#endif // UI_STRESS_TEST_STATE_HPP