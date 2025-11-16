/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "world/WorldGenerator.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace HammerEngine {

WorldGenerator::PerlinNoise::PerlinNoise(int seed) {
  permutation.resize(256);
  std::iota(permutation.begin(), permutation.end(), 0);

  std::default_random_engine engine(seed);
  std::shuffle(permutation.begin(), permutation.end(), engine);

  auto copy = permutation;
  permutation.insert(permutation.end(), copy.begin(), copy.end());
}

float WorldGenerator::PerlinNoise::fade(float t) const {
  return t * t * t * (t * (t * 6 - 15) + 10);
}

float WorldGenerator::PerlinNoise::lerp(float t, float a, float b) const {
  return a + t * (b - a);
}

float WorldGenerator::PerlinNoise::grad(int hash, float x, float y) const {
  int h = hash & 15;
  float u = h < 8 ? x : y;
  float v = h < 4 ? y : h == 12 || h == 14 ? x : 0;
  return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float WorldGenerator::PerlinNoise::noise(float x, float y) const {
  int X = static_cast<int>(std::floor(x)) & 255;
  int Y = static_cast<int>(std::floor(y)) & 255;

  x -= std::floor(x);
  y -= std::floor(y);

  float u = fade(x);
  float v = fade(y);

  int A = permutation[X] + Y;
  int AA = permutation[A];
  int AB = permutation[A + 1];
  int B = permutation[X + 1] + Y;
  int BA = permutation[B];
  int BB = permutation[B + 1];

  return lerp(
      v, lerp(u, grad(permutation[AA], x, y), grad(permutation[BA], x - 1, y)),
      lerp(u, grad(permutation[AB], x, y - 1),
           grad(permutation[BB], x - 1, y - 1)));
}

std::unique_ptr<WorldData>
WorldGenerator::generateWorld(const WorldGenerationConfig &config,
                              const WorldGenerationProgressCallback& progressCallback) {
  WORLD_MANAGER_INFO("Generating world: " + std::to_string(config.width) + "x" +
                     std::to_string(config.height) + " with seed " +
                     std::to_string(config.seed));

  // Report initial progress
  if (progressCallback) {
    progressCallback(0.0f, "Initializing world generation...");
  }

  std::vector<std::vector<float>> elevationMap, humidityMap;
  auto world = generateNoiseMaps(config, elevationMap, humidityMap);

  if (!world) {
    WORLD_MANAGER_ERROR("Failed to generate noise maps for world");
    return nullptr;
  }

  // Progress: Noise maps complete (30%)
  if (progressCallback) {
    progressCallback(30.0f, "Generating terrain...");
  }

  assignBiomes(*world, elevationMap, humidityMap, config);

  // Progress: Biomes assigned (50%)
  if (progressCallback) {
    progressCallback(50.0f, "Creating biomes...");
  }

  createWaterBodies(*world, elevationMap, config);

  // Progress: Water bodies created (70%)
  if (progressCallback) {
    progressCallback(70.0f, "Placing water...");
  }

  distributeObstacles(*world, config);

  // Progress: Obstacles distributed (90%)
  if (progressCallback) {
    progressCallback(90.0f, "Distributing obstacles...");
  }

  calculateInitialResources(*world);

  // Progress: Complete (100%)
  if (progressCallback) {
    progressCallback(100.0f, "Finalizing world...");
  }

  WORLD_MANAGER_INFO("World generation completed successfully");
  return world;
}

std::unique_ptr<WorldData> WorldGenerator::generateNoiseMaps(
    const WorldGenerationConfig &config,
    std::vector<std::vector<float>> &elevationMap,
    std::vector<std::vector<float>> &humidityMap) {
  auto world = std::make_unique<WorldData>();
  world->worldId = "generated_" + std::to_string(config.seed);
  world->grid.resize(config.height, std::vector<Tile>(config.width));

  elevationMap.resize(config.height, std::vector<float>(config.width));
  humidityMap.resize(config.height, std::vector<float>(config.width));

  PerlinNoise elevationNoise(config.seed);
  PerlinNoise humidityNoise(config.seed + 1000);

  for (int y = 0; y < config.height; ++y) {
    for (int x = 0; x < config.width; ++x) {
      float elevation = elevationNoise.noise(x * config.elevationFrequency,
                                             y * config.elevationFrequency);
      elevation = (elevation + 1.0f) * 0.5f; // Normalize to [0, 1]

      float humidity = humidityNoise.noise(x * config.humidityFrequency,
                                           y * config.humidityFrequency);
      humidity = (humidity + 1.0f) * 0.5f; // Normalize to [0, 1]

      elevationMap[y][x] = elevation;
      humidityMap[y][x] = humidity;
      world->grid[y][x].elevation = elevation;
    }
  }

  return world;
}

void WorldGenerator::assignBiomes(
    WorldData &world, const std::vector<std::vector<float>> &elevationMap,
    const std::vector<std::vector<float>> &humidityMap,
    const WorldGenerationConfig &config) {
  int height = world.grid.size();
  int width = world.grid[0].size();

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      float elevation = elevationMap[y][x];
      float humidity = humidityMap[y][x];

      Biome biome;

      if (elevation < config.waterLevel) {
        biome = Biome::OCEAN;
        world.grid[y][x].isWater = true;
      } else if (elevation >= config.mountainLevel) {
        biome = Biome::MOUNTAIN;
      } else {
        // Assign biome based on humidity and elevation
        if (humidity < 0.3f) {
          biome = Biome::DESERT;
        } else if (humidity > 0.7f && elevation < 0.4f) {
          biome = Biome::SWAMP;
        } else if (elevation > 0.6f && humidity > 0.5f) {
          biome = Biome::FOREST;
        } else {
          // Special biomes with lower probability
          std::default_random_engine rng(config.seed + x * 1000 + y);
          std::uniform_real_distribution<float> dist(0.0f, 1.0f);

          float special = dist(rng);
          if (special < 0.05f) {
            biome = Biome::HAUNTED;
          } else if (special < 0.1f) {
            biome = Biome::CELESTIAL;
          } else {
            biome = Biome::FOREST;
          }
        }
      }

      world.grid[y][x].biome = biome;
    }
  }
}

