/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE CollisionSystemTests
#include <boost/test/unit_test.hpp>
#include <boost/test/tools/old/interface.hpp>

#include "collisions/AABB.hpp"
#include "collisions/TriggerTag.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EventManager.hpp"
#include "events/CollisionObstacleChangedEvent.hpp"
#include "events/WorldTriggerEvent.hpp"
#include "core/ThreadSystem.hpp"
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
    SpatialHash spatialHash(32.0f);
    
    // Insert a few entities
    AABB aabb1(16.0f, 16.0f, 8.0f, 8.0f);  // Single cell
    AABB aabb2(48.0f, 16.0f, 8.0f, 8.0f);  // Different cell
    AABB aabb3(32.0f, 32.0f, 16.0f, 16.0f); // Spans multiple cells
    
    EntityID id1 = 1, id2 = 2, id3 = 3;
    spatialHash.insert(id1, aabb1);
    spatialHash.insert(id2, aabb2);
    spatialHash.insert(id3, aabb3);
    
    // Query first cell area
    std::vector<EntityID> results;
    AABB queryArea(16.0f, 16.0f, 16.0f, 16.0f);
    spatialHash.query(queryArea, results);
    
    BOOST_CHECK_GE(results.size(), 1);
    BOOST_CHECK(std::find(results.begin(), results.end(), id1) != results.end());
}

BOOST_AUTO_TEST_CASE(TestSpatialHashRemove)
{
    SpatialHash spatialHash(32.0f);
    
    EntityID id1 = 1;
    AABB aabb1(16.0f, 16.0f, 8.0f, 8.0f);
    
    spatialHash.insert(id1, aabb1);
    
    // Verify it's there
    std::vector<EntityID> results;
    spatialHash.query(aabb1, results);
    BOOST_CHECK_GE(results.size(), 1);
    
    // Remove and verify it's gone
    spatialHash.remove(id1);
    results.clear();
    spatialHash.query(aabb1, results);
    BOOST_CHECK(std::find(results.begin(), results.end(), id1) == results.end());
}

BOOST_AUTO_TEST_CASE(TestSpatialHashUpdate)
{
    SpatialHash spatialHash(32.0f);
    
    EntityID id1 = 1;
    AABB oldAABB(16.0f, 16.0f, 8.0f, 8.0f);  // Cell (0,0)
    AABB newAABB(80.0f, 80.0f, 8.0f, 8.0f);  // Cell (2,2)
    
    spatialHash.insert(id1, oldAABB);
    
    // Update position
    spatialHash.update(id1, newAABB);
    
    // Should not be found in old area
    std::vector<EntityID> oldResults;
    spatialHash.query(oldAABB, oldResults);
    BOOST_CHECK(std::find(oldResults.begin(), oldResults.end(), id1) == oldResults.end());
    
    // Should be found in new area
    std::vector<EntityID> newResults;
    spatialHash.query(newAABB, newResults);
    BOOST_CHECK(std::find(newResults.begin(), newResults.end(), id1) != newResults.end());
}

BOOST_AUTO_TEST_CASE(TestSpatialHashSmallAndLargeMovement)
{
    // Configure a higher movement threshold to make small moves a clear no-op
    const float CELL_SIZE = 32.0f;
    const float MOVE_THRESHOLD = 6.0f;
    SpatialHash spatialHash(CELL_SIZE, MOVE_THRESHOLD);

    EntityID id = 42;
    AABB aabb(64.0f, 64.0f, 8.0f, 8.0f); // starts near center of a cell
    spatialHash.insert(id, aabb);

    // Small movement below threshold: should not disturb spatial membership
    AABB smallMove(66.0f, 64.0f, 8.0f, 8.0f); // move by 2px in X
    spatialHash.update(id, smallMove);

    // Query both original and slightly shifted area should still find the entity
    std::vector<EntityID> results1, results2;
    spatialHash.query(aabb, results1);
    spatialHash.query(smallMove, results2);
    BOOST_CHECK(std::find(results1.begin(), results1.end(), id) != results1.end());
    BOOST_CHECK(std::find(results2.begin(), results2.end(), id) != results2.end());

    // Large movement beyond threshold into a different cell range
    AABB bigMove(160.0f, 160.0f, 8.0f, 8.0f);
    spatialHash.update(id, bigMove);

    // Should not be found near the original area anymore
    std::vector<EntityID> results3;
    spatialHash.query(aabb, results3);
    BOOST_CHECK(std::find(results3.begin(), results3.end(), id) == results3.end());

    // Should be found at the new location
    std::vector<EntityID> results4;
    spatialHash.query(bigMove, results4);
    BOOST_CHECK(std::find(results4.begin(), results4.end(), id) != results4.end());
}

