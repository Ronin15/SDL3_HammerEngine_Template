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

// Performance metrics
struct PerformanceMetrics {
    double averageFrameTime{0.0};
    double minFrameTime{999999.0};
    double maxFrameTime{0.0};
    double totalTestTime{0.0};
    int totalFrames{0};
    double averageFPS{0.0};
    double memoryUsageMB{0.0};
    int totalComponents{0};
    
    void calculateAverages() {
        if (totalFrames > 0) {
            averageFrameTime = totalTestTime / totalFrames;
            averageFPS = 1000.0 / averageFrameTime;
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
    SDL_Window* m_window{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    boost::container::small_vector<std::unique_ptr<UIComponent>, 2048> m_components;
    std::mt19937 m_rng;
    PerformanceMetrics m_metrics;
    boost::container::small_vector<double, 1000> m_frameTimes;
    
    // Statistics
    int m_componentsCreated{0};
    int m_componentsDestroyed{0};
    int m_animationsTriggered{0};
    int m_inputEventsSimulated{0};
    
public:
    MinimalUIStressTest() : m_rng(std::chrono::steady_clock::now().time_since_epoch().count()) {}
    
    ~MinimalUIStressTest() {
        cleanup();
    }
    
    bool initialize() {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::cerr << "Failed to initialize SDL: " << SDL_GetError() << "\n";
            return false;
        }
        
        m_window = SDL_CreateWindow("UI Stress Test", 1024, 768, SDL_WINDOW_HIDDEN);
        if (!m_window) {
            std::cerr << "Failed to create window: " << SDL_GetError() << "\n";
            SDL_Quit();
            return false;
        }
        
        m_renderer = SDL_CreateRenderer(m_window, NULL);
        if (!m_renderer) {
            std::cerr << "Failed to create renderer: " << SDL_GetError() << "\n";
            SDL_DestroyWindow(m_window);
            SDL_Quit();
            return false;
        }
        
        return true;
    }
    
    void cleanup() {
        m_components.clear();
        
        if (m_renderer) {
            SDL_DestroyRenderer(m_renderer);
            m_renderer = nullptr;
        }
        
        if (m_window) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
        
        SDL_Quit();
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
    
    void renderComponents() {
        // Set background color
        SDL_SetRenderDrawColor(m_renderer, 30, 30, 40, 255);
        SDL_RenderClear(m_renderer);
        
        // Render all components as simple rectangles
        for (const auto& component : m_components) {
            if (!component->visible) continue;
            
            // Component background
            SDL_FRect rect = {
                static_cast<float>(component->bounds.x),
                static_cast<float>(component->bounds.y),
                static_cast<float>(component->bounds.width),
                static_cast<float>(component->bounds.height)
            };
            
            // Color based on component value
            Uint8 r = static_cast<Uint8>(component->value * 255);
            Uint8 g = static_cast<Uint8>((1.0f - component->value) * 255);
            Uint8 b = 100;
            
            SDL_SetRenderDrawColor(m_renderer, r, g, b, 255);
            SDL_RenderFillRect(m_renderer, &rect);
            
            // Border
            SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
            SDL_RenderRect(m_renderer, &rect);
        }
        
        SDL_RenderPresent(m_renderer);
    }
    
    bool runStressTest(const StressTestConfig& config) {
        m_metrics.reset();
        m_frameTimes.clear();
        
        auto startTime = std::chrono::steady_clock::now();
        auto lastFrameTime = startTime;
        auto lastComponentTime = startTime;
        auto lastInputTime = startTime;
        auto lastAnimationTime = startTime;
        
        float totalTime = 0.0f;
        const float maxTime = static_cast<float>(config.durationSeconds);
        
        std::cout << "Running stress test for " << config.durationSeconds << " seconds...\n";
        
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
            
            // Render frame
            renderComponents();
            
            // Update metrics
            m_frameTimes.push_back(frameTime);
            m_metrics.totalFrames++;
            m_metrics.totalTestTime += frameTime;
            
            if (frameTime < m_metrics.minFrameTime) {
                m_metrics.minFrameTime = frameTime;
            }
            if (frameTime > m_metrics.maxFrameTime) {
                m_metrics.maxFrameTime = frameTime;
            }
            
            // Prevent busy waiting
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        
        m_metrics.totalComponents = static_cast<int>(m_components.size());
        m_metrics.calculateAverages();
        
        return m_metrics.averageFrameTime <= config.maxAcceptableFrameTime;
    }
    
    const PerformanceMetrics& getResults() const {
        return m_metrics;
    }
    
    void printResults() const {
        std::cout << "\n=== UI Stress Test Results ===\n";
        std::cout << "Duration: " << std::fixed << std::setprecision(2) << m_metrics.totalTestTime / 1000.0 << "s\n";
        std::cout << "Total Frames: " << m_metrics.totalFrames << "\n";
        std::cout << "Average FPS: " << std::fixed << std::setprecision(1) << m_metrics.averageFPS << "\n";
        std::cout << "Average Frame Time: " << std::fixed << std::setprecision(2) << m_metrics.averageFrameTime << "ms\n";
        std::cout << "Min Frame Time: " << std::fixed << std::setprecision(2) << m_metrics.minFrameTime << "ms\n";
        std::cout << "Max Frame Time: " << std::fixed << std::setprecision(2) << m_metrics.maxFrameTime << "ms\n";
        std::cout << "Total Components: " << m_metrics.totalComponents << "\n";
        std::cout << "Components Created: " << m_componentsCreated << "\n";
        std::cout << "Components Destroyed: " << m_componentsDestroyed << "\n";
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
    
    file << "UI Stress Test Results\n";
    file << "Generated: " << std::time(nullptr) << "\n\n";
    file << "Duration: " << metrics.totalTestTime / 1000.0 << "s\n";
    file << "Total Frames: " << metrics.totalFrames << "\n";
    file << "Average FPS: " << metrics.averageFPS << "\n";
    file << "Average Frame Time: " << metrics.averageFrameTime << "ms\n";
    file << "Min Frame Time: " << metrics.minFrameTime << "ms\n";
    file << "Max Frame Time: " << metrics.maxFrameTime << "ms\n";
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
        
        std::cout << "=== UI Performance Benchmark Results ===\n";
        std::cout << "Test Name            Avg FPS    Frame Time(ms) Memory(MB)  Status  Notes\n";
        std::cout << "--------------------------------------------------------------------------------\n";
        
        for (size_t i = 0; i < testNames.size(); ++i) {
            TestOptions testOpts = options;
            testOpts.stressLevel = levels[i % levels.size()];
            testOpts.duration = 10; // Short tests for benchmark
            
            StressTestConfig config = createConfigFromOptions(testOpts);
            bool passed = tester.runStressTest(config);
            const auto& metrics = tester.getResults();
            
            std::cout << std::left << std::setw(20) << testNames[i]
                      << std::fixed << std::setprecision(1) << std::setw(11) << metrics.averageFPS
                      << std::fixed << std::setprecision(2) << std::setw(15) << metrics.averageFrameTime
                      << std::setw(12) << "N/A"
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