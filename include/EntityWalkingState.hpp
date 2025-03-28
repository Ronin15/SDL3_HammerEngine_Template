#ifndef ENTITY_WALKING_STATE_HPP
#define ENTITY_WALKING_STATE_HPP

#include "EntityState.hpp"

class EntityWalkingState : public EntityState {
    void enter() override;
    void update() override;
    void render() override;
    void exit() override;
};

#endif // ENTITY_WALKING_STATE_HPP
