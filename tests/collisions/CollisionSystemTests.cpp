/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE CollisionSystemTests
#include <boost/test/unit_test.hpp>
#include <boost/test/tools/old/interface.hpp>

#include "collisions/AABB.hpp"
#include "collisions/CollisionBody.hpp"
#include "collisions/TriggerTag.hpp"
#include "collisions/HierarchicalSpatialHash.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EventManager.hpp"
#include "events/CollisionObstacleChangedEvent.hpp"
#include "events/WorldTriggerEvent.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "utils/Vector2D.hpp"
#include <vector>
#include <chrono>
#include <random>
#include <atomic>

using namespace HammerEngine;

BOOST_AUTO_TEST_SUITE(AABBTests)

BOOST_AUTO_TEST_CASE(TestAABBBasicProperties)
{
    AABB aabb(10.0f, 20.0f, 5.0f, 7.5f);
    
    BOOST_CHECK_CLOSE(aabb.left(), 5.0f, 0.01f);
    BOOST_CHECK_CLOSE(aabb.right(), 15.0f, 0.01f);
    BOOST_CHECK_CLOSE(aabb.top(), 12.5f, 0.01f);
    BOOST_CHECK_CLOSE(aabb.bottom(), 27.5f, 0.01f);
}

BOOST_AUTO_TEST_CASE(TestAABBIntersection)
{
    AABB aabb1(10.0f, 10.0f, 5.0f, 5.0f);  // center at (10,10), size 10x10
    AABB aabb2(15.0f, 10.0f, 3.0f, 3.0f);  // center at (15,10), size 6x6
    AABB aabb3(20.0f, 10.0f, 2.0f, 2.0f);  // center at (20,10), size 4x4
    
    BOOST_CHECK(aabb1.intersects(aabb2));  // Should overlap
    BOOST_CHECK(aabb2.intersects(aabb1));  // Symmetry
    BOOST_CHECK(!aabb1.intersects(aabb3)); // Should not overlap
    BOOST_CHECK(!aabb3.intersects(aabb1)); // Symmetry
}

BOOST_AUTO_TEST_CASE(TestAABBContainsPoint)
{
    AABB aabb(10.0f, 10.0f, 5.0f, 5.0f);
    
    BOOST_CHECK(aabb.contains(Vector2D(10.0f, 10.0f)));  // Center
    BOOST_CHECK(aabb.contains(Vector2D(5.0f, 5.0f)));    // Corner
    BOOST_CHECK(aabb.contains(Vector2D(15.0f, 15.0f)));  // Opposite corner
    BOOST_CHECK(!aabb.contains(Vector2D(20.0f, 20.0f))); // Outside
    BOOST_CHECK(!aabb.contains(Vector2D(0.0f, 0.0f)));   // Outside
}

BOOST_AUTO_TEST_CASE(TestAABBClosestPoint)
{
    AABB aabb(10.0f, 10.0f, 5.0f, 5.0f);
    
    // Point inside should return itself
    Vector2D inside(10.0f, 10.0f);
    Vector2D closest1 = aabb.closestPoint(inside);
    BOOST_CHECK_CLOSE(closest1.getX(), inside.getX(), 0.01f);
    BOOST_CHECK_CLOSE(closest1.getY(), inside.getY(), 0.01f);
    
    // Point outside should clamp to edge
    Vector2D outside(20.0f, 20.0f);
    Vector2D closest2 = aabb.closestPoint(outside);
    BOOST_CHECK_CLOSE(closest2.getX(), 15.0f, 0.01f); // Right edge
    BOOST_CHECK_CLOSE(closest2.getY(), 15.0f, 0.01f); // Bottom edge
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(SpatialHashTests)

BOOST_AUTO_TEST_CASE(TestSpatialHashInsertAndQuery)
{
    HierarchicalSpatialHash spatialHash;
    
    // Insert a few entities
    AABB aabb1(16.0f, 16.0f, 8.0f, 8.0f);  // Single cell
    AABB aabb2(48.0f, 16.0f, 8.0f, 8.0f);  // Different cell
    AABB aabb3(32.0f, 32.0f, 16.0f, 16.0f); // Spans multiple cells
    
    size_t id1 = 1, id2 = 2, id3 = 3;
    spatialHash.insert(id1, aabb1);
    spatialHash.insert(id2, aabb2);
    spatialHash.insert(id3, aabb3);
    
    // Query first cell area
    std::vector<size_t> results;
    AABB queryArea(16.0f, 16.0f, 16.0f, 16.0f);
    spatialHash.queryRegion(queryArea, results);
    
    BOOST_CHECK_GE(results.size(), 1);
    BOOST_CHECK(std::find(results.begin(), results.end(), id1) != results.end());
}

BOOST_AUTO_TEST_CASE(TestSpatialHashRemove)
{
    HierarchicalSpatialHash spatialHash;
    
    EntityID id1 = 1;
    AABB aabb1(16.0f, 16.0f, 8.0f, 8.0f);
    
    spatialHash.insert(id1, aabb1);
    
    // Verify it's there
    std::vector<size_t> results;
    spatialHash.queryRegion(aabb1, results);
    BOOST_CHECK_GE(results.size(), 1);
    
    // Remove and verify it's gone
    spatialHash.remove(id1);
    results.clear();
    spatialHash.queryRegion(aabb1, results);
    BOOST_CHECK(std::find(results.begin(), results.end(), id1) == results.end());
}

BOOST_AUTO_TEST_CASE(TestSpatialHashUpdate)
{
    HierarchicalSpatialHash spatialHash;
    
    size_t id1 = 1;
    AABB oldAABB(100.0f, 100.0f, 8.0f, 8.0f);  // Coarse cell (0,0)
    AABB newAABB(300.0f, 300.0f, 8.0f, 8.0f);  // Coarse cell (1,1)

    spatialHash.insert(id1, oldAABB);

    // Update position
    spatialHash.update(id1, oldAABB, newAABB);
    
    // Should not be found in old area
    std::vector<size_t> oldResults;
    spatialHash.queryRegion(oldAABB, oldResults);
    BOOST_CHECK(std::find(oldResults.begin(), oldResults.end(), id1) == oldResults.end());
    
    // Should be found in new area
    std::vector<size_t> newResults;
    spatialHash.queryRegion(newAABB, newResults);
    BOOST_CHECK(std::find(newResults.begin(), newResults.end(), id1) != newResults.end());
}

BOOST_AUTO_TEST_CASE(TestSpatialHashSmallAndLargeMovement)
{
    // Testing small moves within same coarse region vs large moves to different regions
    HierarchicalSpatialHash spatialHash;

    size_t id = 42;
    AABB aabb(64.0f, 64.0f, 8.0f, 8.0f); // starts in coarse cell (0,0)
    spatialHash.insert(id, aabb);

    // Small movement within same coarse region: should still be findable
    AABB smallMove(66.0f, 64.0f, 8.0f, 8.0f); // move by 2px in X, still in (0,0)
    spatialHash.update(id, aabb, smallMove);

    // Query both original and slightly shifted area should still find the entity
    std::vector<size_t> results1, results2;
    spatialHash.queryRegion(aabb, results1);
    spatialHash.queryRegion(smallMove, results2);
    BOOST_CHECK(std::find(results1.begin(), results1.end(), id) != results1.end());
    BOOST_CHECK(std::find(results2.begin(), results2.end(), id) != results2.end());

    // Large movement to different coarse region
    AABB bigMove(300.0f, 300.0f, 8.0f, 8.0f); // coarse cell (1,1)
    spatialHash.update(id, smallMove, bigMove);

    // Should not be found near the original area anymore
    std::vector<size_t> results3;
    spatialHash.queryRegion(aabb, results3);
    BOOST_CHECK(std::find(results3.begin(), results3.end(), id) == results3.end());

    // Should be found at the new location
    std::vector<size_t> results4;
    spatialHash.queryRegion(bigMove, results4);
    BOOST_CHECK(std::find(results4.begin(), results4.end(), id) != results4.end());
}

BOOST_AUTO_TEST_CASE(TestSpatialHashClear)
{
    HierarchicalSpatialHash spatialHash;
    
    // Add several entities
    for (EntityID id = 1; id <= 5; ++id) {
        AABB aabb(id * 16.0f, id * 16.0f, 8.0f, 8.0f);
        spatialHash.insert(id, aabb);
    }
    
    // Clear all
    spatialHash.clear();
    
    // Query should return nothing
    std::vector<size_t> results;
    AABB largeQuery(0.0f, 0.0f, 200.0f, 200.0f);
    spatialHash.queryRegion(largeQuery, results);
    BOOST_CHECK_EQUAL(results.size(), 0);
}

BOOST_AUTO_TEST_CASE(TestSpatialHashNoDuplicates)
{
    HierarchicalSpatialHash spatialHash; // Small cells to force multi-cell entities
    
    size_t id1 = 1;
    AABB largeAABB(24.0f, 24.0f, 20.0f, 20.0f); // Spans multiple cells
    spatialHash.insert(id1, largeAABB);
    
    // Query overlapping the entity should return it only once
    std::vector<size_t> results;
    spatialHash.queryRegion(largeAABB, results);
    
    int count = std::count(results.begin(), results.end(), id1);
    BOOST_CHECK_EQUAL(count, 1);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(CollisionPerformanceTests)

BOOST_AUTO_TEST_CASE(TestSpatialHashPerformance)
{
    const int NUM_ENTITIES = 1000;
    const int NUM_QUERIES = 100;
    const float WORLD_SIZE = 1000.0f;
    
    HierarchicalSpatialHash spatialHash;
    
    // Generate random entities
    std::mt19937 rng(42); // Fixed seed for reproducible results
    std::uniform_real_distribution<float> posDist(0.0f, WORLD_SIZE);
    std::uniform_real_distribution<float> sizeDist(5.0f, 25.0f);
    
    std::vector<std::pair<EntityID, AABB>> entities;
    
    // Insertion performance test
    auto startInsert = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        EntityID id = static_cast<EntityID>(i + 1);
        float x = posDist(rng);
        float y = posDist(rng);
        float halfW = sizeDist(rng);
        float halfH = sizeDist(rng);
        
        AABB aabb(x, y, halfW, halfH);
        entities.emplace_back(id, aabb);
        spatialHash.insert(id, aabb);
    }
    
    auto endInsert = std::chrono::high_resolution_clock::now();
    auto insertDuration = std::chrono::duration_cast<std::chrono::microseconds>(endInsert - startInsert);
    
    BOOST_TEST_MESSAGE("Inserted " << NUM_ENTITIES << " entities in " 
                      << insertDuration.count() << " microseconds ("
                      << (insertDuration.count() / NUM_ENTITIES) << " μs per entity)");
    
    // Query performance test
    std::vector<size_t> results;
    int totalFound = 0;
    
    auto startQuery = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_QUERIES; ++i) {
        float queryX = posDist(rng);
        float queryY = posDist(rng);
        float querySize = 100.0f; // Fixed query size
        
        AABB queryArea(queryX, queryY, querySize, querySize);
        results.clear();
        spatialHash.queryRegion(queryArea, results);
        totalFound += results.size();
    }
    
    auto endQuery = std::chrono::high_resolution_clock::now();
    auto queryDuration = std::chrono::duration_cast<std::chrono::microseconds>(endQuery - startQuery);
    
    BOOST_TEST_MESSAGE("Performed " << NUM_QUERIES << " queries in " 
                      << queryDuration.count() << " microseconds ("
                      << (queryDuration.count() / NUM_QUERIES) << " μs per query)");
    BOOST_TEST_MESSAGE("Average entities found per query: " << (totalFound / NUM_QUERIES));
    
    // Performance requirements (adjust based on target performance)
    BOOST_CHECK_LT(insertDuration.count() / NUM_ENTITIES, 50); // < 50μs per insertion
    BOOST_CHECK_LT(queryDuration.count() / NUM_QUERIES, 100);  // < 100μs per query
}

