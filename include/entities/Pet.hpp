/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef PET_HPP
#define PET_HPP

#include "entities/NPC.hpp"
#include "utils/Vector2D.hpp"
#include <memory>
#include <string>

/**
 * Pet - Non-blocking follower NPC that doesn't interfere with player movement
 *
 * Pets are companion NPCs designed to follow the player without physically
 * blocking movement. They use Layer_Pet collision layer which passes through
 * Layer_Player but still collides with environment and enemies.
 */
class Pet : public NPC {
public:
    Pet(const std::string &textureID, const Vector2D &startPosition,
        int frameWidth = 0, int frameHeight = 0);
    ~Pet() override = default;

    // Factory method to ensure proper shared_ptr creation
    static std::shared_ptr<Pet> create(const std::string &textureID,
                                       const Vector2D &startPosition,
                                       int frameWidth = 0, int frameHeight = 0) {
        auto pet = std::make_shared<Pet>(textureID, startPosition, frameWidth, frameHeight);
        pet->ensurePhysicsBodyRegistered();
        return pet;
    }

    // Override to use Pet collision layer (passes through player)
    void ensurePhysicsBodyRegistered() override;
};

#endif // PET_HPP
