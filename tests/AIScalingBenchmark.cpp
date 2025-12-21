/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE AIScalingBenchmark
#include <boost/test/unit_test.hpp>

#include "core/Logger.hpp"
#include <iostream>
#include <chrono>
#include <atomic>
#include <vector>
#include <memory>
#include <iomanip>
#include <random>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <thread>

#include "managers/AIManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/CollisionManager.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"

// Production AI behaviors for integrated testing
#include "ai/behaviors/WanderBehavior.hpp"
#include "ai/behaviors/ChaseBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/GuardBehavior.hpp"
#include "ai/behaviors/FollowBehavior.hpp"

// Forward declaration
namespace HammerEngine {
    class Camera;
}

// Global state to track initialization status
namespace {
    std::mutex g_setupMutex;
    std::atomic<bool> g_systemsInitialized{false};
    std::atomic<bool> g_shutdownInProgress{false};

    /**
     * Calculate scaled iteration count for reliable benchmark measurements.
     *
     * PROBLEM: Low entity count benchmarks (150-200 entities) with fixed iterations
     * complete in ~3-5ms, which is dominated by system noise (scheduler jitter, cache
     * effects). This causes 20-50% variance in measurements.
     *
     * SOLUTION: Scale iterations inversely with entity count to achieve a minimum
     * measurement duration of ~100ms, reducing noise impact to <2%.
     *
     * @param numEntities Number of entities in the benchmark
     * @param baseIterations Minimum iterations (default 20 for high entity counts)
     * @return Scaled iteration count ensuring ~100ms minimum measurement duration
     */
    int calculateScaledIterations(int numEntities, int baseIterations = 20) {
        // Target ~100,000 entity-updates per measurement for ~100ms duration
        // At 150 entities: 100000/150 = 667 iterations
        // At 200 entities: 100000/200 = 500 iterations
        // At 1000 entities: 100000/1000 = 100 iterations
        // At 5000+ entities: baseIterations (20) is sufficient
        constexpr int TARGET_ENTITY_UPDATES = 100000;
        int scaledIterations = TARGET_ENTITY_UPDATES / std::max(1, numEntities);
        return std::max(baseIterations, scaledIterations);
    }
}

// Simple test entity for benchmarking
class BenchmarkEntity : public Entity {
public:
    BenchmarkEntity(int id, const Vector2D& pos) : m_id(id) {
        setPosition(pos);
        setTextureID("benchmark_texture");
        setWidth(32);
        setHeight(32);
    }

    static std::shared_ptr<BenchmarkEntity> create(int id, const Vector2D& pos) {
        return std::make_shared<BenchmarkEntity>(id, pos);
    }

    void update(float deltaTime) override {
        m_updateCount++;
        (void)deltaTime; // Suppress unused parameter warning
    }
    void render(SDL_Renderer* renderer, float cameraX, float cameraY, float interpolationAlpha = 1.0f) override { (void)renderer; (void)cameraX; (void)cameraY; (void)interpolationAlpha; }
    void clean() override {}

    void updatePosition(float dx, float dy) {
        Vector2D pos = getPosition();
        pos.setX(pos.getX() + dx);
        pos.setY(pos.getY() + dy);
        setPosition(pos);
        m_updateCount++;
    }

    int getId() const { return m_id; }
    int getUpdateCount() const { return m_updateCount; }
    void resetUpdateCount() { m_updateCount.store(0, std::memory_order_release); }

    // Make update count public for benchmark access
    std::atomic<int> m_updateCount{0};

private:
    int m_id;
};

// Simple test behavior for benchmarking
class BenchmarkBehavior : public AIBehavior {
public:
    BenchmarkBehavior(int id, int complexity = 5)
        : m_id(id), m_complexity(complexity), m_initialized(false) {
    }

    void executeLogic(EntityPtr entity, [[maybe_unused]] float deltaTime) override {
        if (!entity) return;

        auto benchmarkEntity = std::dynamic_pointer_cast<BenchmarkEntity>(entity);

        // DEBUG: Log first few executions
        static std::atomic<int> callCount{0};
        int currentCall = callCount.fetch_add(1, std::memory_order_relaxed);
        if (currentCall < 5) {
            std::cout << "[DEBUG] BenchmarkBehavior::executeLogic called (count=" << currentCall << ")" << std::endl;
        }

        // === REALISTIC PRODUCTION AI BEHAVIOR PATTERNS ===
        // Based on WanderBehavior, ChaseBehavior, PatrolBehavior production implementations

        // 1. State machine update (all production behaviors have FSMs)
        updateStateMachine(deltaTime);

        // 2. Calculate movement direction based on current state
        Vector2D direction = calculateDirection(benchmarkEntity);

        // 3. Vector normalization (critical path in all production behaviors)
        float length = std::sqrt(direction.getX() * direction.getX() +
                                 direction.getY() * direction.getY());
        if (length > 0.001f) {
            direction.setX(direction.getX() / length);
            direction.setY(direction.getY() / length);
        }

        // 4. World bounds checking and avoidance (from AIManager::processBatch)
        Vector2D boundaryForce = calculateBoundaryAvoidance(benchmarkEntity);
        direction.setX(direction.getX() + boundaryForce.getX());
        direction.setY(direction.getY() + boundaryForce.getY());

        // 5. Separation forces - decimated (every 3 frames like production)
        if (m_frameCounter % 3 == 0) {
            Vector2D separation = simulateSeparationForces(benchmarkEntity);
            direction.setX(direction.getX() + separation.getX() * 0.5f);
            direction.setY(direction.getY() + separation.getY() * 0.5f);
        }

        // 6. Path following simulation (periodic like production pathfinding)
        if (m_hasActivePath) {
            direction = simulatePathFollowing(benchmarkEntity, deltaTime);
        }

        // 7. Apply velocity based on complexity (simulates different movement speeds)
        float moveSpeed = 50.0f * static_cast<float>(m_complexity);
        Vector2D velocity(direction.getX() * moveSpeed,
                         direction.getY() * moveSpeed);
        benchmarkEntity->setVelocity(velocity);

        // Update frame counter for decimation patterns
        m_frameCounter++;

        // Track behavior execution count
        m_updateCount++;
    }

    void init(EntityPtr /* entity */) override {
        m_initialized = true;
    }
    void clean(EntityPtr /* entity */) override { m_initialized = false; }

    std::string getName() const override {
        return "BenchmarkBehavior" + std::to_string(m_id);
    }

    std::shared_ptr<AIBehavior> clone() const override {
        auto cloned = std::make_shared<BenchmarkBehavior>(m_id, m_complexity);
        cloned->setActive(m_active);
        return cloned;
    }

    void onMessage(EntityPtr /* entity */, const std::string& /* message */) override {
        m_messageCount++;
    }

    int getUpdateCount() const { return m_updateCount; }
    int getMessageCount() const { return m_messageCount; }
    void resetUpdateCount() { m_updateCount.store(0, std::memory_order_release); }

private:
    // === Helper Methods for Realistic AI Patterns ===

    void updateStateMachine(float deltaTime) {
        // Simple 4-state FSM matching production behaviors
        m_stateTimer -= deltaTime;
        if (m_stateTimer <= 0.0f) {
            // Transition to random state
            m_currentState = static_cast<State>(m_rng() % 4);
            m_stateTimer = 1.0f + (m_rng() % 300) / 100.0f; // 1-4 seconds
        }
    }

    Vector2D calculateDirection(std::shared_ptr<BenchmarkEntity> entity) {
        // Calculate direction based on current state
        Vector2D pos = entity->getPosition();

        switch (m_currentState) {
            case State::Moving:
                // Move toward wander target
                return Vector2D(m_wanderTarget.getX() - pos.getX(),
                               m_wanderTarget.getY() - pos.getY());

            case State::Seeking:
                // Seek toward world center
                return Vector2D(5000.0f - pos.getX(), 5000.0f - pos.getY());

            case State::Avoiding:
                // Flee from world center
                return Vector2D(pos.getX() - 5000.0f, pos.getY() - 5000.0f);

            case State::Idle:
            default:
                // Default forward direction
                return Vector2D(1.0f, 0.0f);
        }
    }