BOOST_AUTO_TEST_CASE(TestSpatialHashUpdatePerformance)
{
    const int NUM_ENTITIES = 500;
    const int NUM_UPDATES = 1000;
    const float WORLD_SIZE = 500.0f;
    
    HierarchicalSpatialHash spatialHash;
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDist(0.0f, WORLD_SIZE);
    std::uniform_real_distribution<float> sizeDist(5.0f, 15.0f);
    std::uniform_int_distribution<int> idDist(1, NUM_ENTITIES);
    
    // Insert initial entities
    std::vector<std::pair<size_t, AABB>> entities;
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        size_t id = static_cast<size_t>(i);
        float x = posDist(rng);
        float y = posDist(rng);
        float halfW = sizeDist(rng);
        float halfH = sizeDist(rng);

        AABB aabb(x, y, halfW, halfH);
        entities.emplace_back(id, aabb);
        spatialHash.insert(id, aabb);
    }
    
    // Update performance test
    auto startUpdate = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_UPDATES; ++i) {
        // Pick random entity to update
        int entityIndex = idDist(rng) - 1;
        size_t id = entities[entityIndex].first;
        AABB oldAABB = entities[entityIndex].second;

        // Generate new position
        float newX = posDist(rng);
        float newY = posDist(rng);
        float halfW = oldAABB.halfSize.getX();
        float halfH = oldAABB.halfSize.getY();

        AABB newAABB(newX, newY, halfW, halfH);
        spatialHash.update(id, oldAABB, newAABB);
        entities[entityIndex].second = newAABB;
    }
    
    auto endUpdate = std::chrono::high_resolution_clock::now();
    auto updateDuration = std::chrono::duration_cast<std::chrono::microseconds>(endUpdate - startUpdate);
    
    BOOST_TEST_MESSAGE("Performed " << NUM_UPDATES << " updates in " 
                      << updateDuration.count() << " microseconds ("
                      << (updateDuration.count() / NUM_UPDATES) << " μs per update)");
    
    // Performance requirement
    BOOST_CHECK_LT(updateDuration.count() / NUM_UPDATES, 75); // < 75μs per update
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(CollisionStressTests)

BOOST_AUTO_TEST_CASE(TestHighDensityCollisions)
{
    const int ENTITIES_PER_CELL = 20;
    const int GRID_SIZE = 10;
    const float CELL_SIZE = 50.0f;
    const int TOTAL_ENTITIES = ENTITIES_PER_CELL * GRID_SIZE * GRID_SIZE;
    
    HierarchicalSpatialHash spatialHash;
    
    std::mt19937 rng(123);
    std::uniform_real_distribution<float> offsetDist(-20.0f, 20.0f);
    
    // Place multiple entities in each grid cell
    EntityID currentId = 1;
    for (int gridX = 0; gridX < GRID_SIZE; ++gridX) {
        for (int gridY = 0; gridY < GRID_SIZE; ++gridY) {
            float cellCenterX = (gridX + 0.5f) * CELL_SIZE;
            float cellCenterY = (gridY + 0.5f) * CELL_SIZE;
            
            for (int e = 0; e < ENTITIES_PER_CELL; ++e) {
                float x = cellCenterX + offsetDist(rng);
                float y = cellCenterY + offsetDist(rng);
                AABB aabb(x, y, 5.0f, 5.0f);
                
                spatialHash.insert(currentId++, aabb);
            }
        }
    }
    
    // Query each cell and verify reasonable entity counts
    int totalQueriesChecked = 0;
    for (int gridX = 0; gridX < GRID_SIZE; ++gridX) {
        for (int gridY = 0; gridY < GRID_SIZE; ++gridY) {
            float cellCenterX = (gridX + 0.5f) * CELL_SIZE;
            float cellCenterY = (gridY + 0.5f) * CELL_SIZE;
            
            AABB queryArea(cellCenterX, cellCenterY, CELL_SIZE * 0.4f, CELL_SIZE * 0.4f);
            std::vector<size_t> results;
            spatialHash.queryRegion(queryArea, results);
            
            // Should find at least some entities in each dense cell
            BOOST_CHECK_GE(results.size(), 1);
            totalQueriesChecked++;
        }
    }
    
    BOOST_TEST_MESSAGE("Stress test completed with " << TOTAL_ENTITIES 
                      << " entities across " << totalQueriesChecked << " cells");
}

