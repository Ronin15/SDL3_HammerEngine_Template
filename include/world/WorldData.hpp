#ifndef WORLD_DATA_HPP
#define WORLD_DATA_HPP

#include <vector>
#include <string>
#include <iostream>
#include "utils/ResourceHandle.hpp"

namespace HammerEngine {

struct WorldGenerationConfig {
    int width;
    int height;
    int seed;
    float elevationFrequency;
    float humidityFrequency;
    float waterLevel;
    float mountainLevel;
};

// World rendering and spatial constants
constexpr float TILE_SIZE = 32.0f;  // Tile size in pixels

// Default fallback world dimensions (100x100 tiles = 3200x3200 pixels)
// Used by systems when WorldManager has no active world loaded
constexpr float DEFAULT_WORLD_WIDTH = 32000.0f;   // 1000 tiles * 32px
constexpr float DEFAULT_WORLD_HEIGHT = 32000.0f;  // 1000 tiles * 32px

enum class Biome {
    DESERT,
    FOREST,
    MOUNTAIN,
    SWAMP,
    HAUNTED,
    CELESTIAL,
    OCEAN
};

enum class ObstacleType {
    NONE,
    ROCK,
    TREE,
    WATER,
    BUILDING
};

// Stream operators for test output
inline std::ostream& operator<<(std::ostream& os, const Biome& biome) {
    switch (biome) {
        case Biome::DESERT: return os << "DESERT";
        case Biome::FOREST: return os << "FOREST";
        case Biome::MOUNTAIN: return os << "MOUNTAIN";
        case Biome::SWAMP: return os << "SWAMP";
        case Biome::HAUNTED: return os << "HAUNTED";
        case Biome::CELESTIAL: return os << "CELESTIAL";
        case Biome::OCEAN: return os << "OCEAN";
        default: return os << "UNKNOWN";
    }
}

inline std::ostream& operator<<(std::ostream& os, const ObstacleType& obstacle) {
    switch (obstacle) {
        case ObstacleType::NONE: return os << "NONE";
        case ObstacleType::ROCK: return os << "ROCK";
        case ObstacleType::TREE: return os << "TREE";
        case ObstacleType::WATER: return os << "WATER";
        case ObstacleType::BUILDING: return os << "BUILDING";
        default: return os << "UNKNOWN";
    }
}

struct Tile {
    Biome biome;
    ObstacleType obstacleType = ObstacleType::NONE;
    float elevation = 0.0f;
    bool isWater = false;
    HammerEngine::ResourceHandle resourceHandle;
    
    // Building support for multi-tile structures
    uint32_t buildingId = 0;        // 0 = no building, >0 = unique building ID
    uint8_t buildingSize = 0;       // 0 = no building, 1-4 = connected building count
};

struct WorldData {
    std::string worldId;
    std::vector<std::vector<Tile>> grid;
};

}

#endif
