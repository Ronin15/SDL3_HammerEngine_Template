#ifndef ENTITY_JUMPING_STATE_HPP
#define ENTITY_JUMPING_STATE_HPP

#include "EntityState.hpp"

class EntityJumpingState : public EntityState {
    void enter() override;
    void update() override;
    void render() override;
    void exit() override;
};

#endif // ENTITY_JUMPING_STATE_HPP
