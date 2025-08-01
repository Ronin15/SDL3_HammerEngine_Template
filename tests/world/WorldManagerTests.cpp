/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE WorldManagerTests
#include <boost/test/unit_test.hpp>

#include "managers/WorldManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include "world/WorldData.hpp"
#include "core/Logger.hpp"
#include <memory>

using namespace HammerEngine;

class WorldManagerTestFixture {
public:
    WorldManagerTestFixture() {
        // Initialize WorldResourceManager first (required dependency)
        worldResourceManager = &WorldResourceManager::Instance();
        BOOST_REQUIRE(worldResourceManager != nullptr);
        bool resourceInitialized = worldResourceManager->init();
        BOOST_REQUIRE(resourceInitialized);
        
        // Initialize WorldManager
        worldManager = &WorldManager::Instance();
        BOOST_REQUIRE(worldManager != nullptr);
        bool managerInitialized = worldManager->init();
        BOOST_REQUIRE(managerInitialized);
    }
    
    ~WorldManagerTestFixture() {
        // Clean up in reverse order
        worldManager->clean();
        worldResourceManager->clean();
    }
    
protected:
    WorldResourceManager* worldResourceManager;
    WorldManager* worldManager;
};

BOOST_FIXTURE_TEST_SUITE(WorldManagerTestSuite, WorldManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestSingletonPattern) {
    WorldManager* instance1 = &WorldManager::Instance();
    WorldManager* instance2 = &WorldManager::Instance();
    
    BOOST_CHECK(instance1 == instance2);
    BOOST_CHECK(instance1 == worldManager);
}

BOOST_AUTO_TEST_CASE(TestInitialization) {
    BOOST_CHECK(worldManager->isInitialized());
    BOOST_CHECK(!worldManager->isShutdown());
    BOOST_CHECK(!worldManager->hasActiveWorld());
    BOOST_CHECK(worldManager->getCurrentWorldId().empty());
}

BOOST_AUTO_TEST_CASE(TestLoadNewWorld) {
    WorldGenerationConfig config;
    config.width = 20;
    config.height = 20;
    config.seed = 12345;
    config.elevationFrequency = 0.1f;
    config.humidityFrequency = 0.1f;
    config.waterLevel = 0.3f;
    config.mountainLevel = 0.7f;
    
    bool loadResult = worldManager->loadNewWorld(config);
    
    BOOST_CHECK(loadResult);
    BOOST_CHECK(worldManager->hasActiveWorld());
    BOOST_CHECK(!worldManager->getCurrentWorldId().empty());
    
    const WorldData* worldData = worldManager->getWorldData();
    BOOST_REQUIRE(worldData != nullptr);
    BOOST_CHECK_EQUAL(worldData->grid.size(), 20);
    BOOST_CHECK_EQUAL(worldData->grid[0].size(), 20);
}

BOOST_AUTO_TEST_CASE(TestGetTileAt) {
    WorldGenerationConfig config;
    config.width = 10;
    config.height = 10;
    config.seed = 54321;
    config.elevationFrequency = 0.2f;
    config.humidityFrequency = 0.2f;
    config.waterLevel = 0.3f;
    config.mountainLevel = 0.7f;
    
    BOOST_REQUIRE(worldManager->loadNewWorld(config));
    
    // Test valid positions
    const Tile* tile = worldManager->getTileAt(5, 5);
    BOOST_CHECK(tile != nullptr);
    
    tile = worldManager->getTileAt(0, 0);
    BOOST_CHECK(tile != nullptr);
    
    tile = worldManager->getTileAt(9, 9);
    BOOST_CHECK(tile != nullptr);
    
    // Test invalid positions
    tile = worldManager->getTileAt(-1, 5);
    BOOST_CHECK(tile == nullptr);
    
    tile = worldManager->getTileAt(5, -1);
    BOOST_CHECK(tile == nullptr);
    
    tile = worldManager->getTileAt(10, 5);
    BOOST_CHECK(tile == nullptr);
    
    tile = worldManager->getTileAt(5, 10);
    BOOST_CHECK(tile == nullptr);
}

