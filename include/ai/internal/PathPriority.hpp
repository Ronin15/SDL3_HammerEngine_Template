/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef PATH_PRIORITY_HPP
#define PATH_PRIORITY_HPP

/**
 * @file PathPriority.hpp
 * @brief Pathfinding priority enumeration for request scheduling
 */

namespace AIInternal {

/**
 * @brief Pathfinding priority levels for request scheduling
 */
enum class PathPriority : uint8_t {
    Critical = 0, // Player, combat situations
    High = 1,     // Close NPCs, important behaviors  
    Normal = 2,   // Regular NPC navigation
    Low = 3       // Background/distant NPCs
};

} // namespace AIInternal

#endif // PATH_PRIORITY_HPP