#ifndef ENTITY_IDLE_STATE_HPP
#define ENTITY_IDLE_STATE_HPP

#include "EntityState.hpp"

class EntityIdleState : public EntityState {
    void enter() override;
    void update() override;
    void render() override;
    void exit() override;
};

#endif // ENTITY_IDLE_STATE_HPP
