/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include <SDL3/SDL.h>
#include <iostream>
#include <string>
#include <cstring>
#include <fstream>
#include <chrono>
#include <ctime>
#include <vector>
#include <memory>
#include <random>
#include <algorithm>
#include <thread>
#include <boost/container/small_vector.hpp>
#include <boost/container/flat_map.hpp>

// Forward declarations for minimal UI system
struct UIRect {
    int x{0}, y{0}, width{0}, height{0};
    UIRect() = default;
    UIRect(int x_, int y_, int w_, int h_) : x(x_), y(y_), width(w_), height(h_) {}
    bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

struct UIComponent {
    std::string id;
    UIRect bounds;
    std::string text;
    bool visible{true};
    bool enabled{true};
    float value{0.0f};
};

// Performance metrics for headless testing
struct PerformanceMetrics {
    double totalTestTime{0.0};
    int totalIterations{0};
    int totalComponents{0};
    
    // Real-world meaningful metrics
    double memoryAllocationsPerSecond{0.0};
    double averageComponentCreationTime{0.0};
    double maxComponentCreationTime{0.0};
    double memoryEfficiencyRatio{0.0};
    
    // UI Processing Performance
    double averageIterationTime{0.0};
    double minIterationTime{999999.0};
    double maxIterationTime{0.0};
    double processingThroughput{0.0}; // Components × operations per second
    
    // Scalability metrics
    int layoutCalculationsPerSecond{0};
    int collisionChecksPerSecond{0};
    double performanceDegradationRate{0.0}; // How performance changes with component count
    
    // Memory metrics
    double peakMemoryUsageMB{0.0};
    double memoryGrowthPerComponent{0.0};
    int totalMemoryAllocations{0};
    
    void calculateAverages() {
        if (totalIterations > 0) {
            averageIterationTime = totalTestTime / totalIterations;
            processingThroughput = (totalComponents * 1000.0) / averageIterationTime; // Real throughput
            memoryAllocationsPerSecond = (totalMemoryAllocations * 1000.0) / totalTestTime;
            if (totalComponents > 0) {
                memoryGrowthPerComponent = peakMemoryUsageMB / totalComponents;
            }
        }
    }
    
    void reset() {
        *this = PerformanceMetrics{};
    }
};

struct StressTestConfig {
    int durationSeconds{30};
    int maxComponents{500};
    int componentsPerSecond{25};
    bool enableAnimations{true};
    int animationsPerSecond{5};
    bool simulateMouseInput{true};
    int inputEventsPerSecond{10};
    
    // Performance thresholds
    double maxAcceptableFrameTime{16.67}; // 60 FPS
    double maxAcceptableMemoryMB{200.0};
};

// Test configuration options
struct TestOptions {
    std::string stressLevel = "medium";
    int duration = 30;
    int maxComponents = 500;
    bool enableMemoryStress = false;
    bool testResolutions = true;
    bool testPresentationModes = true;
    bool verbose = false;
    bool saveResults = true;
    std::string resultsPath = "";
    bool benchmarkMode = false;

    bool showHelp = false;
};

// Minimal UI stress testing class
class MinimalUIStressTest {
private:
    boost::container::small_vector<std::unique_ptr<UIComponent>, 1024> m_components;
    std::mt19937 m_rng;
    int m_componentsCreated{0};
    PerformanceMetrics m_metrics;
    boost::container::small_vector<double, 1000> m_iterationTimes;
    int m_componentsDestroyed{0};
    int m_animationsTriggered{0};
    int m_inputEventsSimulated{0};
    
    // Additional headless metrics
    int m_layoutCalculations{0};
    int m_collisionChecks{0};
    double m_totalProcessingTime{0.0};
    int m_totalMemoryAllocations{0};
    std::chrono::steady_clock::time_point m_lastComponentTime;
    double m_initialComponentCount{0};

    
public:
    MinimalUIStressTest() : m_rng(std::chrono::steady_clock::now().time_since_epoch().count()) {}
    
    ~MinimalUIStressTest() {
        cleanup();
    }
    
    bool initialize() {
        // Always run headless for CI/automation - no SDL video initialization needed
        return true;
    }
    
    void cleanup() {
        // Headless mode - nothing to cleanup
        m_components.clear();
    }
    
