/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "world/HarvestConfig.hpp"
#include <array>

namespace VoidLight {

// ============================================================================
// HARVEST TYPE CONFIGURATIONS
// ============================================================================

namespace {

// Static configuration tables - zero runtime allocation

constexpr std::array<HarvestTypeConfig, static_cast<size_t>(HarvestType::COUNT)> HARVEST_TYPE_CONFIGS{{
    // Gathering - herbs, flowers, bushes (fastest)
    {0.5f, 0.0f, "Gathering..."},
    // Chopping - trees
    {2.0f, 5.0f, "Chopping..."},
    // Mining - ore and gem deposits
    {3.0f, 10.0f, "Mining..."},
    // Quarrying - stone, limestone
    {2.5f, 8.0f, "Quarrying..."},
    // Fishing - water resources (future)
    {4.0f, 3.0f, "Fishing..."},
}};

// Default config for unknown types
constexpr HarvestTypeConfig DEFAULT_TYPE_CONFIG{1.0f, 0.0f, "Harvesting..."};

// ============================================================================
// DEPOSIT CONFIGURATIONS
// ============================================================================

// Ore deposits
constexpr DepositConfig IRON_DEPOSIT_CONFIG{"iron_ore", 2, 5, 90.0f, HarvestType::Mining};
constexpr DepositConfig GOLD_DEPOSIT_CONFIG{"gold_ore", 1, 3, 150.0f, HarvestType::Mining};
constexpr DepositConfig COPPER_DEPOSIT_CONFIG{"copper_ore", 2, 4, 60.0f, HarvestType::Mining};
constexpr DepositConfig COAL_DEPOSIT_CONFIG{"coal", 3, 6, 75.0f, HarvestType::Mining};
constexpr DepositConfig MITHRIL_DEPOSIT_CONFIG{"mithril_ore", 1, 2, 300.0f, HarvestType::Mining};
constexpr DepositConfig LIMESTONE_DEPOSIT_CONFIG{"limestone", 2, 4, 120.0f, HarvestType::Quarrying};

// Gem deposits
constexpr DepositConfig EMERALD_DEPOSIT_CONFIG{"rough_emerald", 1, 2, 180.0f, HarvestType::Mining};
constexpr DepositConfig RUBY_DEPOSIT_CONFIG{"rough_ruby", 1, 2, 180.0f, HarvestType::Mining};
constexpr DepositConfig SAPPHIRE_DEPOSIT_CONFIG{"rough_sapphire", 1, 2, 180.0f, HarvestType::Mining};
constexpr DepositConfig DIAMOND_DEPOSIT_CONFIG{"rough_diamond", 1, 1, 360.0f, HarvestType::Mining};

// Natural resources
constexpr DepositConfig TREE_CONFIG{"wood", 2, 4, 120.0f, HarvestType::Chopping};
constexpr DepositConfig ROCK_CONFIG{"stone", 1, 3, 180.0f, HarvestType::Quarrying};

// Default config for non-harvestable obstacles
constexpr DepositConfig DEFAULT_DEPOSIT_CONFIG{"", 0, 0, 0.0f, HarvestType::Gathering};

} // anonymous namespace

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

const HarvestTypeConfig& getHarvestTypeConfig(HarvestType type) {
    auto index = static_cast<size_t>(type);
    if (index < HARVEST_TYPE_CONFIGS.size()) {
        return HARVEST_TYPE_CONFIGS[index];
    }
    return DEFAULT_TYPE_CONFIG;
}

const DepositConfig& getDepositConfig(ObstacleType obstacle) {
    switch (obstacle) {
        // Ore deposits
        case ObstacleType::IRON_DEPOSIT: return IRON_DEPOSIT_CONFIG;
        case ObstacleType::GOLD_DEPOSIT: return GOLD_DEPOSIT_CONFIG;
        case ObstacleType::COPPER_DEPOSIT: return COPPER_DEPOSIT_CONFIG;
        case ObstacleType::COAL_DEPOSIT: return COAL_DEPOSIT_CONFIG;
        case ObstacleType::MITHRIL_DEPOSIT: return MITHRIL_DEPOSIT_CONFIG;
        case ObstacleType::LIMESTONE_DEPOSIT: return LIMESTONE_DEPOSIT_CONFIG;

        // Gem deposits
        case ObstacleType::EMERALD_DEPOSIT: return EMERALD_DEPOSIT_CONFIG;
        case ObstacleType::RUBY_DEPOSIT: return RUBY_DEPOSIT_CONFIG;
        case ObstacleType::SAPPHIRE_DEPOSIT: return SAPPHIRE_DEPOSIT_CONFIG;
        case ObstacleType::DIAMOND_DEPOSIT: return DIAMOND_DEPOSIT_CONFIG;

        // Natural resources
        case ObstacleType::TREE: return TREE_CONFIG;
        case ObstacleType::ROCK: return ROCK_CONFIG;

        default: return DEFAULT_DEPOSIT_CONFIG;
    }
}

bool isHarvestableObstacle(ObstacleType obstacle) {
    switch (obstacle) {
        case ObstacleType::TREE:
        case ObstacleType::ROCK:
        case ObstacleType::IRON_DEPOSIT:
        case ObstacleType::GOLD_DEPOSIT:
        case ObstacleType::COPPER_DEPOSIT:
        case ObstacleType::COAL_DEPOSIT:
        case ObstacleType::MITHRIL_DEPOSIT:
        case ObstacleType::LIMESTONE_DEPOSIT:
        case ObstacleType::EMERALD_DEPOSIT:
        case ObstacleType::RUBY_DEPOSIT:
        case ObstacleType::SAPPHIRE_DEPOSIT:
        case ObstacleType::DIAMOND_DEPOSIT:
            return true;
        default:
            return false;
    }
}

HarvestType getHarvestTypeForObstacle(ObstacleType obstacle) {
    return getDepositConfig(obstacle).harvestType;
}

HarvestType getHarvestTypeForResource(std::string_view resourceId) {
    // Wood and tree-related resources → Chopping
    if (resourceId == "wood" || resourceId == "lumber" || resourceId == "log") {
        return HarvestType::Chopping;
    }

    // Ores → Mining
    if (resourceId == "iron_ore" || resourceId == "gold_ore" ||
        resourceId == "copper_ore" || resourceId == "mithril_ore" ||
        resourceId == "coal") {
        return HarvestType::Mining;
    }

    // Gems → Mining
    if (resourceId == "rough_emerald" || resourceId == "rough_ruby" ||
        resourceId == "rough_sapphire" || resourceId == "rough_diamond" ||
        resourceId == "emerald" || resourceId == "ruby" ||
        resourceId == "sapphire" || resourceId == "diamond") {
        return HarvestType::Mining;
    }

    // Stone and related → Quarrying
    if (resourceId == "stone" || resourceId == "limestone" ||
        resourceId == "granite" || resourceId == "marble") {
        return HarvestType::Quarrying;
    }

    // Herbs, plants, mushrooms → Gathering
    if (resourceId == "herb" || resourceId == "healing_herb" ||
        resourceId == "mushroom" || resourceId == "flower" ||
        resourceId == "berry" || resourceId == "plant") {
        return HarvestType::Gathering;
    }

    // Fish → Fishing
    if (resourceId == "fish" || resourceId == "salmon" ||
        resourceId == "trout" || resourceId == "carp") {
        return HarvestType::Fishing;
    }

    // Default to Gathering for unknown resources
    return HarvestType::Gathering;
}

} // namespace VoidLight