BOOST_AUTO_TEST_CASE(TestIsValidPosition) {
    WorldGenerationConfig config;
    config.width = 15;
    config.height = 10;
    config.seed = 11111;
    config.elevationFrequency = 0.1f;
    config.humidityFrequency = 0.1f;
    config.waterLevel = 0.3f;
    config.mountainLevel = 0.7f;
    
    BOOST_REQUIRE(worldManager->loadNewWorld(config));
    
    // Test valid positions
    BOOST_CHECK(worldManager->isValidPosition(0, 0));
    BOOST_CHECK(worldManager->isValidPosition(14, 9));
    BOOST_CHECK(worldManager->isValidPosition(7, 5));
    
    // Test invalid positions
    BOOST_CHECK(!worldManager->isValidPosition(-1, 0));
    BOOST_CHECK(!worldManager->isValidPosition(0, -1));
    BOOST_CHECK(!worldManager->isValidPosition(15, 0));
    BOOST_CHECK(!worldManager->isValidPosition(0, 10));
    BOOST_CHECK(!worldManager->isValidPosition(20, 20));
}

BOOST_AUTO_TEST_CASE(TestUpdateTile) {
    WorldGenerationConfig config;
    config.width = 5;
    config.height = 5;
    config.seed = 22222;
    config.elevationFrequency = 0.1f;
    config.humidityFrequency = 0.1f;
    config.waterLevel = 0.3f;
    config.mountainLevel = 0.7f;
    
    BOOST_REQUIRE(worldManager->loadNewWorld(config));
    
    // Get original tile
    const Tile* originalTile = worldManager->getTileAt(2, 2);
    BOOST_REQUIRE(originalTile != nullptr);
    
    // Create modified tile
    Tile newTile = *originalTile;
    newTile.biome = Biome::DESERT;
    newTile.obstacleType = ObstacleType::ROCK;
    newTile.elevation = 0.8f;
    
    // Update tile
    bool updateResult = worldManager->updateTile(2, 2, newTile);
    BOOST_CHECK(updateResult);
    
    // Verify the update
    const Tile* updatedTile = worldManager->getTileAt(2, 2);
    BOOST_REQUIRE(updatedTile != nullptr);
    BOOST_CHECK_EQUAL(updatedTile->biome, Biome::DESERT);
    BOOST_CHECK_EQUAL(updatedTile->obstacleType, ObstacleType::ROCK);
    BOOST_CHECK_CLOSE(updatedTile->elevation, 0.8f, 0.001f);
    
    // Test invalid position update
    bool invalidUpdate = worldManager->updateTile(-1, -1, newTile);
    BOOST_CHECK(!invalidUpdate);
}

BOOST_AUTO_TEST_CASE(TestHarvestResource) {
    WorldGenerationConfig config;
    config.width = 50;
    config.height = 50;
    config.seed = 33333;
    config.elevationFrequency = 0.1f;
    config.humidityFrequency = 0.1f;
    config.waterLevel = 0.1f; // Low water level for more land
    config.mountainLevel = 0.9f; // High mountain level
    
    BOOST_REQUIRE(worldManager->loadNewWorld(config));
    
    // Find a tile with an obstacle
    int obstacleX = -1, obstacleY = -1;
    for (int y = 0; y < 50; ++y) {
        for (int x = 0; x < 50; ++x) {
            const Tile* tile = worldManager->getTileAt(x, y);
            if (tile && tile->obstacleType != ObstacleType::NONE) {
                obstacleX = x;
                obstacleY = y;
                break;
            }
        }
        if (obstacleX != -1) break;
    }
    
    // We should find at least one obstacle in a 50x50 world
    BOOST_REQUIRE(obstacleX != -1);
    BOOST_REQUIRE(obstacleY != -1);
    
    // Verify tile has obstacle before harvesting
    const Tile* beforeHarvest = worldManager->getTileAt(obstacleX, obstacleY);
    BOOST_REQUIRE(beforeHarvest != nullptr);
    BOOST_CHECK(beforeHarvest->obstacleType != ObstacleType::NONE);
    
    // Harvest the resource
    bool harvestResult = worldManager->handleHarvestResource(1, obstacleX, obstacleY);
    BOOST_CHECK(harvestResult);
    
    // Verify tile no longer has obstacle
    const Tile* afterHarvest = worldManager->getTileAt(obstacleX, obstacleY);
    BOOST_REQUIRE(afterHarvest != nullptr);
    BOOST_CHECK_EQUAL(afterHarvest->obstacleType, ObstacleType::NONE);
}