    Vector2D calculateBoundaryAvoidance(std::shared_ptr<BenchmarkEntity> entity) {
        // World bounds checking (matches AIManager::processBatch pattern)
        Vector2D pos = entity->getPosition();
        Vector2D avoidanceForce(0.0f, 0.0f);

        const float WORLD_MIN = 0.0f;
        const float WORLD_MAX = 10000.0f;
        const float BOUNDARY_MARGIN = 500.0f;

        // X-axis boundary forces
        if (pos.getX() < WORLD_MIN + BOUNDARY_MARGIN) {
            avoidanceForce.setX((WORLD_MIN + BOUNDARY_MARGIN - pos.getX()) / BOUNDARY_MARGIN);
        } else if (pos.getX() > WORLD_MAX - BOUNDARY_MARGIN) {
            avoidanceForce.setX((WORLD_MAX - BOUNDARY_MARGIN - pos.getX()) / BOUNDARY_MARGIN);
        }

        // Y-axis boundary forces
        if (pos.getY() < WORLD_MIN + BOUNDARY_MARGIN) {
            avoidanceForce.setY((WORLD_MIN + BOUNDARY_MARGIN - pos.getY()) / BOUNDARY_MARGIN);
        } else if (pos.getY() > WORLD_MAX - BOUNDARY_MARGIN) {
            avoidanceForce.setY((WORLD_MAX - BOUNDARY_MARGIN - pos.getY()) / BOUNDARY_MARGIN);
        }

        return avoidanceForce;
    }

    Vector2D simulateSeparationForces(std::shared_ptr<BenchmarkEntity> entity) {
        // Simulate separation from nearby entities (matches production crowd management)
        Vector2D pos = entity->getPosition();
        Vector2D separationForce(0.0f, 0.0f);

        // Simulate checking 3-5 neighbors (production uses CollisionManager queries)
        int neighborCount = 3 + (m_rng() % 3);
        for (int i = 0; i < neighborCount; ++i) {
            // Generate simulated neighbor position within typical separation range
            Vector2D neighborPos(
                pos.getX() + (m_rng() % 200 - 100),
                pos.getY() + (m_rng() % 200 - 100)
            );

            // Calculate separation force
            Vector2D diff(pos.getX() - neighborPos.getX(),
                         pos.getY() - neighborPos.getY());
            float distance = std::sqrt(diff.getX() * diff.getX() +
                                      diff.getY() * diff.getY());

            if (distance < 100.0f && distance > 0.001f) {
                // Normalize and accumulate separation force
                separationForce.setX(separationForce.getX() + diff.getX() / distance);
                separationForce.setY(separationForce.getY() + diff.getY() / distance);
            }
        }

        return separationForce;
    }

    Vector2D simulatePathFollowing(std::shared_ptr<BenchmarkEntity> entity, float deltaTime) {
        // Simulate path following (matches production PathfinderManager usage)
        Vector2D pos = entity->getPosition();

        // Calculate direction to path target
        Vector2D toTarget(m_pathTarget.getX() - pos.getX(),
                         m_pathTarget.getY() - pos.getY());
        float distanceToTarget = std::sqrt(toTarget.getX() * toTarget.getX() +
                                          toTarget.getY() * toTarget.getY());

        // Check if we've reached the target
        if (distanceToTarget < 50.0f) {
            m_hasActivePath = false;
            // Generate new path target on next path request
        }

        // Update path request cooldown
        m_pathCooldown -= deltaTime;
        if (m_pathCooldown <= 0.0f && !m_hasActivePath) {
            // Request new path (production uses async PathfinderManager requests)
            m_pathTarget = Vector2D(
                static_cast<float>(m_rng() % 10000),
                static_cast<float>(m_rng() % 10000)
            );
            m_hasActivePath = true;
            m_pathCooldown = 15.0f + (m_rng() % 1500) / 100.0f; // 15-30 seconds
        }

        return toTarget;
    }

    // === State Variables ===

    // AI state machine
    enum class State { Idle, Moving, Seeking, Avoiding };
    State m_currentState = State::Moving;
    float m_stateTimer = 2.0f;

    // Path following state
    bool m_hasActivePath = false;
    Vector2D m_pathTarget{5000.0f, 5000.0f};
    float m_pathCooldown = 5.0f; // Start with short cooldown

    // Wander targets
    Vector2D m_wanderTarget{5000.0f, 5000.0f};

    // Frame counter for decimation patterns
    int m_frameCounter = 0;

    // Original members
    int m_id;
    int m_complexity;
    bool m_initialized{false};
    std::atomic<int> m_updateCount{0};
    std::atomic<int> m_messageCount{0};
    std::mt19937 m_rng{std::random_device{}()};
};

// Global fixture for the entire test suite
struct GlobalFixture {
    GlobalFixture() {
        std::lock_guard<std::mutex> lock(g_setupMutex);
        if (!g_systemsInitialized) {
            // Enable benchmark mode to silence manager logging during tests
            HAMMER_ENABLE_BENCHMARK_MODE();

            // Initialize dependencies in correct order
            HammerEngine::ThreadSystem::Instance().init();

            // AIManager requires these managers to be initialized first
            PathfinderManager::Instance().init();
            // Rebuild pathfinding grid for production behaviors (uses default world from init)
            PathfinderManager::Instance().rebuildGrid();
            CollisionManager::Instance().init();

            // Now AIManager can initialize successfully
            if (!AIManager::Instance().init()) {
                std::cerr << "FATAL: AIManager::init() failed!" << std::endl;
            }

            g_systemsInitialized = true;
        }
    }

    ~GlobalFixture() {
        std::lock_guard<std::mutex> lock(g_setupMutex);
        if (g_systemsInitialized) {
            // Signal that shutdown is in progress
            g_shutdownInProgress.store(true);

            // Wait for any pending operations to complete
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Clean up in reverse order of initialization
            try {
                // Reset all behaviors first
                AIManager::Instance().resetBehaviors();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Clean AIManager
                AIManager::Instance().clean();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Clean dependencies in reverse order
                CollisionManager::Instance().clean();
                PathfinderManager::Instance().clean();

                // Clean ThreadSystem last
                HammerEngine::ThreadSystem::Instance().clean();
            } catch (const std::exception& e) {
                std::cerr << "Exception during cleanup: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Unknown exception during cleanup" << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            // Disable benchmark mode after cleanup
            HAMMER_DISABLE_BENCHMARK_MODE();

            g_systemsInitialized = false;
        }
    }
};

// Register global fixture
BOOST_GLOBAL_FIXTURE(GlobalFixture);

// Fixture for benchmark setup/teardown
struct AIScalingFixture {
    AIScalingFixture() {
        // Configure threading for AIManager
        AIManager::Instance().configureThreading(true);

        std::cout << "=========================================" << std::endl;
        std::cout << "AI SCALING BENCHMARK" << std::endl;
        std::cout << "=========================================" << std::endl;
    }

    ~AIScalingFixture() {
        // Clean up at end of test
        cleanupEntitiesAndBehaviors();

        // Clear collections
        entities.clear();
        behaviors.clear();
    }