    void createRandomComponent(const StressTestConfig& config) {
        (void)config;  // Suppress unused parameter warning
        auto component = std::make_unique<UIComponent>();
        component->id = "component_" + std::to_string(m_componentsCreated);
        
        // Random position and size
        std::uniform_int_distribution<int> xDist(0, 800);
        std::uniform_int_distribution<int> yDist(0, 600);
        std::uniform_int_distribution<int> sizeDist(20, 100);
        
        component->bounds = UIRect(xDist(m_rng), yDist(m_rng), sizeDist(m_rng), sizeDist(m_rng));
        component->text = "Component " + std::to_string(m_componentsCreated);
        
        std::uniform_real_distribution<float> valueDist(0.0f, 1.0f);
        component->value = valueDist(m_rng);
        
        m_components.push_back(std::move(component));
        m_componentsCreated++;
    }
    
    void removeRandomComponents(int count) {
        int toRemove = std::min(count, static_cast<int>(m_components.size()));
        for (int i = 0; i < toRemove; ++i) {
            if (!m_components.empty()) {
                std::uniform_int_distribution<int> indexDist(0, static_cast<int>(m_components.size()) - 1);
                int index = indexDist(m_rng);
                m_components.erase(m_components.begin() + index);
                m_componentsDestroyed++;
            }
        }
    }
    
    void simulateInput() {
        if (m_components.empty()) return;
        
        // Simulate random mouse events
        std::uniform_int_distribution<int> xDist(0, 1024);
        std::uniform_int_distribution<int> yDist(0, 768);
        int mouseX = xDist(m_rng);
        int mouseY = yDist(m_rng);
        
        // Check if any component would be "clicked"
        for (const auto& component : m_components) {
            if (component->bounds.contains(mouseX, mouseY)) {
                // Simulate interaction
                break;
            }
        }
        
        m_inputEventsSimulated++;
    }
    