BOOST_AUTO_TEST_CASE(TestBoundaryConditions)
{
    HierarchicalSpatialHash spatialHash;
    
    // Test entities exactly on cell boundaries
    EntityID id1 = 1;
    AABB boundaryAABB(32.0f, 32.0f, 1.0f, 1.0f); // Exactly on boundary
    spatialHash.insert(id1, boundaryAABB);
    
    // Query should find it in adjacent cells
    std::vector<size_t> results;
    AABB queryArea(31.0f, 31.0f, 2.0f, 2.0f);
    spatialHash.queryRegion(queryArea, results);
    
    BOOST_CHECK_GE(results.size(), 1);
    BOOST_CHECK(std::find(results.begin(), results.end(), id1) != results.end());
    
    // Test very large entities
    EntityID id2 = 2;
    AABB largeAABB(64.0f, 64.0f, 100.0f, 100.0f); // Spans many cells
    spatialHash.insert(id2, largeAABB);
    
    // Should be found in multiple query areas
    AABB query1(0.0f, 0.0f, 32.0f, 32.0f);
    AABB query2(128.0f, 128.0f, 32.0f, 32.0f);
    
    std::vector<size_t> results1, results2;
    spatialHash.queryRegion(query1, results1);
    spatialHash.queryRegion(query2, results2);
    
    bool foundInFirst = std::find(results1.begin(), results1.end(), id2) != results1.end();
    bool foundInSecond = std::find(results2.begin(), results2.end(), id2) != results2.end();
    
    BOOST_CHECK(foundInFirst || foundInSecond); // Should be found in at least one
}

BOOST_AUTO_TEST_SUITE_END()

// Dual Spatial Hash System Tests for CollisionManager
BOOST_AUTO_TEST_SUITE(DualSpatialHashTests)

