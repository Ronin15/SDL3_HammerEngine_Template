/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include <iostream>
#include <vector>
#include <chrono>
#include <unordered_map>
#include "world/WorldGenerator.hpp"
#include "world/WorldData.hpp"

using namespace HammerEngine;

// Simple test to reproduce timeout patterns with specific coordinates
int main() {
    std::cout << "=== Pathfinding Timeout Analysis ===" << std::endl;
    
    // Create a test world similar to the one that has problems
    WorldGenerationConfig config;
    config.width = 100;
    config.height = 100;
    config.seed = -803134486; // Use the seed from the log to reproduce exact world
    config.elevationFrequency = 0.05f;
    config.humidityFrequency = 0.03f;
    config.waterLevel = 0.3f;
    config.mountainLevel = 0.7f;
    
    std::cout << "Generating world with seed: " << config.seed << std::endl;
    
    WorldGenerator generator;
    auto world = generator.generateWorld(config);
    
    if (!world) {
        std::cerr << "Failed to generate world!" << std::endl;
        return 1;
    }
    
    std::cout << "Analyzing world layout around problematic coordinates..." << std::endl;
    
    // Check the problematic coordinates
    std::vector<std::pair<int,int>> problematicCoords = {
        {90, 7}, {91, 9}, {90, 47}
    };
    
    for (auto& coord : problematicCoords) {
        int x = coord.first, y = coord.second;
        std::cout << "\n--- Analyzing coordinate (" << x << "," << y << ") ---" << std::endl;
        
        if (x >= 0 && x < config.width && y >= 0 && y < config.height) {
            const auto& tile = world->grid[y][x];
            
            std::cout << "Biome: ";
            switch (tile.biome) {
                case Biome::FOREST: std::cout << "FOREST"; break;
                case Biome::MOUNTAIN: std::cout << "MOUNTAIN"; break;
                case Biome::DESERT: std::cout << "DESERT"; break;
                case Biome::SWAMP: std::cout << "SWAMP"; break;
                case Biome::OCEAN: std::cout << "OCEAN"; break;
                case Biome::HAUNTED: std::cout << "HAUNTED"; break;
                case Biome::CELESTIAL: std::cout << "CELESTIAL"; break;
                default: std::cout << "UNKNOWN"; break;
            }
            std::cout << std::endl;
            
            std::cout << "Elevation: " << tile.elevation << std::endl;
            std::cout << "IsWater: " << (tile.isWater ? "YES" : "NO") << std::endl;
            std::cout << "Obstacle: ";
            switch (tile.obstacleType) {
                case ObstacleType::NONE: std::cout << "NONE"; break;
                case ObstacleType::TREE: std::cout << "TREE"; break;
                case ObstacleType::ROCK: std::cout << "ROCK"; break;
                case ObstacleType::WATER: std::cout << "WATER"; break;
                case ObstacleType::BUILDING: std::cout << "BUILDING"; break;
                default: std::cout << "UNKNOWN"; break;
            }
            std::cout << std::endl;
            
            // Check surrounding area
            std::cout << "Surrounding 5x5 area:" << std::endl;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < config.width && ny >= 0 && ny < config.height) {
                        const auto& neighborTile = world->grid[ny][nx];
                        char symbol = '.'; // walkable
                        
                        if (neighborTile.isWater) symbol = '~';
                        else if (neighborTile.obstacleType == ObstacleType::TREE) symbol = 'T';
                        else if (neighborTile.obstacleType == ObstacleType::ROCK) symbol = 'R';
                        else if (neighborTile.obstacleType == ObstacleType::BUILDING) symbol = 'B';
                        
                        std::cout << symbol;
                    } else {
                        std::cout << '#'; // out of bounds
                    }
                }
                std::cout << std::endl;
            }
        }
    }
    
    // Analyze broader region statistics
    std::cout << "\n=== Regional Analysis ===" << std::endl;
    
    // Check the areas around x=90-91 that seem problematic
    int regionX = 90, regionY = 7;
    int regionSize = 20; // 20x20 area around the problem coordinate
    
    int blockedCount = 0, waterCount = 0, totalCount = 0;
    
    for (int y = std::max(0, regionY - regionSize/2); 
         y < std::min(config.height, regionY + regionSize/2); y++) {
        for (int x = std::max(0, regionX - regionSize/2); 
             x < std::min(config.width, regionX + regionSize/2); x++) {
            totalCount++;
            const auto& tile = world->grid[y][x];
            
            if (tile.isWater) waterCount++;
            if (tile.obstacleType != ObstacleType::NONE) blockedCount++;
        }
    }
    
    std::cout << "Region around (" << regionX << "," << regionY << "):" << std::endl;
    std::cout << "Total tiles: " << totalCount << std::endl;
    std::cout << "Water tiles: " << waterCount << " (" << (100.0f * waterCount / totalCount) << "%)" << std::endl;
    std::cout << "Blocked tiles: " << blockedCount << " (" << (100.0f * blockedCount / totalCount) << "%)" << std::endl;
    std::cout << "Walkable tiles: " << (totalCount - waterCount - blockedCount) << " (" << (100.0f * (totalCount - waterCount - blockedCount) / totalCount) << "%)" << std::endl;
    
    // Check if there are large barrier areas
    std::cout << "\n=== Connectivity Analysis ===" << std::endl;
    std::cout << "Checking for large blocked regions or connectivity issues..." << std::endl;
    
    // Look for horizontal/vertical barriers that could cause major pathfinding issues
    int horizontalBarriers = 0, verticalBarriers = 0;
    
    // Check for horizontal barriers (rows of blocked tiles)
    for (int y = 0; y < config.height; y++) {
        int consecutiveBlocked = 0;
        for (int x = 0; x < config.width; x++) {
            const auto& tile = world->grid[y][x];
            if (tile.obstacleType != ObstacleType::NONE || tile.isWater) {
                consecutiveBlocked++;
            } else {
                if (consecutiveBlocked >= 20) { // Long barrier
                    horizontalBarriers++;
                    std::cout << "Long horizontal barrier at row " << y << " (length: " << consecutiveBlocked << ")" << std::endl;
                }
                consecutiveBlocked = 0;
            }
        }
        if (consecutiveBlocked >= 20) {
            horizontalBarriers++;
            std::cout << "Long horizontal barrier at row " << y << " (length: " << consecutiveBlocked << ")" << std::endl;
        }
    }
    
    // Check for vertical barriers (columns of blocked tiles)  
    for (int x = 0; x < config.width; x++) {
        int consecutiveBlocked = 0;
        for (int y = 0; y < config.height; y++) {
            const auto& tile = world->grid[y][x];
            if (tile.obstacleType != ObstacleType::NONE || tile.isWater) {
                consecutiveBlocked++;
            } else {
                if (consecutiveBlocked >= 20) { // Long barrier
                    verticalBarriers++;
                    std::cout << "Long vertical barrier at column " << x << " (length: " << consecutiveBlocked << ")" << std::endl;
                }
                consecutiveBlocked = 0;
            }
        }
        if (consecutiveBlocked >= 20) {
            verticalBarriers++;
            std::cout << "Long vertical barrier at column " << x << " (length: " << consecutiveBlocked << ")" << std::endl;
        }
    }
    
    std::cout << "Total long horizontal barriers: " << horizontalBarriers << std::endl;
    std::cout << "Total long vertical barriers: " << verticalBarriers << std::endl;
    
    // Check overall world statistics
    int totalTiles = config.width * config.height;
    int totalWater = 0, totalBlocked = 0;
    
    for (int y = 0; y < config.height; y++) {
        for (int x = 0; x < config.width; x++) {
            const auto& tile = world->grid[y][x];
            if (tile.isWater) totalWater++;
            if (tile.obstacleType != ObstacleType::NONE) totalBlocked++;
        }
    }
    
    std::cout << "\n=== Overall World Statistics ===" << std::endl;
    std::cout << "World size: " << config.width << "x" << config.height << " = " << totalTiles << " tiles" << std::endl;
    std::cout << "Water tiles: " << totalWater << " (" << (100.0f * totalWater / totalTiles) << "%)" << std::endl;
    std::cout << "Obstacle tiles: " << totalBlocked << " (" << (100.0f * totalBlocked / totalTiles) << "%)" << std::endl;
    std::cout << "Walkable tiles: " << (totalTiles - totalWater - totalBlocked) << " (" << (100.0f * (totalTiles - totalWater - totalBlocked) / totalTiles) << "%)" << std::endl;
    
    return 0;
}