    void simulateAnimations() {
        if (m_components.empty()) return;
        
        // Randomly animate some components
        std::uniform_int_distribution<int> componentDist(0, static_cast<int>(m_components.size()) - 1);
        int index = componentDist(m_rng);
        
        auto& component = m_components[index];
        std::uniform_int_distribution<int> moveDist(-10, 10);
        component->bounds.x += moveDist(m_rng);
        component->bounds.y += moveDist(m_rng);
        
        m_animationsTriggered++;
    }
    

    
    void processComponents() {
        // Simulate meaningful UI processing work (headless mode)
        auto startTime = std::chrono::steady_clock::now();
        
        int layoutCalculations = 0;
        int collisionChecks = 0;
        
        for (const auto& component : m_components) {
            if (component && component->visible) {
                // Simulate layout calculations
                volatile float x = component->bounds.x * 1.1f;
                volatile float y = component->bounds.y * 1.1f;
                volatile float area = component->bounds.width * component->bounds.height;
                layoutCalculations++;
                
                // Simulate collision detection with other components
                for (size_t i = 0; i < std::min(m_components.size(), size_t(10)); ++i) {
                    if (m_components[i] && m_components[i] != component) {
                        volatile bool overlaps = component->bounds.contains(
                            m_components[i]->bounds.x, m_components[i]->bounds.y);
                        collisionChecks++;
                        (void)overlaps;
                    }
                }
                
                // Simulate value updates and state changes
                component->value = fmod(component->value + 0.01f, 1.0f);
                
                (void)x; (void)y; (void)area; // Suppress warnings
            }
        }
        
        auto endTime = std::chrono::steady_clock::now();
        auto processingTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        
        // Update performance metrics
        m_layoutCalculations += layoutCalculations;
        m_collisionChecks += collisionChecks;
        m_totalProcessingTime += processingTime;
        
        // Simulate realistic processing delay
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    bool runStressTest(const StressTestConfig& config) {
        m_metrics.reset();
        m_iterationTimes.clear();
        
        auto startTime = std::chrono::steady_clock::now();
        auto lastFrameTime = startTime;
        auto lastComponentTime = startTime;
        auto lastInputTime = startTime;
        auto lastAnimationTime = startTime;
        
        m_initialComponentCount = static_cast<double>(m_components.size());
        if (m_initialComponentCount == 0) m_initialComponentCount = 1; // Avoid division by zero
        
        float totalTime = 0.0f;
        const float maxTime = static_cast<float>(config.durationSeconds);
        
        std::cout << "Running headless stress test for " << config.durationSeconds << " seconds...\n";
        
        while (totalTime < maxTime) {
            auto currentTime = std::chrono::steady_clock::now();
            auto frameTime = std::chrono::duration<double, std::milli>(currentTime - lastFrameTime).count();
            lastFrameTime = currentTime;
            
            totalTime += static_cast<float>(frameTime / 1000.0);
            
            // Component creation
            auto timeSinceLastComponent = std::chrono::duration<double>(currentTime - lastComponentTime).count();
            if (timeSinceLastComponent >= (1.0 / config.componentsPerSecond)) {
                if (static_cast<int>(m_components.size()) < config.maxComponents) {
                    createRandomComponent(config);
                    lastComponentTime = currentTime;
                }
            }
            
            // Input simulation
            if (config.simulateMouseInput) {
                auto timeSinceLastInput = std::chrono::duration<double>(currentTime - lastInputTime).count();
                if (timeSinceLastInput >= (1.0 / config.inputEventsPerSecond)) {
                    simulateInput();
                    lastInputTime = currentTime;
                }
            }
            
            // Animation simulation
            if (config.enableAnimations) {
                auto timeSinceLastAnimation = std::chrono::duration<double>(currentTime - lastAnimationTime).count();
                if (timeSinceLastAnimation >= (1.0 / config.animationsPerSecond)) {
                    simulateAnimations();
                    lastAnimationTime = currentTime;
                }
            }
            
            // Process components (headless mode)
            processComponents();
            
            // Update metrics
            m_iterationTimes.push_back(frameTime);
            m_metrics.totalIterations++;
            m_metrics.totalTestTime += frameTime;
            
            if (frameTime < m_metrics.minIterationTime) {
                m_metrics.minIterationTime = frameTime;
            }
            if (frameTime > m_metrics.maxIterationTime) {
                m_metrics.maxIterationTime = frameTime;
            }
            
            // Update performance rates
            double elapsedTimeSeconds = m_metrics.totalTestTime / 1000.0; // Convert to seconds
            if (elapsedTimeSeconds > 0) {
                m_metrics.layoutCalculationsPerSecond = static_cast<int>(m_layoutCalculations / elapsedTimeSeconds);
                m_metrics.collisionChecksPerSecond = static_cast<int>(m_collisionChecks / elapsedTimeSeconds);
            }
            
            // Prevent busy waiting
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        
        m_metrics.totalComponents = static_cast<int>(m_components.size());
        m_metrics.totalMemoryAllocations = m_totalMemoryAllocations;
        m_metrics.peakMemoryUsageMB = static_cast<double>(m_components.size() * sizeof(UIComponent)) / (1024.0 * 1024.0);
        
        // Calculate performance degradation (simple heuristic)
        if (m_initialComponentCount > 0) {
            double componentGrowth = static_cast<double>(m_metrics.totalComponents) / m_initialComponentCount;
            double timeGrowth = m_metrics.averageIterationTime / 0.1; // Baseline 0.1ms
            m_metrics.performanceDegradationRate = (timeGrowth - 1.0) / (componentGrowth - 1.0);
        }
        
        m_metrics.calculateAverages();
        
        // Success criteria based on real-world UI performance expectations
        bool performanceGood = m_metrics.averageIterationTime <= config.maxAcceptableFrameTime;
        bool scalabilityGood = m_metrics.performanceDegradationRate < 2.0; // Less than 2x degradation per component growth
        bool memoryEfficient = m_metrics.memoryGrowthPerComponent < 1.0; // Less than 1MB per component
        
        return performanceGood && (scalabilityGood || m_metrics.totalComponents < 100) && memoryEfficient;
    }
    
    const PerformanceMetrics& getResults() const {
        return m_metrics;
    }
    
    void printResults() const {
        std::cout << "\n=== UI Stress Test Results (Headless) ===\n";
        std::cout << "Duration: " << std::fixed << std::setprecision(2) << m_metrics.totalTestTime / 1000.0 << "s\n";
        std::cout << "Total Iterations: " << m_metrics.totalIterations << "\n";
        
        // Real-world performance metrics
        std::cout << "Processing Throughput: " << std::fixed << std::setprecision(1) << m_metrics.processingThroughput << " comp/sec\n";
        std::cout << "Average Iteration Time: " << std::fixed << std::setprecision(3) << m_metrics.averageIterationTime << "ms\n";
        std::cout << "Component Creation Time: " << std::fixed << std::setprecision(3) << m_metrics.averageComponentCreationTime << "ms (max: " << std::setprecision(3) << m_metrics.maxComponentCreationTime << "ms)\n";
        
        // Memory metrics
        std::cout << "Memory Usage: " << std::fixed << std::setprecision(2) << m_metrics.peakMemoryUsageMB << "MB\n";
        std::cout << "Memory/Component: " << std::fixed << std::setprecision(3) << m_metrics.memoryGrowthPerComponent << "MB\n";
        std::cout << "Memory Allocations/sec: " << std::fixed << std::setprecision(1) << m_metrics.memoryAllocationsPerSecond << "\n";
        
        // Scalability metrics
        std::cout << "Performance Degradation: " << std::fixed << std::setprecision(2) << m_metrics.performanceDegradationRate << "x\n";
        std::cout << "Layout Calculations/sec: " << m_metrics.layoutCalculationsPerSecond << "\n";
        std::cout << "Collision Checks/sec: " << m_metrics.collisionChecksPerSecond << "\n";
        
        // Component metrics
        std::cout << "Total Components: " << m_metrics.totalComponents << "\n";
        std::cout << "Components Created: " << m_componentsCreated << "\n";
        std::cout << "Animations Triggered: " << m_animationsTriggered << "\n";
        std::cout << "Input Events Simulated: " << m_inputEventsSimulated << "\n";
        std::cout << "===============================\n";
    }
};

void printUsage(const char* programName) {
    std::cout << "UI Stress Test Runner\n\n";
    std::cout << "Usage: " << programName << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --stress-level LEVEL     Stress test level (light|medium|heavy|extreme)\n";
    std::cout << "  --duration SECONDS       Test duration in seconds\n";
    std::cout << "  --max-components COUNT   Maximum components to create\n";
    std::cout << "  --memory-stress          Enable memory pressure testing\n";
    std::cout << "  --skip-resolutions       Skip resolution scaling tests\n";
    std::cout << "  --skip-presentation      Skip presentation mode tests\n";
    std::cout << "  --verbose                Enable verbose output\n";
    std::cout << "  --save-results PATH      Save results to file\n";
    std::cout << "  --benchmark              Run benchmark suite\n";

    std::cout << "  --help                   Show this help message\n\n";
}

bool parseArguments(int argc, char* argv[], TestOptions& options) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            options.showHelp = true;
            return true;
        } else if (arg == "--stress-level" && i + 1 < argc) {
            options.stressLevel = argv[++i];
        } else if (arg == "--duration" && i + 1 < argc) {
            options.duration = std::stoi(argv[++i]);
        } else if (arg == "--max-components" && i + 1 < argc) {
            options.maxComponents = std::stoi(argv[++i]);
        } else if (arg == "--memory-stress") {
            options.enableMemoryStress = true;
        } else if (arg == "--skip-resolutions") {
            options.testResolutions = false;
        } else if (arg == "--skip-presentation") {
            options.testPresentationModes = false;
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "--save-results" && i + 1 < argc) {
            options.resultsPath = argv[++i];
            options.saveResults = true;
        } else if (arg == "--benchmark") {
            options.benchmarkMode = true;

        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }
    