void WorldGenerator::createWaterBodies(
    WorldData &world, const std::vector<std::vector<float>> &elevationMap,
    const WorldGenerationConfig &config) {
  int height = world.grid.size();
  int width = world.grid[0].size();

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (elevationMap[y][x] < config.waterLevel) {
        world.grid[y][x].isWater = true;
        world.grid[y][x].biome = Biome::OCEAN;
        world.grid[y][x].obstacleType = ObstacleType::NONE;
      }
    }
  }

  // Create rivers by connecting low elevation areas
  std::default_random_engine rng(config.seed + 5000);
  std::uniform_int_distribution<int> xDist(0, width - 1);
  std::uniform_int_distribution<int> yDist(0, height - 1);

  int riverCount = std::max(1, (width * height) / 1000);

  for (int i = 0; i < riverCount; ++i) {
    int startX = xDist(rng);
    int startY = yDist(rng);

    if (elevationMap[startY][startX] > config.waterLevel + 0.2f) {
      int currentX = startX;
      int currentY = startY;

      // Flow downhill for up to 50 steps
      for (int step = 0; step < 50; ++step) {
        float currentElevation = elevationMap[currentY][currentX];

        // Find lowest neighboring tile
        int bestX = currentX;
        int bestY = currentY;
        float lowestElevation = currentElevation;

        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0)
              continue;

            int newX = currentX + dx;
            int newY = currentY + dy;

            if (newX >= 0 && newX < width && newY >= 0 && newY < height) {
              float neighborElevation = elevationMap[newY][newX];
              if (neighborElevation < lowestElevation) {
                lowestElevation = neighborElevation;
                bestX = newX;
                bestY = newY;
              }
            }
          }
        }

        // If we found a lower neighbor, create water and continue
        if (bestX != currentX || bestY != currentY) {
          if (!world.grid[currentY][currentX].isWater) {
            world.grid[currentY][currentX].isWater = true;
            world.grid[currentY][currentX].biome = Biome::OCEAN;
            world.grid[currentY][currentX].obstacleType = ObstacleType::NONE;
          }
          currentX = bestX;
          currentY = bestY;
        } else {
          break; // No lower neighbor found, stop the river
        }
      }
    }
  }
}

