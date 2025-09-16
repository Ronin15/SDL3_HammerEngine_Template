/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef COLLISION_EVENT_HPP
#define COLLISION_EVENT_HPP

#include "events/Event.hpp"
#include "events/EventTypeId.hpp"
#include "collisions/CollisionInfo.hpp"
#include <string>

class CollisionEvent : public Event {
public:
  explicit CollisionEvent(const HammerEngine::CollisionInfo &info)
      : m_info(info) {}

  // Minimal behavior: events are passive containers
  void update() override {}
  void execute() override {}
  void clean() override {}
  bool checkConditions() override { return true; }
  void reset() override { Event::resetCooldown(); m_consumed = false; }

  std::string getName() const override { return "CollisionEvent"; }
  std::string getType() const override { return "CollisionEvent"; }
  std::string getTypeName() const override { return "CollisionEvent"; }
  EventTypeId getTypeId() const override { return EventTypeId::Collision; }

  const HammerEngine::CollisionInfo &getInfo() const { return m_info; }
  void setInfo(const HammerEngine::CollisionInfo &info) { m_info = info; }
  bool isConsumed() const { return m_consumed; }
  void setConsumed(bool c) { m_consumed = c; }

private:
  HammerEngine::CollisionInfo m_info{};
  bool m_consumed{false};
};

#endif // COLLISION_EVENT_HPP