BOOST_AUTO_TEST_CASE(TestHarvestEmptyTile) {
    WorldGenerationConfig config;
    config.width = 10;
    config.height = 10;
    config.seed = 44444;
    config.elevationFrequency = 0.1f;
    config.humidityFrequency = 0.1f;
    config.waterLevel = 0.8f; // High water level, mostly water tiles
    config.mountainLevel = 0.9f;
    
    BOOST_REQUIRE(worldManager->loadNewWorld(config));
    
    // Find a tile without obstacles (likely water)
    int emptyX = -1, emptyY = -1;
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            const Tile* tile = worldManager->getTileAt(x, y);
            if (tile && tile->obstacleType == ObstacleType::NONE) {
                emptyX = x;
                emptyY = y;
                break;
            }
        }
        if (emptyX != -1) break;
    }
    
    BOOST_REQUIRE(emptyX != -1);
    
    // Try to harvest from empty tile
    bool harvestResult = worldManager->handleHarvestResource(1, emptyX, emptyY);
    BOOST_CHECK(!harvestResult); // Should fail
}

BOOST_AUTO_TEST_CASE(TestRenderingState) {
    BOOST_CHECK(worldManager->isRenderingEnabled()); // Default enabled
    
    worldManager->enableRendering(false);
    BOOST_CHECK(!worldManager->isRenderingEnabled());
    
    worldManager->enableRendering(true);
    BOOST_CHECK(worldManager->isRenderingEnabled());
}

BOOST_AUTO_TEST_CASE(TestCameraSettings) {
    worldManager->setCamera(10, 20);
    worldManager->setCameraViewport(80, 25);
    
    // Cannot directly test camera position as it's private,
    // but we can verify the methods don't crash
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestUnloadWorld) {
    WorldGenerationConfig config;
    config.width = 10;
    config.height = 10;
    config.seed = 55555;
    config.elevationFrequency = 0.1f;
    config.humidityFrequency = 0.1f;
    config.waterLevel = 0.3f;
    config.mountainLevel = 0.7f;
    
    BOOST_REQUIRE(worldManager->loadNewWorld(config));
    BOOST_CHECK(worldManager->hasActiveWorld());
    
    worldManager->unloadWorld();
    BOOST_CHECK(!worldManager->hasActiveWorld());
    BOOST_CHECK(worldManager->getCurrentWorldId().empty());
    
    // Verify tile access returns null after unload
    const Tile* tile = worldManager->getTileAt(5, 5);
    BOOST_CHECK(tile == nullptr);
}

BOOST_AUTO_TEST_CASE(TestMultipleWorldLoads) {
    WorldGenerationConfig config1;
    config1.width = 10;
    config1.height = 10;
    config1.seed = 1111;
    config1.elevationFrequency = 0.1f;
    config1.humidityFrequency = 0.1f;
    config1.waterLevel = 0.3f;
    config1.mountainLevel = 0.7f;
    
    WorldGenerationConfig config2;
    config2.width = 15;
    config2.height = 15;
    config2.seed = 2222;
    config2.elevationFrequency = 0.1f;
    config2.humidityFrequency = 0.1f;
    config2.waterLevel = 0.3f;
    config2.mountainLevel = 0.7f;
    
    // Load first world
    BOOST_REQUIRE(worldManager->loadNewWorld(config1));
    std::string firstWorldId = worldManager->getCurrentWorldId();
    
    const WorldData* firstWorldData = worldManager->getWorldData();
    BOOST_REQUIRE(firstWorldData != nullptr);
    BOOST_CHECK_EQUAL(firstWorldData->grid.size(), 10);
    
    // Load second world (should replace first)
    BOOST_REQUIRE(worldManager->loadNewWorld(config2));
    std::string secondWorldId = worldManager->getCurrentWorldId();
    
    const WorldData* secondWorldData = worldManager->getWorldData();
    BOOST_REQUIRE(secondWorldData != nullptr);
    BOOST_CHECK_EQUAL(secondWorldData->grid.size(), 15);
    
    // World IDs should be different
    BOOST_CHECK_NE(firstWorldId, secondWorldId);
}

BOOST_AUTO_TEST_SUITE_END()