void WorldGenerator::distributeObstacles(WorldData &world,
                                         const WorldGenerationConfig &config) {
  int height = world.grid.size();
  int width = world.grid[0].size();

  std::default_random_engine rng(config.seed + 10000);
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      Tile &tile = world.grid[y][x];

      if (tile.isWater) {
        continue; // No obstacles in water
      }

      float obstacleChance = 0.0f;
      ObstacleType obstacleType = ObstacleType::NONE;

      switch (tile.biome) {
      case Biome::FOREST:
        obstacleChance = 0.4f;
        obstacleType = ObstacleType::TREE;
        break;
      case Biome::MOUNTAIN:
        obstacleChance = 0.3f;
        obstacleType = ObstacleType::ROCK;
        break;
      case Biome::SWAMP:
        obstacleChance = 0.2f;
        obstacleType =
            dist(rng) < 0.7f ? ObstacleType::TREE : ObstacleType::WATER;
        break;
      case Biome::DESERT:
        obstacleChance = 0.1f;
        obstacleType = ObstacleType::ROCK;
        break;
      case Biome::HAUNTED:
        obstacleChance = 0.3f;
        obstacleType =
            dist(rng) < 0.6f ? ObstacleType::TREE : ObstacleType::ROCK;
        break;
      case Biome::CELESTIAL:
        obstacleChance = 0.15f;
        obstacleType = ObstacleType::ROCK;
        break;
      default:
        break;
      }

      if (dist(rng) < obstacleChance) {
        tile.obstacleType = obstacleType;
      }
    }
  }

  // Second pass: Generate multi-tile buildings with connection logic
  generateBuildings(world, rng);
}

void WorldGenerator::calculateInitialResources(const WorldData &world) {
  WORLD_MANAGER_INFO("Calculating initial resources for world: " + world.worldId);

  int treeCount = 0;
  int rockCount = 0;
  int waterCount = 0;

  for (const auto &row : world.grid) {
    for (const auto &tile : row) {
      switch (tile.obstacleType) {
      case ObstacleType::TREE:
        treeCount++;
        break;
      case ObstacleType::ROCK:
        rockCount++;
        break;
      case ObstacleType::WATER:
        waterCount++;
        break;
      default:
        break;
      }
    }
  }

  WORLD_MANAGER_INFO("Initial resources - Trees: " + std::to_string(treeCount) +
                     ", Rocks: " + std::to_string(rockCount) +
                     ", Water: " + std::to_string(waterCount));
}

