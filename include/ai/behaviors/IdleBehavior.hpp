/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef IDLE_BEHAVIOR_HPP
#define IDLE_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <unordered_map>
#include <random>

class IdleBehavior : public AIBehavior {
public:
    enum class IdleMode {
        STATIONARY,     // Completely still
        SUBTLE_SWAY,    // Small swaying motion
        OCCASIONAL_TURN, // Turn around occasionally
        LIGHT_FIDGET    // Small random movements
    };

    explicit IdleBehavior(IdleMode mode = IdleMode::STATIONARY, float idleRadius = 20.0f);

    void init(EntityPtr entity) override;
    void executeLogic(EntityPtr entity) override;
    void clean(EntityPtr entity) override;
    void onMessage(EntityPtr entity, const std::string& message) override;
    std::string getName() const override;

    // Configuration methods
    void setIdleMode(IdleMode mode);
    void setIdleRadius(float radius);
    void setMovementFrequency(float frequency); // How often to make small movements (in seconds)
    void setTurnFrequency(float frequency);     // How often to turn (in seconds)

    // Get current state
    IdleMode getIdleMode() const;
    float getIdleRadius() const;

    // Clone method for creating unique behavior instances
    std::shared_ptr<AIBehavior> clone() const override;

private:
    // Entity-specific state data
    struct EntityState {
        Vector2D originalPosition{0, 0};
        Vector2D currentOffset{0, 0};
        Uint64 lastMovementTime{0};
        Uint64 lastTurnTime{0};
        Uint64 nextMovementTime{0};
        Uint64 nextTurnTime{0};
        float currentAngle{0.0f};
        bool initialized{false};

        EntityState() 
            : originalPosition(0, 0)
            , currentOffset(0, 0)
            , lastMovementTime(0)
            , lastTurnTime(0)
            , nextMovementTime(0)
            , nextTurnTime(0)
            , currentAngle(0.0f)
            , initialized(false)
        {}
    };

    // Map to store per-entity state
    std::unordered_map<EntityPtr, EntityState> m_entityStates;

    // Behavior parameters
    IdleMode m_idleMode{IdleMode::STATIONARY};
    float m_idleRadius{20.0f};
    float m_movementFrequency{3.0f};  // Seconds between movements
    float m_turnFrequency{5.0f};      // Seconds between turns

    // Random number generation
    mutable std::mt19937 m_rng{std::random_device{}()};
    mutable std::uniform_real_distribution<float> m_angleDistribution{0.0f, 2.0f * M_PI};
    mutable std::uniform_real_distribution<float> m_radiusDistribution{0.0f, 1.0f};
    mutable std::uniform_real_distribution<float> m_frequencyVariation{0.5f, 1.5f};

    // Helper methods
    void initializeEntityState(EntityPtr entity, EntityState& state);
    void updateStationary(EntityPtr entity, EntityState& state);
    void updateSubtleSway(EntityPtr entity, EntityState& state);
    void updateOccasionalTurn(EntityPtr entity, EntityState& state);
    void updateLightFidget(EntityPtr entity, EntityState& state);
    
    Vector2D generateRandomOffset() const;
    Uint64 getRandomMovementInterval() const;
    Uint64 getRandomTurnInterval() const;
};

#endif // IDLE_BEHAVIOR_HPP