#ifndef WORLD_GENERATOR_HPP
#define WORLD_GENERATOR_HPP

#include <memory>
#include <vector>
#include <random>
#include "world/WorldData.hpp"

namespace HammerEngine {

class WorldGenerator {
private:
    struct PerlinNoise {
        std::vector<int> permutation;
        
        explicit PerlinNoise(int seed);
        float noise(float x, float y) const;
        float fade(float t) const;
        float lerp(float t, float a, float b) const;
        float grad(int hash, float x, float y) const;
    };

    static std::unique_ptr<WorldData> generateNoiseMaps(
        const WorldGenerationConfig& config,
        std::vector<std::vector<float>>& elevationMap,
        std::vector<std::vector<float>>& humidityMap
    );
    
    static void assignBiomes(
        WorldData& world,
        const std::vector<std::vector<float>>& elevationMap,
        const std::vector<std::vector<float>>& humidityMap,
        const WorldGenerationConfig& config
    );
    
    static void createWaterBodies(
        WorldData& world,
        const std::vector<std::vector<float>>& elevationMap,
        const WorldGenerationConfig& config
    );
    
    static void distributeObstacles(
        WorldData& world,
        const WorldGenerationConfig& config
    );
    
    static void calculateInitialResources(
        const WorldData& world
    );

public:
    static std::unique_ptr<WorldData> generateWorld(const WorldGenerationConfig& config);
};

}

#endif