void WorldGenerator::generateBuildings(WorldData& world, std::default_random_engine& rng) {
  int height = static_cast<int>(world.grid.size());
  int width = height > 0 ? static_cast<int>(world.grid[0].size()) : 0;
  
  if (width <= 2 || height <= 2) return; // Need at least 3x3 to place 2x2 buildings
  
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  uint32_t nextBuildingId = 1;

  // Iterate through potential building sites (leaving room for 2x2 structures)
  for (int y = 0; y < height - 1; ++y) {
    for (int x = 0; x < width - 1; ++x) {
      // Skip if this tile already has a building
      if (world.grid[y][x].buildingId > 0) {
        continue;
      }

      // Check if we can place a building here
      if (!canPlaceBuilding(world, x, y)) {
        continue;
      }

      // Determine building chance based on biome
      float buildingChance = 0.0f;
      const Tile& tile = world.grid[y][x];
      
      switch (tile.biome) {
      case Biome::FOREST:
        buildingChance = 0.025f; // Increased for better visibility
        break;
      case Biome::HAUNTED:
        buildingChance = 0.03f; // Increased for better visibility
        break;
      case Biome::DESERT:
        buildingChance = 0.015f; // Increased for better visibility
        break;
      case Biome::SWAMP:
        buildingChance = 0.015f; // Increased for better visibility
        break;
      case Biome::CELESTIAL:
        buildingChance = 0.02f; // Increased for better visibility
        break;
      case Biome::MOUNTAIN:
      case Biome::OCEAN:
        // No buildings in these biomes
        continue;
      default:
        buildingChance = 0.015f; // Increased default chance
        break;
      }

      if (buildingChance > 0.0f && dist(rng) < buildingChance) {
        // Create a new building at this location
        uint32_t newBuildingId = createBuilding(world, x, y, nextBuildingId);

        // Only try to connect if building was successfully created
        if (newBuildingId > 0) {
          // Try to connect to adjacent buildings
          tryConnectBuildings(world, x, y, newBuildingId);
        }
      }
    }
  }
}

bool WorldGenerator::canPlaceBuilding(const WorldData& world, int x, int y) {
  int height = static_cast<int>(world.grid.size());
  int width = height > 0 ? static_cast<int>(world.grid[0].size()) : 0;

  // Check bounds for 2x2 building
  if (x < 0 || y < 0 || x >= width - 1 || y >= height - 1) {
    return false;
  }

  // Check all 4 tiles of the 2x2 area
  for (int dy = 0; dy < 2; ++dy) {
    for (int dx = 0; dx < 2; ++dx) {
      const Tile& tile = world.grid[y + dy][x + dx];
      
      // Can't place on water, existing obstacles, or mountain biome
      if (tile.isWater || 
          tile.obstacleType != ObstacleType::NONE ||
          tile.biome == Biome::MOUNTAIN ||
          tile.biome == Biome::OCEAN ||
          tile.buildingId > 0) {
        return false;
      }
    }
  }

  return true;
}

uint32_t WorldGenerator::createBuilding(WorldData& world, int x, int y, uint32_t& nextBuildingId) {
  int height = static_cast<int>(world.grid.size());
  int width = height > 0 ? static_cast<int>(world.grid[0].size()) : 0;

  // Validate bounds before creating building
  if (x < 0 || y < 0 || x >= width - 1 || y >= height - 1) {
    WORLD_MANAGER_ERROR("createBuilding: Invalid position (" + std::to_string(x) +
                        ", " + std::to_string(y) + ") - out of bounds");
    return 0;
  }

  uint32_t buildingId = nextBuildingId++;

  // Mark all 4 tiles as part of this building (2x2)
  int tilesMarked = 0;
  for (int dy = 0; dy < 2; ++dy) {
    for (int dx = 0; dx < 2; ++dx) {
      int tileX = x + dx;
      int tileY = y + dy;

      // Double-check bounds for safety
      if (tileX < 0 || tileY < 0 || tileX >= width || tileY >= height) {
        WORLD_MANAGER_ERROR("createBuilding: Tile (" + std::to_string(tileX) +
                            ", " + std::to_string(tileY) + ") out of bounds during building creation");
        continue;
      }

      Tile& tile = world.grid[tileY][tileX];

      // Verify tile is available before marking
      if (tile.obstacleType != ObstacleType::NONE || tile.buildingId > 0) {
        WORLD_MANAGER_WARN("createBuilding: Tile (" + std::to_string(tileX) +
                           ", " + std::to_string(tileY) + ") already occupied");
        continue;
      }

      tile.obstacleType = ObstacleType::BUILDING;
      tile.buildingId = buildingId;
      tile.buildingSize = 1; // Start as size 1 (hut)
      tilesMarked++;
    }
  }

  // Validate that all 4 tiles were successfully marked
  if (tilesMarked != 4) {
    WORLD_MANAGER_ERROR("createBuilding: Building " + std::to_string(buildingId) +
                        " at (" + std::to_string(x) + ", " + std::to_string(y) +
                        ") only marked " + std::to_string(tilesMarked) + "/4 tiles");
  } else {
    WORLD_MANAGER_DEBUG("createBuilding: Building " + std::to_string(buildingId) +
                        " created at (" + std::to_string(x) + ", " + std::to_string(y) +
                        ") covering tiles (" + std::to_string(x) + "-" + std::to_string(x+1) +
                        ", " + std::to_string(y) + "-" + std::to_string(y+1) + ")");
  }

  return buildingId;
}