    /**
     * Run synthetic benchmark with automatic threading behavior using BenchmarkBehavior.
     *
     * This tests AIManager batch processing in isolation without PathfinderManager/CollisionManager
     * integration overhead. Uses simplified BenchmarkBehavior that simulates production patterns
     * without actual pathfinding requests or spatial queries.
     *
     * IMPORTANT: This benchmark includes a 16-frame warmup phase before measurements.
     * The warmup is REQUIRED due to AIManager's SIMD distance staggering optimization
     * which updates only 1/16th of entities per frame (rotating through entities mod 16).
     * Without warmup, most entities won't have distance initialized, causing them to
     * fail culling checks and report 0 or very low update counts.
     *
     * @param numEntities Number of entities to create
     * @param numBehaviors Number of different behaviors to use
     * @param numUpdates Number of update cycles to measure
     * @param numMeasurements Number of measurement runs to average (5 recommended for statistical robustness)
     */
    void runSyntheticBenchmark(int numEntities, int numBehaviors, int numUpdates, int numMeasurements = 5) {
        // Skip if shutdown is in progress
        if (g_shutdownInProgress.load()) {
            return;
        }

        // Clear local collections only (don't reset AIManager state)
        entities.clear();
        behaviors.clear();

        // Enable threading and let AIManager decide based on entity count and thresholds
        AIManager::Instance().configureThreading(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Determine expected behavior based on actual thresholds
        const int AI_THRESHOLD = 200;  // Updated threshold
        bool willUseThreading = (numEntities >= AI_THRESHOLD);
        std::string expectedMode = willUseThreading ? "Automatic Threading" : "Automatic Single-Threaded";

        // Get system threading information
        unsigned int systemThreads = std::thread::hardware_concurrency();
        std::cout << "\nSynthetic Benchmark (Isolated AIManager): " << expectedMode << ", "
                  << numEntities << " entities, "
                  << numBehaviors << " behaviors, "
                  << numUpdates << " updates" << std::endl;
        std::cout << "  System: " << systemThreads << " hardware threads available" << std::endl;
        if (willUseThreading) {
            // Use real WorkerBudgetManager for production-matching behavior
            const auto& budget = HammerEngine::WorkerBudgetManager::Instance().getBudget();
            std::cout << "  WorkerBudget: " << budget.totalWorkers << " total workers, "
                      << budget.aiAllocated << " allocated to AI ("
                      << (budget.totalWorkers > 0 ? (budget.aiAllocated * 100 / budget.totalWorkers) : 0)
                      << "%)" << std::endl;
        }

        // Create behaviors with varying complexity using valid behavior names
        const std::vector<std::string> validBehaviors = {"Wander", "Guard", "Patrol", "Follow", "Chase"};
        for (int i = 0; i < numBehaviors && i < static_cast<int>(validBehaviors.size()); ++i) {
            int complexity = 5 + (i % 11);
            behaviors.push_back(std::make_shared<BenchmarkBehavior>(i, complexity));
            AIManager::Instance().registerBehavior(validBehaviors[i], behaviors.back());
        }

        // Create entities at the same position to ensure they're close to player
        Vector2D centralPosition(500.0f, 500.0f);
        for (int i = 0; i < numEntities; ++i) {
            auto entity = BenchmarkEntity::create(i, centralPosition);
            entities.push_back(entity);

            // Assign behaviors in a round-robin fashion using valid AI behavior names
            const std::vector<std::string> validBehaviors = {"Wander", "Guard", "Patrol", "Follow", "Chase"};
            std::string behaviorName = validBehaviors[i % validBehaviors.size()];
            AIManager::Instance().assignBehaviorToEntity(entity, behaviorName);
            // Register entity for managed updates with maximum priority to ensure updates
            AIManager::Instance().registerEntityForUpdates(entity, 9); // Max priority

        }


        // Set the first entity as player reference and ensure all entities are positioned close
        if (!entities.empty()) {
            // Set player first to ensure proper distance calculations
            AIManager::Instance().setPlayerForDistanceOptimization(entities[0]);

            // Position all entities very close to the first entity (which becomes the player reference)
            Vector2D playerPosition = entities[0]->getPosition();

            // Keep ALL entities within optimal AI update range (under 2000 units from player)
            // Use very tight clustering to ensure high execution rates in benchmarks
            const float MAX_CLUSTER_RADIUS = 1500.0f; // Well within 4000 unit update range

            for (size_t i = 1; i < entities.size(); ++i) {
                // Create a very tight cluster using random positioning within small radius
                static std::mt19937 rng(42); // Fixed seed for consistent results
                std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159f);
                std::uniform_real_distribution<float> radiusDist(0.0f, MAX_CLUSTER_RADIUS);

                float angle = angleDist(rng);
                float radius = radiusDist(rng);

                float offsetX = radius * std::cos(angle);
                float offsetY = radius * std::sin(angle);

                Vector2D closePosition(playerPosition.getX() + offsetX, playerPosition.getY() + offsetY);
                entities[i]->setPosition(closePosition);
            }

            // Debug: Check if entities have behaviors assigned and distances calculated
            size_t entitiesWithBehaviors = 0;
            for (const auto& entity : entities) {
                if (AIManager::Instance().entityHasBehavior(entity)) {
                    entitiesWithBehaviors++;
                }
            }

            std::cout << "  [DEBUG] Set player reference and positioned " << entities.size()
                      << " entities in tight cluster within " << MAX_CLUSTER_RADIUS
                      << " units for optimal AI execution rates" << std::endl;
            std::cout << "  [DEBUG] Entities with behaviors: " << entitiesWithBehaviors << "/" << entities.size() << std::endl;
            std::cout << "  [DEBUG] Managed entity count: " << AIManager::Instance().getManagedEntityCount() << std::endl;


        }

        // Organize entities by behavior for batch updates
        // Organize entities by behavior
        std::vector<std::vector<EntityPtr>> behaviorEntities(numBehaviors);
        for (size_t i = 0; i < entities.size(); ++i) {
            int behaviorIdx = i % numBehaviors;
            behaviorEntities[behaviorIdx].push_back(entities[i]);
        }

        // ===== WARMUP PHASE =====
        // Run 16 warmup frames to initialize distance staggering.
        // AIManager's SIMD distance optimization staggers distance calculations across 16 frames
        // (updating 1/16th of entities per frame). Without warmup, entities may not have their
        // distance calculated, causing them to fail culling checks and report 0 updates.
        // This warmup ensures all entities get at least one distance update before measurement.
        std::cout << "  [DEBUG] Running 16 warmup frames for distance staggering initialization..." << std::endl;
        for (int warmup = 0; warmup < 16; ++warmup) {
            AIManager::Instance().update(0.016f);
        }

        // Wait for warmup to complete
        while (HammerEngine::ThreadSystem::Instance().isBusy()) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::cout << "  [DEBUG] Warmup complete. Starting measurement..." << std::endl;

        // Run specified number of times for measurements
        std::vector<double> durations;

        // Get starting behavior execution count from AIManager (AFTER warmup)
        // Store in totalBehaviorExecutions so we can calculate delta at cleanup
        totalBehaviorExecutions = AIManager::Instance().getBehaviorUpdateCount();



        std::vector<double> completionDurations;

        for (int run = 0; run < numMeasurements; run++) {
            // Measure DISPATCH timing (async performance - does not include completion wait)
            auto dispatchStartTime = std::chrono::high_resolution_clock::now();

            // Use automatic threading behavior (already configured in function setup)
            for (int update = 0; update < numUpdates; ++update) {
                AIManager::Instance().update(0.016f);
            }

            // End dispatch timing immediately after dispatch (true async performance)
            auto dispatchEndTime = std::chrono::high_resolution_clock::now();
            auto dispatchDuration = std::chrono::duration_cast<std::chrono::microseconds>(dispatchEndTime - dispatchStartTime);

            // Store the DISPATCH duration (fire-and-forget performance)
            durations.push_back(std::max(1.0, static_cast<double>(dispatchDuration.count())));

            // Measure COMPLETION timing (includes waiting for all work to finish)
            // This metric is critical for frame budget planning
            while (HammerEngine::ThreadSystem::Instance().isBusy()) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }

            auto completionEndTime = std::chrono::high_resolution_clock::now();
            auto completionDuration = std::chrono::duration_cast<std::chrono::microseconds>(completionEndTime - dispatchStartTime);

            // Store the COMPLETION duration (total time including async work)
            completionDurations.push_back(std::max(1.0, static_cast<double>(completionDuration.count())));

            // Add small additional delay to ensure all behavior counts are recorded
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Allow async AI processing to complete before measuring executions
        // For large entity counts, poll until behavior count stabilizes
        if (numEntities > 50000) {
            size_t lastCount = 0;
            size_t stableCount = 0;
            const size_t maxWaitIterations = 100; // Maximum 10 seconds wait

            for (size_t i = 0; i < maxWaitIterations; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                size_t currentCount = AIManager::Instance().getBehaviorUpdateCount();

                if (currentCount == lastCount) {
                    stableCount++;
                    // Consider stable after count hasn't changed for 5 iterations (500ms)
                    if (stableCount >= 5) {
                        break;
                    }
                } else {
                    stableCount = 0;
                    lastCount = currentCount;
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Calculate the average dispatch duration
        double avgDispatchDuration = 0.0;
        if (numMeasurements == 1) {
            avgDispatchDuration = durations[0];
        } else {
            // Exclude the highest value for stability with multiple measurements
            std::sort(durations.begin(), durations.end());
            for (size_t i = 0; i < durations.size() - 1; i++) {
                avgDispatchDuration += durations[i];
            }
            avgDispatchDuration /= (durations.size() - 1);
        }

        // Calculate the average completion duration
        double avgCompletionDuration = 0.0;
        if (numMeasurements == 1) {
            avgCompletionDuration = completionDurations[0];
        } else {
            // Exclude the highest value for stability with multiple measurements
            std::sort(completionDurations.begin(), completionDurations.end());
            for (size_t i = 0; i < completionDurations.size() - 1; i++) {
                avgCompletionDuration += completionDurations[i];
            }
            avgCompletionDuration /= (completionDurations.size() - 1);
        }

        // Calculate statistics with both dispatch and completion durations
        double dispatchTimeMs = avgDispatchDuration / 1000.0;
        double completionTimeMs = avgCompletionDuration / 1000.0;
        double timePerUpdateMs = completionTimeMs / numUpdates;

        // Get total behavior execution count from AIManager for all runs
        size_t endingExecutions = AIManager::Instance().getBehaviorUpdateCount();
        size_t startingExecutions = totalBehaviorExecutions;  // Starting count was stored in member variable
        size_t totalBehaviorUpdates = endingExecutions - startingExecutions;

        double timePerEntityMs = totalBehaviorUpdates > 0 ?
            completionTimeMs / static_cast<double>(totalBehaviorUpdates) : 0.0;

        // Calculate entity updates per second based on actual executions and COMPLETION time
        double entitiesPerSecond = totalBehaviorUpdates > 0 ?
            static_cast<double>(totalBehaviorUpdates) / (completionTimeMs / 1000.0) : 0.0;

        // Print results in clean format with BOTH dispatch and completion metrics
        std::cout << "\nPerformance Results (avg of " << numMeasurements << " runs):" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Dispatch time (async): " << dispatchTimeMs << " ms" << std::endl;
        std::cout << "  Completion time (total): " << completionTimeMs << " ms" << std::endl;
        std::cout << "  Time per update cycle: " << timePerUpdateMs << " ms" << std::endl;
        std::cout << "  Time per entity: " << timePerEntityMs << " ms" << std::endl;
        std::cout << std::setprecision(0);
        std::cout << "  Entity updates per second: " << entitiesPerSecond << std::endl;
        std::cout << std::setprecision(2);
        std::cout << "  Total behavior updates: " << totalBehaviorUpdates << std::endl;
        std::cout << "  Threading mode: " << (willUseThreading ? "WorkerBudget Multi-threaded" : "Single-threaded") << std::endl;

        // Verification status based on behavior executions - TIGHTENED to 90% threshold
        size_t expectedExecutions = static_cast<size_t>(numEntities) * static_cast<size_t>(numUpdates);
        size_t requiredExecutions = (expectedExecutions * 9) / 10; // 90% threshold
        std::cout << "  Entity updates: " << totalBehaviorExecutions << "/" << expectedExecutions;
        if (totalBehaviorExecutions >= requiredExecutions) {
            std::cout << " ✓" << std::endl;
        } else {
            std::cout << " ✗ (Expected at least " << requiredExecutions
                      << " updates [90% threshold]. Some updates may be skipped due to distance culling.)" << std::endl;
        }

        // Clear all entity frame counters
        // clearFrameCounters no longer needed - AIManager controls all update timing

        // Clean up
        cleanupEntitiesAndBehaviors();

        // No cleanup needed for benchmark mode (removed)
    }

    /**
     * Run integrated benchmark using real production AI behaviors.
     *
     * This tests AIManager with full PathfinderManager/CollisionManager integration,
     * using actual WanderBehavior, ChaseBehavior, etc. Measures real-world performance
     * including pathfinding requests, spatial queries, and all production overhead.
     *
     * @param numEntities Number of entities to create
     * @param numBehaviors Number of different behavior types to use (max 5)
     * @param numUpdates Number of update cycles to measure
     * @param numMeasurements Number of measurement runs to average (5 recommended for statistical robustness)
     */
    void runIntegratedBenchmark(int numEntities, int numBehaviors, int numUpdates, int numMeasurements = 5) {
        // Skip if shutdown is in progress
        if (g_shutdownInProgress.load()) {
            return;
        }

        // Clear local collections
        entities.clear();
        behaviors.clear();

        // Enable threading and let AIManager decide based on entity count
        AIManager::Instance().configureThreading(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Determine expected behavior based on actual thresholds
        const int AI_THRESHOLD = 200;
        bool willUseThreading = (numEntities >= AI_THRESHOLD);
        std::string expectedMode = willUseThreading ? "Automatic Threading" : "Automatic Single-Threaded";

        unsigned int systemThreads = std::thread::hardware_concurrency();
        std::cout << "\nIntegrated Benchmark (Production Behaviors): " << expectedMode << ", "
                  << numEntities << " entities, "
                  << numBehaviors << " behaviors, "
                  << numUpdates << " updates" << std::endl;
        std::cout << "  System: " << systemThreads << " hardware threads available" << std::endl;
        if (willUseThreading) {
            // Use real WorkerBudgetManager for production-matching behavior
            const auto& budget = HammerEngine::WorkerBudgetManager::Instance().getBudget();
            std::cout << "  WorkerBudget: " << budget.totalWorkers << " total workers, "
                      << budget.aiAllocated << " allocated to AI ("
                      << (budget.totalWorkers > 0 ? (budget.aiAllocated * 100 / budget.totalWorkers) : 0)
                      << "%)" << std::endl;
        }

        // Create REAL production behaviors (limit to 5 available types)
        const int MAX_BEHAVIORS = 5;
        numBehaviors = std::min(numBehaviors, MAX_BEHAVIORS);

        // Register real production behaviors with AIManager
        if (numBehaviors >= 1) {
            auto wander = std::make_shared<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 100.0f);
            AIManager::Instance().registerBehavior("Wander", wander);
        }
        if (numBehaviors >= 2) {
            auto guard = std::make_shared<GuardBehavior>(Vector2D(5000.0f, 5000.0f), 500.0f);
            AIManager::Instance().registerBehavior("Guard", guard);
        }
        if (numBehaviors >= 3) {
            // PatrolBehavior requires waypoints - use simple 2-point patrol
            std::vector<Vector2D> waypoints = {
                Vector2D(4000.0f, 5000.0f),
                Vector2D(6000.0f, 5000.0f)
            };
            auto patrol = std::make_shared<PatrolBehavior>(waypoints, 100.0f, true);
            AIManager::Instance().registerBehavior("Patrol", patrol);
        }
        if (numBehaviors >= 4) {
            // FollowBehavior(float followSpeed, float followDistance, float maxDistance)
            auto follow = std::make_shared<FollowBehavior>(2.5f, 200.0f, 400.0f);
            AIManager::Instance().registerBehavior("Follow", follow);
        }
        if (numBehaviors >= 5) {
            // ChaseBehavior(float chaseSpeed, float maxRange, float minRange)
            auto chase = std::make_shared<ChaseBehavior>(100.0f, 500.0f, 50.0f);
            AIManager::Instance().registerBehavior("Chase", chase);
        }

        // Create entities at central position
        Vector2D centralPosition(5000.0f, 5000.0f);
        const std::vector<std::string> behaviorNames = {"Wander", "Guard", "Patrol", "Follow", "Chase"};

        for (int i = 0; i < numEntities; ++i) {
            auto entity = BenchmarkEntity::create(i, centralPosition);
            entities.push_back(entity);

            // Assign behaviors in round-robin fashion
            std::string behaviorName = behaviorNames[i % numBehaviors];
            AIManager::Instance().assignBehaviorToEntity(entity, behaviorName);
            AIManager::Instance().registerEntityForUpdates(entity, 9); // Max priority
        }

        // Set player reference and position entities
        if (!entities.empty()) {
            AIManager::Instance().setPlayerForDistanceOptimization(entities[0]);
            Vector2D playerPosition = entities[0]->getPosition();

            // Tight clustering within optimal AI update range
            const float MAX_CLUSTER_RADIUS = 1500.0f;
            for (size_t i = 1; i < entities.size(); ++i) {
                static std::mt19937 rng(42);
                std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159f);
                std::uniform_real_distribution<float> radiusDist(0.0f, MAX_CLUSTER_RADIUS);

                float angle = angleDist(rng);
                float radius = radiusDist(rng);
                float offsetX = radius * std::cos(angle);
                float offsetY = radius * std::sin(angle);

                Vector2D closePosition(playerPosition.getX() + offsetX, playerPosition.getY() + offsetY);
                entities[i]->setPosition(closePosition);
            }

            std::cout << "  [DEBUG] Positioned " << entities.size()
                      << " entities with production behaviors" << std::endl;
        }

        // Warmup phase (16 frames for distance staggering)
        std::cout << "  [DEBUG] Running 16 warmup frames..." << std::endl;
        for (int warmup = 0; warmup < 16; ++warmup) {
            AIManager::Instance().update(0.016f);
        }
        while (HammerEngine::ThreadSystem::Instance().isBusy()) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::cout << "  [DEBUG] Warmup complete. Starting measurement..." << std::endl;

        // Run measurements
        std::vector<double> durations;
        std::vector<double> completionDurations;
        size_t startingExecutions = AIManager::Instance().getBehaviorUpdateCount();

        for (int run = 0; run < numMeasurements; run++) {
            // Measure DISPATCH timing (async performance - does not include completion wait)
            auto dispatchStartTime = std::chrono::high_resolution_clock::now();

            for (int update = 0; update < numUpdates; ++update) {
                AIManager::Instance().update(0.016f);
            }

            auto dispatchEndTime = std::chrono::high_resolution_clock::now();
            auto dispatchDuration = std::chrono::duration_cast<std::chrono::microseconds>(dispatchEndTime - dispatchStartTime);
            durations.push_back(std::max(1.0, static_cast<double>(dispatchDuration.count())));

            // Measure COMPLETION timing (includes waiting for all work to finish)
            while (HammerEngine::ThreadSystem::Instance().isBusy()) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }

            auto completionEndTime = std::chrono::high_resolution_clock::now();
            auto completionDuration = std::chrono::duration_cast<std::chrono::microseconds>(completionEndTime - dispatchStartTime);
            completionDurations.push_back(std::max(1.0, static_cast<double>(completionDuration.count())));

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Wait for any async operations
        if (numEntities > 50000) {
            size_t lastCount = 0;
            size_t stableCount = 0;
            for (size_t i = 0; i < 100; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                size_t currentCount = AIManager::Instance().getBehaviorUpdateCount();
                if (currentCount == lastCount) {
                    stableCount++;
                    if (stableCount >= 5) break;
                } else {
                    stableCount = 0;
                    lastCount = currentCount;
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Calculate results for both dispatch and completion
        double avgDispatchDuration = 0.0;
        if (numMeasurements == 1) {
            avgDispatchDuration = durations[0];
        } else {
            std::sort(durations.begin(), durations.end());
            for (size_t i = 0; i < durations.size() - 1; i++) {
                avgDispatchDuration += durations[i];
            }
            avgDispatchDuration /= (durations.size() - 1);
        }

        double avgCompletionDuration = 0.0;
        if (numMeasurements == 1) {
            avgCompletionDuration = completionDurations[0];
        } else {
            std::sort(completionDurations.begin(), completionDurations.end());
            for (size_t i = 0; i < completionDurations.size() - 1; i++) {
                avgCompletionDuration += completionDurations[i];
            }
            avgCompletionDuration /= (completionDurations.size() - 1);
        }

        double dispatchTimeMs = avgDispatchDuration / 1000.0;
        double completionTimeMs = avgCompletionDuration / 1000.0;
        double timePerUpdateMs = completionTimeMs / numUpdates;

        size_t endingExecutions = AIManager::Instance().getBehaviorUpdateCount();
        size_t totalBehaviorExecutions = endingExecutions - startingExecutions;

        double timePerEntityMs = totalBehaviorExecutions > 0 ?
            completionTimeMs / static_cast<double>(totalBehaviorExecutions) : 0.0;

        double entitiesPerSecond = totalBehaviorExecutions > 0 ?
            static_cast<double>(totalBehaviorExecutions) / (completionTimeMs / 1000.0) : 0.0;

        // Print results
        std::cout << "\nPerformance Results (avg of " << numMeasurements << " runs):" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Dispatch time (async): " << dispatchTimeMs << " ms" << std::endl;
        std::cout << "  Completion time (total): " << completionTimeMs << " ms" << std::endl;
        std::cout << "  Time per update cycle: " << timePerUpdateMs << " ms" << std::endl;
        std::cout << "  Time per entity: " << timePerEntityMs << " ms" << std::endl;
        std::cout << std::setprecision(0);
        std::cout << "  Entity updates per second: " << entitiesPerSecond << std::endl;
        std::cout << std::setprecision(2);
        std::cout << "  Total behavior updates: " << totalBehaviorExecutions << std::endl;
        std::cout << "  Threading mode: " << (willUseThreading ? "WorkerBudget Multi-threaded" : "Single-threaded") << std::endl;

        // Verification - TIGHTENED to 90% threshold
        size_t expectedExecutions = static_cast<size_t>(numEntities) * static_cast<size_t>(numUpdates);
        size_t requiredExecutions = (expectedExecutions * 9) / 10; // 90% threshold
        std::cout << "  Entity updates: " << totalBehaviorExecutions << "/" << expectedExecutions;
        if (totalBehaviorExecutions >= requiredExecutions) {
            std::cout << " ✓" << std::endl;
        } else {
            std::cout << " ✗ (Expected at least " << requiredExecutions
                      << " updates [90% threshold]. Some updates may be skipped due to distance culling.)" << std::endl;
        }

        // Clean up
        cleanupEntitiesAndBehaviors();
    }

    // Helper to clean up entities and behaviors safely
    void cleanupEntitiesAndBehaviors() {
        // Skip if shutdown is in progress
        if (g_shutdownInProgress.load()) {
            return;
        }

        // Clear all frame counters
        for (const auto& behavior : behaviors) {
            if (behavior) {
                try {
                    // clearFrameCounters no longer needed - AIManager controls all update timing
                } catch (const std::exception& e) {
                    std::cerr << "Error clearing frame counters: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Unknown error clearing frame counters" << std::endl;
                }
            }
        }

        // First unregister from managed updates and unassign all behaviors from entities
        for (auto& entity : entities) {
            if (entity) {
                try {
                    AIManager::Instance().unregisterEntityFromUpdates(entity);
                    AIManager::Instance().unassignBehaviorFromEntity(entity);
                } catch (const std::exception& e) {
                    std::cerr << "Error unregistering/unassigning entity: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Unknown error unregistering/unassigning entity" << std::endl;
                }
            }
        }

        // Wait for unassign operations to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Capture behavior execution count BEFORE resetting (reset clears the counter)
        // This gives us the delta for just this test run (subtract the starting count captured in runSyntheticBenchmark)
        size_t endCount = AIManager::Instance().getBehaviorUpdateCount();
        totalBehaviorExecutions = endCount - totalBehaviorExecutions;  // Delta for this test only

        // Reset all behaviors
        try {
            AIManager::Instance().resetBehaviors();
            std::cout << "AI behaviors reset successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error resetting behaviors: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown error resetting behaviors" << std::endl;
        }

        // Wait for any pending operations to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Historical performance data from previous benchmark runs (BenchmarkBehavior)
    // WARNING: This is NOT a live benchmark - it's a lookup table of past results
    // Updated Nov 7, 2025: Using actual measured values from historical TestSyntheticPerformance runs
    double getHistoricalPerformanceRate(size_t numEntities, bool useThreading) {
        if (useThreading) {
            // Automatic threading behavior (respects 200 entity threshold)
            // These are HISTORICAL MEASURED values from past TestSyntheticPerformance runs
            if (numEntities < 200) return 6987578.0;       // Single-threaded below threshold (~150 entities)
            else if (numEntities <= 200) return 8154944.0; // Threading activation
            else if (numEntities <= 500) return 12000000.0; // Interpolated (not directly measured)
            else if (numEntities <= 1000) return 19933555.0; // Measured at 1000 entities
            else if (numEntities <= 2000) return 23000000.0; // Interpolated
            else if (numEntities <= 5000) return 26665482.0; // Measured at 5000 entities
            else if (numEntities <= 10000) return 28000000.0; // Interpolated (measured was 26.6M @ 5K)
            else if (numEntities <= 50000) return 30000000.0; // Approaching saturation
            else return 31942589.0; // Measured at 100K entities (stress test)
        } else {
            // Forced single-threaded performance (legacy estimates, rarely used)
            if (numEntities <= 500) return 6987578.0; // Use measured single-threaded baseline
            else if (numEntities <= 1000) return 6500000.0;
            else if (numEntities <= 5000) return 6000000.0;
            else return 5500000.0;
        }
    }

    // Display historical benchmark results (NOT a live benchmark)
    void displayHistoricalResults() {
        std::cout << "\n=========================================================================" << std::endl;
        std::cout << "                    HISTORICAL DATA - NOT A LIVE BENCHMARK                " << std::endl;
        std::cout << "=========================================================================" << std::endl;
        std::cout << "This displays lookup table data from previous TestSyntheticPerformance runs." << std::endl;
        std::cout << "For ACTUAL performance measurements, see TestSyntheticPerformance test results." << std::endl;
        std::cout << "=========================================================================" << std::endl;

        // Skip if shutdown is in progress
        if (g_shutdownInProgress.load()) {
            return;
        }

        std::cout << "\nHISTORICAL SCALABILITY SUMMARY (BenchmarkBehavior - Past Results):" << std::endl;
        std::cout << "Entity Count | Threading Mode | Updates Per Second | Performance Ratio" << std::endl;
        std::cout << "-------------|----------------|-------------------|------------------" << std::endl;

        // Display historical data across entity counts
        std::vector<int> entityCounts = {100, 200, 500, 1000, 2000, 5000, 10000};
        const int AI_THRESHOLD = 200;

        double baselineRate = 0.0;
        for (size_t i = 0; i < entityCounts.size(); ++i) {
            int numEntities = entityCounts[i];
            bool willUseThreading = (numEntities >= AI_THRESHOLD);
            std::string threadingMode = willUseThreading ? "Auto-Threaded" : "Auto-Single";

            // Get historical rate from lookup table
            double historicalRate = getHistoricalPerformanceRate(numEntities, willUseThreading);

            // Calculate performance ratio relative to smallest entity count
            if (i == 0) {
                baselineRate = historicalRate;
            }
            double performanceRatio = historicalRate / baselineRate;

            std::cout << std::setw(12) << numEntities << " | "
                      << std::setw(14) << threadingMode << " | "
                      << std::setw(17) << static_cast<int>(historicalRate) << " | "
                      << std::fixed << std::setprecision(2) << std::setw(16) << performanceRatio << "x" << std::endl;
        }

        std::cout << "\n=========================================================================" << std::endl;
        std::cout << "Note: This data is HISTORICAL and may not reflect current performance." << std::endl;
        std::cout << "      Run TestSyntheticPerformance for live benchmarks." << std::endl;
        std::cout << "Threshold: " << AI_THRESHOLD << " entities for automatic threading activation." << std::endl;
        std::cout << "=========================================================================" << std::endl;
    }

    std::vector<std::shared_ptr<BenchmarkEntity>> entities;
    std::vector<std::shared_ptr<BenchmarkBehavior>> behaviors;
    size_t totalBehaviorExecutions = 0;  // Track total behavior executions before cleanup

};

BOOST_FIXTURE_TEST_SUITE(AIScalingTests, AIScalingFixture)

// Test synthetic automatic threading behavior across different entity counts using BenchmarkBehavior
BOOST_AUTO_TEST_CASE(TestSyntheticPerformance) {
    // Enable benchmark mode to suppress AI manager logging
    HAMMER_ENABLE_BENCHMARK_MODE();

    // Skip if shutdown is in progress
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    const int numBehaviors = 5;
    // NOTE: numUpdates is now calculated per-entity-count using calculateScaledIterations()
    // This ensures ~100ms measurement duration for statistical reliability at all entity counts

    std::cout << "\n===== SYNTHETIC PERFORMANCE TESTING (Isolated AIManager) =====" << std::endl;
    std::cout << "Testing WorkerBudget automatic threading behavior at various entity counts using BenchmarkBehavior" << std::endl;
    std::cout << "NOTE: Iterations scaled inversely with entity count to ensure ~100ms measurement duration" << std::endl;
    // Use real WorkerBudgetManager for production-matching behavior
    const auto& budget = HammerEngine::WorkerBudgetManager::Instance().getBudget();
    unsigned int systemThreads = std::thread::hardware_concurrency();
    std::cout << "System Configuration: " << systemThreads << " hardware threads, "
              << budget.totalWorkers << " workers (" << budget.aiAllocated << " for AI)" << std::endl;

    // Test below threshold (should use single-threaded automatically)
    // 150 entities × 667 iterations = ~100K entity-updates for reliable measurement
    int entities150 = 150;
    int updates150 = calculateScaledIterations(entities150);
    std::cout << "\n--- Test 1: Below Threshold (" << entities150 << " entities, " << updates150 << " iterations) ---" << std::endl;
    runSyntheticBenchmark(entities150, numBehaviors, updates150);

    // Test at threshold boundary (should use threading automatically)
    // 200 entities × 500 iterations = ~100K entity-updates
    int entities200 = 200;
    int updates200 = calculateScaledIterations(entities200);
    std::cout << "\n--- Test 2: At Threshold (" << entities200 << " entities, " << updates200 << " iterations) ---" << std::endl;
    runSyntheticBenchmark(entities200, numBehaviors, updates200);

    // Test well above threshold (should use threading automatically)
    // 1000 entities × 100 iterations = ~100K entity-updates
    int entities1000 = 1000;
    int updates1000 = calculateScaledIterations(entities1000);
    std::cout << "\n--- Test 3: Above Threshold (" << entities1000 << " entities, " << updates1000 << " iterations) ---" << std::endl;
    runSyntheticBenchmark(entities1000, numBehaviors, updates1000);

    // Test target performance (should use threading automatically)
    // 5000 entities × 20 iterations = 100K entity-updates (base iterations sufficient)
    int entities5000 = 5000;
    int updates5000 = calculateScaledIterations(entities5000);
    std::cout << "\n--- Test 4: Target Performance (" << entities5000 << " entities, " << updates5000 << " iterations) ---" << std::endl;
    runSyntheticBenchmark(entities5000, numBehaviors, updates5000);

    // Clean up after test
    cleanupEntitiesAndBehaviors();
}

// Display historical benchmark results (NOT a live test - just displays lookup table)
BOOST_AUTO_TEST_CASE(TestSyntheticScalability) {
    // Skip if shutdown is in progress
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    try {
        // This is NOT a live benchmark - it just displays historical data
        displayHistoricalResults();

        // No cleanup needed - no entities were created
    }
    catch (const std::exception& e) {
        std::cerr << "Error displaying historical results: " << e.what() << std::endl;
        throw;
    }
}

// Legacy comparison test - forced threading modes for comparison
BOOST_AUTO_TEST_CASE(TestLegacyComparison) {
    // Skip if shutdown is in progress
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    std::cout << "\n===== LEGACY COMPARISON TEST =====" << std::endl;
    std::cout << "Forced threading modes for comparison with previous benchmarks" << std::endl;

    const int numEntities = 1000;
    const int numBehaviors = 5;
    const int numUpdates = 20;

    // Simplified legacy benchmark function for forced threading modes
    auto runLegacyBenchmark = [this](int numEntities, int numBehaviors, int numUpdates, bool forceThreading) {
        cleanupEntitiesAndBehaviors();
        entities.clear();
        behaviors.clear();

        // Force the threading mode (bypassing automatic threshold detection)
        AIManager::Instance().configureThreading(forceThreading);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::string mode = forceThreading ? "Forced Multi-Threaded" : "Forced Single-Threaded";
        std::cout << "\n--- " << mode << " Test: " << numEntities << " entities ---" << std::endl;

        // Create behaviors
        const std::vector<std::string> validBehaviors = {"Wander", "Guard", "Patrol", "Follow", "Chase"};
        for (int i = 0; i < numBehaviors && i < static_cast<int>(validBehaviors.size()); ++i) {
            int complexity = 5 + (i % 11);
            behaviors.push_back(std::make_shared<BenchmarkBehavior>(i, complexity));
            AIManager::Instance().registerBehavior(validBehaviors[i], behaviors.back());
        }

        // Create entities
        Vector2D centralPosition(500.0f, 500.0f);
        for (int i = 0; i < numEntities; ++i) {
            auto entity = BenchmarkEntity::create(i, centralPosition);
            entities.push_back(entity);
            const std::vector<std::string> validBehaviors = {"Wander", "Guard", "Patrol", "Follow", "Chase"};
            std::string behaviorName = validBehaviors[i % validBehaviors.size()];
            AIManager::Instance().assignBehaviorToEntity(entity, behaviorName);
            AIManager::Instance().registerEntityForUpdates(entity, 9);
        }

        if (!entities.empty()) {
            AIManager::Instance().setPlayerForDistanceOptimization(entities[0]);
        }

        // ===== WARMUP PHASE =====
        // Run 16 warmup frames to initialize distance staggering.
        // AIManager's SIMD distance optimization staggers distance calculations across 16 frames.
        // Without warmup, distance calculations won't be initialized for most entities,
        // causing them to fail culling checks and report 0 or very low updates.
        // See runSyntheticBenchmark() for detailed explanation.
        for (int warmup = 0; warmup < 16; ++warmup) {
            AIManager::Instance().update(0.016f);
        }

        // Wait for warmup to complete
        while (HammerEngine::ThreadSystem::Instance().isBusy()) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Run benchmark measurements
        std::vector<double> times;
        for (int run = 0; run < 3; ++run) {
            // Measure DISPATCH timing only (fire-and-forget performance)
            auto startTime = std::chrono::high_resolution_clock::now();

            for (int update = 0; update < numUpdates; ++update) {
                AIManager::Instance().update(0.016f); // 60 FPS deltaTime
            }

            // End timing immediately after dispatch (true async performance)
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration<double, std::milli>(endTime - startTime).count();
            times.push_back(duration);

            // SEPARATELY wait for completion for accuracy verification (not timed)
            while (HammerEngine::ThreadSystem::Instance().isBusy()) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }

            // Add small additional delay to ensure all behavior counts are recorded
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Calculate and display results
        double avgTime = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
        double updatesPerSecond = (numEntities * numUpdates * 1000.0) / avgTime;

        std::cout << "  Legacy " << mode << " Results:" << std::endl;
        std::cout << "    Avg time: " << avgTime << " ms" << std::endl;
        std::cout << "    Updates per second: " << static_cast<int>(updatesPerSecond) << std::endl;
    };

    // Test forced single-threaded
    runLegacyBenchmark(numEntities, numBehaviors, numUpdates, false);

    // Test forced multi-threaded
    runLegacyBenchmark(numEntities, numBehaviors, numUpdates, true);

    // Clean up
    cleanupEntitiesAndBehaviors();
}

// Test with extreme number of entities using realistic automatic behavior
BOOST_AUTO_TEST_CASE(TestExtremeEntityCount) {
    // Skip if shutdown is in progress
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    std::cout << "\n===== EXTREME ENTITY COUNT TEST (STRESS TESTING) =====" << std::endl;
    std::cout << "Testing 100K entities - designed to stress test the system" << std::endl;

    try {
        // Enable threading and let system decide automatically
        unsigned int maxThreads = std::thread::hardware_concurrency();
        AIManager::Instance().configureThreading(true, maxThreads);
        std::cout << "Running extreme entity test with " << maxThreads << " threads available" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Test 100K entities for proper stress testing
        const int numEntities = 100000;

        // Use fewer behaviors and updates for extreme scale to avoid memory issues
        int adjustedNumBehaviors = 5;
        int adjustedNumUpdates = 10; // Increased to ensure distance calculations work properly

        std::cout << "\n--- Stress Test: " << numEntities << " entities, "
                  << adjustedNumBehaviors << " behaviors, " << adjustedNumUpdates << " updates ---" << std::endl;
        std::cout << "Expected behavior: Automatic threading with stress-level performance" << std::endl;

        AIScalingFixture fixture;
        // Run synthetic benchmark - system will automatically use threading
        fixture.runSyntheticBenchmark(numEntities, adjustedNumBehaviors, adjustedNumUpdates);

        // Verify entities were actually created and behaviors executed
        size_t actualEntityCount = fixture.entities.size();
        size_t expectedExecutions = static_cast<size_t>(numEntities) * adjustedNumUpdates;
        size_t behaviorExecutions = fixture.totalBehaviorExecutions;  // Use saved count from before reset

        std::cout << "\nVerification:" << std::endl;
        std::cout << "  Created entities: " << actualEntityCount << "/" << numEntities;
        if (actualEntityCount == numEntities) {
            std::cout << " ✓" << std::endl;
        } else {
            std::cout << " ✗" << std::endl;
        }

        size_t requiredExecutions = (expectedExecutions * 9) / 10; // 90% threshold
        std::cout << "  Behavior executions: " << behaviorExecutions;
        if (behaviorExecutions >= requiredExecutions) {
            std::cout << " ✓ (threshold: " << requiredExecutions << " [90%])" << std::endl;
        } else {
            std::cout << " ✗ (threshold: " << requiredExecutions << " [90%], low execution count)" << std::endl;
        }

        std::cout << "\n===== EXTREME ENTITY COUNT TEST COMPLETED =====\n" << std::endl;

        // Clean up after test
        fixture.cleanupEntitiesAndBehaviors();

    } catch (const std::exception& e) {
        std::cerr << "Exception in extreme entity test: " << e.what() << std::endl;
        AIManager::Instance().configureThreading(true); // Reset to default
        throw;
    }
}

BOOST_AUTO_TEST_CASE(TestThreadSystemQueueLoad) {
    std::cout << "\n===== THREAD SYSTEM QUEUE LOAD MONITORING =====" << std::endl;
    std::cout << "DEFENSIVE TEST: Monitoring ThreadSystem queue to prevent future overload issues" << std::endl;
    std::cout << "This test ensures AIManager respects ThreadSystem's 4096 task limit" << std::endl;
    std::cout << "PURPOSE: Early warning system for code changes that might overwhelm ThreadSystem" << std::endl;

    if (!HammerEngine::ThreadSystem::Exists()) {
        BOOST_FAIL("ThreadSystem not available for queue monitoring test");
    }

    auto& threadSystem = HammerEngine::ThreadSystem::Instance();
    std::cout << "ThreadSystem queue capacity: " << threadSystem.getQueueCapacity() << std::endl;

    // Test different entity counts and monitor queue usage
    std::vector<int> testCounts = {500, 1000, 2000, 5000, 10000};
    bool anyQueueGrowth = false;

    for (int entityCount : testCounts) {
        std::cout << "\n--- Testing " << entityCount << " entities ---" << std::endl;

        AIScalingFixture fixture;

        size_t maxQueueSize = 0;
        size_t totalQueueSamples = 0;
        double avgQueueSize = 0.0;
        bool queueOverflow = false;
        std::vector<size_t> queueSnapshots;

        // Use runSyntheticBenchmark to create entities and monitor during updates
        (void)threadSystem.getQueueSize(); // Initial queue state (for monitoring baseline)

        // Create entities and run a few updates while monitoring
        std::thread monitorThread([&]() {
            for (int sample = 0; sample < 30; ++sample) { // More samples for better detection
                size_t currentQueue = threadSystem.getQueueSize();
                queueSnapshots.push_back(currentQueue);
                maxQueueSize = std::max(maxQueueSize, currentQueue);
                avgQueueSize = (avgQueueSize * totalQueueSamples + currentQueue) / (totalQueueSamples + 1);
                totalQueueSamples++;

                if (currentQueue > 3500) { // Critical threshold - 85% of 4096
                    queueOverflow = true;
                }
                if (currentQueue > 0) {
                    anyQueueGrowth = true;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(5)); // More frequent sampling
            }
        });

        // Run synthetic benchmark with minimal updates to trigger task submission
        fixture.runSyntheticBenchmark(entityCount, 3, 5, 1);

        // Wait for monitoring to complete
        monitorThread.join();

        (void)threadSystem.getQueueSize(); // Final queue state (monitoring complete)

        // Calculate more detailed statistics
        size_t nonZeroSamples = std::count_if(queueSnapshots.begin(), queueSnapshots.end(),
                                             [](size_t q) { return q > 0; });
        double queueUtilization = (maxQueueSize / 4096.0) * 100.0;

        std::cout << "  Results:" << std::endl;
        std::cout << "    Peak queue size: " << maxQueueSize << " (" << std::fixed << std::setprecision(1)
                  << queueUtilization << "% of capacity)" << std::endl;
        std::cout << "    Average queue size: " << std::fixed << std::setprecision(1) << avgQueueSize << std::endl;
        std::cout << "    Non-zero queue samples: " << nonZeroSamples << "/" << queueSnapshots.size() << std::endl;
        std::cout << "    Queue overflow risk: " << (queueOverflow ? "CRITICAL" : "SAFE") << std::endl;
        std::cout << "    Queue impact on performance: " << (queueOverflow ? "DEGRADED" : "NORMAL") << std::endl;

        // DEFENSIVE ASSERTIONS - Will fail if future changes break queue management
        BOOST_CHECK_LT(maxQueueSize, 4000); // Critical: Must stay below ThreadSystem limit
        BOOST_CHECK_LT(queueUtilization, 85.0); // Warning: Should stay under 85% capacity

        if (queueOverflow) {
            std::cout << "    ⚠️  CRITICAL: Queue approaching ThreadSystem limit!" << std::endl;
            std::cout << "    ⚠️  Future code changes may have broken queue management!" << std::endl;
        }

        if (maxQueueSize > 2000) {
            std::cout << "    ⚠️  WARNING: High queue usage detected - monitor for performance impact" << std::endl;
        }

        // Let queue drain between tests
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\n===== THREAD SYSTEM QUEUE MONITORING COMPLETED =====" << std::endl;
    std::cout << "ThreadSystem queue capacity limit: 4096 tasks" << std::endl;
    std::cout << "Queue growth detected: " << (anyQueueGrowth ? "YES" : "NO") << std::endl;
    std::cout << "\n🛡️  DEFENSIVE TEST STATUS: " << std::endl;
    std::cout << "   - This test will FAIL if future changes overwhelm ThreadSystem" << std::endl;
    std::cout << "   - Peak queue >4000 = CRITICAL failure" << std::endl;
    std::cout << "   - Peak queue >3500 = WARNING in logs" << std::endl;
    std::cout << "   - Keep this test to catch regressions early!" << std::endl;
}

// Test integrated performance with real production AI behaviors
BOOST_AUTO_TEST_CASE(TestIntegratedPerformance) {
    // Enable benchmark mode to suppress AI manager logging
    HAMMER_ENABLE_BENCHMARK_MODE();

    // Skip if shutdown is in progress
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    const int numBehaviors = 5;
    // NOTE: numUpdates is now calculated per-entity-count using calculateScaledIterations()
    // This ensures ~100ms measurement duration for statistical reliability at all entity counts

    std::cout << "\n===== INTEGRATED PERFORMANCE TESTING (Production AI Behaviors) =====" << std::endl;
    std::cout << "Testing real production behaviors: WanderBehavior, ChaseBehavior, PatrolBehavior, GuardBehavior, FollowBehavior" << std::endl;
    std::cout << "These behaviors use PathfinderManager and CollisionManager for realistic AI processing" << std::endl;
    std::cout << "NOTE: Iterations scaled inversely with entity count to ensure ~100ms measurement duration" << std::endl;
    // Use real WorkerBudgetManager for production-matching behavior
    const auto& budget = HammerEngine::WorkerBudgetManager::Instance().getBudget();
    unsigned int systemThreads = std::thread::hardware_concurrency();
    std::cout << "System Configuration: " << systemThreads << " hardware threads, "
              << budget.totalWorkers << " workers (" << budget.aiAllocated << " for AI)" << std::endl;

    // Test below threshold (should use single-threaded automatically)
    // Scaled iterations ensure reliable measurement at low entity counts
    int entities150 = 150;
    int updates150 = calculateScaledIterations(entities150);
    std::cout << "\n--- Test 1: Below Threshold (" << entities150 << " entities, " << updates150 << " iterations) ---" << std::endl;
    runIntegratedBenchmark(entities150, numBehaviors, updates150);

    // Test at threshold boundary (should use threading automatically)
    int entities200 = 200;
    int updates200 = calculateScaledIterations(entities200);
    std::cout << "\n--- Test 2: At Threshold (" << entities200 << " entities, " << updates200 << " iterations) ---" << std::endl;
    runIntegratedBenchmark(entities200, numBehaviors, updates200);

    // Test well above threshold (should use threading automatically)
    int entities1000 = 1000;
    int updates1000 = calculateScaledIterations(entities1000);
    std::cout << "\n--- Test 3: Above Threshold (" << entities1000 << " entities, " << updates1000 << " iterations) ---" << std::endl;
    runIntegratedBenchmark(entities1000, numBehaviors, updates1000);

    // Test target performance (should use threading automatically)
    int entities2000 = 2000;
    int updates2000 = calculateScaledIterations(entities2000);
    std::cout << "\n--- Test 4: Target Performance (" << entities2000 << " entities, " << updates2000 << " iterations) ---" << std::endl;
    runIntegratedBenchmark(entities2000, numBehaviors, updates2000);

    // Clean up after test
    cleanupEntitiesAndBehaviors();
}

// Test integrated scalability with production behaviors
BOOST_AUTO_TEST_CASE(TestIntegratedScalability) {
    // Skip if shutdown is in progress
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    try {
        // Use maximum available threads for optimal performance
        unsigned int maxThreads = std::thread::hardware_concurrency();
        AIManager::Instance().configureThreading(true, maxThreads);
        std::cout << "\n===== INTEGRATED SCALABILITY TEST SUITE (Production Behaviors) =====" << std::endl;
        std::cout << "Testing automatic threading behavior with real production AI behaviors" << std::endl;
        std::cout << "Running with " << maxThreads << " threads available" << std::endl;
        std::cout << "NOTE: Iterations scaled inversely with entity count to ensure ~100ms measurement duration" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Enable threading and let system decide automatically
        AIManager::Instance().configureThreading(true);

        std::cout << "\nINTEGRATED SCALABILITY SUMMARY (Production Behaviors):" << std::endl;
        std::cout << "Entity Count | Iterations | Threading Mode | Updates Per Second | Performance Notes" << std::endl;
        std::cout << "-------------|------------|----------------|-------------------|------------------" << std::endl;

        // Test across realistic entity counts with automatic behavior
        std::vector<int> entityCounts = {100, 200, 500, 1000, 2000};
        const int AI_THRESHOLD = 200;

        for (size_t i = 0; i < entityCounts.size(); ++i) {
            int numEntities = entityCounts[i];
            int numUpdates = calculateScaledIterations(numEntities);
            bool willUseThreading = (numEntities >= AI_THRESHOLD);
            std::string threadMode = willUseThreading ? "Multi-threaded" : "Single-threaded";

            std::cout << std::setw(12) << numEntities << " | "
                      << std::setw(10) << numUpdates << " | "
                      << std::setw(14) << threadMode << " | ";

            // Run integrated benchmark with scaled iterations
            runIntegratedBenchmark(numEntities, 5, numUpdates, 1);

            std::cout << " | PathfinderManager + CollisionManager" << std::endl;

            // Clean up between runs
            cleanupEntitiesAndBehaviors();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\n===== INTEGRATED SCALABILITY TEST COMPLETED =====" << std::endl;

        // Clean up after test
        cleanupEntitiesAndBehaviors();
    }
    catch (const std::exception& e) {
        std::cerr << "Error in integrated scalability test: " << e.what() << std::endl;
        AIManager::Instance().configureThreading(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cleanupEntitiesAndBehaviors();
        throw;
    }
}

BOOST_AUTO_TEST_SUITE_END() // AIScalingTests