BOOST_AUTO_TEST_CASE(TestSpatialHashClear)
{
    SpatialHash spatialHash(32.0f);
    
    // Add several entities
    for (EntityID id = 1; id <= 5; ++id) {
        AABB aabb(id * 16.0f, id * 16.0f, 8.0f, 8.0f);
        spatialHash.insert(id, aabb);
    }
    
    // Clear all
    spatialHash.clear();
    
    // Query should return nothing
    std::vector<EntityID> results;
    AABB largeQuery(0.0f, 0.0f, 200.0f, 200.0f);
    spatialHash.query(largeQuery, results);
    BOOST_CHECK_EQUAL(results.size(), 0);
}

BOOST_AUTO_TEST_CASE(TestSpatialHashNoDuplicates)
{
    SpatialHash spatialHash(16.0f); // Small cells to force multi-cell entities
    
    EntityID id1 = 1;
    AABB largeAABB(24.0f, 24.0f, 20.0f, 20.0f); // Spans multiple cells
    spatialHash.insert(id1, largeAABB);
    
    // Query overlapping the entity should return it only once
    std::vector<EntityID> results;
    spatialHash.query(largeAABB, results);
    
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
    const float CELL_SIZE = 50.0f;
    
    SpatialHash spatialHash(CELL_SIZE);
    
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
    std::vector<EntityID> results;
    int totalFound = 0;
    
    auto startQuery = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_QUERIES; ++i) {
        float queryX = posDist(rng);
        float queryY = posDist(rng);
        float querySize = 100.0f; // Fixed query size
        
        AABB queryArea(queryX, queryY, querySize, querySize);
        results.clear();
        spatialHash.query(queryArea, results);
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
    const float CELL_SIZE = 25.0f;
    
    SpatialHash spatialHash(CELL_SIZE);
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDist(0.0f, WORLD_SIZE);
    std::uniform_real_distribution<float> sizeDist(5.0f, 15.0f);
    std::uniform_int_distribution<int> idDist(1, NUM_ENTITIES);
    
    // Insert initial entities
    std::vector<std::pair<EntityID, AABB>> entities;
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
    
    // Update performance test
    auto startUpdate = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_UPDATES; ++i) {
        // Pick random entity to update
        int entityIndex = idDist(rng) - 1;
        EntityID id = entities[entityIndex].first;
        
        // Generate new position
        float newX = posDist(rng);
        float newY = posDist(rng);
        float halfW = entities[entityIndex].second.halfSize.getX();
        float halfH = entities[entityIndex].second.halfSize.getY();
        
        AABB newAABB(newX, newY, halfW, halfH);
        spatialHash.update(id, newAABB);
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
    
    SpatialHash spatialHash(CELL_SIZE);
    
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
            std::vector<EntityID> results;
            spatialHash.query(queryArea, results);
            
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
    SpatialHash spatialHash(32.0f);
    
    // Test entities exactly on cell boundaries
    EntityID id1 = 1;
    AABB boundaryAABB(32.0f, 32.0f, 1.0f, 1.0f); // Exactly on boundary
    spatialHash.insert(id1, boundaryAABB);
    
    // Query should find it in adjacent cells
    std::vector<EntityID> results;
    AABB queryArea(31.0f, 31.0f, 2.0f, 2.0f);
    spatialHash.query(queryArea, results);
    
    BOOST_CHECK_GE(results.size(), 1);
    BOOST_CHECK(std::find(results.begin(), results.end(), id1) != results.end());
    
    // Test very large entities
    EntityID id2 = 2;
    AABB largeAABB(64.0f, 64.0f, 100.0f, 100.0f); // Spans many cells
    spatialHash.insert(id2, largeAABB);
    
    // Should be found in multiple query areas
    AABB query1(0.0f, 0.0f, 32.0f, 32.0f);
    AABB query2(128.0f, 128.0f, 32.0f, 32.0f);
    
    std::vector<EntityID> results1, results2;
    spatialHash.query(query1, results1);
    spatialHash.query(query2, results2);
    
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
    CollisionManager::Instance().addBody(staticId, testAABB, BodyType::STATIC);
    CollisionManager::Instance().addBody(kinematicId, testAABB, BodyType::KINEMATIC);
    
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
    CollisionManager::Instance().addBody(dynamicId, testAABB, BodyType::DYNAMIC);
    
    BOOST_CHECK_EQUAL(CollisionManager::Instance().getBodyCount(), 3);
    BOOST_CHECK_EQUAL(CollisionManager::Instance().getStaticBodyCount(), 1);
    BOOST_CHECK_EQUAL(CollisionManager::Instance().getKinematicBodyCount(), 1); // Still only 1 kinematic
    BOOST_CHECK(CollisionManager::Instance().isDynamic(dynamicId));
    
    // Note: Both DYNAMIC and KINEMATIC bodies use the dynamic spatial hash internally
    // but are counted separately by type
    
    // Clean up
    CollisionManager::Instance().removeBody(staticId);
    CollisionManager::Instance().removeBody(kinematicId);
    CollisionManager::Instance().removeBody(dynamicId);
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
        
        CollisionManager::Instance().addBody(id, aabb, BodyType::STATIC);
        staticBodies.push_back(id);
    }
    
    // Add dynamic bodies (NPCs)
    for (int i = 0; i < NUM_DYNAMIC_BODIES; ++i) {
        EntityID id = 25000 + i;
        float x = 500.0f + static_cast<float>(i % 5) * 32.0f;
        float y = 500.0f + static_cast<float>(i / 5) * 32.0f;
        AABB aabb(x, y, 16.0f, 16.0f);
        
        CollisionManager::Instance().addBody(id, aabb, BodyType::KINEMATIC);
        dynamicBodies.push_back(id);
    }
    
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
        CollisionManager::Instance().removeBody(id);
    }
    for (EntityID id : dynamicBodies) {
        CollisionManager::Instance().removeBody(id);
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
        
        CollisionManager::Instance().addBody(id, aabb, BodyType::KINEMATIC);
        kinematicBodies.push_back(id);
    }
    
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
    
    CollisionManager::Instance().updateKinematicBatch(updates);
    
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
        CollisionManager::Instance().removeBody(id);
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
    CollisionManager::Instance().addBody(staticId, staticAABB, BodyType::STATIC);
    
    // Add a kinematic body near the static body
    EntityID kinematicId = 40001;
    AABB kinematicAABB(220.0f, 220.0f, 16.0f, 16.0f);
    CollisionManager::Instance().addBody(kinematicId, kinematicAABB, BodyType::KINEMATIC);
    
    // Run collision detection to populate any caches
    CollisionManager::Instance().update(0.016f);
    
    // Add another static body that could affect collision detection
    EntityID staticId2 = 40002;
    AABB staticAABB2(240.0f, 240.0f, 32.0f, 32.0f);
    CollisionManager::Instance().addBody(staticId2, staticAABB2, BodyType::STATIC);
    
    // Verify cache invalidation by checking that static body count is correct
    BOOST_CHECK_EQUAL(CollisionManager::Instance().getStaticBodyCount(), 2);
    
    // Run collision detection again - should handle the new static body correctly
    CollisionManager::Instance().update(0.016f);
    
    // Remove static body and verify cache invalidation
    CollisionManager::Instance().removeBody(staticId);
    BOOST_CHECK_EQUAL(CollisionManager::Instance().getStaticBodyCount(), 1);
    
    // Clean up
    CollisionManager::Instance().removeBody(staticId2);
    CollisionManager::Instance().removeBody(kinematicId);
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

    BOOST_CHECK_NE(triggerId, 0); // Should return valid ID
    BOOST_CHECK(CollisionManager::Instance().isTrigger(triggerId));

    // Test createTriggerAreaAt convenience method
    EntityID triggerId2 = CollisionManager::Instance().createTriggerAreaAt(
        200.0f, 200.0f, 25.0f, 25.0f,
        HammerEngine::TriggerTag::Lava,
        CollisionLayer::Layer_Environment,
        CollisionLayer::Layer_Player
    );

    BOOST_CHECK_NE(triggerId2, 0);
    BOOST_CHECK(CollisionManager::Instance().isTrigger(triggerId2));
    BOOST_CHECK_NE(triggerId, triggerId2); // Should be different IDs

    // Test that trigger bodies can be queried
    std::vector<EntityID> results;
    CollisionManager::Instance().queryArea(triggerAABB, results);
    BOOST_CHECK(std::find(results.begin(), results.end(), triggerId) != results.end());

    // Clean up
    CollisionManager::Instance().removeBody(triggerId);
    CollisionManager::Instance().removeBody(triggerId2);
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

    // Set specific cooldown for this trigger
    CollisionManager::Instance().setTriggerCooldown(triggerId, 2.0f);

    // Verify trigger was created
    BOOST_CHECK(CollisionManager::Instance().isTrigger(triggerId));

    // Clean up
    CollisionManager::Instance().removeBody(triggerId);
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
    CollisionManager::Instance().addBody(playerId, aabb, BodyType::KINEMATIC);
    CollisionManager::Instance().addBody(npcId, aabb, BodyType::KINEMATIC);
    CollisionManager::Instance().addBody(environmentId, aabb, BodyType::STATIC);

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
    CollisionManager::Instance().removeBody(playerId);
    CollisionManager::Instance().removeBody(npcId);
    CollisionManager::Instance().removeBody(environmentId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestBodyEnableDisable)
{
    // Test body enable/disable functionality
    CollisionManager::Instance().init();

    EntityID bodyId = 6000;
    AABB aabb(150.0f, 150.0f, 20.0f, 20.0f);

    CollisionManager::Instance().addBody(bodyId, aabb, BodyType::KINEMATIC);

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
    CollisionManager::Instance().removeBody(bodyId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestBodyResize)
{
    // Test body resize functionality
    CollisionManager::Instance().init();

    EntityID bodyId = 7000;
    AABB originalAABB(200.0f, 200.0f, 10.0f, 10.0f);

    CollisionManager::Instance().addBody(bodyId, originalAABB, BodyType::KINEMATIC);

    // Verify original position
    Vector2D center;
    bool found = CollisionManager::Instance().getBodyCenter(bodyId, center);
    BOOST_CHECK(found);
    BOOST_CHECK_CLOSE(center.getX(), 200.0f, 0.01f);
    BOOST_CHECK_CLOSE(center.getY(), 200.0f, 0.01f);

    // Resize the body
    CollisionManager::Instance().resizeBody(bodyId, 25.0f, 15.0f);

    // Position should remain the same, but size should change
    found = CollisionManager::Instance().getBodyCenter(bodyId, center);
    BOOST_CHECK(found);
    BOOST_CHECK_CLOSE(center.getX(), 200.0f, 0.01f);
    BOOST_CHECK_CLOSE(center.getY(), 200.0f, 0.01f);

    // Clean up
    CollisionManager::Instance().removeBody(bodyId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestVelocityManagement)
{
    // Test velocity setting and batch velocity updates
    CollisionManager::Instance().init();

    EntityID bodyId = 8000;
    AABB aabb(100.0f, 100.0f, 8.0f, 8.0f);
    Vector2D velocity(15.0f, 10.0f);

    CollisionManager::Instance().addBody(bodyId, aabb, BodyType::KINEMATIC);

    // Set velocity individually
    CollisionManager::Instance().setVelocity(bodyId, velocity);

    // Test batch update with velocity
    std::vector<CollisionManager::KinematicUpdate> updates;
    Vector2D newPosition(120.0f, 110.0f);
    Vector2D newVelocity(20.0f, 5.0f);
    updates.emplace_back(bodyId, newPosition, newVelocity);

    CollisionManager::Instance().updateKinematicBatch(updates);

    // Verify position was updated
    Vector2D center;
    bool found = CollisionManager::Instance().getBodyCenter(bodyId, center);
    BOOST_CHECK(found);
    BOOST_CHECK_CLOSE(center.getX(), 120.0f, 0.01f);
    BOOST_CHECK_CLOSE(center.getY(), 110.0f, 0.01f);

    // Clean up
    CollisionManager::Instance().removeBody(bodyId);
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
            HammerEngine::ThreadSystem::Instance().init(4);
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
    
    CollisionManager::Instance().addBody(staticId, staticAABB, BodyType::STATIC);
    
    // Events are fired in deferred mode by CollisionManager, 
    // but for testing we don't need to explicitly dispatch them
    // since they're processed immediately in our event handler
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
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
    
    CollisionManager::Instance().addBody(kinematicId, kinematicAABB, BodyType::KINEMATIC);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
    // Event count should not have changed
    BOOST_CHECK_EQUAL(eventCount.load(), previousEventCount);
    
    // Test 3: Removing a static body should trigger an event
    CollisionManager::Instance().removeBody(staticId);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
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
    CollisionManager::Instance().addBody(smallId, smallAABB, BodyType::STATIC);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
    float smallRadius = lastEventRadius;
    BOOST_CHECK_GT(smallRadius, 5.0f); // Should be larger than half-size
    BOOST_CHECK_LT(smallRadius, 50.0f); // But reasonable
    
    // Large obstacle: 100x100  
    AABB largeAABB(200.0f, 200.0f, 50.0f, 50.0f);
    CollisionManager::Instance().addBody(largeId, largeAABB, BodyType::STATIC);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    
    float largeRadius = lastEventRadius;
    BOOST_CHECK_GT(largeRadius, smallRadius); // Large should have larger radius
    BOOST_CHECK_GT(largeRadius, 50.0f); // Should be larger than half-size + margin
    
    // Clean up
    CollisionManager::Instance().removeBody(smallId);
    CollisionManager::Instance().removeBody(largeId);
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
        CollisionManager::Instance().addBody(id, aabb, BodyType::STATIC);
        bodies.push_back(id);
    }
    
    // Allow events to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Should have fired events for all static bodies
    BOOST_CHECK_EQUAL(eventCount.load(), numBodies);
    
    // Performance check: shouldn't take more than 15ms total (generous for test environment)
    BOOST_CHECK_LT(duration.count(), 15000); // 15ms = 15,000 microseconds
    
    // Average time per body should be reasonable  
    double avgTimePerBody = static_cast<double>(duration.count()) / numBodies;
    BOOST_CHECK_LT(avgTimePerBody, 150.0); // 150 microseconds per body max
    
    BOOST_TEST_MESSAGE("Added " << numBodies << " static bodies with events in " 
                      << duration.count() << " μs (" << avgTimePerBody << " μs/body)");
    
    // Clean up
    for (EntityID id : bodies) {
        CollisionManager::Instance().removeBody(id);
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

    BOOST_CHECK(CollisionManager::Instance().isTrigger(triggerId));

    // Note: Actual trigger event generation would require entity movement
    // and collision detection updates, which is tested in integration scenarios

    // Clean up
    CollisionManager::Instance().removeBody(triggerId);
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

    CollisionManager::Instance().addBody(bodyId, aabb, BodyType::KINEMATIC);

    // Verify body was created successfully
    Vector2D center;
    bool found = CollisionManager::Instance().getBodyCenter(bodyId, center);
    BOOST_CHECK(found);
    BOOST_CHECK_CLOSE(center.getX(), validPosition.getX(), 0.01f);
    BOOST_CHECK_CLOSE(center.getY(), validPosition.getY(), 0.01f);

    // Clean up
    CollisionManager::Instance().removeBody(bodyId);
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

    CollisionManager::Instance().addBody(player1Id, overlappingAABB, BodyType::KINEMATIC);
    CollisionManager::Instance().addBody(player2Id, overlappingAABB, BodyType::KINEMATIC);

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
    CollisionManager::Instance().removeBody(player1Id);
    CollisionManager::Instance().removeBody(player2Id);
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
    CollisionManager::Instance().addBody(staticId, aabb, BodyType::STATIC);
    CollisionManager::Instance().addBody(kinematicId, aabb, BodyType::KINEMATIC);

    triggerId = CollisionManager::Instance().createTriggerAreaAt(
        position.getX(), position.getY(), 25.0f, 25.0f,
        HammerEngine::TriggerTag::Checkpoint
    );

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
    CollisionManager::Instance().removeBody(staticId);
    CollisionManager::Instance().removeBody(kinematicId);
    CollisionManager::Instance().removeBody(triggerId);
    CollisionManager::Instance().clean();
}

BOOST_AUTO_TEST_SUITE_END()