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
WorldGenerator::generateWorld(const WorldGenerationConfig &config) {
  GAMEENGINE_INFO("Generating world: " + std::to_string(config.width) + "x" +
                  std::to_string(config.height) + " with seed " +
                  std::to_string(config.seed));

  std::vector<std::vector<float>> elevationMap, humidityMap;
  auto world = generateNoiseMaps(config, elevationMap, humidityMap);

  if (!world) {
    GAMEENGINE_ERROR("Failed to generate noise maps for world");
    return nullptr;
  }

  assignBiomes(*world, elevationMap, humidityMap, config);
  createWaterBodies(*world, elevationMap, config);
  distributeObstacles(*world, config);
  calculateInitialResources(*world);

  GAMEENGINE_INFO("World generation completed successfully");
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
}

void WorldGenerator::calculateInitialResources(const WorldData &world) {
  GAMEENGINE_INFO("Calculating initial resources for world: " + world.worldId);

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

  GAMEENGINE_INFO("Initial resources - Trees: " + std::to_string(treeCount) +
                  ", Rocks: " + std::to_string(rockCount) +
                  ", Water: " + std::to_string(waterCount));
}

} // namespace HammerEngine