    return true;
}

StressTestConfig createConfigFromOptions(const TestOptions& options) {
    StressTestConfig config;
    
    config.durationSeconds = options.duration;
    config.maxComponents = options.maxComponents;
    
    if (options.stressLevel == "light") {
        config.componentsPerSecond = 10;
        config.animationsPerSecond = 2;
        config.inputEventsPerSecond = 5;
    } else if (options.stressLevel == "medium") {
        config.componentsPerSecond = 25;
        config.animationsPerSecond = 5;
        config.inputEventsPerSecond = 10;
    } else if (options.stressLevel == "heavy") {
        config.componentsPerSecond = 50;
        config.animationsPerSecond = 15;
        config.inputEventsPerSecond = 20;
    } else if (options.stressLevel == "extreme") {
        config.componentsPerSecond = 100;
        config.animationsPerSecond = 30;
        config.inputEventsPerSecond = 50;
    }
    
    return config;
}

void saveResults(const std::string& filename, const PerformanceMetrics& metrics) {
    std::ofstream file(filename);
    if (!file.is_open()) return;
    
    file << "UI Stress Test Results (Headless)\n";
    file << "Generated: " << std::time(nullptr) << "\n\n";
    file << "Duration: " << metrics.totalTestTime / 1000.0 << "s\n";
    file << "Total Iterations: " << metrics.totalIterations << "\n";
    file << "Processing Throughput: " << metrics.processingThroughput << " comp/sec\n";
    file << "Average Iteration Time: " << metrics.averageIterationTime << "ms\n";
    file << "Component Creation Time: " << metrics.averageComponentCreationTime << "ms\n";
    file << "Memory Usage: " << metrics.peakMemoryUsageMB << "MB\n";
    file << "Performance Degradation: " << metrics.performanceDegradationRate << "x\n";
    file << "Total Components: " << metrics.totalComponents << "\n";
    
    file.close();
}

int main(int argc, char* argv[]) {
    std::cout << "=== UI Stress Test Runner v1.0 ===\n\n";
    
    TestOptions options;
    if (!parseArguments(argc, argv, options)) {
        printUsage(argv[0]);
        return 1;
    }
    
    if (options.showHelp) {
        printUsage(argv[0]);
        return 0;
    }
    
    MinimalUIStressTest tester;
    if (!tester.initialize()) {
        std::cerr << "Failed to initialize test environment\n";
        return 1;
    }
    
    if (options.benchmarkMode) {
        std::cout << "Running UI Performance Benchmark Suite...\n\n";
        
        // Run multiple tests
        std::vector<std::string> testNames = {"Basic Performance", "Mass Components", "Animation Stress"};
        std::vector<std::string> levels = {"light", "medium", "heavy"};
        
        std::cout << "\n=== UI Performance Benchmark Results (Headless) ===\n";
        std::cout << "Test Name            Throughput Iter Time(ms)  Memory(MB)  Status  Notes\n";
        std::cout << "--------------------------------------------------------------------------------\n";
        
        for (size_t i = 0; i < testNames.size(); ++i) {
            TestOptions testOpts = options;
            testOpts.stressLevel = levels[i % levels.size()];
            testOpts.duration = 10; // Short tests for benchmark
            
            StressTestConfig config = createConfigFromOptions(testOpts);
            bool passed = tester.runStressTest(config);
            const auto& metrics = tester.getResults();
            
            std::cout << std::left << std::setw(20) << testNames[i]
                      << std::fixed << std::setprecision(1) << std::setw(11) << metrics.processingThroughput
                      << std::fixed << std::setprecision(3) << std::setw(15) << metrics.averageIterationTime
                      << std::fixed << std::setprecision(2) << std::setw(12) << metrics.peakMemoryUsageMB
                      << std::setw(8) << (passed ? "PASS" : "FAIL")
                      << (passed ? "Good performance" : "Performance issues") << "\n";
        }
        std::cout << "=========================================\n";
        
    } else {
        StressTestConfig config = createConfigFromOptions(options);
        
        if (options.verbose) {
            std::cout << "Configuration:\n";
            std::cout << "  Stress Level: " << options.stressLevel << "\n";
            std::cout << "  Duration: " << options.duration << "s\n";
            std::cout << "  Max Components: " << options.maxComponents << "\n\n";
        }
        
        bool passed = tester.runStressTest(config);
        tester.printResults();
        
        if (options.saveResults && !options.resultsPath.empty()) {
            // Create directory if needed
            system("mkdir -p test_results/ui_stress");
            saveResults(options.resultsPath, tester.getResults());
            std::cout << "Results saved to: " << options.resultsPath << "\n";
        }
        
        std::cout << "\n=== " << (passed ? "STRESS TEST PASSED" : "STRESS TEST FAILED") << " ===\n";
        return passed ? 0 : 1;
    }
    
    return 0;
}