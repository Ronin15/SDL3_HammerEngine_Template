/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "MockAIBehavior.hpp"

void MockAIBehavior::executeLogic(EntityPtr /*entity*/) {
    executeLogicCallCount++;
}

void MockAIBehavior::init(EntityPtr /*entity*/) {
    initCallCount++;
}

void MockAIBehavior::clean(EntityPtr /*entity*/) {
    cleanCallCount++;
}

std::string MockAIBehavior::getName() const {
    return name;
}

std::shared_ptr<AIBehavior> MockAIBehavior::clone() const {
    return std::make_shared<MockAIBehavior>(*this);
}