BOOST_AUTO_TEST_CASE(TestStaticDynamicHashSeparation)
{
    // Initialize CollisionManager for testing
    CollisionManager::Instance().init();
    
    // Test that static and dynamic bodies are correctly separated into different spatial hashes
    EntityID staticId = 10000;
    EntityID kinematicId = 10002;  // Use only kinematic for simpler test
    
    Vector2D testPos(100.0f, 100.0f);
    AABB testAABB(testPos.getX(), testPos.getY(), 32.0f, 32.0f);
    
    // Add bodies of different types
    CollisionManager::Instance().addCollisionBodySOA(staticId, testAABB.center, testAABB.halfSize, BodyType::STATIC, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
    CollisionManager::Instance().addCollisionBodySOA(kinematicId, testAABB.center, testAABB.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();

    // Verify body count includes all types
    BOOST_CHECK_EQUAL(CollisionManager::Instance().getBodyCount(), 2);
    
    // Test that static body count is tracked separately
    BOOST_CHECK_EQUAL(CollisionManager::Instance().getStaticBodyCount(), 1);
    BOOST_CHECK_EQUAL(CollisionManager::Instance().getKinematicBodyCount(), 1); // Only kinematic body
    
    // Verify type checking methods work correctly
    BOOST_CHECK(CollisionManager::Instance().isKinematic(kinematicId));
    BOOST_CHECK(!CollisionManager::Instance().isDynamic(staticId));
    BOOST_CHECK(!CollisionManager::Instance().isKinematic(staticId));
    
    // Test with a dynamic body as well
    EntityID dynamicId = 10001;
    CollisionManager::Instance().addCollisionBodySOA(dynamicId, testAABB.center, testAABB.halfSize, BodyType::DYNAMIC, CollisionLayer::Layer_Player, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();

    BOOST_CHECK_EQUAL(CollisionManager::Instance().getBodyCount(), 3);
    BOOST_CHECK_EQUAL(CollisionManager::Instance().getStaticBodyCount(), 1);
    BOOST_CHECK_EQUAL(CollisionManager::Instance().getKinematicBodyCount(), 1); // Still only 1 kinematic
    BOOST_CHECK(CollisionManager::Instance().isDynamic(dynamicId));
    
    // Note: Both DYNAMIC and KINEMATIC bodies use the dynamic spatial hash internally
    // but are counted separately by type
    
    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(staticId);
    CollisionManager::Instance().removeCollisionBodySOA(kinematicId);
    CollisionManager::Instance().removeCollisionBodySOA(dynamicId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestBroadphasePerformanceWithDualHashes)
{
    // Test that broadphase performance is improved with separate static/dynamic hashes
    CollisionManager::Instance().init();
    
    const int NUM_STATIC_BODIES = 200; // Simulate world tiles
    const int NUM_DYNAMIC_BODIES = 20;  // Simulate NPCs
    
    std::vector<EntityID> staticBodies;
    std::vector<EntityID> dynamicBodies;
    
    // Add many static bodies (world tiles)
    for (int i = 0; i < NUM_STATIC_BODIES; ++i) {
        EntityID id = 20000 + i;
        float x = static_cast<float>(i % 20) * 64.0f; // Grid layout
        float y = static_cast<float>(i / 20) * 64.0f;
        AABB aabb(x, y, 32.0f, 32.0f);

        CollisionManager::Instance().addCollisionBodySOA(id, aabb.center, aabb.halfSize, BodyType::STATIC, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
        staticBodies.push_back(id);
    }

    // Add dynamic bodies (NPCs)
    for (int i = 0; i < NUM_DYNAMIC_BODIES; ++i) {
        EntityID id = 25000 + i;
        float x = 500.0f + static_cast<float>(i % 5) * 32.0f;
        float y = 500.0f + static_cast<float>(i / 5) * 32.0f;
        AABB aabb(x, y, 16.0f, 16.0f);

        CollisionManager::Instance().addCollisionBodySOA(id, aabb.center, aabb.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu);
        dynamicBodies.push_back(id);
    }
    CollisionManager::Instance().processPendingCommands();

    // Reset performance stats before measurement
    CollisionManager::Instance().resetPerfStats();
    
    // Run several collision detection cycles
    const int NUM_CYCLES = 10;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int cycle = 0; cycle < NUM_CYCLES; ++cycle) {
        CollisionManager::Instance().update(0.016f); // 60 FPS simulation
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Get performance statistics
    auto perfStats = CollisionManager::Instance().getPerfStats();
    
    // Performance assertions - broadphase should be fast with dual hashes
    BOOST_CHECK_LT(perfStats.lastBroadphaseMs, 0.5); // < 0.5ms broadphase
    BOOST_CHECK_LT(perfStats.lastTotalMs, 2.0);     // < 2ms total collision time
    
    // Average cycle time should be reasonable
    double avgCycleTimeMs = duration.count() / 1000.0 / NUM_CYCLES;
    BOOST_CHECK_LT(avgCycleTimeMs, 1.0); // < 1ms per collision cycle
    
    BOOST_TEST_MESSAGE("Dual hash broadphase: " << perfStats.lastBroadphaseMs << "ms, "
                      << "Total: " << perfStats.lastTotalMs << "ms, "
                      << "Avg cycle: " << avgCycleTimeMs << "ms");
    
    // Clean up
    for (EntityID id : staticBodies) {
        CollisionManager::Instance().removeCollisionBodySOA(id);
    }
    for (EntityID id : dynamicBodies) {
        CollisionManager::Instance().removeCollisionBodySOA(id);
    }
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestKinematicBatchUpdateWithDualHashes)
{
    // Test that batch kinematic updates work correctly with dual spatial hash system
    CollisionManager::Instance().init();
    
    const int NUM_KINEMATIC_BODIES = 50;
    std::vector<EntityID> kinematicBodies;
    
    // Add kinematic bodies
    for (int i = 0; i < NUM_KINEMATIC_BODIES; ++i) {
        EntityID id = 30000 + i;
        AABB aabb(i * 20.0f, i * 20.0f, 8.0f, 8.0f);

        CollisionManager::Instance().addCollisionBodySOA(id, aabb.center, aabb.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu);
        kinematicBodies.push_back(id);
    }
    CollisionManager::Instance().processPendingCommands();

    // Prepare batch update data
    std::vector<CollisionManager::KinematicUpdate> updates;
    for (int i = 0; i < NUM_KINEMATIC_BODIES; ++i) {
        EntityID id = kinematicBodies[i];
        Vector2D newPos(i * 25.0f + 100.0f, i * 25.0f + 100.0f); // Move all bodies
        Vector2D velocity(10.0f, 5.0f);
        updates.emplace_back(id, newPos, velocity);
    }
    
    // Measure batch update performance
    auto start = std::chrono::high_resolution_clock::now();
    
    CollisionManager::Instance().updateKinematicBatchSOA(updates);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Verify bodies were updated by checking position and that velocity was set
    Vector2D center;
    bool found = CollisionManager::Instance().getBodyCenter(kinematicBodies[0], center);
    BOOST_CHECK(found);
    BOOST_CHECK_CLOSE(center.getX(), 100.0f, 1.0f);
    BOOST_CHECK_CLOSE(center.getY(), 100.0f, 1.0f);

    // Verify last body was also updated correctly
    found = CollisionManager::Instance().getBodyCenter(kinematicBodies[NUM_KINEMATIC_BODIES-1], center);
    BOOST_CHECK(found);
    float expectedX = (NUM_KINEMATIC_BODIES-1) * 25.0f + 100.0f;
    float expectedY = (NUM_KINEMATIC_BODIES-1) * 25.0f + 100.0f;
    BOOST_CHECK_CLOSE(center.getX(), expectedX, 1.0f);
    BOOST_CHECK_CLOSE(center.getY(), expectedY, 1.0f);
    
    // Performance check - batch update should be fast
    double avgUpdateTimeUs = static_cast<double>(duration.count()) / NUM_KINEMATIC_BODIES;
    BOOST_CHECK_LT(avgUpdateTimeUs, 20.0); // < 20μs per body update
    
    BOOST_TEST_MESSAGE("Batch updated " << NUM_KINEMATIC_BODIES << " kinematic bodies in "
                      << duration.count() << "μs (" << avgUpdateTimeUs << "μs/body)");
    
    // Clean up
    for (EntityID id : kinematicBodies) {
        CollisionManager::Instance().removeCollisionBodySOA(id);
    }
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestStaticBodyCacheInvalidation)
{
    // Test that static body cache is properly invalidated when static bodies change
    CollisionManager::Instance().init();
    
    // Add a static body
    EntityID staticId = 40000;
    AABB staticAABB(200.0f, 200.0f, 32.0f, 32.0f);
    CollisionManager::Instance().addCollisionBodySOA(staticId, staticAABB.center, staticAABB.halfSize, BodyType::STATIC, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);

    // Add a kinematic body near the static body
    EntityID kinematicId = 40001;
    AABB kinematicAABB(220.0f, 220.0f, 16.0f, 16.0f);
    CollisionManager::Instance().addCollisionBodySOA(kinematicId, kinematicAABB.center, kinematicAABB.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();

    // Run collision detection to populate any caches
    CollisionManager::Instance().update(0.016f);
    
    // Add another static body that could affect collision detection
    EntityID staticId2 = 40002;
    AABB staticAABB2(240.0f, 240.0f, 32.0f, 32.0f);
    CollisionManager::Instance().addCollisionBodySOA(staticId2, staticAABB2.center, staticAABB2.halfSize, BodyType::STATIC, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();

    // Verify cache invalidation by checking that static body count is correct
    BOOST_CHECK_EQUAL(CollisionManager::Instance().getStaticBodyCount(), 2);
    
    // Run collision detection again - should handle the new static body correctly
    CollisionManager::Instance().update(0.016f);
    
    // Remove static body and verify cache invalidation
    CollisionManager::Instance().removeCollisionBodySOA(staticId);
    CollisionManager::Instance().processPendingCommands();
    BOOST_CHECK_EQUAL(CollisionManager::Instance().getStaticBodyCount(), 1);
    
    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(staticId2);
    CollisionManager::Instance().removeCollisionBodySOA(kinematicId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestTriggerSystemCreation)
{
    // Test trigger area creation and basic functionality
    CollisionManager::Instance().init();

    // Test createTriggerArea method
    AABB triggerAABB(100.0f, 100.0f, 50.0f, 50.0f);
    EntityID triggerId = CollisionManager::Instance().createTriggerArea(
        triggerAABB,
        HammerEngine::TriggerTag::Water,
        CollisionLayer::Layer_Environment,
        CollisionLayer::Layer_Player | CollisionLayer::Layer_Enemy
    );
    CollisionManager::Instance().processPendingCommands();

    BOOST_CHECK_NE(triggerId, 0); // Should return valid ID
    BOOST_CHECK(CollisionManager::Instance().isTrigger(triggerId));

    // Test createTriggerAreaAt convenience method
    EntityID triggerId2 = CollisionManager::Instance().createTriggerAreaAt(
        200.0f, 200.0f, 25.0f, 25.0f,
        HammerEngine::TriggerTag::Lava,
        CollisionLayer::Layer_Environment,
        CollisionLayer::Layer_Player
    );
    CollisionManager::Instance().processPendingCommands();

    BOOST_CHECK_NE(triggerId2, 0);
    BOOST_CHECK(CollisionManager::Instance().isTrigger(triggerId2));
    BOOST_CHECK_NE(triggerId, triggerId2); // Should be different IDs

    // Test that trigger bodies can be queried
    std::vector<EntityID> results;
    CollisionManager::Instance().queryArea(triggerAABB, results);
    BOOST_CHECK(std::find(results.begin(), results.end(), triggerId) != results.end());

    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(triggerId);
    CollisionManager::Instance().removeCollisionBodySOA(triggerId2);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestTriggerCooldowns)
{
    // Test trigger cooldown functionality
    CollisionManager::Instance().init();

    // Set default cooldown
    CollisionManager::Instance().setDefaultTriggerCooldown(1.5f);

    // Create a trigger
    EntityID triggerId = CollisionManager::Instance().createTriggerAreaAt(
        50.0f, 50.0f, 20.0f, 20.0f,
        HammerEngine::TriggerTag::Portal
    );
    CollisionManager::Instance().processPendingCommands();

    // Set specific cooldown for this trigger
    CollisionManager::Instance().setTriggerCooldown(triggerId, 2.0f);

    // Verify trigger was created
    BOOST_CHECK(CollisionManager::Instance().isTrigger(triggerId));

    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(triggerId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestBodyLayerFiltering)
{
    // Test collision layer filtering functionality
    CollisionManager::Instance().init();

    // Create bodies with different layers
    EntityID playerId = 5000;
    EntityID npcId = 5001;
    EntityID environmentId = 5002;

    AABB aabb(100.0f, 100.0f, 16.0f, 16.0f);

    // Add bodies
    CollisionManager::Instance().addCollisionBodySOA(playerId, aabb.center, aabb.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Player, 0xFFFFFFFFu);
    CollisionManager::Instance().addCollisionBodySOA(npcId, aabb.center, aabb.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu);
    CollisionManager::Instance().addCollisionBodySOA(environmentId, aabb.center, aabb.halfSize, BodyType::STATIC, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();

    // Set layers - Player collides with NPCs and environment
    CollisionManager::Instance().setBodyLayer(
        playerId,
        CollisionLayer::Layer_Player,
        CollisionLayer::Layer_Enemy | CollisionLayer::Layer_Environment
    );

    // NPC collides with players and environment, but not other NPCs
    CollisionManager::Instance().setBodyLayer(
        npcId,
        CollisionLayer::Layer_Enemy,
        CollisionLayer::Layer_Player | CollisionLayer::Layer_Environment
    );

    // Environment collides with everything
    const auto Layer_All = CollisionLayer::Layer_Default | CollisionLayer::Layer_Player | CollisionLayer::Layer_Enemy | CollisionLayer::Layer_Environment | CollisionLayer::Layer_Projectile | CollisionLayer::Layer_Trigger;
    CollisionManager::Instance().setBodyLayer(
        environmentId,
        CollisionLayer::Layer_Environment,
        Layer_All
    );

    // Test that bodies exist
    BOOST_CHECK(CollisionManager::Instance().isKinematic(playerId));
    BOOST_CHECK(CollisionManager::Instance().isKinematic(npcId));
    BOOST_CHECK(!CollisionManager::Instance().isKinematic(environmentId));

    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(playerId);
    CollisionManager::Instance().removeCollisionBodySOA(npcId);
    CollisionManager::Instance().removeCollisionBodySOA(environmentId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestBodyEnableDisable)
{
    // Test body enable/disable functionality
    CollisionManager::Instance().init();

    EntityID bodyId = 6000;
    AABB aabb(150.0f, 150.0f, 20.0f, 20.0f);

    CollisionManager::Instance().addCollisionBodySOA(bodyId, aabb.center, aabb.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Player, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();

    // Body should exist and be queryable
    std::vector<EntityID> results;
    CollisionManager::Instance().queryArea(aabb, results);
    BOOST_CHECK(std::find(results.begin(), results.end(), bodyId) != results.end());

    // Disable the body
    CollisionManager::Instance().setBodyEnabled(bodyId, false);

    // Re-enable the body
    CollisionManager::Instance().setBodyEnabled(bodyId, true);

    // Should still be queryable after re-enabling
    results.clear();
    CollisionManager::Instance().queryArea(aabb, results);
    BOOST_CHECK(std::find(results.begin(), results.end(), bodyId) != results.end());

    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(bodyId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestBodyResize)
{
    // Test body resize functionality
    CollisionManager::Instance().init();

    EntityID bodyId = 7000;
    AABB originalAABB(200.0f, 200.0f, 10.0f, 10.0f);

    CollisionManager::Instance().addCollisionBodySOA(bodyId, originalAABB.center, originalAABB.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Player, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();

    // Verify original position
    Vector2D center;
    bool found = CollisionManager::Instance().getBodyCenter(bodyId, center);
    BOOST_CHECK(found);
    BOOST_CHECK_CLOSE(center.getX(), 200.0f, 0.01f);
    BOOST_CHECK_CLOSE(center.getY(), 200.0f, 0.01f);

    // Resize the body
    CollisionManager::Instance().updateCollisionBodySizeSOA(bodyId, Vector2D(25.0f, 15.0f));

    // Position should remain the same, but size should change
    found = CollisionManager::Instance().getBodyCenter(bodyId, center);
    BOOST_CHECK(found);
    BOOST_CHECK_CLOSE(center.getX(), 200.0f, 0.01f);
    BOOST_CHECK_CLOSE(center.getY(), 200.0f, 0.01f);

    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(bodyId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestVelocityManagement)
{
    // Test velocity setting and batch velocity updates
    CollisionManager::Instance().init();

    EntityID bodyId = 8000;
    AABB aabb(100.0f, 100.0f, 8.0f, 8.0f);
    Vector2D velocity(15.0f, 10.0f);

    CollisionManager::Instance().addCollisionBodySOA(bodyId, aabb.center, aabb.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Player, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();

    // Set velocity individually
    CollisionManager::Instance().setVelocity(bodyId, velocity);

    // Test batch update with velocity
    std::vector<CollisionManager::KinematicUpdate> updates;
    Vector2D newPosition(120.0f, 110.0f);
    Vector2D newVelocity(20.0f, 5.0f);
    updates.emplace_back(bodyId, newPosition, newVelocity);

    CollisionManager::Instance().updateKinematicBatchSOA(updates);

    // Verify position was updated
    Vector2D center;
    bool found = CollisionManager::Instance().getBodyCenter(bodyId, center);
    BOOST_CHECK(found);
    BOOST_CHECK_CLOSE(center.getX(), 120.0f, 0.01f);
    BOOST_CHECK_CLOSE(center.getY(), 110.0f, 0.01f);

    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(bodyId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_SUITE_END()

// Collision Info and Index Tests
BOOST_AUTO_TEST_SUITE(CollisionInfoTests)

BOOST_AUTO_TEST_CASE(TestCollisionInfoIndicesIntegrity)
{
    // CRITICAL TEST: Verify that our CollisionInfo index optimization works correctly
    // This test validates that indexA and indexB are properly populated
    CollisionManager::Instance().init();

    EntityID bodyA = 60000;
    EntityID bodyB = 60001;

    // Create two overlapping bodies
    AABB aabbA(100.0f, 100.0f, 25.0f, 25.0f);
    AABB aabbB(120.0f, 120.0f, 25.0f, 25.0f);  // Overlapping

    CollisionManager::Instance().addCollisionBodySOA(bodyA, aabbA.center, aabbA.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Player, 0xFFFFFFFFu);
    CollisionManager::Instance().addCollisionBodySOA(bodyB, aabbB.center, aabbB.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();

    // Set up collision callback to inspect CollisionInfo
    std::vector<CollisionInfo> capturedCollisions;

    CollisionManager::Instance().addCollisionCallback([&capturedCollisions](const CollisionInfo& collision) {
        capturedCollisions.push_back(collision);
    });

    // Run collision detection
    CollisionManager::Instance().update(0.016f);

    // Verify we captured collisions
    BOOST_REQUIRE(!capturedCollisions.empty());

    for (const auto& collision : capturedCollisions) {
        // Verify entity IDs are valid
        BOOST_CHECK(collision.a != 0);
        BOOST_CHECK(collision.b != 0);

        // CRITICAL: Verify indices are populated and valid
        BOOST_CHECK_NE(collision.indexA, SIZE_MAX);  // Should not be default value
        BOOST_CHECK_NE(collision.indexB, SIZE_MAX);  // Should not be default value

        // Verify indices are within reasonable bounds
        size_t bodyCount = CollisionManager::Instance().getBodyCount();
        BOOST_CHECK_LT(collision.indexA, bodyCount);
        BOOST_CHECK_LT(collision.indexB, bodyCount);

        // Verify indices are different (can't collide with self)
        BOOST_CHECK_NE(collision.indexA, collision.indexB);

        // Verify collision normal is reasonable
        float normalLength = collision.normal.length();
        BOOST_CHECK_GT(normalLength, 0.1f);  // Should have meaningful normal

        // Verify penetration is positive for actual collision
        if (!collision.trigger) {
            BOOST_CHECK_GT(collision.penetration, 0.0f);
        }
    }

    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(bodyA);
    CollisionManager::Instance().removeCollisionBodySOA(bodyB);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_SUITE_END()

// Integration Tests for CollisionManager Event System
BOOST_AUTO_TEST_SUITE(CollisionIntegrationTests)

// Test fixture for manager integration tests
struct CollisionIntegrationFixture {
    CollisionIntegrationFixture() {
        // Initialize ThreadSystem first (following established pattern)
        if (!HammerEngine::ThreadSystem::Exists()) {
            HammerEngine::ThreadSystem::Instance().init(); // Auto-detect system threads
        }
        
        // Initialize EventManager for event testing
        EventManager::Instance().init();
        
        // Initialize CollisionManager
        CollisionManager::Instance().init();
        
        eventCount = 0;
        lastEventPosition = Vector2D(0, 0);
        lastEventRadius = 0.0f;
        lastEventDescription = "";
    }
    
    ~CollisionIntegrationFixture() {
        // Clean up in reverse order (following established pattern)
        CollisionManager::Instance().clean();
        EventManager::Instance().clean();
        // Note: Don't clean ThreadSystem as it's shared across tests
    }
    
    // Event tracking variables
    std::atomic<int> eventCount{0};
    Vector2D lastEventPosition;
    float lastEventRadius;
    std::string lastEventDescription;
};

BOOST_FIXTURE_TEST_CASE(TestCollisionManagerEventNotification, CollisionIntegrationFixture)
{
    // Subscribe to collision obstacle changed events
    auto token = EventManager::Instance().registerHandlerWithToken(
        EventTypeId::CollisionObstacleChanged,
        [this](const EventData& data) {
            if (data.isActive() && data.event) {
                auto obstacleEvent = std::dynamic_pointer_cast<CollisionObstacleChangedEvent>(data.event);
                if (obstacleEvent) {
                    eventCount++;
                    lastEventPosition = obstacleEvent->getPosition();
                    lastEventRadius = obstacleEvent->getRadius();
                    lastEventDescription = obstacleEvent->getDescription();
                }
            }
        });
    
    // Test 1: Adding a static body should trigger an event
    EntityID staticId = 1000;
    Vector2D staticPos(100.0f, 200.0f);
    AABB staticAABB(staticPos.getX(), staticPos.getY(), 32.0f, 32.0f);

    CollisionManager::Instance().addCollisionBodySOA(staticId, staticAABB.center, staticAABB.halfSize, BodyType::STATIC, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();

    // Process deferred events - CollisionManager fires events in Deferred mode
    // so we need to drain all events to ensure deterministic test behavior
    EventManager::Instance().drainAllDeferredEvents();

    // Should have received 1 event for the static body
    BOOST_CHECK_EQUAL(eventCount.load(), 1);
    BOOST_CHECK_CLOSE(lastEventPosition.getX(), staticPos.getX(), 0.01f);
    BOOST_CHECK_CLOSE(lastEventPosition.getY(), staticPos.getY(), 0.01f);
    BOOST_CHECK_GT(lastEventRadius, 32.0f); // Should be radius + safety margin
    BOOST_CHECK(lastEventDescription.find("Static obstacle added") != std::string::npos);
    
    // Test 2: Adding a kinematic body should NOT trigger an event
    EntityID kinematicId = 1001;
    AABB kinematicAABB(150.0f, 250.0f, 16.0f, 16.0f);
    int previousEventCount = eventCount.load();

    CollisionManager::Instance().addCollisionBodySOA(kinematicId, kinematicAABB.center, kinematicAABB.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();
    EventManager::Instance().drainAllDeferredEvents();  // Process any events (should be none for kinematic)

    // Event count should not have changed
    BOOST_CHECK_EQUAL(eventCount.load(), previousEventCount);
    
    // Test 3: Removing a static body should trigger an event
    CollisionManager::Instance().removeCollisionBodySOA(staticId);
    CollisionManager::Instance().processPendingCommands();
    EventManager::Instance().drainAllDeferredEvents();

    // Should have received another event for removal
    BOOST_CHECK_EQUAL(eventCount.load(), 2);
    BOOST_CHECK(lastEventDescription.find("Static obstacle removed") != std::string::npos);
    
    // Clean up
    EventManager::Instance().removeHandler(token);
}

BOOST_FIXTURE_TEST_CASE(TestCollisionEventRadiusCalculation, CollisionIntegrationFixture)
{
    // Subscribe to events
    auto token = EventManager::Instance().registerHandlerWithToken(
        EventTypeId::CollisionObstacleChanged,
        [this](const EventData& data) {
            if (data.isActive() && data.event) {
                auto obstacleEvent = std::dynamic_pointer_cast<CollisionObstacleChangedEvent>(data.event);
                if (obstacleEvent) {
                    eventCount++;
                    lastEventRadius = obstacleEvent->getRadius();
                }
            }
        });
    
    // Test different sized obstacles produce appropriate radii
    EntityID smallId = 2000;
    EntityID largeId = 2001;
    
    // Small obstacle: 10x10
    AABB smallAABB(0.0f, 0.0f, 5.0f, 5.0f);
    CollisionManager::Instance().addCollisionBodySOA(smallId, smallAABB.center, smallAABB.halfSize, BodyType::STATIC, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();
    EventManager::Instance().drainAllDeferredEvents();

    float smallRadius = lastEventRadius;
    BOOST_CHECK_GT(smallRadius, 5.0f); // Should be larger than half-size
    BOOST_CHECK_LT(smallRadius, 50.0f); // But reasonable

    // Large obstacle: 100x100
    AABB largeAABB(200.0f, 200.0f, 50.0f, 50.0f);
    CollisionManager::Instance().addCollisionBodySOA(largeId, largeAABB.center, largeAABB.halfSize, BodyType::STATIC, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();
    EventManager::Instance().drainAllDeferredEvents();

    float largeRadius = lastEventRadius;
    BOOST_CHECK_GT(largeRadius, smallRadius); // Large should have larger radius
    BOOST_CHECK_GT(largeRadius, 50.0f); // Should be larger than half-size + margin
    
    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(smallId);
    CollisionManager::Instance().removeCollisionBodySOA(largeId);
    EventManager::Instance().removeHandler(token);
}

BOOST_FIXTURE_TEST_CASE(TestCollisionEventPerformanceImpact, CollisionIntegrationFixture)
{
    // Test that event firing doesn't significantly impact collision performance
    std::atomic<int> eventCount{0};
    
    // Subscribe to events but don't do heavy work
    auto token = EventManager::Instance().registerHandlerWithToken(
        EventTypeId::CollisionObstacleChanged,
        [&eventCount](const EventData& data) {
            if (data.isActive() && data.event) {
                eventCount++;
            }
        });
    
    const int numBodies = 100;
    std::vector<EntityID> bodies;
    
    // Measure time to add many static bodies (which trigger events)
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numBodies; ++i) {
        EntityID id = 3000 + i;
        AABB aabb(i * 10.0f, i * 10.0f, 16.0f, 16.0f);
        CollisionManager::Instance().addCollisionBodySOA(id, aabb.center, aabb.halfSize, BodyType::STATIC, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
        bodies.push_back(id);
    }
    CollisionManager::Instance().processPendingCommands();

    // Process deferred events
    EventManager::Instance().update();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // The EventManager processes events in batches with a limit (base 32 + budget allocation).
    // Formula: maxToProcess = 32 + (budget.eventAllocated * 32)
    // This is expected behavior for performance reasons - verify the batching works correctly.
    size_t expectedBatchSize = 64; // base when ThreadSystem doesn't exist
    if (HammerEngine::ThreadSystem::Exists()) {
        const auto& budget = HammerEngine::WorkerBudgetManager::Instance().getBudget();
        expectedBatchSize = 32 + (budget.eventAllocated * 32);
    }
    int actualEvents = eventCount.load();
    BOOST_CHECK_EQUAL(actualEvents, static_cast<int>(expectedBatchSize));
    BOOST_TEST_MESSAGE("Event batching performance: " << actualEvents << "/" << numBodies
                      << " events processed in first batch (expected: " << expectedBatchSize << ")");
    
    // Performance check: shouldn't take more than 20ms total (generous for test environment)
    BOOST_CHECK_LT(duration.count(), 20000); // 20ms = 20,000 microseconds
    
    // Average time per body should be reasonable  
    double avgTimePerBody = static_cast<double>(duration.count()) / numBodies;
    BOOST_CHECK_LT(avgTimePerBody, 200.0); // 200 microseconds per body max
    
    BOOST_TEST_MESSAGE("Added " << numBodies << " static bodies with events in " 
                      << duration.count() << " μs (" << avgTimePerBody << " μs/body)");
    
    // Clean up
    for (EntityID id : bodies) {
        CollisionManager::Instance().removeCollisionBodySOA(id);
    }
    EventManager::Instance().removeHandler(token);
}

BOOST_AUTO_TEST_CASE(TestTriggerEventNotifications)
{
    // Test that trigger events are properly generated
    std::atomic<int> triggerEventCount{0};
    Vector2D lastTriggerPosition;
    HammerEngine::TriggerTag lastTriggerTag;
    bool lastTriggerEntering = false;

    // Subscribe to trigger events
    auto token = EventManager::Instance().registerHandlerWithToken(
        EventTypeId::WorldTrigger,
        [&](const EventData& data) {
            if (data.isActive() && data.event) {
                auto triggerEvent = std::dynamic_pointer_cast<WorldTriggerEvent>(data.event);
                if (triggerEvent) {
                    triggerEventCount++;
                    lastTriggerPosition = triggerEvent->getPosition();
                    lastTriggerTag = triggerEvent->getTag();
                    lastTriggerEntering = triggerEvent->getPhase() == TriggerPhase::Enter;
                }
            }
        });

    // Create a trigger
    EntityID triggerId = CollisionManager::Instance().createTriggerAreaAt(
        300.0f, 300.0f, 30.0f, 30.0f,
        HammerEngine::TriggerTag::Water
    );
    CollisionManager::Instance().processPendingCommands();

    BOOST_CHECK(CollisionManager::Instance().isTrigger(triggerId));

    // Note: Actual trigger event generation would require entity movement
    // and collision detection updates, which is tested in integration scenarios

    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(triggerId);
    EventManager::Instance().removeHandler(token);
}

BOOST_AUTO_TEST_CASE(TestWorldBounds)
{
    // Test world bounds functionality
    CollisionManager::Instance().init();

    // Set world bounds
    float minX = -500.0f, minY = -300.0f;
    float maxX = 1000.0f, maxY = 800.0f;
    CollisionManager::Instance().setWorldBounds(minX, minY, maxX, maxY);

    // Create a body within bounds
    EntityID bodyId = 9000;
    Vector2D validPosition(500.0f, 400.0f);
    AABB aabb(validPosition.getX(), validPosition.getY(), 20.0f, 20.0f);

    CollisionManager::Instance().addCollisionBodySOA(bodyId, aabb.center, aabb.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Player, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();

    // Verify body was created successfully
    Vector2D center;
    bool found = CollisionManager::Instance().getBodyCenter(bodyId, center);
    BOOST_CHECK(found);
    BOOST_CHECK_CLOSE(center.getX(), validPosition.getX(), 0.01f);
    BOOST_CHECK_CLOSE(center.getY(), validPosition.getY(), 0.01f);

    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(bodyId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestLayerCollisionFiltering)
{
    // Test that collision detection respects layer filtering
    CollisionManager::Instance().init();

    // Create two bodies that should NOT collide due to layer filtering
    EntityID player1Id = 10000;
    EntityID player2Id = 10001;
    AABB overlappingAABB(400.0f, 400.0f, 16.0f, 16.0f);

    CollisionManager::Instance().addCollisionBodySOA(player1Id, overlappingAABB.center, overlappingAABB.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Player, 0xFFFFFFFFu);
    CollisionManager::Instance().addCollisionBodySOA(player2Id, overlappingAABB.center, overlappingAABB.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Player, 0xFFFFFFFFu);
    CollisionManager::Instance().processPendingCommands();

    // Set both as players - players don't collide with other players
    CollisionManager::Instance().setBodyLayer(
        player1Id,
        CollisionLayer::Layer_Player,
        CollisionLayer::Layer_Enemy | CollisionLayer::Layer_Environment  // No Layer_Player
    );

    CollisionManager::Instance().setBodyLayer(
        player2Id,
        CollisionLayer::Layer_Player,
        CollisionLayer::Layer_Enemy | CollisionLayer::Layer_Environment  // No Layer_Player
    );

    // Even though AABBs overlap, layer filtering should prevent collision
    BOOST_CHECK(CollisionManager::Instance().isKinematic(player1Id));
    BOOST_CHECK(CollisionManager::Instance().isKinematic(player2Id));

    // Test overlap query - both should be found in same area
    std::vector<EntityID> results;
    CollisionManager::Instance().queryArea(overlappingAABB, results);
    BOOST_CHECK_GE(results.size(), 2);

    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(player1Id);
    CollisionManager::Instance().removeCollisionBodySOA(player2Id);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestMixedBodyTypeInteractions)
{
    // Test interactions between different body types
    CollisionManager::Instance().init();

    EntityID staticId = 11000;
    EntityID kinematicId = 11001;
    EntityID triggerId = 11002;

    Vector2D position(500.0f, 500.0f);
    AABB aabb(position.getX(), position.getY(), 25.0f, 25.0f);

    // Add different body types
    CollisionManager::Instance().addCollisionBodySOA(staticId, aabb.center, aabb.halfSize, BodyType::STATIC, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
    CollisionManager::Instance().addCollisionBodySOA(kinematicId, aabb.center, aabb.halfSize, BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu);

    triggerId = CollisionManager::Instance().createTriggerAreaAt(
        position.getX(), position.getY(), 25.0f, 25.0f,
        HammerEngine::TriggerTag::Checkpoint
    );
    CollisionManager::Instance().processPendingCommands();

    // Verify body types
    BOOST_CHECK(!CollisionManager::Instance().isKinematic(staticId));
    BOOST_CHECK(!CollisionManager::Instance().isDynamic(staticId));
    BOOST_CHECK(!CollisionManager::Instance().isTrigger(staticId));

    BOOST_CHECK(CollisionManager::Instance().isKinematic(kinematicId));
    BOOST_CHECK(!CollisionManager::Instance().isDynamic(kinematicId));
    BOOST_CHECK(!CollisionManager::Instance().isTrigger(kinematicId));

    BOOST_CHECK(CollisionManager::Instance().isTrigger(triggerId));
    BOOST_CHECK(!CollisionManager::Instance().isKinematic(triggerId));
    BOOST_CHECK(!CollisionManager::Instance().isDynamic(triggerId));

    // All should be queryable in the same area
    std::vector<EntityID> results;
    CollisionManager::Instance().queryArea(aabb, results);
    BOOST_CHECK_GE(results.size(), 3);

    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(staticId);
    CollisionManager::Instance().removeCollisionBodySOA(kinematicId);
    CollisionManager::Instance().removeCollisionBodySOA(triggerId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_SUITE_END()

// Spatial Hash Edge Case Tests
BOOST_AUTO_TEST_SUITE(CollisionSpatialHashTests)

BOOST_AUTO_TEST_CASE(TestGridHashEdgeCases)
{
    // Test spatial partitioning edge cases that could cause problems
    // Initialize ThreadSystem first (following established pattern)
    if (!HammerEngine::ThreadSystem::Exists()) {
        HammerEngine::ThreadSystem::Instance().init(); // Auto-detect system threads
    }

    CollisionManager::Instance().init();

    // Test 1: Bodies exactly at grid boundaries
    EntityID boundaryId = 40000;
    // Use exact cell boundary positions based on COARSE_CELL_SIZE = 128.0f
    float cellBoundary = 128.0f;
    AABB boundaryAABB(cellBoundary, cellBoundary, 10.0f, 10.0f);

    CollisionManager::Instance().addCollisionBodySOA(
        boundaryId, boundaryAABB.center, boundaryAABB.halfSize,
        BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu
    );
    CollisionManager::Instance().processPendingCommands(); // Process queued add command

    // Should be findable via area query
    std::vector<EntityID> results;
    CollisionManager::Instance().queryArea(boundaryAABB, results);
    BOOST_CHECK(std::find(results.begin(), results.end(), boundaryId) != results.end());

    // Test 2: Very large bodies spanning multiple cells
    EntityID largeId = 40001;
    AABB largeAABB(200.0f, 200.0f, 300.0f, 300.0f); // 600x600 body spanning many cells

    CollisionManager::Instance().addCollisionBodySOA(
        largeId, largeAABB.center, largeAABB.halfSize,
        BodyType::STATIC, CollisionLayer::Layer_Environment, 0xFFFFFFFFu
    );
    CollisionManager::Instance().processPendingCommands(); // Process queued add command

    // Should be findable from multiple query regions
    AABB queryTopLeft(50.0f, 50.0f, 20.0f, 20.0f);
    AABB queryBottomRight(350.0f, 350.0f, 20.0f, 20.0f);

    results.clear();
    CollisionManager::Instance().queryArea(queryTopLeft, results);
    bool foundInTopLeft = std::find(results.begin(), results.end(), largeId) != results.end();

    results.clear();
    CollisionManager::Instance().queryArea(queryBottomRight, results);
    bool foundInBottomRight = std::find(results.begin(), results.end(), largeId) != results.end();

    BOOST_CHECK(foundInTopLeft);
    BOOST_CHECK(foundInBottomRight);

    // Test 3: Bodies at extreme coordinates
    EntityID extremeId = 40002;
    AABB extremeAABB(-1000000.0f, -1000000.0f, 50.0f, 50.0f);

    CollisionManager::Instance().addCollisionBodySOA(
        extremeId, extremeAABB.center, extremeAABB.halfSize,
        BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu
    );
    CollisionManager::Instance().processPendingCommands(); // Process queued add command

    // Should still be queryable
    results.clear();
    CollisionManager::Instance().queryArea(extremeAABB, results);
    BOOST_CHECK(std::find(results.begin(), results.end(), extremeId) != results.end());

    // Test 4: Zero-sized bodies (degenerate case)
    EntityID zeroId = 40003;
    AABB zeroAABB(100.0f, 100.0f, 0.0f, 0.0f); // Zero size

    CollisionManager::Instance().addCollisionBodySOA(
        zeroId, zeroAABB.center, zeroAABB.halfSize,
        BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu
    );
    CollisionManager::Instance().processPendingCommands(); // Process queued add command

    // Should still be tracked and queryable
    results.clear();
    AABB zeroQuery(99.0f, 99.0f, 2.0f, 2.0f); // Small area around zero-sized body
    CollisionManager::Instance().queryArea(zeroQuery, results);
    BOOST_CHECK(std::find(results.begin(), results.end(), zeroId) != results.end());

    // Test 5: Bodies moving between fine/coarse grid transitions
    EntityID movingId = 40004;
    Vector2D startPos(64.0f, 64.0f); // Start in one fine cell
    AABB movingAABB(startPos.getX(), startPos.getY(), 15.0f, 15.0f);

    CollisionManager::Instance().addCollisionBodySOA(
        movingId, movingAABB.center, movingAABB.halfSize,
        BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu
    );
    CollisionManager::Instance().processPendingCommands(); // Process queued add command

    // Move across fine cell boundaries multiple times
    for (int i = 1; i <= 5; ++i) {
        Vector2D newPos(startPos.getX() + (i * 20.0f), startPos.getY() + (i * 20.0f));
        AABB newAABB(newPos.getX(), newPos.getY(), 15.0f, 15.0f);

        CollisionManager::Instance().updateCollisionBodyPositionSOA(
            movingId, newAABB.center
        );

        // Should still be queryable at new position
        results.clear();
        CollisionManager::Instance().queryArea(newAABB, results);
        BOOST_CHECK(std::find(results.begin(), results.end(), movingId) != results.end());
    }

    BOOST_TEST_MESSAGE("Grid hash edge case testing completed successfully");

    // Clean up
    CollisionManager::Instance().removeCollisionBodySOA(boundaryId);
    CollisionManager::Instance().removeCollisionBodySOA(largeId);
    CollisionManager::Instance().removeCollisionBodySOA(extremeId);
    CollisionManager::Instance().removeCollisionBodySOA(zeroId);
    CollisionManager::Instance().removeCollisionBodySOA(movingId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_SUITE_END()

// Tests for KinematicUpdate batch API - critical for AI entity movement optimization
BOOST_AUTO_TEST_SUITE(KinematicBatchTests)

BOOST_AUTO_TEST_CASE(TestUpdateKinematicBatchSOA)
{
    // Initialize collision manager
    CollisionManager::Instance().init();

    const int NUM_ENTITIES = 50;
    std::vector<EntityID> entityIds;

    // Create kinematic bodies
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        EntityID id = static_cast<EntityID>(1000 + i);
        entityIds.push_back(id);
        Vector2D pos(100.0f + i * 10.0f, 100.0f + i * 10.0f);
        Vector2D halfSize(8.0f, 8.0f);

        CollisionManager::Instance().addCollisionBodySOA(
            id, pos, halfSize,
            BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu
        );
    }
    CollisionManager::Instance().processPendingCommands();

    // Build batch updates
    std::vector<CollisionManager::KinematicUpdate> updates;
    updates.reserve(NUM_ENTITIES);
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        Vector2D newPos(200.0f + i * 10.0f, 200.0f + i * 10.0f);
        Vector2D velocity(1.0f, 0.5f);
        updates.emplace_back(entityIds[i], newPos, velocity);
    }

    // Apply batch update
    CollisionManager::Instance().updateKinematicBatchSOA(updates);
    CollisionManager::Instance().processPendingCommands();

    // Verify positions updated - query around new positions
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        Vector2D expectedPos(200.0f + i * 10.0f, 200.0f + i * 10.0f);
        AABB queryArea(expectedPos.getX(), expectedPos.getY(), 20.0f, 20.0f);
        std::vector<EntityID> results;
        CollisionManager::Instance().queryArea(queryArea, results);
        BOOST_CHECK(std::find(results.begin(), results.end(), entityIds[i]) != results.end());
    }

    // Cleanup
    for (auto id : entityIds) {
        CollisionManager::Instance().removeCollisionBodySOA(id);
    }
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestApplyBatchedKinematicUpdates)
{
    // Initialize collision manager
    CollisionManager::Instance().init();

    const int NUM_BATCHES = 4;
    const int ENTITIES_PER_BATCH = 25;
    std::vector<std::vector<EntityID>> batchEntityIds(NUM_BATCHES);

    // Create kinematic bodies for each batch
    for (int batch = 0; batch < NUM_BATCHES; ++batch) {
        for (int i = 0; i < ENTITIES_PER_BATCH; ++i) {
            EntityID id = static_cast<EntityID>(2000 + batch * 100 + i);
            batchEntityIds[batch].push_back(id);
            Vector2D pos(50.0f + batch * 200.0f + i * 5.0f, 50.0f + i * 5.0f);
            Vector2D halfSize(6.0f, 6.0f);

            CollisionManager::Instance().addCollisionBodySOA(
                id, pos, halfSize,
                BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu
            );
        }
    }
    CollisionManager::Instance().processPendingCommands();

    // Build batched updates (like AIManager does per-thread)
    std::vector<std::vector<CollisionManager::KinematicUpdate>> batchUpdates(NUM_BATCHES);
    for (int batch = 0; batch < NUM_BATCHES; ++batch) {
        batchUpdates[batch].reserve(ENTITIES_PER_BATCH);
        for (int i = 0; i < ENTITIES_PER_BATCH; ++i) {
            Vector2D newPos(100.0f + batch * 200.0f + i * 5.0f, 150.0f + i * 5.0f);
            batchUpdates[batch].emplace_back(batchEntityIds[batch][i], newPos);
        }
    }

    // Apply all batches at once (zero contention pattern)
    CollisionManager::Instance().applyBatchedKinematicUpdates(batchUpdates);
    CollisionManager::Instance().processPendingCommands();

    // Verify all entities moved correctly
    int entitiesFound = 0;
    for (int batch = 0; batch < NUM_BATCHES; ++batch) {
        for (int i = 0; i < ENTITIES_PER_BATCH; ++i) {
            Vector2D expectedPos(100.0f + batch * 200.0f + i * 5.0f, 150.0f + i * 5.0f);
            AABB queryArea(expectedPos.getX(), expectedPos.getY(), 15.0f, 15.0f);
            std::vector<EntityID> results;
            CollisionManager::Instance().queryArea(queryArea, results);
            if (std::find(results.begin(), results.end(), batchEntityIds[batch][i]) != results.end()) {
                entitiesFound++;
            }
        }
    }
    BOOST_CHECK_EQUAL(entitiesFound, NUM_BATCHES * ENTITIES_PER_BATCH);

    // Cleanup
    for (int batch = 0; batch < NUM_BATCHES; ++batch) {
        for (auto id : batchEntityIds[batch]) {
            CollisionManager::Instance().removeCollisionBodySOA(id);
        }
    }
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestApplyKinematicUpdatesSingleVector)
{
    // Initialize collision manager
    CollisionManager::Instance().init();

    const int NUM_ENTITIES = 30;
    std::vector<EntityID> entityIds;

    // Create kinematic bodies
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        EntityID id = static_cast<EntityID>(3000 + i);
        entityIds.push_back(id);
        Vector2D pos(300.0f + i * 8.0f, 300.0f);
        Vector2D halfSize(5.0f, 5.0f);

        CollisionManager::Instance().addCollisionBodySOA(
            id, pos, halfSize,
            BodyType::KINEMATIC, CollisionLayer::Layer_Player, 0xFFFFFFFFu
        );
    }
    CollisionManager::Instance().processPendingCommands();

    // Build single vector of updates (convenience API)
    std::vector<CollisionManager::KinematicUpdate> updates;
    updates.reserve(NUM_ENTITIES);
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        Vector2D newPos(400.0f + i * 8.0f, 400.0f);
        updates.emplace_back(entityIds[i], newPos);
    }

    // Apply updates using single-vector overload
    CollisionManager::Instance().applyKinematicUpdates(updates);
    CollisionManager::Instance().processPendingCommands();

    // Verify positions updated
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        Vector2D expectedPos(400.0f + i * 8.0f, 400.0f);
        AABB queryArea(expectedPos.getX(), expectedPos.getY(), 12.0f, 12.0f);
        std::vector<EntityID> results;
        CollisionManager::Instance().queryArea(queryArea, results);
        BOOST_CHECK(std::find(results.begin(), results.end(), entityIds[i]) != results.end());
    }

    // Cleanup
    for (auto id : entityIds) {
        CollisionManager::Instance().removeCollisionBodySOA(id);
    }
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestKinematicBatchPerformance)
{
    // Initialize collision manager
    CollisionManager::Instance().init();

    const int NUM_ENTITIES = 500;
    std::vector<EntityID> entityIds;

    // Create many kinematic bodies
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        EntityID id = static_cast<EntityID>(4000 + i);
        entityIds.push_back(id);
        Vector2D pos(static_cast<float>(i % 50) * 20.0f, static_cast<float>(i / 50) * 20.0f);
        Vector2D halfSize(8.0f, 8.0f);

        CollisionManager::Instance().addCollisionBodySOA(
            id, pos, halfSize,
            BodyType::KINEMATIC, CollisionLayer::Layer_Enemy, 0xFFFFFFFFu
        );
    }
    CollisionManager::Instance().processPendingCommands();

    // Build batch updates
    std::vector<CollisionManager::KinematicUpdate> updates;
    updates.reserve(NUM_ENTITIES);
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        Vector2D newPos(static_cast<float>(i % 50) * 20.0f + 5.0f, static_cast<float>(i / 50) * 20.0f + 5.0f);
        updates.emplace_back(entityIds[i], newPos);
    }

    // Measure batch update performance
    auto start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < 100; ++iter) {
        CollisionManager::Instance().updateKinematicBatchSOA(updates);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    BOOST_TEST_MESSAGE("Batch update of " << NUM_ENTITIES << " entities x 100 iterations: "
                      << duration.count() << " μs ("
                      << (duration.count() / 100) << " μs per batch)");

    // Performance requirement: batch update should be fast
    BOOST_CHECK_LT(duration.count() / 100, 5000); // Less than 5ms per batch of 500 entities

    // Cleanup
    for (auto id : entityIds) {
        CollisionManager::Instance().removeCollisionBodySOA(id);
    }
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_SUITE_END()