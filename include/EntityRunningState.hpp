#ifndef ENTITY_RUNNING_STATE_HPP
#define ENTITY_RUNNING_STATE_HPP

#include "EntityState.hpp"

class EntityRunningState : public EntityState {
    void enter() override;
    void update() override;
    void render() override;
    void exit() override;
};

#endif // ENTITY_RUNNING_STATE_HPP
