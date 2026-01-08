/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef IDLE_BEHAVIOR_HPP
#define IDLE_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "entities/EntityHandle.hpp"
#include "managers/EntityDataManager.hpp"
#include "utils/Vector2D.hpp"
#include <random>

class IdleBehavior : public AIBehavior {
public:
  enum class IdleMode : uint8_t {
    STATIONARY,      // Completely still
    SUBTLE_SWAY,     // Small swaying motion
    OCCASIONAL_TURN, // Turn around occasionally
    LIGHT_FIDGET     // Small random movements
  };

  explicit IdleBehavior(IdleMode mode = IdleMode::STATIONARY,
                        float idleRadius = 20.0f);

  void init(EntityHandle handle) override;
  void executeLogic(BehaviorContext& ctx) override;
  void clean(EntityHandle handle) override;
  void onMessage(EntityHandle handle, const std::string &message) override;
  std::string getName() const override;

  // Configuration methods
  void setIdleMode(IdleMode mode);
  void setIdleRadius(float radius);
  void setMovementFrequency(
      float frequency); // How often to make small movements (in seconds)
  void setTurnFrequency(float frequency); // How often to turn (in seconds)

  // Get current state
  IdleMode getIdleMode() const;
  float getIdleRadius() const;

  // Clone method for creating unique behavior instances
  std::shared_ptr<AIBehavior> clone() const override;

private:
  // Behavior parameters
  IdleMode m_idleMode{IdleMode::STATIONARY};
  float m_idleRadius{20.0f};
  float m_movementFrequency{3.0f}; // Seconds between movements
  float m_turnFrequency{5.0f};     // Seconds between turns

  // Random number generation
  mutable std::mt19937 m_rng{std::random_device{}()};
  mutable std::uniform_real_distribution<float> m_angleDistribution{
      0.0f, 2.0f * M_PI};
  mutable std::uniform_real_distribution<float> m_radiusDistribution{0.0f,
                                                                     1.0f};
  mutable std::uniform_real_distribution<float> m_frequencyVariation{0.5f,
                                                                     1.5f};

  // Helper methods (all entity state stored in EDM BehaviorData)
  void initializeIdleState(const Vector2D& position, BehaviorData& data) const;
  void updateStationary(BehaviorContext& ctx);
  void updateSubtleSway(BehaviorContext& ctx, BehaviorData& data) const;
  void updateOccasionalTurn(BehaviorContext& ctx, BehaviorData& data) const;
  void updateLightFidget(BehaviorContext& ctx, BehaviorData& data) const;

  Vector2D generateRandomOffset() const;
  float getRandomMovementInterval() const;
  float getRandomTurnInterval() const;
};

#endif // IDLE_BEHAVIOR_HPP