void WorldGenerator::tryConnectBuildings(WorldData& world, int x, int y, uint32_t buildingId) {
  int height = static_cast<int>(world.grid.size());
  int width = height > 0 ? static_cast<int>(world.grid[0].size()) : 0;

  std::vector<uint32_t> connectedBuildings;
  connectedBuildings.push_back(buildingId);

  // Check for adjacent buildings to connect to (check all sides of the 2x2 building)
  // For a 2x2 building at (x,y), check where other 2x2 buildings would be adjacent
  
  // Check left side - building at (x-2, y) would be directly adjacent
  if (x >= 2) {
    for (int dy = 0; dy < 2; ++dy) {
      if (y + dy < height) {
        const Tile& leftTile = world.grid[y + dy][x - 1]; // Check the edge tile
        if (leftTile.buildingId > 0 && leftTile.buildingId != buildingId) {
          if (std::find(connectedBuildings.begin(), connectedBuildings.end(), leftTile.buildingId) == connectedBuildings.end()) {
            connectedBuildings.push_back(leftTile.buildingId);
          }
        }
      }
    }
  }

  // Check right side - building at (x+2, y) would be directly adjacent
  if (x + 2 < width) {
    for (int dy = 0; dy < 2; ++dy) {
      if (y + dy < height) {
        const Tile& rightTile = world.grid[y + dy][x + 2]; // Check the edge tile
        if (rightTile.buildingId > 0 && rightTile.buildingId != buildingId) {
          if (std::find(connectedBuildings.begin(), connectedBuildings.end(), rightTile.buildingId) == connectedBuildings.end()) {
            connectedBuildings.push_back(rightTile.buildingId);
          }
        }
      }
    }
  }

  // Check top side - building at (x, y-2) would be directly adjacent
  if (y >= 1) {
    for (int dx = 0; dx < 2; ++dx) {
      if (x + dx < width) {
        const Tile& topTile = world.grid[y - 1][x + dx]; // Check the edge tile
        if (topTile.buildingId > 0 && topTile.buildingId != buildingId) {
          if (std::find(connectedBuildings.begin(), connectedBuildings.end(), topTile.buildingId) == connectedBuildings.end()) {
            connectedBuildings.push_back(topTile.buildingId);
          }
        }
      }
    }
  }

  // Check bottom side - building at (x, y+2) would be directly adjacent
  if (y + 2 < height) {
    for (int dx = 0; dx < 2; ++dx) {
      if (x + dx < width) {
        const Tile& bottomTile = world.grid[y + 2][x + dx]; // Check the edge tile
        if (bottomTile.buildingId > 0 && bottomTile.buildingId != buildingId) {
          if (std::find(connectedBuildings.begin(), connectedBuildings.end(), bottomTile.buildingId) == connectedBuildings.end()) {
            connectedBuildings.push_back(bottomTile.buildingId);
          }
        }
      }
    }
  }

  // Update building sizes for all connected buildings
  uint8_t newSize = static_cast<uint8_t>(std::min(4u, static_cast<uint32_t>(connectedBuildings.size())));
  
  for (uint32_t connectedId : connectedBuildings) {
    // Update all tiles belonging to each connected building
    for (int row = 0; row < height; ++row) {
      for (int col = 0; col < width; ++col) {
        if (world.grid[row][col].buildingId == connectedId) {
          world.grid[row][col].buildingSize = newSize;
        }
      }
    }
  }
}

} // namespace HammerEngine