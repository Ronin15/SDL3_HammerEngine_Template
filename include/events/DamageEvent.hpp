/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef DAMAGE_EVENT_HPP
#define DAMAGE_EVENT_HPP

#include "events/Event.hpp"
#include "events/EventTypeId.hpp"

/**
 * @brief Stub for combat damage events
 *
 * TODO: Define fields based on your combat system design.
 * Pool and trigger infrastructure is ready in EventManager.
 */
class DamageEvent : public Event {
public:
  DamageEvent() = default;

  void update() override {}
  void execute() override {}
  void clean() override {}
  bool checkConditions() override { return true; }
  void reset() override { Event::resetCooldown(); }

  std::string getName() const override { return "DamageEvent"; }
  std::string getType() const override { return "DamageEvent"; }
  std::string getTypeName() const override { return "DamageEvent"; }
  EventTypeId getTypeId() const override { return EventTypeId::Combat; }

  // TODO: Add your combat fields here
};

#endif // DAMAGE_EVENT_HPP
